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
 *   录像 loop_duration_sec 秒 → 关闭分片 → CamPipe_Suspend（保留管线）
 *   → 轻量卸载 SD → 关USB → 关非引导CPU → 进入系统休眠 suspend_time_ms
 *   → 唤醒 → 恢复CPU/USB（异步） → 轻量挂载 SD
 *   → CamPipe_Resume（恢复ISP 3A + 绑定 + 请求IDR） → 继续录像
 *   → 下一轮循环
 *
 * 优化：
 *   1. ISP pause/resume 替代 stop/init/run（节省 IQ 文件加载）
 *   2. 保留 MPP 管线，仅 suspend/resume（节省 VI/VPSS/VENC 重建）
 *   3. SD 卡轻量 mount/umount（跳过 netlink 5s 超时等待）
 *   4. 唤醒后主动请求 VENC IDR（加速首帧产出）
 *   5. CPU/USB 异步恢复（不阻塞首帧输出）
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
#define RAW_SPLIT_SEC 60*4

/*
 * Aov_EnterSleep() 的结果通过此全局标志传递给主循环。
 * true  = 休眠失败（write /sys/power/state 返回错误），
 *         主循环跳过重建管线，继续下一轮录像。
 */
static bool g_sleep_failed = false;

/* ======================== AOV 休眠回调 ======================== */

/*
 * 休眠回调在 Aov_EnterSleep() 调用前/后执行。
 *
 * 【AOV 休眠前】：
 *   1. UmountSdcardLight — 仅 umount，跳过 unbind（节省 netlink 5s 等待）
 *   2. Aov_DisableUSB — 解绑 xhci，避免阻塞 suspend
 *   3. Aov_DisableNonBootCPUs — 休眠前关非引导核降漏电
 *   4. Aov_EnterSleep — 阻塞，等待 RTC 唤醒
 *
 * 【AOV 唤醒后】：
 *   回调内不再做 CPU/USB 恢复，而是将这个操作移到主循环中异步执行，
 *   避免在 WakeupLock 持有的关键路径上增加延迟。
 *   CPU/USB 的恢复不依赖 MPP 管线，可以和 RecordLoop 并行。
 */
static void aov_notify_callback(AovEvent_e enEvent, void *msg)
{
    int sleep_ret;

    (void)msg;
    switch (enEvent) {
    case AOV_ENTER_SLEEP:
        printf("[AOV_RUNNER] +++++ AOV_ENTER_SLEEP +++++\n");

        Aov_WakeupLock();
        printf("[AOV_RUNNER] fs sdcard lock\n");

        /* ★ 优化3：轻量卸载 SD 卡，跳过驱动解绑（unbind），节省 ~100ms */
        UmountSdcardLight();

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

        /*
         * ★★ 优化5：唤醒后不在回调中恢复 CPU/USB，而是推迟到主循环异步执行。
         * 回调持有 WakeupLock，应尽快释放，让主循环可以恢复管线。
         * CPU/USB 恢复可以和录像并行，不阻塞首帧。
         */

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
    int frame_count = 0;
    int file_count = 0;

    printf("[RECORD_LOOP] START: recording for %d seconds\n", duration_sec);

    while (*running) {
        time_t now = time(NULL);
        if (now >= deadline) {
            printf("[RECORD_LOOP] END: deadline reached (%d frames, %d files)\n", frame_count, file_count);
            break;
        }

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
                        file_count++;
                        printf("[AOV_RUNNER] >>>> new file #%d: %s <<<<\n", file_count, file_path);
                    } else {
                        printf("[AOV_RUNNER] fopen failed: %s\n", file_path);
                    }
                }

                if (*fd_ptr) {
                    void *payload = (unsigned char *)buf + sizeof(FrameMeta_t);
                    fwrite(payload, 1, meta.data_len, *fd_ptr);
                    frame_count++;

                    if (fps > 0 && meta.is_keyframe) {
                        printf("[AOV_RUNNER] route[%d] pts:%llu size:%u keyframe (total frames: %d)\n",
                               meta.tag, meta.timestamp, meta.data_len, frame_count);
                    }
                }
            }
            DataQueue_Release(queue, buf);
        } else if (pop_ret < 0) {
            usleep(5000);
        }
    }
}

/* ======================== 异步 CPU/USB 恢复线程 ======================== */
static void *async_recover_thread(void *arg)
{
    (void)arg;

    printf("[AOV_RUNNER] async: recovering non-boot CPUs...\n");
    Aov_EnableNonBootCPUs();

    printf("[AOV_RUNNER] async: re-binding USB xhci...\n");
    Aov_EnableUSB();

    printf("[AOV_RUNNER] async: recovery done\n");
    return NULL;
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
        printf("[AOV_RUNNER] >>>>>> loop %d START: recording %ds, loop_count=%d >>>>>>\n",
               loop_idx + 1, cfg->loop_duration_sec, cfg->loop_count);

        record_loop(queue, &fd, cfg->rec_dir, cfg->rec_prefix, cfg->use_h265,
                    cfg->fps, cfg->loop_duration_sec, running, &current_file_start_pts);

        printf("[AOV_RUNNER] <<<<<< loop %d END: record_loop returned\n", loop_idx + 1);

        if (!*running) {
            printf("[AOV_RUNNER] *running is false, breaking out of main loop\n");
            break;
        }

        /*
         * 检查是否已经是最后一轮：如果 loop_idx+1 == loop_count，说明当前是最后一轮，
         * 录像完成后不应再进入 AOV 休眠，否则程序会永久阻塞在系统休眠中。
         */
        int is_last_loop = (cfg->loop_count > 0 && loop_idx + 1 >= cfg->loop_count);
        
        if (cfg->enable_aov && !is_last_loop) {
            /*
             * 注意：不再关闭 fd，让文件跨 loop 持续写入。
             * 分片完全由 record_loop 内的 RAW_SPLIT_SEC 宏控制。
             */

            /*
             * 使用 CamPipe_Suspend 轻量暂停管线。
             * 停止流线程 → 暂停 ISP 3A → drain VENC → 请求 IDR。
             * 不销毁 VI/VPSS/VENC 硬件，唤醒后快速恢复。
             */
            ret = CamPipe_Suspend(pipe);
            if (ret != 0) {
                printf("[AOV_RUNNER] CamPipe_Suspend failed, fallback to Stop+Destroy\n");
                CamPipe_Stop(pipe);
                CamPipe_Destroy(pipe);
                pipe = NULL;
            }

            /* 清空 DataQueue 残留帧，避免旧帧污染新录像 */
            DataQueue_Flush(queue);

            /* 重置休眠失败标志 */
            g_sleep_failed = false;

            /* 通知回调：轻量卸载 SD → 关USB → 关非引导CPU → 进入休眠 */
            Aov_Notify(AOV_ENTER_SLEEP, NULL);
            if (!*running)
                break;

            if (g_sleep_failed) {
                printf("[AOV_RUNNER] ++++ sleep FAILED, recovering ++++\n");

                /* 休眠失败，回调已做 UmountSdcardLight + DisableUSB + DisableCPUs */
                /* 需要恢复：CPU → USB → 挂载 SD */
                Aov_EnableNonBootCPUs();
                Aov_EnableUSB();

                if (pipe) {
                    /* 管线已 suspend，恢复 */
                    CamPipe_Resume(pipe);
                } else {
                    /* 回退到完整重建 */
                    MountSdcard();
                    queue = DataQueue_Create(16, DATA_QUEUE_ITEM_MAX);
                    if (!queue) goto cleanup;
                    pipe = CamPipe_Create((CamPipeCfg_t *)&cfg->pipe_cfg);
                    if (!pipe) goto cleanup;
                    for (int i = 0; i < cfg->pipe_cfg.output_count; ++i) {
                        CamPipe_RegisterFrameCallback(pipe, cfg->pipe_cfg.outputs[i].route_id,
                                                      frame_callback, queue);
                    }
                    CamPipe_Start(pipe);
                }
            } else {
                printf("[AOV_RUNNER] loop %d: woke up, continuing...\n", loop_idx + 1);

                /*
                 * ★★ 优化5：异步恢复 CPU/USB，不阻塞首帧
                 *
                 * CPU/USB 恢复不依赖 MPP 管线，可以与管线恢复并行。
                 * 这里先执行 CamPipe_Resume（恢复 ISP 3A + 绑定 + IDR），
                 * CPU/USB 恢复在异步线程中并行完成。
                 */
                pthread_t recover_tid = 0;
                if (pthread_create(&recover_tid, NULL, async_recover_thread,
                                   NULL) != 0) {
                    /* 异步创建失败，同步执行 */
                    Aov_EnableNonBootCPUs();
                    Aov_EnableUSB();
                }

                /* ★ 优化4：轻量挂载 SD 卡，跳过驱动绑定，节省 ~100ms */
                MountSdcardLight();

                if (pipe) {
                    /*
                     * ★★ 优化1+2+4：CamPipe_Resume
                     * 恢复 ISP 3A + 重绑定 MPP + 请求 VENC IDR + 启动流线程
                     */
                    if (CamPipe_Resume(pipe) != 0) {
                        printf("[AOV_RUNNER] CamPipe_Resume failed, fallback to full rebuild\n");
                        /* Resume 失败，管线处于不一致状态，需要完全重建 */
                        CamPipe_Destroy(pipe);
                        pipe = NULL;
                    }
                }
                
                if (!pipe) {
                    /* 回退到完整重建 */
                    printf("[AOV_RUNNER] Rebuilding pipeline from scratch...\n");
                    pipe = CamPipe_Create((CamPipeCfg_t *)&cfg->pipe_cfg);
                    if (!pipe) {
                        printf("[AOV_RUNNER] CamPipe_Create failed during rebuild\n");
                        goto cleanup;
                    }
                    for (int i = 0; i < cfg->pipe_cfg.output_count; ++i) {
                        CamPipe_RegisterFrameCallback(pipe, cfg->pipe_cfg.outputs[i].route_id,
                                                      frame_callback, queue);
                    }
                    if (CamPipe_Start(pipe) != 0) {
                        printf("[AOV_RUNNER] CamPipe_Start failed during rebuild\n");
                        goto cleanup;
                    }
                }

                /* 等待异步 CPU/USB 恢复完成 */
                if (recover_tid) {
                    pthread_join(recover_tid, NULL);
                }
            }
        } else {
            /* AOV 未启用时，不清除队列，继续下一轮录像 */
        }

        loop_idx++;
        printf("[AOV_RUNNER] loop_idx=%d, loop_count=%d, *running=%d\n",
               loop_idx, cfg->loop_count, *running);
        
        if (cfg->loop_count > 0 && loop_idx >= cfg->loop_count) {
            printf("[AOV_RUNNER] >>>> loop_count=%d reached, exiting after %d loops <<<<\n",
                   cfg->loop_count, loop_idx);
            break;
        }
        
        printf("[AOV_RUNNER] >>>> continuing to next loop <<<<\n");
    }
    
    printf("[AOV_RUNNER] Main loop exited after %d loops\n", loop_idx);

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

    /*
     * ★ 关键修复：确保所有 fwrite/fclose 的数据已从 page cache 刷入 SD 卡物理介质。
     *
     * fclose() 仅将数据提交到 kernel page cache，若在 UmountSdcard() 的
     * umount2(MNT_DETACH) + unbind 前未 sync，MMC 驱动移除时未落盘的数据会丢失，
     * 导致录像文件虽被 fclose() 但实际文件为空/不存在。
     */
    sync();

    UmountSdcard();

    if (cfg->enable_aov) {
        Aov_DeInit();
    }

    printf("[AOV_RUNNER] exit!\n");
    return ret;
}


