#include "pipeline.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../platform/sys/rockchip_sys.h"

typedef struct _CamPipe CamPipeImpl_t;

typedef struct {
    Rk1126bCtx_t *ctx;
    bool created;
    bool isp_running;
    bool vi_inited;
    bool vpss_inited;
} CamPipeCameraRt_t;

typedef struct {
    bool inited;
    int grp_id;
} CamPipeAvsRt_t;

typedef struct {
    bool inited;
    int route_id;
    int venc_chn_id;
    int codec_type;
    CamPipeFrameCallback_t frame_cb;
    void *frame_cb_userdata;
} CamPipeOutputRt_t;

struct _CamPipe {
    CamPipeCfg_t cfg;
    Rk1126bCtx_t *graph_ctx;
    bool graph_created;
    CamPipeCameraRt_t cameras[CAM_PIPE_MAX_CAMERAS];
    CamPipeAvsRt_t avss[CAM_PIPE_MAX_AVS];
    CamPipeOutputRt_t outputs[CAM_PIPE_MAX_OUTPUTS];
    bool sys_inited;
    bool started;

    pthread_t stream_tid;
    bool stream_running;
};

static void *stream_thread(void *arg)
{
    CamPipeImpl_t *pipe = (CamPipeImpl_t *)arg;

    while (pipe->stream_running) {
        bool got_frame = false;
        for (int i = 0; i < pipe->cfg.output_count; i++) {
            CamPipeOutputRt_t *output_rt = &pipe->outputs[i];
            if (!output_rt->inited || !pipe->stream_running)
                continue;

            CamFrameData_t frame;
            if (rockchip_get_venc_stream(pipe->graph_ctx, output_rt->venc_chn_id,
                                          output_rt->codec_type, &frame, 200) == 0) {
                if (output_rt->frame_cb) {
                    output_rt->frame_cb(frame.data, frame.data_len,
                                        frame.pts,
                                        output_rt->route_id, frame.is_keyframe,
                                        output_rt->frame_cb_userdata);
                }
                rockchip_release_venc_stream(pipe->graph_ctx, output_rt->venc_chn_id);
                got_frame = true;
            }
        }

        if (!got_frame) {
            usleep(2000);
        }
    }

    return NULL;
}

static int camera_has_vpss_binds(const CamPipeCfg_t *cfg, int camera_index)
{
    for (int i = 0; i < cfg->bind_count; ++i) {
        const CamPipeBindCfg_t *bind = &cfg->binds[i];
        if (bind->camera_index != camera_index)
            continue;
        if (bind->bind_cfg.src.mod_type == CAM_MODULE_VPSS ||
            bind->bind_cfg.dst.mod_type == CAM_MODULE_VPSS)
            return 1;
    }
    return 0;
}

static void cleanup_outputs(CamPipeImpl_t *pipe)
{
    if (!pipe || !pipe->graph_ctx)
        return;
    for (int i = pipe->cfg.output_count - 1; i >= 0; --i) {
        CamPipeOutputRt_t *output_rt = &pipe->outputs[i];
        if (!output_rt->inited)
            continue;
        rockchip_venc_deinit(pipe->graph_ctx, output_rt->venc_chn_id);
        output_rt->inited = false;
    }
}

static void cleanup_avss(CamPipeImpl_t *pipe)
{
    if (!pipe || !pipe->graph_ctx)
        return;
    for (int i = pipe->cfg.avs_count - 1; i >= 0; --i) {
        CamPipeAvsRt_t *avs_rt = &pipe->avss[i];
        if (!avs_rt->inited)
            continue;
        rockchip_avs_deinit(pipe->graph_ctx, avs_rt->grp_id);
        avs_rt->inited = false;
    }
}

static void cleanup_binds(CamPipeImpl_t *pipe)
{
    if (!pipe || !pipe->graph_ctx)
        return;
    for (int i = pipe->cfg.bind_count - 1; i >= 0; --i) {
        rockchip_unbind_modules(pipe->graph_ctx, &pipe->cfg.binds[i].bind_cfg);
    }
}

static void cleanup_cameras(CamPipeImpl_t *pipe)
{
    if (!pipe)
        return;
    for (int i = pipe->cfg.camera_count - 1; i >= 0; --i) {
        CamPipeCameraRt_t *camera_rt = &pipe->cameras[i];
        const CamPipeCameraCfg_t *camera_cfg = &pipe->cfg.cameras[i];
        if (camera_rt->vpss_inited) {
            rockchip_vpss_deinit(camera_rt->ctx, camera_cfg->vpss_cfg.grp_id);
            camera_rt->vpss_inited = false;
        }
        if (camera_rt->vi_inited) {
            rockchip_vi_deinit(camera_rt->ctx,
                              camera_cfg->vi_cfg.pipe_id,
                              camera_cfg->vi_cfg.chn_id);
            camera_rt->vi_inited = false;
        }
        if (camera_rt->isp_running) {
            rockchip_isp_stop(camera_rt->ctx, camera_cfg->isp_cfg.cam_id);
            camera_rt->isp_running = false;
        }
        if (camera_rt->created) {
            rockchip_destroy(camera_rt->ctx);
            camera_rt->ctx = NULL;
            camera_rt->created = false;
        }
    }
}

static int validate_cfg(const CamPipeCfg_t *cfg)
{
    if (!cfg)
        return -1;
    if (cfg->camera_count <= 0 || cfg->camera_count > CAM_PIPE_MAX_CAMERAS)
        return -1;
    if (cfg->avs_count < 0 || cfg->avs_count > CAM_PIPE_MAX_AVS)
        return -1;
    if (cfg->output_count <= 0 || cfg->output_count > CAM_PIPE_MAX_OUTPUTS)
        return -1;
    if (cfg->bind_count < 0 || cfg->bind_count > CAM_PIPE_MAX_BINDS)
        return -1;
    return 0;
}

CamPipeHandle CamPipe_Create(CamPipeCfg_t *cfg)
{
    CamPipeImpl_t *pipe = NULL;

    if (validate_cfg(cfg) != 0)
        return NULL;

    pipe = (CamPipeImpl_t *)calloc(1, sizeof(CamPipeImpl_t));
    if (!pipe)
        return NULL;

    memcpy(&pipe->cfg, cfg, sizeof(CamPipeCfg_t));

    pipe->graph_ctx = rockchip_create();
    if (!pipe->graph_ctx) {
        free(pipe);
        return NULL;
    }
    pipe->graph_created = true;

    for (int i = 0; i < pipe->cfg.camera_count; ++i) {
        CamPipeCameraRt_t *camera_rt = &pipe->cameras[i];
        CamPipeCameraCfg_t *camera_cfg = &pipe->cfg.cameras[i];

        camera_rt->ctx = rockchip_create();
        if (!camera_rt->ctx) {
            printf("[CAM_PIPE] rockchip_create failed for camera[%d]\n", i);
            goto fail;
        }
        camera_rt->created = true;

        if (rockchip_isp_init(camera_rt->ctx, &camera_cfg->isp_cfg) != 0) {
            printf("[CAM_PIPE] ISP init failed for camera[%d]\n", i);
            goto fail;
        }
        if (rockchip_isp_run(camera_rt->ctx, camera_cfg->isp_cfg.cam_id) != 0) {
            printf("[CAM_PIPE] ISP run failed for camera[%d]\n", i);
            goto fail;
        }
        camera_rt->isp_running = true;
    }

    if (rockchip_sys_init() != 0)
        goto fail;
    pipe->sys_inited = true;

    for (int i = 0; i < pipe->cfg.camera_count; ++i) {
        CamPipeCameraRt_t *camera_rt = &pipe->cameras[i];
        CamPipeCameraCfg_t *camera_cfg = &pipe->cfg.cameras[i];

        if (rockchip_vi_init(camera_rt->ctx, &camera_cfg->vi_cfg) != 0) {
            printf("[CAM_PIPE] VI init failed for camera[%d]\n", i);
            goto fail;
        }
        camera_rt->vi_inited = true;

        if (camera_cfg->enable_vpss || camera_has_vpss_binds(&pipe->cfg, i)) {
            if (rockchip_vpss_init(camera_rt->ctx, &camera_cfg->vpss_cfg) != 0) {
                printf("[CAM_PIPE] VPSS init failed for camera[%d]\n", i);
                goto fail;
            }
            camera_rt->vpss_inited = true;
        }
    }

    for (int i = 0; i < pipe->cfg.avs_count; ++i) {
        if (rockchip_avs_init(pipe->graph_ctx, &pipe->cfg.avss[i].avs_cfg) != 0) {
            printf("[CAM_PIPE] AVS init failed for avs[%d]\n", i);
            goto fail;
        }
        pipe->avss[i].inited = true;
        pipe->avss[i].grp_id = pipe->cfg.avss[i].avs_cfg.grp_id;
    }

    for (int i = 0; i < pipe->cfg.output_count; ++i) {
        CamPipeOutputRt_t *output_rt = &pipe->outputs[i];
        CamPipeOutputCfg_t *output_cfg = &pipe->cfg.outputs[i];

        if (rockchip_venc_init(pipe->graph_ctx, &output_cfg->venc_cfg) != 0) {
            printf("[CAM_PIPE] VENC init failed for output[%d]\n", i);
            goto fail;
        }

        output_rt->inited = true;
        output_rt->route_id = output_cfg->route_id;
        output_rt->venc_chn_id = output_cfg->venc_cfg.chn_id;
        output_rt->codec_type = output_cfg->venc_cfg.codec_type;
    }

    for (int i = 0; i < pipe->cfg.bind_count; ++i) {
        if (rockchip_bind_modules(pipe->graph_ctx, &pipe->cfg.binds[i].bind_cfg) != 0) {
            printf("[CAM_PIPE] bind[%d] failed\n", i);
            goto fail;
        }
    }

    printf("[CAM_PIPE] graph created: cameras=%d, avs=%d, outputs=%d, binds=%d\n",
           pipe->cfg.camera_count, pipe->cfg.avs_count,
           pipe->cfg.output_count, pipe->cfg.bind_count);
    return (CamPipeHandle)pipe;

fail:
    cleanup_binds(pipe);
    cleanup_outputs(pipe);
    cleanup_avss(pipe);
    cleanup_cameras(pipe);
    if (pipe->sys_inited) {
        rockchip_sys_exit();
        pipe->sys_inited = false;
    }
    if (pipe->graph_created) {
        rockchip_destroy(pipe->graph_ctx);
        pipe->graph_ctx = NULL;
        pipe->graph_created = false;
    }
    free(pipe);
    return NULL;
}

int CamPipe_RegisterFrameCallback(CamPipeHandle handle, int route_id,
                                  CamPipeFrameCallback_t cb, void *user_data)
{
    CamPipeImpl_t *pipe = (CamPipeImpl_t *)handle;

    if (!pipe)
        return -1;

    for (int i = 0; i < pipe->cfg.output_count; ++i) {
        CamPipeOutputRt_t *output_rt = &pipe->outputs[i];
        if (output_rt->route_id == route_id) {
            output_rt->frame_cb = cb;
            output_rt->frame_cb_userdata = user_data;
            return 0;
        }
    }

    return -1;
}

int CamPipe_Start(CamPipeHandle handle)
{
    CamPipeImpl_t *pipe = (CamPipeImpl_t *)handle;

    if (!pipe || !pipe->graph_ctx)
        return -1;
    if (pipe->started)
        return 0;

    pipe->stream_running = true;
    if (pthread_create(&pipe->stream_tid, NULL, stream_thread, pipe) != 0) {
        pipe->stream_running = false;
        printf("[CAM_PIPE] create stream thread failed\n");
        return -1;
    }

    pipe->started = true;
    return 0;
}

int CamPipe_Stop(CamPipeHandle handle)
{
    CamPipeImpl_t *pipe = (CamPipeImpl_t *)handle;

    if (!pipe)
        return -1;
    if (!pipe->started)
        return 0;

    pipe->stream_running = false;
    if (pipe->stream_tid) {
        pthread_join(pipe->stream_tid, NULL);
        pipe->stream_tid = 0;
    }

    usleep(50000);

    cleanup_binds(pipe);

    usleep(50000);

    cleanup_outputs(pipe);
    cleanup_avss(pipe);
    cleanup_cameras(pipe);

    if (pipe->sys_inited) {
        rockchip_sys_exit();
        pipe->sys_inited = false;
    }
    if (pipe->graph_created) {
        rockchip_destroy(pipe->graph_ctx);
        pipe->graph_ctx = NULL;
        pipe->graph_created = false;
    }

    pipe->started = false;
    return 0;
}

int CamPipe_Destroy(CamPipeHandle handle)
{
    CamPipeImpl_t *pipe = (CamPipeImpl_t *)handle;

    if (!pipe)
        return -1;

    if (pipe->started)
        CamPipe_Stop(handle);
    else {
        cleanup_binds(pipe);
        cleanup_outputs(pipe);
        cleanup_avss(pipe);
        cleanup_cameras(pipe);
        if (pipe->sys_inited) {
            rockchip_sys_exit();
            pipe->sys_inited = false;
        }
        if (pipe->graph_created) {
            rockchip_destroy(pipe->graph_ctx);
            pipe->graph_ctx = NULL;
            pipe->graph_created = false;
        }
    }

    free(pipe);
    return 0;
}
