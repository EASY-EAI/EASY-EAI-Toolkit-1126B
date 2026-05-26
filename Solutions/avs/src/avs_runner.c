#include "avs_runner.h"
#include "data_queue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DATA_QUEUE_ITEM_MAX (2 * 1024 * 1024)

typedef struct {
    RtspServer *server;
    unsigned int frame_count;
} RtspRouteCtx_t;

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

static RtspRouteCtx_t *find_route_ctx(const AvsRunnerCfg_t *cfg,
                                      RtspRouteCtx_t *route_ctxs,
                                      int route_id)
{
    for (int i = 0; i < cfg->rtsp_count; ++i) {
        if (cfg->rtsps[i].route_id == route_id)
            return &route_ctxs[i];
    }
    return NULL;
}

int AvsRunner_Run(const AvsRunnerCfg_t *cfg, volatile bool *running)
{
    CamPipeHandle pipe = NULL;
    DataQueueHandle queue = NULL;
    RtspRouteCtx_t route_ctxs[AVS_RUNNER_MAX_RTSPS];
    int ret = -1;

    if (!cfg || !running) {
        printf("[AVS_RUNNER] invalid input args\n");
        return -1;
    }

    if (cfg->rtsp_count <= 0 || cfg->rtsp_count > AVS_RUNNER_MAX_RTSPS) {
        printf("[AVS_RUNNER] invalid rtsp count: %d\n", cfg->rtsp_count);
        return -1;
    }

    memset(route_ctxs, 0, sizeof(route_ctxs));

    pipe = CamPipe_Create((CamPipeCfg_t *)&cfg->pipe_cfg);
    if (!pipe) {
        printf("[AVS_RUNNER] CamPipe_Create failed\n");
        return -1;
    }

    queue = DataQueue_Create(16, DATA_QUEUE_ITEM_MAX);
    if (!queue) {
        printf("[AVS_RUNNER] DataQueue_Create failed\n");
        goto cleanup;
    }

    for (int i = 0; i < cfg->pipe_cfg.output_count; ++i) {
        int route_id = cfg->pipe_cfg.outputs[i].route_id;
        if (CamPipe_RegisterFrameCallback(pipe, route_id,
                                          frame_callback, queue) != 0) {
            printf("[AVS_RUNNER] register callback failed for route[%d]\n", route_id);
            goto cleanup;
        }
    }

    if (CamPipe_Start(pipe) != 0) {
        printf("[AVS_RUNNER] CamPipe_Start failed\n");
        goto cleanup;
    }

    for (int i = 0; i < cfg->rtsp_count; ++i) {
        route_ctxs[i].server = RtspServer_Create(cfg->rtsps[i].rtsp_port,
                                                 cfg->rtsps[i].rtsp_path,
                                                 cfg->rtsps[i].codec);
        if (!route_ctxs[i].server) {
            printf("[AVS_RUNNER] RtspServer_Create failed, port:%d, path:%s\n",
                   cfg->rtsps[i].rtsp_port, cfg->rtsps[i].rtsp_path);
            goto cleanup;
        }
        printf("[AVS_RUNNER] route[%d] ready: rtsp://<board-ip>:%d%s\n",
               cfg->rtsps[i].route_id, cfg->rtsps[i].rtsp_port, cfg->rtsps[i].rtsp_path);
    }

    while (*running) {
        void *buf = NULL;
        unsigned int buf_size = 0;
        int pop_ret = DataQueue_Pop(queue, &buf, &buf_size, 50);

        if (pop_ret > 0 && buf && buf_size >= sizeof(FrameMeta_t)) {
            FrameMeta_t meta;
            memcpy(&meta, buf, sizeof(FrameMeta_t));

            if (meta.magic == FRAME_META_MAGIC &&
                meta.data_len > 0 &&
                buf_size == sizeof(FrameMeta_t) + meta.data_len) {

                void *payload = (unsigned char *)buf + sizeof(FrameMeta_t);
                RtspRouteCtx_t *route_ctx = find_route_ctx(cfg, route_ctxs, meta.tag);
                if (route_ctx && route_ctx->server) {
                    RtspServer_PushFrame(route_ctx->server, payload, meta.data_len,
                                         meta.timestamp);
                    route_ctx->frame_count++;
                    if (cfg->fps > 0 && route_ctx->frame_count % (cfg->fps * 5) == 0) {
                        printf("[AVS_RUNNER] route[%d] pts:%llu size:%u frames:%u\n",
                               meta.tag, meta.timestamp, meta.data_len, route_ctx->frame_count);
                    }
                }
            }
            DataQueue_Release(queue, buf);
        } else if (pop_ret < 0) {
            usleep(10000);
        }
    }

    ret = 0;

cleanup:
    if (queue)
        DataQueue_Destroy(queue);
    for (int i = 0; i < cfg->rtsp_count; ++i) {
        if (route_ctxs[i].server) {
            RtspServer_Destroy(route_ctxs[i].server);
            route_ctxs[i].server = NULL;
        }
    }
    if (pipe) {
        CamPipe_Stop(pipe);
        CamPipe_Destroy(pipe);
    }
    return ret;
}
