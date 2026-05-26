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

/* ======================== AOV 休眠回调 ======================== */
static void aov_notify_callback(AovEvent_e enEvent, void *msg)
{
    (void)msg;
    switch (enEvent) {
    case AOV_ENTER_SLEEP:
        printf("[AOV_RUNNER] +++++ AOV_ENTER_SLEEP +++++\n");

        Aov_WakeupLock();
        printf("[AOV_RUNNER] fs sdcard lock\n");
        UmountSdcard();
        Aov_EnterSleep();
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
            Aov_Notify(AOV_ENTER_SLEEP, NULL);
            if (!*running)
                break;

            printf("[AOV_RUNNER] loop %d: woke up, continuing...\n", loop_idx + 1);
            
            // 重新挂载 SD 卡
            MountSdcard();
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




