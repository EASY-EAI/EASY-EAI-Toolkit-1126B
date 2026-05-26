/*
 * Rockchip RV1126B 平台硬件适配层实现
 *
 * 基于 Rockchip MPP (Media Process Platform) 多媒体接口，提供：
 *   - ISP 图像信号处理（通过 RKAIQ）
 *   - VI（视频输入）初始化和管理
 *   - VPSS（视频处理子系统）初始化和管理
 *   - AVS（全景拼接）初始化和管理
 *   - VENC（视频编码）初始化和管理
 *   - 模块绑定/解绑、码流获取与分发
 *
 * 数据流:
 *   图像 sensor → ISP → VI → VPSS/AVS → VENC → stream_thread → 用户回调
 */

#include "rockchip_cam.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "rk_mpi_sys.h"
#include "rk_mpi_venc.h"
#include "rk_mpi_vi.h"
#include "rk_mpi_vpss.h"
#include "rk_mpi_avs.h"
#include "rk_mpi_sys.h"
#include "rk_mpi_mb.h"

#define GET_STREAM_TIMEOUT_MS 2000

#ifndef RKAIQ
#define RKAIQ
#endif

#ifdef RKAIQ
#include <rk_aiq_user_api_sysctl.h>
#endif

/* ======================== 内部数据结构 ======================== */

/** @brief 编码通道上下文 */
typedef struct {
    int chn_id;
    int width;
    int height;
    int fps;
    int gop;
    int bitrate;
    int codec_type;
    int rc_mode;
    bool inited;
    VENC_STREAM_S stream;
    bool stream_held;
} VencChnCtx_t;

/** @brief VI（视频输入）通道上下文 */
typedef struct {
    int pipe_id;
    int chn_id;
    int width;
    int height;
    int buf_cnt;
    int pix_fmt;
    bool buf_wrap_enable;
    int buf_line;
    bool inited;
} ViChnCtx_t;

/** @brief VPSS（视频处理子系统）组上下文 */
typedef struct {
    int grp_id;
    int channel_count;
    bool chn_enabled[CAM_ADAPTER_MAX_VPSS_CHN];
    bool inited;
} VpssGrpCtx_t;

/** @brief AVS（全景拼接）组上下文 */
typedef struct {
    int grp_id;
    int channel_count;
    bool chn_enabled[CAM_ADAPTER_MAX_AVS_CHN];
    bool inited;
} AvsGrpCtx_t;

/** @brief RK1126B 平台总上下文 */
struct Rk1126bCtx {
    int cam_id;
    int fps;
    bool isp_inited;
    bool mpi_inited;

#ifdef RKAIQ
    rk_aiq_sys_ctx_t *aiq_ctx;
#endif

    ViChnCtx_t vi;
    AvsGrpCtx_t avs;
    VpssGrpCtx_t vpss;
    VencChnCtx_t venc[CAM_ADAPTER_MAX_VENC_CHN];
    int venc_cnt;
};

/**
 * @brief 将上层模块类型转换为 RK MPP 模块 ID
 * @param mod_type 上层模块类型（CAM_MODULE_VI / VPSS / VENC / AVS）
 * @return RK MPP 模块 ID，失败返回 -1
 */
static int to_rk_mod_id(int mod_type)
{
    switch (mod_type) {
    case CAM_MODULE_VI:
        return RK_ID_VI;
    case CAM_MODULE_VPSS:
        return RK_ID_VPSS;
    case CAM_MODULE_VENC:
        return RK_ID_VENC;
    case CAM_MODULE_AVS:
        return RK_ID_AVS;
    default:
        return -1;
    }
}

static int fill_mpp_chn(const CamMppChn_t *cfg, MPP_CHN_S *chn)
{
    int rk_mod = -1;

    if (!cfg || !chn)
        return -1;

    rk_mod = to_rk_mod_id(cfg->mod_type);
    if (rk_mod < 0)
        return -1;

    chn->enModId = (MOD_ID_E)rk_mod;
    chn->s32DevId = cfg->dev_id;
    chn->s32ChnId = cfg->chn_id;
    return 0;
}

/**
 * @brief 创建 RK1126B 平台上下文实例
 * @return 成功返回上下文指针，失败返回 NULL
 */
Rk1126bCtx_t *rockchip_create(void)
{
    Rk1126bCtx_t *ctx = (Rk1126bCtx_t *)calloc(1, sizeof(Rk1126bCtx_t));
    return ctx;
}

/**
 * @brief 初始化 ISP（图像信号处理器）
 *        通过 RKAIQ 库读取标定文件，初始化 ISP 硬件
 * @param ctx 平台上下文
 * @param cfg ISP 配置参数（包含 cam_id、iq_file_dir、hdr_mode 等）
 * @return 成功返回 0，失败返回 -1
 */
int rockchip_isp_init(Rk1126bCtx_t *ctx, CamIspCfg_t *cfg)
{
    if (!ctx || !cfg)
        return -1;

    if (ctx->isp_inited)
        return 0;

    ctx->cam_id = cfg->cam_id;
    ctx->fps = cfg->fps;

#ifdef RKAIQ
    if (!cfg->iq_file_dir) {
        printf("[CAM_ADAPTER] ISP: no iq_file_dir, skip RKAIQ init\n");
        ctx->isp_inited = true;
        return 0;
    }

    int hdr_mode = 0; // RK_AIQ_WORKING_MODE_NORMAL
    switch (cfg->hdr_mode) {
    case CAM_ADAPTER_HDR_HDR2:
        hdr_mode = 0x10; // RK_AIQ_WORKING_MODE_ISP_HDR2
        break;
    case CAM_ADAPTER_HDR_HDR3:
        hdr_mode = 0x20; // RK_AIQ_WORKING_MODE_ISP_HDR3
        break;
    default:
        hdr_mode = 0;
        break;
    }

    printf("[CAM_ADAPTER] ISP: init cam%d iq_dir=%s hdr=%d\n",
           cfg->cam_id, cfg->iq_file_dir, hdr_mode);

    char hdr_str[16];
    snprintf(hdr_str, sizeof(hdr_str), "%d", hdr_mode);
    setenv("HDR_MODE", hdr_str, 1);

    rk_aiq_static_info_t aiq_static_info;
    rk_aiq_uapi_sysctl_enumStaticMetas(cfg->cam_id, &aiq_static_info);

    printf("[CAM_ADAPTER] ISP: sensor_name is %s\n", aiq_static_info.sensor_info.sensor_name);

    ctx->aiq_ctx = rk_aiq_uapi_sysctl_init(aiq_static_info.sensor_info.sensor_name, cfg->iq_file_dir, NULL, NULL);
    if (!ctx->aiq_ctx) {
        printf("[CAM_ADAPTER] ISP: rk_aiq_uapi_sysctl_init failed\n");
        return -1;
    }
#endif

    ctx->isp_inited = true;
    return 0;
}

/**
 * @brief 启动 ISP 运行
 *        通过 RKAIQ 执行 prepare → start 流程，使 ISP 开始处理图像数据
 * @param ctx 平台上下文
 * @param cam_id 摄像头 ID
 * @return 成功返回 0，失败返回 -1
 */
int rockchip_isp_run(Rk1126bCtx_t *ctx, int cam_id)
{
    if (!ctx)
        return -1;

#ifdef RKAIQ
    if (!ctx->aiq_ctx) {
        printf("[CAM_ADAPTER] ISP: aiq_ctx is NULL, skip run\n");
        return -1;
    }

    // Default to WDRMode 0 (normal) or retrieve from saved context if needed
    // Usually hdr_mode is mapped to 0 (normal), 0x10 (HDR2), 0x20 (HDR3)
    int wdr_mode = 0; // Using 0 for now as it's typically normal unless specifically passed
    const char *hdr_mode_env = getenv("HDR_MODE");
    if (hdr_mode_env) {
        wdr_mode = atoi(hdr_mode_env);
    }

    if (rk_aiq_uapi_sysctl_prepare(ctx->aiq_ctx, 0, 0, wdr_mode)) {
        printf("[CAM_ADAPTER] ISP: rk_aiq_uapi_sysctl_prepare failed\n");
        return -1;
    }

    if (rk_aiq_uapi_sysctl_start(ctx->aiq_ctx)) {
        printf("[CAM_ADAPTER] ISP: rk_aiq_uapi_sysctl_start failed\n");
        return -1;
    }
    
    if (ctx->fps > 0) {
        printf("[CAM_ADAPTER] ISP: fps=%d (set via VENC encoder, skip AIQ frame rate)\n", ctx->fps);
    }
#endif
    return 0;
}

/**
 * @brief 初始化 VI（视频输入）通道
 *        配置视频输入设备、绑定管道、设置通道属性
 * @param ctx 平台上下文
 * @param cfg VI 通道配置（包含 pipe_id、chn_id、width、height、pix_fmt 等）
 * @return 成功返回 0，失败返回 -1
 */
int rockchip_vi_init(Rk1126bCtx_t *ctx, CamViCfg_t *cfg)
{
    if (!ctx || !cfg)
        return -1;

    VI_DEV_ATTR_S vi_dev_attr;
    memset(&vi_dev_attr, 0, sizeof(VI_DEV_ATTR_S));

    int ret = RK_MPI_VI_GetDevAttr(cfg->pipe_id, &vi_dev_attr);
    if (ret == RK_ERR_VI_NOT_CONFIG) {
        ret = RK_MPI_VI_SetDevAttr(cfg->pipe_id, &vi_dev_attr);
        if (ret != RK_SUCCESS) {
            printf("[CAM_ADAPTER] VI SetDevAttr[%d] failed: %x\n", cfg->pipe_id, ret);
            return -1;
        }
    } else if (ret != RK_SUCCESS) {
        printf("[CAM_ADAPTER] VI GetDevAttr[%d] failed: %x\n", cfg->pipe_id, ret);
    }

    ret = RK_MPI_VI_GetDevIsEnable(cfg->pipe_id);
    if (ret != RK_SUCCESS) {
        ret = RK_MPI_VI_EnableDev(cfg->pipe_id);
        if (ret != RK_SUCCESS) {
            printf("[CAM_ADAPTER] VI EnableDev[%d] failed: %x\n", cfg->pipe_id, ret);
            return -1;
        }

        VI_DEV_BIND_PIPE_S stBindPipe;
        memset(&stBindPipe, 0, sizeof(VI_DEV_BIND_PIPE_S));
        stBindPipe.u32Num = 1;
        stBindPipe.PipeId[0] = cfg->pipe_id;
        ret = RK_MPI_VI_SetDevBindPipe(cfg->pipe_id, &stBindPipe);
        if (ret != RK_SUCCESS) {
            printf("[CAM_ADAPTER] VI SetDevBindPipe[%d] failed: %x\n", cfg->pipe_id, ret);
            return -1;
        }
    }

    VI_CHN_ATTR_S vi_chn_attr;
    memset(&vi_chn_attr, 0, sizeof(VI_CHN_ATTR_S));

    vi_chn_attr.stSize.u32Width = cfg->width;
    vi_chn_attr.stSize.u32Height = cfg->height;
    vi_chn_attr.enPixelFormat = (PIXEL_FORMAT_E)cfg->pix_fmt;
    vi_chn_attr.enCompressMode = COMPRESS_MODE_NONE;
    vi_chn_attr.stFrameRate.s32SrcFrameRate = -1;
    vi_chn_attr.stFrameRate.s32DstFrameRate = -1;
    vi_chn_attr.u32Depth = 0;

    vi_chn_attr.stIspOpt.stMaxSize.u32Width = cfg->width;
    vi_chn_attr.stIspOpt.stMaxSize.u32Height = cfg->height;
    vi_chn_attr.stIspOpt.u32BufCount = cfg->buf_cnt;
    vi_chn_attr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;

    ret = RK_MPI_VI_SetChnAttr(cfg->pipe_id, cfg->chn_id, &vi_chn_attr);
    if (ret != RK_SUCCESS) {
        printf("[CAM_ADAPTER] VI SetChnAttr failed: %x\n", ret);
        return -1;
    }

    // VI buffer wrap：按行分配缓冲，节省内存（对齐 SDK sample_comm_vi.c）
    if (cfg->buf_wrap_enable) {
        VI_CHN_BUF_WRAP_S stViWrap;
        memset(&stViWrap, 0, sizeof(VI_CHN_BUF_WRAP_S));
        if (cfg->buf_line < 64 || cfg->buf_line > cfg->height) {
            printf("[CAM_ADAPTER] VI buf_wrap line %d out of range [64, %d]\n",
                   cfg->buf_line, cfg->height);
            return -1;
        }
        stViWrap.bEnable = RK_TRUE;
        stViWrap.u32BufLine = cfg->buf_line;
        stViWrap.u32WrapBufferSize = stViWrap.u32BufLine * cfg->width * 3 / 2;
        ret = RK_MPI_VI_SetChnWrapBufAttr(cfg->pipe_id, cfg->chn_id, &stViWrap);
        if (ret != RK_SUCCESS) {
            printf("[CAM_ADAPTER] VI SetChnWrapBufAttr failed: %x\n", ret);
            return -1;
        }
    }

    ret = RK_MPI_VI_EnableChn(cfg->pipe_id, cfg->chn_id);
    if (ret != RK_SUCCESS) {
        printf("[CAM_ADAPTER] VI EnableChn failed: %x\n", ret);
        return -1;
    }

    ctx->vi.pipe_id = cfg->pipe_id;
    ctx->vi.chn_id = cfg->chn_id;
    ctx->vi.width = cfg->width;
    ctx->vi.height = cfg->height;
    ctx->vi.buf_cnt = cfg->buf_cnt;
    ctx->vi.pix_fmt = cfg->pix_fmt;
    ctx->vi.buf_wrap_enable = cfg->buf_wrap_enable;
    ctx->vi.buf_line = cfg->buf_line;
    ctx->vi.inited = true;

    return 0;
}

/**
 * @brief 初始化 AVS（全景拼接）组
 *        创建拼接组、设置通道属性、启动拼接处理
 * @param ctx 平台上下文
 * @param cfg AVS 配置参数（包含拼接模式、输入输出尺寸、标定文件等）
 * @return 成功返回 0，失败返回 -1
 */
int rockchip_avs_init(Rk1126bCtx_t *ctx, CamAvsCfg_t *cfg)
{
    int enabled_chn_count = 0;

    if (!ctx || !cfg)
        return -1;

    if (cfg->cam_num <= 0 || cfg->cam_num > CAM_ADAPTER_MAX_CAMERA_NUM) {
        printf("[CAM_ADAPTER] invalid AVS cam num: %d\n", cfg->cam_num);
        return -1;
    }

    ctx->avs.grp_id = cfg->grp_id;
    ctx->avs.channel_count = cfg->channel_count > 0 ? cfg->channel_count : 1;
    memset(ctx->avs.chn_enabled, 0, sizeof(ctx->avs.chn_enabled));

    AVS_GRP_ATTR_S stAvsGrpAttr;
    memset(&stAvsGrpAttr, 0, sizeof(AVS_GRP_ATTR_S));
    
    stAvsGrpAttr.enMode = cfg->blend_mode;
    stAvsGrpAttr.u32PipeNum = cfg->cam_num;
    stAvsGrpAttr.stGainAttr.enMode = AVS_GAIN_MODE_AUTO;
    stAvsGrpAttr.stOutAttr.enPrjMode = AVS_PROJECTION_EQUIRECTANGULAR;
    stAvsGrpAttr.stOutAttr.stSize.u32Width = cfg->dst_width;
    stAvsGrpAttr.stOutAttr.stSize.u32Height = cfg->dst_height;
    stAvsGrpAttr.bSyncPipe = RK_TRUE;
    stAvsGrpAttr.stFrameRate.s32SrcFrameRate = -1;
    stAvsGrpAttr.stFrameRate.s32DstFrameRate = -1;
    stAvsGrpAttr.stInAttr.stSize.u32Width = cfg->src_width;
    stAvsGrpAttr.stInAttr.stSize.u32Height = cfg->src_height;
    stAvsGrpAttr.stOutAttr.fDistance = cfg->distance;
    if (cfg->calib_file && cfg->calib_file[0]) {
        stAvsGrpAttr.stInAttr.enParamSource = AVS_PARAM_SOURCE_CALIB;
        stAvsGrpAttr.stInAttr.stCalib.pCalibFilePath = (RK_CHAR *)cfg->calib_file;
    }

    int ret = RK_MPI_AVS_CreateGrp(cfg->grp_id, &stAvsGrpAttr);
    if (ret != RK_SUCCESS) {
        printf("[CAM_ADAPTER] AVS CreateGrp failed: %x\n", ret);
        return -1;
    }

    for (int i = 0; i < ctx->avs.channel_count; ++i) {
        AVS_CHN_ATTR_S stAvsChnAttr;
        const CamAvsChannelCfg_t *chn_cfg = NULL;
        int chn_id = 0;
        int width = cfg->dst_width;
        int height = cfg->dst_height;
        bool enabled = true;

        memset(&stAvsChnAttr, 0, sizeof(AVS_CHN_ATTR_S));

        if (cfg->channel_count > 0) {
            chn_cfg = &cfg->channels[i];
            chn_id = chn_cfg->chn_id;
            width = chn_cfg->width;
            height = chn_cfg->height;
            enabled = chn_cfg->enabled;
        } else {
            chn_id = cfg->chn_id;
        }

        if (!enabled)
            continue;

        if (chn_id < 0 || chn_id >= CAM_ADAPTER_MAX_AVS_CHN) {
            printf("[CAM_ADAPTER] invalid AVS chn id: %d\n", chn_id);
            goto fail_destroy;
        }

        stAvsChnAttr.enCompressMode = COMPRESS_MODE_NONE;
        stAvsChnAttr.stFrameRate.s32SrcFrameRate = -1;
        stAvsChnAttr.stFrameRate.s32DstFrameRate = -1;
        stAvsChnAttr.u32FrameBufCnt = 4;
        stAvsChnAttr.u32Depth = 0;
        stAvsChnAttr.u32Width = width;
        stAvsChnAttr.u32Height = height;
        stAvsChnAttr.enDynamicRange = DYNAMIC_RANGE_SDR8;

        ret = RK_MPI_AVS_SetChnAttr(cfg->grp_id, chn_id, &stAvsChnAttr);
        if (ret != RK_SUCCESS) {
            printf("[CAM_ADAPTER] AVS SetChnAttr[%d] failed: %x\n", chn_id, ret);
            goto fail_destroy;
        }

        ret = RK_MPI_AVS_EnableChn(cfg->grp_id, chn_id);
        if (ret != RK_SUCCESS) {
            printf("[CAM_ADAPTER] AVS EnableChn[%d] failed: %x\n", chn_id, ret);
            goto fail_destroy;
        }

        ctx->avs.chn_enabled[chn_id] = true;
        enabled_chn_count++;
    }

    ret = RK_MPI_AVS_StartGrp(cfg->grp_id);
    if (ret != RK_SUCCESS) {
        printf("[CAM_ADAPTER] AVS StartGrp failed: %x\n", ret);
        goto fail_destroy;
    }

    if (enabled_chn_count == 0) {
        printf("[CAM_ADAPTER] no AVS channel enabled\n");
        goto fail_destroy;
    }

    ctx->avs.inited = true;
    return 0;

fail_destroy:
    for (int i = 0; i < CAM_ADAPTER_MAX_AVS_CHN; ++i) {
        if (ctx->avs.chn_enabled[i]) {
            RK_MPI_AVS_DisableChn(cfg->grp_id, i);
            ctx->avs.chn_enabled[i] = false;
        }
    }
    RK_MPI_AVS_DestroyGrp(cfg->grp_id);
    return -1;
}

/**
 * @brief 初始化 VPSS（视频处理子系统）组
 *        创建 VPSS 组、配置各输出通道、启动图像处理
 *        支持 RGA 硬件加速和 AIISP 功能
 * @param ctx 平台上下文
 * @param cfg VPSS 配置参数（包含通道数、各通道分辨率等）
 * @return 成功返回 0，失败返回 -1
 */
int rockchip_vpss_init(Rk1126bCtx_t *ctx, CamVpssCfg_t *cfg)
{
    int enabled_chn_count = 0;

    if (!ctx || !cfg)
        return -1;

    ctx->vpss.grp_id = cfg->grp_id;
    ctx->vpss.channel_count = cfg->channel_count;
    memset(ctx->vpss.chn_enabled, 0, sizeof(ctx->vpss.chn_enabled));

    if (cfg->channel_count <= 0 || cfg->channel_count > CAM_ADAPTER_MAX_VPSS_CHN) {
        printf("[CAM_ADAPTER] invalid VPSS channel count: %d\n", cfg->channel_count);
        return -1;
    }

    VPSS_GRP_ATTR_S stGrpVpssAttr;
    memset(&stGrpVpssAttr, 0, sizeof(VPSS_GRP_ATTR_S));
    stGrpVpssAttr.u32MaxW = 4096;
    stGrpVpssAttr.u32MaxH = 4096;
    stGrpVpssAttr.enPixelFormat = RK_FMT_YUV420SP;
    stGrpVpssAttr.enCompressMode = COMPRESS_MODE_NONE;
    stGrpVpssAttr.stFrameRate.s32SrcFrameRate = -1;
    stGrpVpssAttr.stFrameRate.s32DstFrameRate = -1;
// #ifdef RV1126B
    stGrpVpssAttr.enVProcDev = VIDEO_PROC_DEV_RGA;
// #endif

    int ret = RK_MPI_VPSS_CreateGrp(cfg->grp_id, &stGrpVpssAttr);
    if (ret != RK_SUCCESS) {
        printf("[CAM_ADAPTER] VPSS CreateGrp failed: %x\n", ret);
        return -1;
    }

    ret = RK_MPI_VPSS_SetVProcDev(cfg->grp_id, VIDEO_PROC_DEV_RGA);
    if (ret != RK_SUCCESS) {
        printf("[CAM_ADAPTER] VPSS SetVProcDev failed: %x\n", ret);
        goto fail_destroy;
    }

    // 验证 SetVProcDev 是否生效（对齐 SDK sample_comm_vpss.c）
    {
        VIDEO_PROC_DEV_TYPE_E enTmpVProcDevType;
        ret = RK_MPI_VPSS_GetVProcDev(cfg->grp_id, &enTmpVProcDevType);
        if (ret != RK_SUCCESS) {
            printf("[CAM_ADAPTER] VPSS GetVProcDev failed: %x\n", ret);
            goto fail_destroy;
        }
        printf("[CAM_ADAPTER] VPSS grp %d work unit: %d\n", cfg->grp_id, enTmpVProcDevType);
    }

    ret = RK_MPI_VPSS_ResetGrp(cfg->grp_id);
    if (ret != RK_SUCCESS) {
        printf("[CAM_ADAPTER] VPSS ResetGrp failed: %x\n", ret);
        goto fail_destroy;
    }

    // VPSS 每个输出通道都独立描述，便于后续扩展主/子/第三码流。
    for (int i = 0; i < cfg->channel_count; ++i) {
        const CamVpssChannelCfg_t *chn_cfg = &cfg->channels[i];
        VPSS_CHN_ATTR_S stVpssChnAttr;

        if (!chn_cfg->enabled)
            continue;

        if (chn_cfg->chn_id < 0 || chn_cfg->chn_id >= CAM_ADAPTER_MAX_VPSS_CHN) {
            printf("[CAM_ADAPTER] invalid VPSS chn id: %d\n", chn_cfg->chn_id);
            goto fail_destroy;
        }

        memset(&stVpssChnAttr, 0, sizeof(VPSS_CHN_ATTR_S));
        stVpssChnAttr.enChnMode = (chn_cfg->chn_mode == CAM_VPSS_CHN_MODE_AUTO) ?
                                  VPSS_CHN_MODE_AUTO : VPSS_CHN_MODE_PASSTHROUGH;
        stVpssChnAttr.enCompressMode = COMPRESS_MODE_NONE;
        stVpssChnAttr.enDynamicRange = DYNAMIC_RANGE_SDR8;
        stVpssChnAttr.enPixelFormat = (PIXEL_FORMAT_E)chn_cfg->pix_fmt;
        stVpssChnAttr.stFrameRate.s32SrcFrameRate = -1;
        stVpssChnAttr.stFrameRate.s32DstFrameRate = -1;
        stVpssChnAttr.u32Width = chn_cfg->width;
        stVpssChnAttr.u32Height = chn_cfg->height;
        stVpssChnAttr.u32Depth = 0;

        ret = RK_MPI_VPSS_SetChnAttr(cfg->grp_id, chn_cfg->chn_id, &stVpssChnAttr);
        if (ret != RK_SUCCESS) {
            printf("[CAM_ADAPTER] VPSS SetChnAttr[%d] failed: %x\n", chn_cfg->chn_id, ret);
            goto fail_destroy;
        }

        // 验证 SetChnAttr 是否生效（对齐 SDK sample_comm_vpss.c）
        {
            VPSS_CHN_ATTR_S stChkAttr;
            memset(&stChkAttr, 0, sizeof(VPSS_CHN_ATTR_S));
            ret = RK_MPI_VPSS_GetChnAttr(cfg->grp_id, chn_cfg->chn_id, &stChkAttr);
            if (ret != RK_SUCCESS) {
                printf("[CAM_ADAPTER] VPSS GetChnAttr[%d] failed: %x\n", chn_cfg->chn_id, ret);
            }
        }

        ret = RK_MPI_VPSS_EnableChn(cfg->grp_id, chn_cfg->chn_id);
        if (ret != RK_SUCCESS) {
            printf("[CAM_ADAPTER] VPSS EnableChn[%d] failed: %x\n", chn_cfg->chn_id, ret);
            goto fail_destroy;
        }

        ctx->vpss.chn_enabled[chn_cfg->chn_id] = true;
        enabled_chn_count++;
    }

    if (enabled_chn_count == 0) {
        printf("[CAM_ADAPTER] no VPSS channel enabled\n");
        goto fail_destroy;
    }

    // 先启动 VPSS 组
    ret = RK_MPI_VPSS_StartGrp(cfg->grp_id);
    if (ret != RK_SUCCESS) {
        printf("[CAM_ADAPTER] VPSS StartGrp failed: %x\n", ret);
        goto fail_destroy;
    }

    ctx->vpss.inited = true;
    return 0;

fail_destroy:
    for (int i = 0; i < CAM_ADAPTER_MAX_VPSS_CHN; ++i) {
        if (ctx->vpss.chn_enabled[i])
            RK_MPI_VPSS_DisableChn(cfg->grp_id, i);
    }
    RK_MPI_VPSS_DestroyGrp(cfg->grp_id);
    return -1;
}

/**
 * @brief 初始化 VENC（视频编码）通道
 *        创建编码通道、配置编码参数（编码格式、码率、GOP、帧率等）
 * @param ctx 平台上下文
 * @param cfg 编码通道配置参数
 * @return 成功返回 0，失败返回 -1
 */
int rockchip_venc_init(Rk1126bCtx_t *ctx, CamVencCfg_t *cfg)
{
    if (!ctx || !cfg)
        return -1;

    VENC_CHN_ATTR_S venc_chn_attr;
    memset(&venc_chn_attr, 0, sizeof(VENC_CHN_ATTR_S));

    int stride_width = (cfg->width + 15) & (~15);
    int stride_height = (cfg->height + 15) & (~15);

    switch (cfg->codec_type) {
    case CAM_ADAPTER_CODEC_H265:
        venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_HEVC;
        break;
    case CAM_ADAPTER_CODEC_MJPEG:
        venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_MJPEG;
        break;
    case CAM_ADAPTER_CODEC_H264:
    default:
        venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_AVC;
        break;
    }

    venc_chn_attr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
    venc_chn_attr.stVencAttr.u32MaxPicWidth = cfg->width;
    venc_chn_attr.stVencAttr.u32MaxPicHeight = cfg->height;
    venc_chn_attr.stVencAttr.u32PicWidth = cfg->width;
    venc_chn_attr.stVencAttr.u32PicHeight = cfg->height;
    venc_chn_attr.stVencAttr.u32VirWidth = stride_width;
    venc_chn_attr.stVencAttr.u32VirHeight = stride_height;
    venc_chn_attr.stVencAttr.u32Profile = (cfg->codec_type == CAM_ADAPTER_CODEC_H264) ? 100 : 0;
    venc_chn_attr.stVencAttr.u32StreamBufCnt = 4;
    venc_chn_attr.stVencAttr.u32BufSize = stride_width * stride_height * 3 / 2;

    if (cfg->codec_type == CAM_ADAPTER_CODEC_H264) {
        venc_chn_attr.stRcAttr.enRcMode = (cfg->rc_mode == CAM_ADAPTER_RC_CBR) ? VENC_RC_MODE_H264CBR : VENC_RC_MODE_H264VBR;
        venc_chn_attr.stRcAttr.stH264Cbr.u32Gop = cfg->gop;
        venc_chn_attr.stRcAttr.stH264Cbr.u32BitRate = cfg->bitrate;
        venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateNum = cfg->fps;
        venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateDen = 1;
        venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateNum = cfg->fps;
        venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateDen = 1;
    } else {
        venc_chn_attr.stRcAttr.enRcMode = (cfg->rc_mode == CAM_ADAPTER_RC_CBR) ? VENC_RC_MODE_H265CBR : VENC_RC_MODE_H265VBR;
        venc_chn_attr.stRcAttr.stH265Cbr.u32Gop = cfg->gop;
        venc_chn_attr.stRcAttr.stH265Cbr.u32BitRate = cfg->bitrate;
        venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateNum = cfg->fps;
        venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateDen = 1;
        venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateNum = cfg->fps;
        venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateDen = 1;
    }

    venc_chn_attr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;

    int ret = RK_MPI_VENC_CreateChn(cfg->chn_id, &venc_chn_attr);
    if (ret != RK_SUCCESS) {
        printf("[CAM_ADAPTER] VENC CreateChn[%d] failed: %d\n", cfg->chn_id, ret);
        return -1;
    }

    // 参考帧缓冲区共享：外部入参控制，默认关闭
    if (cfg->ref_buf_share) {
        VENC_CHN_REF_BUF_SHARE_S stVencChnRefBufShare;
        memset(&stVencChnRefBufShare, 0, sizeof(VENC_CHN_REF_BUF_SHARE_S));
        stVencChnRefBufShare.bEnable = RK_TRUE;
        ret = RK_MPI_VENC_SetChnRefBufShareAttr(cfg->chn_id, &stVencChnRefBufShare);
        if (ret != RK_SUCCESS) {
            printf("[CAM_ADAPTER] VENC SetChnRefBufShareAttr[%d] failed: %d\n", cfg->chn_id, ret);
        }
    }

    // buffer wrap：按行分配缓冲区，可节省内存（对齐 SDK sample_comm_venc.c）
    if (cfg->buf_wrap_enable) {
        VENC_CHN_BUF_WRAP_S stVencChnBufWrap;
        memset(&stVencChnBufWrap, 0, sizeof(VENC_CHN_BUF_WRAP_S));
        stVencChnBufWrap.bEnable = RK_TRUE;
        stVencChnBufWrap.u32BufLine = cfg->buf_line;

        ret = RK_MPI_VENC_SetChnBufWrapAttr(cfg->chn_id, &stVencChnBufWrap);
        if (ret != RK_SUCCESS) {
            printf("[CAM_ADAPTER] VENC SetChnBufWrapAttr[%d] failed: %d\n", cfg->chn_id, ret);
        }

        // buf_wrap 开启时强制开启 RefBufShare
        if (!cfg->ref_buf_share) {
            VENC_CHN_REF_BUF_SHARE_S stRefBuf;
            memset(&stRefBuf, 0, sizeof(VENC_CHN_REF_BUF_SHARE_S));
            stRefBuf.bEnable = RK_TRUE;
            RK_MPI_VENC_SetChnRefBufShareAttr(cfg->chn_id, &stRefBuf);
        }
    }

    // 设置码控参数 QP 范围（对齐 SDK sample_comm_venc.c）
    VENC_RC_PARAM_S stRcParam;
    memset(&stRcParam, 0, sizeof(VENC_RC_PARAM_S));
    if (cfg->codec_type == CAM_ADAPTER_CODEC_H264) {
        stRcParam.stParamH264.u32MinQp = 10;
        stRcParam.stParamH264.u32MaxQp = 51;
        stRcParam.stParamH264.u32MinIQp = 10;
        stRcParam.stParamH264.u32MaxIQp = 51;
        stRcParam.stParamH264.u32FrmMinQp = 28;
        stRcParam.stParamH264.u32FrmMinIQp = 28;
    } else if (cfg->codec_type == CAM_ADAPTER_CODEC_H265) {
        stRcParam.stParamH265.u32MinQp = 10;
        stRcParam.stParamH265.u32MaxQp = 51;
        stRcParam.stParamH265.u32MinIQp = 10;
        stRcParam.stParamH265.u32MaxIQp = 51;
        stRcParam.stParamH265.u32FrmMinQp = 28;
        stRcParam.stParamH265.u32FrmMinIQp = 28;
    }
    ret = RK_MPI_VENC_SetRcParam(cfg->chn_id, &stRcParam);
    if (ret != RK_SUCCESS) {
        printf("[CAM_ADAPTER] VENC SetRcParam[%d] failed: %d\n", cfg->chn_id, ret);
    }

    // SVC 可伸缩视频编码（对齐 SDK）
    if (cfg->svc_enable) {
        ret = RK_MPI_VENC_EnableSvc(cfg->chn_id, RK_TRUE);
        if (ret != RK_SUCCESS) {
            printf("[CAM_ADAPTER] VENC EnableSvc[%d] failed: %d\n", cfg->chn_id, ret);
        }
    }

    // 运动去模糊（对齐 SDK）
    if (cfg->motion_deblur_enable) {
        ret = RK_MPI_VENC_EnableMotionDeblur(cfg->chn_id, RK_TRUE);
        if (ret != RK_SUCCESS) {
            printf("[CAM_ADAPTER] VENC EnableMotionDeblur[%d] failed: %d\n", cfg->chn_id, ret);
        }
    }

    int idx = ctx->venc_cnt;
    if (idx >= CAM_ADAPTER_MAX_VENC_CHN) {
        printf("[CAM_ADAPTER] VENC channel count exceeds max\n");
        RK_MPI_VENC_DestroyChn(cfg->chn_id);
        return -1;
    }

    ctx->venc[idx].chn_id = cfg->chn_id;
    ctx->venc[idx].width = cfg->width;
    ctx->venc[idx].height = cfg->height;
    ctx->venc[idx].fps = cfg->fps;
    ctx->venc[idx].gop = cfg->gop;
    ctx->venc[idx].bitrate = cfg->bitrate;
    ctx->venc[idx].codec_type = cfg->codec_type;
    ctx->venc[idx].rc_mode = cfg->rc_mode;
    ctx->venc[idx].inited = true;
    ctx->venc[idx].stream_held = false;
    memset(&ctx->venc[idx].stream, 0, sizeof(VENC_STREAM_S));
    ctx->venc[idx].stream.pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S));
    ctx->venc[idx].stream.u32PackCount = 1;
    ctx->venc_cnt++;

    // StartRecvFrame 提前到创建阶段，确保绑定时 VENC 已经就绪（对齐 SDK）
    VENC_RECV_PIC_PARAM_S recv_param;
    recv_param.s32RecvPicNum = -1;
    ret = RK_MPI_VENC_StartRecvFrame(cfg->chn_id, &recv_param);
    if (ret != RK_SUCCESS) {
        printf("[CAM_ADAPTER] VENC StartRecvFrame[%d] failed: %d\n", cfg->chn_id, ret);
    }

    return 0;
}

int rockchip_get_venc_stream(Rk1126bCtx_t *ctx, int chn_id, int codec_type,
                              CamFrameData_t *frame, int timeout_ms)
{
    if (!ctx || !frame)
        return -1;

    for (int i = 0; i < ctx->venc_cnt; i++) {
        VencChnCtx_t *vc = &ctx->venc[i];
        if (vc->chn_id != chn_id || !vc->inited)
            continue;

        memset(vc->stream.pstPack, 0, sizeof(VENC_PACK_S));
        int ret = RK_MPI_VENC_GetStream(chn_id, &vc->stream, timeout_ms);
        if (ret != RK_SUCCESS)
            return -1;

        if (!vc->stream.pstPack || !vc->stream.pstPack->pMbBlk || vc->stream.pstPack->u32Len == 0) {
            RK_MPI_VENC_ReleaseStream(chn_id, &vc->stream);
            return -1;
        }

        void *vaddr = RK_MPI_MB_Handle2VirAddr(vc->stream.pstPack->pMbBlk);
        if (!vaddr) {
            RK_MPI_VENC_ReleaseStream(chn_id, &vc->stream);
            return -1;
        }

        int is_keyframe = 0;
        if (codec_type == CAM_ADAPTER_CODEC_H264) {
            is_keyframe = (vc->stream.pstPack->DataType.enH264EType == H264E_NALU_IDRSLICE) ? 1 : 0;
        } else if (codec_type == CAM_ADAPTER_CODEC_H265) {
            is_keyframe = (vc->stream.pstPack->DataType.enH265EType == H265E_NALU_IDRSLICE) ? 1 : 0;
        }

        frame->data = vaddr;
        frame->data_len = vc->stream.pstPack->u32Len;
        frame->pts = vc->stream.pstPack->u64PTS;
        frame->is_keyframe = is_keyframe;

        vc->stream_held = true;
        return 0;
    }

    return -1;
}

void rockchip_release_venc_stream(Rk1126bCtx_t *ctx, int chn_id)
{
    if (!ctx)
        return;

    for (int i = 0; i < ctx->venc_cnt; i++) {
        if (ctx->venc[i].chn_id == chn_id && ctx->venc[i].stream_held) {
            RK_MPI_VENC_ReleaseStream(chn_id, &ctx->venc[i].stream);
            ctx->venc[i].stream_held = false;
            break;
        }
    }
}

/**
 * @brief 绑定两个模块（如 VI→VPSS、VPSS→VENC）
 *        建立模块间的数据传输通道
 * @param ctx 平台上下文
 * @param cfg 绑定配置（源模块和目标模块信息）
 * @return 成功返回 0，失败返回 -1
 */
int rockchip_bind_modules(Rk1126bCtx_t *ctx, CamBindCfg_t *cfg)
{
    if (!ctx)
        return -1;

    MPP_CHN_S stSrcChn, stDestChn;
    int ret = 0;

    if (!cfg || fill_mpp_chn(&cfg->src, &stSrcChn) != 0 ||
        fill_mpp_chn(&cfg->dst, &stDestChn) != 0) {
        printf("[CAM_ADAPTER] invalid bind cfg\n");
        return -1;
    }

    ret = RK_MPI_SYS_Bind(&stSrcChn, &stDestChn);
    if (ret != RK_SUCCESS) {
        printf("[CAM_ADAPTER] Bind mod[%d,%d,%d] -> mod[%d,%d,%d] failed: %d\n",
               cfg->src.mod_type, cfg->src.dev_id, cfg->src.chn_id,
               cfg->dst.mod_type, cfg->dst.dev_id, cfg->dst.chn_id, ret);
    }

    return ret;
}

/**
 * @brief 解绑两个模块
 *        断开模块间的数据传输通道
 * @param ctx 平台上下文
 * @param cfg 解绑配置（源模块和目标模块信息）
 * @return 成功返回 0，失败返回 -1
 */
int rockchip_unbind_modules(Rk1126bCtx_t *ctx, CamBindCfg_t *cfg)
{
    if (!ctx)
        return -1;

    MPP_CHN_S stSrcChn, stDestChn;
    int ret = 0;

    if (!cfg || fill_mpp_chn(&cfg->src, &stSrcChn) != 0 ||
        fill_mpp_chn(&cfg->dst, &stDestChn) != 0) {
        printf("[CAM_ADAPTER] invalid unbind cfg\n");
        return -1;
    }

    ret = RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
    if (ret != RK_SUCCESS) {
        printf("[CAM_ADAPTER] UnBind mod[%d,%d,%d] -> mod[%d,%d,%d] failed: %d\n",
               cfg->src.mod_type, cfg->src.dev_id, cfg->src.chn_id,
               cfg->dst.mod_type, cfg->dst.dev_id, cfg->dst.chn_id, ret);
    }

    return ret;
}

/**
 * @brief 反初始化 VI（视频输入）通道
 * @param ctx 平台上下文
 * @param vi_pipe VI 管道 ID
 * @param vi_chn VI 通道 ID
 * @return 成功返回 0，失败返回 -1
 */
int rockchip_vi_deinit(Rk1126bCtx_t *ctx, int vi_pipe, int vi_chn)
{
    if (!ctx) return -1;
    RK_MPI_VI_DisableChn(vi_pipe, vi_chn);

    RK_MPI_VI_DisableDev(vi_pipe);
    RK_MPI_SYS_WaitFreeMB();
    return 0;
}

/**
 * @brief 反初始化 VPSS（视频处理子系统）组
 *        停止 VPSS 组、禁用并释放所有通道
 * @param ctx 平台上下文
 * @param grp_id VPSS 组 ID
 * @return 成功返回 0，失败返回 -1
 */
int rockchip_vpss_deinit(Rk1126bCtx_t *ctx, int grp_id)
{
    if (!ctx || !ctx->vpss.inited)
        return 0;

    if (ctx->vpss.grp_id != grp_id)
        return 0;

    RK_MPI_VPSS_StopGrp(ctx->vpss.grp_id);
    for (int i = 0; i < CAM_ADAPTER_MAX_VPSS_CHN; ++i) {
        if (ctx->vpss.chn_enabled[i]) {
            RK_MPI_VPSS_DisableChn(ctx->vpss.grp_id, i);
            ctx->vpss.chn_enabled[i] = false;
        }
    }

    RK_MPI_VPSS_DestroyGrp(ctx->vpss.grp_id);
    ctx->vpss.inited = false;
    ctx->vpss.channel_count = 0;

    return 0;
}

/**
 * @brief 反初始化 AVS（全景拼接）组
 *        停止拼接组、禁用并释放所有通道
 * @param ctx 平台上下文
 * @param grp_id AVS 组 ID
 * @return 成功返回 0，失败返回 -1
 */
int rockchip_avs_deinit(Rk1126bCtx_t *ctx, int grp_id)
{
    if (!ctx || !ctx->avs.inited)
        return 0;
    if (ctx->avs.grp_id != grp_id)
        return 0;

    RK_MPI_AVS_StopGrp(grp_id);
    for (int i = 0; i < CAM_ADAPTER_MAX_AVS_CHN; ++i) {
        if (ctx->avs.chn_enabled[i]) {
            RK_MPI_AVS_DisableChn(grp_id, i);
            ctx->avs.chn_enabled[i] = false;
        }
    }
    RK_MPI_AVS_DestroyGrp(grp_id);
    ctx->avs.inited = false;
    ctx->avs.channel_count = 0;
    return 0;
}

/**
 * @brief 反初始化 VENC（视频编码）通道
 *        停止接收帧并销毁编码通道
 * @param ctx 平台上下文
 * @param venc_chn 编码通道 ID
 * @return 成功返回 0，失败返回 -1
 */
int rockchip_venc_deinit(Rk1126bCtx_t *ctx, int venc_chn)
{
    if (!ctx) return -1;

    for (int i = 0; i < ctx->venc_cnt; i++) {
        if (ctx->venc[i].chn_id == venc_chn) {
            ctx->venc[i].inited = false;
            if (ctx->venc[i].stream.pstPack) {
                free(ctx->venc[i].stream.pstPack);
                ctx->venc[i].stream.pstPack = NULL;
            }
            ctx->venc[i].stream_held = false;
        }
    }

    RK_MPI_VENC_StopRecvFrame(venc_chn);
    RK_MPI_VENC_DestroyChn(venc_chn);
    return 0;
}

/**
 * @brief 停止 ISP 并释放 AIQ 资源
 * @param ctx 平台上下文
 * @param cam_id 摄像头 ID
 * @return 成功返回 0，失败返回 -1
 */
int rockchip_isp_stop(Rk1126bCtx_t *ctx, int cam_id) {
    if (!ctx || !ctx->isp_inited)
        return 0;

#ifdef RKAIQ
    if (ctx->aiq_ctx) {
        rk_aiq_uapi_sysctl_stop(ctx->aiq_ctx, false);
        rk_aiq_uapi_sysctl_deinit(ctx->aiq_ctx);
        ctx->aiq_ctx = NULL;
    }
#endif

    usleep(50000);

    ctx->isp_inited = false;
    return 0;
}

/**
 * @brief 销毁平台上下文，释放所有资源
 * @param ctx 平台上下文
 * @return 成功返回 0
 */
int rockchip_destroy(Rk1126bCtx_t *ctx)
{
    if (!ctx)
        return 0;

    free(ctx);

    return 0;
}
