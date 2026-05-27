/*
 * AOV Runner -- 基于 rockit_adapter 图式配置的 AOV 录像调度器
 *
 * 数据流：
 *   CamPipe (ISP -> VI -> VPSS -> VENC)
 *       ↓ frame_callback
 *   DataQueue
 *       ↓ DataQueue_Pop
 *   fopen/fwrite → SD卡 原始h26x流
 *
 * 休眠循环（loop_count 控制循环次数，<=0 表示无限循环）：
 *   录像 loop_duration_sec 秒 → 关闭分片 → 进入系统休眠 suspend_time_ms
 *   → 唤醒 → 挂载 SD 卡 → 继续录像
 *   → 下一轮循环
 */

#include "aov_runner.h"
#include "aov_helper.h"
#include "sdcard.h"
#include "data_queue.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define DATA_QUEUE_ITEM_MAX (2 * 1024 * 1024)
#define RAW_SPLIT_SEC 60

/*
 * Aov_EnterSleep() 的结果通过此全局标志传递给主循环。
 * true  = 休眠失败（write /sys/power/state 返回错误），
 *         主循环跳过重建管线，继续下一轮录像。
 */
static bool g_sleep_failed = false;

/* ======================== AOV 休眠回调 ======================== */
static void aov_notify_callback(AovEvent_e enEvent, void *msg)
{
    int sleep_ret;

    (void)msg;
    switch (enEvent) {
    case AOV_ENTER_SLEEP:
        printf("[AOV_RUNNER] +++++ AOV_ENTER_SLEEP +++++\n");

        Aov_WakeupLock();
        printf("[AOV_RUNNER] fs sdcard lock\n");
        UmountSdcard();

        /*
         * 卸载 USB xhci 控制器驱动，避免其阻塞系统 suspend。
         *
         * 内核日志显示：
         *   xhci-hcd xhci-hcd.0.auto: PM: failed to suspend async: error -22
         */
        Aov_DisableUSB();

        /* 休眠前关闭非引导 CPU 核以降低漏电流 */
        Aov_DisableNonBootCPUs();

        /* 阻塞等待唤醒（写入 /sys/power/state） */
        sleep_ret = Aov_EnterSleep();

        /* 唤醒后恢复非引导 CPU */
        Aov_EnableNonBootCPUs();

        /* 唤醒后重新挂载 USB xhci 驱动 */
        Aov_EnableUSB();

        if (sleep_ret != 0) {
            /*
             * 休眠失败（即使 unbound USB，仍有其他设备拒绝 suspend），
             * 标记失败，主循环会重建管线继续录像。
             */
            printf("[AOV_RUNNER] ++++ AOV_ENTER_SLEEP FAILED (%d) ++++\n", sleep_ret);
            g_sleep_failed = true;
        } else {
            g_sleep_failed = false;
        }

        Aov_WakeupUnlock();
        printf("[AOV_RUNNER] fs sdcard unlock\n");
        break;
    default:
        printf("[AOV_RUNNER] Unknown event: %d\n", enEvent);
        break;
    }
}

/* ======================== 帧回调 ======================== */
static void frame_callback(void *data, unsigned int data_len,
                           unsigned long long pts,
                           int route_id, int is_keyframe,
                           void *user_data)
{
    DataQueueHandle queue = (DataQueueHandle)user_data;
    FrameMeta_t meta;

    if (!queue || !data || data_len == 0)
        return;

    memset(&meta, 0, sizeof(meta));
    meta.magic = FRAME_META_MAGIC;
    meta.data_len = data_len;
    meta.timestamp = pts;
    meta.tag = route_id;
    meta.is_keyframe = is_keyframe;

    DataQueueScatter_t segs[2] = {
        { &meta, sizeof(FrameMeta_t) },
        { data,  data_len },
    };

    DataQueue_PushScatter(queue, segs, 2);
}

static void ensure_dir(const char *dir)
{
    struct stat st = {0};
    if (stat(dir, &st) == -1)
        mkdir(dir, 0755);
}

static void make_file_path(const char *dir, const char *prefix, bool use_h265,
                           char *buf, size_t max_len)
{
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    const char *pf = (prefix && prefix[0] != '\0') ? prefix : "stream";
    const char *ext = use_h265 ? "h265" : "h264";

    snprintf(buf, max_len, "%s/%s_%04d%02d%02d_%02d%02d%02d.%s",
             dir, pf,
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
             ext);
}

/*
 * 从队列取帧直接写入 SD 卡，持续 sec 秒或直到 *running 变为 false
 */
static void record_loop(DataQueueHandle queue, FILE **fd_ptr, const char *rec_dir, 
                        const char *rec_prefix, bool use_h265, int fps, 
                        int duration_sec, volatile bool *running, 
                        uint64_t *current_file_start_pts)
{
    time_t deadline = time(NULL) + duration_sec;

    while (*running) {
        time_t now = time(NULL);
        if (now >= deadline)
            break;

        void *buf = NULL;
        unsigned int buf_size = 0;
        int remain_ms = (int)(deadline - now) * 1000;
        if (remain_ms > 100)
            remain_ms = 100;

        int pop_ret = DataQueue_Pop(queue, &buf, &buf_size, remain_ms);

        if (pop_ret > 0 && buf && buf_size >= sizeof(FrameMeta_t)) {
            FrameMeta_t meta;
            memcpy(&meta, buf, sizeof(FrameMeta_t));

            if (meta.magic == FRAME_META_MAGIC &&
                meta.data_len > 0 &&
                buf_size == sizeof(FrameMeta_t) + meta.data_len) {

                bool need_split = false;
                if (!(*fd_ptr)) {
                    if (meta.is_keyframe)
                        need_split = true;
                } else if (meta.is_keyframe) {
                    uint64_t dur = meta.timestamp > *current_file_start_pts
                                   ? (meta.timestamp - *current_file_start_pts) : 0;
                    if (dur >= (uint64_t)RAW_SPLIT_SEC * 1000000ULL)
                        need_split = true;
                }

                if (need_split) {
                    if (*fd_ptr) {
                        fclose(*fd_ptr);
                        *fd_ptr = NULL;
                    }

                    char file_path[256];
                    make_file_path(rec_dir, rec_prefix, use_h265,
                                   file_path, sizeof(file_path));
                    *fd_ptr = fopen(file_path, "wb");
                    if (*fd_ptr) {
                        *current_file_start_pts = meta.timestamp;
                        printf("[AOV_RUNNER] new file: %s\n", file_path);
                    } else {
                        printf("[AOV_RUNNER] fopen failed: %s\n", file_path);
                    }
                }

                if (*fd_ptr) {
                    void *payload = (unsigned char *)buf + sizeof(FrameMeta_t);
                    fwrite(payload, 1, meta.data_len, *fd_ptr);

                    if (fps > 0 && meta.is_keyframe) {
                        printf("[AOV_RUNNER] route[%d] pts:%llu size:%u keyframe\n",
                               meta.tag, meta.timestamp, meta.data_len);
                    }
                }
            }
            DataQueue_Release(queue, buf);
        } else if (pop_ret < 0) {
            usleep(5000);
        }
    }
}

/* ======================== 主入口 ======================== */
int AovRunner_Run(const AovRunnerCfg_t *cfg, volatile bool *running)
{
    CamPipeHandle pipe = NULL;
    DataQueueHandle queue = NULL;
    FILE *fd = NULL;
    uint64_t current_file_start_pts = 0;
    int ret = -1;
    int loop_idx = 0;

    if (!cfg || !running) {
        printf("[AOV_RUNNER] invalid input args\n");
        return -1;
    }

    if (cfg->enable_aov) {
        AovArg_t aov_arg;
        memset(&aov_arg, 0, sizeof(aov_arg));
        aov_arg.pfnNotifyCallback = aov_notify_callback;
        Aov_Init(&aov_arg);
        Aov_SetSuspendTime(cfg->suspend_time_ms);
    }

    MountSdcard();
    ensure_dir(cfg->rec_dir);

    pipe = CamPipe_Create((CamPipeCfg_t *)&cfg->pipe_cfg);
    if (!pipe) {
        printf("[AOV_RUNNER] CamPipe_Create failed\n");
        goto cleanup;
    }

    queue = DataQueue_Create(16, DATA_QUEUE_ITEM_MAX);
    if (!queue) {
        printf("[AOV_RUNNER] DataQueue_Create failed\n");
        goto cleanup;
    }

    for (int i = 0; i < cfg->pipe_cfg.output_count; ++i) {
        int route_id = cfg->pipe_cfg.outputs[i].route_id;
        if (CamPipe_RegisterFrameCallback(pipe, route_id,
                                          frame_callback, queue) != 0) {
            printf("[AOV_RUNNER] register callback failed for route[%d]\n", route_id);
            goto cleanup;
        }
    }

    if (CamPipe_Start(pipe) != 0) {
        printf("[AOV_RUNNER] CamPipe_Start failed\n");
        goto cleanup;
    }

    printf("[AOV_RUNNER] AOV recording started, loop_duration=%ds, suspend=%dms\n",
           cfg->loop_duration_sec, cfg->suspend_time_ms);

    while (*running) {
        printf("[AOV_RUNNER] ==== loop %d: recording %ds ====\n",
               loop_idx + 1, cfg->loop_duration_sec);

        record_loop(queue, &fd, cfg->rec_dir, cfg->rec_prefix, cfg->use_h265,
                    cfg->fps, cfg->loop_duration_sec, running, &current_file_start_pts);

        if (!*running)
            break;

        if (cfg->enable_aov) {
            if (fd) {
                fclose(fd);
                fd = NULL;
            }

            /*
             * ★ 核心修复：休眠前必须停止并销毁整个摄像头管线
             *
             * 原因：如果 ISP/VI/VPSS/VENC 等硬件模块仍在运行中，
             * 内核 suspend 时会因为设备 busy 而拒绝进入休眠，
             * 导致写入 /sys/power/state 不生效，功耗无法下降。
             */
            CamPipe_Stop(pipe);
            CamPipe_Destroy(pipe);
            pipe = NULL;

            /* 销毁并重新创建 DataQueue，清空残留帧数据 */
            if (queue) {
                DataQueue_Destroy(queue);
                queue = NULL;
            }

            /* 重置休眠失败标志 */
            g_sleep_failed = false;

            /* 通知回调：卸载 SD 卡 + 关非引导 CPU + 进入休眠 */
            Aov_Notify(AOV_ENTER_SLEEP, NULL);
            if (!*running)
                break;

            if (g_sleep_failed) {
                /*
                 * 休眠失败（如 xhci USB 控制器拒绝 suspend）。
                 * 回调已经卸载了 SD 卡，这里需要重新挂载。
                 * 不输出 "woke up" 信息，继续执行重建流程。
                 */
                printf("[AOV_RUNNER] ++++ sleep FAILED, rebuilding pipeline ++++\n");
                MountSdcard();
            } else {
                printf("[AOV_RUNNER] loop %d: woke up, continuing...\n", loop_idx + 1);
                /* 唤醒后重新挂载 SD 卡 */
                MountSdcard();
            }

            /* 重建 DataQueue */
            queue = DataQueue_Create(16, DATA_QUEUE_ITEM_MAX);
            if (!queue) {
                printf("[AOV_RUNNER] DataQueue_Create failed after wakeup\n");
                goto cleanup;
            }

            /* 重新创建摄像头管线 */
            pipe = CamPipe_Create((CamPipeCfg_t *)&cfg->pipe_cfg);
            if (!pipe) {
                printf("[AOV_RUNNER] CamPipe_Create failed after wakeup\n");
                goto cleanup;
            }

            /* 重新注册帧回调 */
            for (int i = 0; i < cfg->pipe_cfg.output_count; ++i) {
                int route_id = cfg->pipe_cfg.outputs[i].route_id;
                if (CamPipe_RegisterFrameCallback(pipe, route_id,
                                                  frame_callback, queue) != 0) {
                    printf("[AOV_RUNNER] register callback failed for route[%d] after wakeup\n",
                           route_id);
                    goto cleanup;
                }
            }

            /* 重新启动管线 */
            if (CamPipe_Start(pipe) != 0) {
                printf("[AOV_RUNNER] CamPipe_Start failed after wakeup\n");
                goto cleanup;
            }

            /* 重置分片时间戳，重新开始新的文件记录 */
            current_file_start_pts = 0;
        } else {
            /* AOV 未启用时，不清除队列，继续下一轮录像 */
        }

        loop_idx++;
        if (cfg->loop_count > 0 && loop_idx >= cfg->loop_count) {
            printf("[AOV_RUNNER] loop_count=%d reached, exiting\n", cfg->loop_count);
            break;
        }
    }

    ret = 0;

cleanup:
    if (queue)
        DataQueue_Destroy(queue);
    if (fd) {
        fclose(fd);
    }
    if (pipe) {
        CamPipe_Stop(pipe);
        CamPipe_Destroy(pipe);
    }

    UmountSdcard();

    if (cfg->enable_aov) {
        Aov_DeInit();
    }

    printf("[AOV_RUNNER] exit!\n");
    return ret;
}




