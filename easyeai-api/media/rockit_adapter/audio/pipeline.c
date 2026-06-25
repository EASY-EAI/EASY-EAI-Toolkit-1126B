#include "pipeline.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../platform/sys/rockchip_sys.h"

typedef struct _AudioPipe {
    AudioPipeCfg_t cfg;
    void *ai_ctx;
    void *aenc_ctx;
    bool sys_inited;
    bool ai_inited;
    bool aenc_inited;
    bool ai_aenc_bound;
    bool started;

    pthread_t stream_tid;
    bool stream_running;

    AudioPipeFrameCallback_t frame_cb;
    void *frame_cb_userdata;
} AudioPipeImpl_t;

static inline bool is_pcm_mode(AudioPipeImpl_t *pipe)
{
    return pipe->cfg.aenc_cfg.codec_type == CAM_ADAPTER_AUDIO_CODEC_PCM;
}

static void audio_pipe_unbind(AudioPipeImpl_t *pipe)
{
    if (!pipe->ai_aenc_bound)
        return;

    rockchip_ai_aenc_unbind(&pipe->cfg.ai_cfg, &pipe->cfg.aenc_cfg);
    pipe->ai_aenc_bound = false;
}

static void *stream_thread(void *arg)
{
    AudioPipeImpl_t *pipe = (AudioPipeImpl_t *)arg;

    while (pipe->stream_running) {
        CamAudioFrame_t frame;

        if (is_pcm_mode(pipe)) {
            if (rockchip_ai_get_frame(pipe->ai_ctx, &frame) == 0) {
                if (pipe->frame_cb) {
                    pipe->frame_cb(frame.data, frame.size, frame.pts,
                                   pipe->cfg.route_id, pipe->frame_cb_userdata);
                }
                rockchip_ai_release_frame(pipe->ai_ctx);
            }
        } else {
            if (rockchip_aenc_get_stream(pipe->aenc_ctx, &frame) == 0) {
                if (pipe->frame_cb) {
                    pipe->frame_cb(frame.data, frame.size, frame.pts,
                                   pipe->cfg.route_id, pipe->frame_cb_userdata);
                }
                rockchip_aenc_release_stream(pipe->aenc_ctx);
            }
        }
    }

    return NULL;
}

AudioPipeHandle AudioPipe_Create(AudioPipeCfg_t *cfg)
{
    AudioPipeImpl_t *pipe;
    int ret;

    if (!cfg)
        return NULL;

    pipe = (AudioPipeImpl_t *)calloc(1, sizeof(AudioPipeImpl_t));
    if (!pipe)
        return NULL;

    memcpy(&pipe->cfg, cfg, sizeof(AudioPipeCfg_t));

    if (rockchip_sys_init() != 0) {
        printf("[AUDIO_PIPE] sys init failed\n");
        goto fail;
    }
    pipe->sys_inited = true;

    if (rockchip_ai_init(&pipe->ai_ctx, &cfg->ai_cfg) != 0) {
        printf("[AUDIO_PIPE] AI init failed\n");
        goto fail;
    }
    pipe->ai_inited = true;

    if (!is_pcm_mode(pipe)) {
        if (rockchip_aenc_init(&pipe->aenc_ctx, &cfg->aenc_cfg) != 0) {
            printf("[AUDIO_PIPE] AENC init failed\n");
            goto fail;
        }
        pipe->aenc_inited = true;

        ret = rockchip_ai_aenc_bind(&cfg->ai_cfg, &cfg->aenc_cfg);
        if (ret != 0) {
            printf("[AUDIO_PIPE] AI->AENC bind failed\n");
            goto fail;
        }
        pipe->ai_aenc_bound = true;
    }

    printf("[AUDIO_PIPE] created: route=%d ai_dev=%d ai_chn=%d %s\n",
           cfg->route_id, cfg->ai_cfg.dev_id, cfg->ai_cfg.chn_id,
           is_pcm_mode(pipe) ? "PCM(RAW)" : "AENC");
    return (AudioPipeHandle)pipe;

fail:
    if (pipe->aenc_inited) {
        rockchip_aenc_deinit(pipe->aenc_ctx);
        pipe->aenc_inited = false;
    }
    if (pipe->ai_inited) {
        rockchip_ai_deinit(pipe->ai_ctx);
        pipe->ai_inited = false;
    }
    if (pipe->sys_inited) {
        rockchip_sys_exit();
        pipe->sys_inited = false;
    }
    free(pipe);
    return NULL;
}

int AudioPipe_Destroy(AudioPipeHandle handle)
{
    AudioPipeImpl_t *pipe = (AudioPipeImpl_t *)handle;

    if (!pipe)
        return -1;

    if (pipe->started)
        AudioPipe_Stop(handle);
    else {
        audio_pipe_unbind(pipe);
        if (pipe->aenc_inited) {
            rockchip_aenc_deinit(pipe->aenc_ctx);
            pipe->aenc_inited = false;
        }
        if (pipe->ai_inited) {
            rockchip_ai_deinit(pipe->ai_ctx);
            pipe->ai_inited = false;
        }
    }

    if (pipe->sys_inited) {
        rockchip_sys_exit();
        pipe->sys_inited = false;
    }

    free(pipe);
    return 0;
}

int AudioPipe_Start(AudioPipeHandle handle)
{
    AudioPipeImpl_t *pipe = (AudioPipeImpl_t *)handle;

    if (!pipe)
        return -1;
    if (pipe->started)
        return 0;

    pipe->stream_running = true;
    if (pthread_create(&pipe->stream_tid, NULL, stream_thread, pipe) != 0) {
        pipe->stream_running = false;
        printf("[AUDIO_PIPE] create stream thread failed\n");
        return -1;
    }

    pipe->started = true;
    return 0;
}

int AudioPipe_Stop(AudioPipeHandle handle)
{
    AudioPipeImpl_t *pipe = (AudioPipeImpl_t *)handle;

    if (!pipe)
        return -1;
    if (!pipe->started)
        return 0;

    pipe->stream_running = false;
    if (pipe->stream_tid) {
        pthread_join(pipe->stream_tid, NULL);
        pipe->stream_tid = 0;
    }

    audio_pipe_unbind(pipe);

    if (pipe->aenc_inited) {
        rockchip_aenc_deinit(pipe->aenc_ctx);
        pipe->aenc_inited = false;
    }
    if (pipe->ai_inited) {
        rockchip_ai_deinit(pipe->ai_ctx);
        pipe->ai_inited = false;
    }

    if (pipe->sys_inited) {
        rockchip_sys_exit();
        pipe->sys_inited = false;
    }

    pipe->started = false;
    return 0;
}

int AudioPipe_RegisterFrameCallback(AudioPipeHandle handle, int route_id,
                                     AudioPipeFrameCallback_t cb, void *user_data)
{
    AudioPipeImpl_t *pipe = (AudioPipeImpl_t *)handle;

    if (!pipe)
        return -1;
    if (pipe->cfg.route_id != route_id)
        return -1;

    pipe->frame_cb = cb;
    pipe->frame_cb_userdata = user_data;
    return 0;
}
