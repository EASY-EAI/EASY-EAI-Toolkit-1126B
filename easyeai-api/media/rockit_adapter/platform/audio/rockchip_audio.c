/*
 * Rockchip RV1126B 平台音频适配层实现
 *
 * 基于 Rockchip MPP 多媒体接口，实现：
 *   - AI（音频输入）采集
 *   - AENC（音频编码）
 *
 * 对齐 SDK sample_comm_ai.c / sample_comm_aenc.c
 */

#include "rockchip_audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rk_mpi_ai.h"
#include "rk_mpi_aenc.h"
#include "rk_mpi_mb.h"
#include "rk_mpi_sys.h"
#include "rk_mpi_amix.h"

#define LOG_TAG "[CAM_AUDIO]"

/* ======================== AI (Audio Input) ======================== */

typedef struct {
    int dev_id;
    int chn_id;
    AUDIO_FRAME_S stFrame;
    bool inited;
} AiCtx_t;

int rockchip_ai_init(void **ai_ctx, const CamAiCfg_t *cfg)
{
    AiCtx_t *ctx;
    int ret;

    if (!ai_ctx || !cfg)
        return -1;

    ctx = (AiCtx_t *)calloc(1, sizeof(AiCtx_t));
    if (!ctx)
        return -1;

    ctx->dev_id = cfg->dev_id;
    ctx->chn_id = cfg->chn_id;

    // 配置音频输入属性（对齐 SDK sample_comm_ai.c）
    AIO_ATTR_S stAiAttr;
    memset(&stAiAttr, 0, sizeof(AIO_ATTR_S));

    AUDIO_SAMPLE_RATE_E sample_rate_e;
    switch (cfg->sample_rate) {
    case 8000:  sample_rate_e = AUDIO_SAMPLE_RATE_8000;  break;
    case 16000: sample_rate_e = AUDIO_SAMPLE_RATE_16000; break;
    case 44100: sample_rate_e = AUDIO_SAMPLE_RATE_44100; break;
    case 48000: sample_rate_e = AUDIO_SAMPLE_RATE_48000; break;
    default:
        printf(LOG_TAG " unsupported sample rate: %d\n", cfg->sample_rate);
        free(ctx);
        return -1;
    }

    AUDIO_BIT_WIDTH_E bit_width_e;
    switch (cfg->bit_width) {
    case 16: bit_width_e = AUDIO_BIT_WIDTH_16; break;
    case 24: bit_width_e = AUDIO_BIT_WIDTH_24; break;
    case 32: bit_width_e = AUDIO_BIT_WIDTH_32; break;
    default:
        printf(LOG_TAG " unsupported bit width: %d\n", cfg->bit_width);
        free(ctx);
        return -1;
    }
    //fix:缺少soundCard成员初始化 报错误码：[CAM_AUDIO] AI SetPubAttr failed: 0xa00a8008
    // soundCard.channels 和 u32ChnCnt 必须设为硬件声卡实际通道数（2），mono 用 enSoundmode 控制
    stAiAttr.soundCard.channels = 2;
    stAiAttr.soundCard.sampleRate = cfg->sample_rate;
    stAiAttr.soundCard.bitWidth = bit_width_e;
    stAiAttr.u32ChnCnt = 2;
    sprintf((char *)stAiAttr.u8CardName, "%s", "hw:0,0");  // rockit 通过此字段定位 ALSA 设备节点
    stAiAttr.enSamplerate = sample_rate_e;
    stAiAttr.enBitwidth = bit_width_e;
    stAiAttr.enSoundmode = (cfg->channels == 2) ? AUDIO_SOUND_MODE_STEREO
                                                 : AUDIO_SOUND_MODE_MONO;
    stAiAttr.u32FrmNum = 4;  // 缓冲帧槽数，SDK 典型值 4~8，不应与采样点数混用
    stAiAttr.u32PtNumPerFrm = cfg->frame_samples > 0 ? cfg->frame_samples : 1024;
    stAiAttr.u32EXFlag = 0;
    stAiAttr.u32ChnCnt = cfg->channels;

    // RV1126B 专属：配置 SAI 回采路由，否则 AI_IOCTL_NODE_START 失败
    // 对齐 SDK simple_ai_get_frame.c 中 RV1103B/RV1126B 分支
    ret = RK_MPI_AMIX_SetControl(cfg->dev_id, "SAI SDI0 Loopback Src Select",
                                 (char *)"From SDO0");
    if (ret != RK_SUCCESS)
        printf(LOG_TAG " AMIX SAI SDI0 Loopback Src Select failed: %#x (non-fatal)\n", ret);

    ret = RK_MPI_AMIX_SetControl(cfg->dev_id, "SAI SDI0 Loopback I2S LR Switch",
                                 (char *)"Enable");
    if (ret != RK_SUCCESS)
        printf(LOG_TAG " AMIX SAI SDI0 Loopback I2S LR Switch failed: %#x (non-fatal)\n", ret);

    ret = RK_MPI_AMIX_SetControl(cfg->dev_id, "SAI SDI0 Loopback Switch",
                                 (char *)"Enable");
    if (ret != RK_SUCCESS)
        printf(LOG_TAG " AMIX SAI SDI0 Loopback Switch failed: %#x (non-fatal)\n", ret);

    // 1. 设置 AI 设备属性
    ret = RK_MPI_AI_SetPubAttr(cfg->dev_id, &stAiAttr);
    if (ret != 0) {
        printf(LOG_TAG " AI SetPubAttr failed: %#x\n", ret);
        goto fail;
    }

    // 2. 启用设备
    ret = RK_MPI_AI_Enable(cfg->dev_id);
    if (ret != 0) {
        printf(LOG_TAG " AI Enable failed: %#x\n", ret);
        goto fail;
    }

    // 3. 设置通道参数：s32UsrFrmDepth 必须 > 0，否则 GetFrame 永远失败
    AI_CHN_PARAM_S stChnParam;
    memset(&stChnParam, 0, sizeof(AI_CHN_PARAM_S));
    stChnParam.s32UsrFrmDepth = 4;
    ret = RK_MPI_AI_SetChnParam(cfg->dev_id, cfg->chn_id, &stChnParam);
    if (ret != 0) {
        printf(LOG_TAG " AI SetChnParam failed: %#x\n", ret);
        goto fail_disable;
    }

    // 4. 启用通道
    ret = RK_MPI_AI_EnableChn(cfg->dev_id, cfg->chn_id);
    if (ret != 0) {
        printf(LOG_TAG " AI EnableChn failed: %#x\n", ret);
        goto fail_disable;
    }

    // 5. 启用重采样（可选，对齐 SDK）
    if (cfg->enable_resample && cfg->dst_sample_rate > 0) {
        AUDIO_SAMPLE_RATE_E dst_e;
        switch (cfg->dst_sample_rate) {
        case 8000:  dst_e = AUDIO_SAMPLE_RATE_8000;  break;
        case 16000: dst_e = AUDIO_SAMPLE_RATE_16000; break;
        case 44100: dst_e = AUDIO_SAMPLE_RATE_44100; break;
        case 48000: dst_e = AUDIO_SAMPLE_RATE_48000; break;
        default:
            printf(LOG_TAG " unsupported dst sample rate: %d\n", cfg->dst_sample_rate);
            goto fail_chn;
        }
        ret = RK_MPI_AI_EnableReSmp(cfg->dev_id, cfg->chn_id, dst_e);
        if (ret != 0) {
            printf(LOG_TAG " AI EnableReSmp failed: %#x\n", ret);
            goto fail_chn;
        }
    }

    ctx->inited = true;
    *ai_ctx = ctx;
    printf(LOG_TAG " AI init: dev=%d chn=%d %dHz %dch\n",
           cfg->dev_id, cfg->chn_id, cfg->sample_rate, cfg->channels);
    return 0;

fail_chn:
    RK_MPI_AI_DisableChn(cfg->dev_id, cfg->chn_id);
fail_disable:
    RK_MPI_AI_Disable(cfg->dev_id);
fail:
    free(ctx);
    return -1;
}

int rockchip_ai_get_frame(void *ai_ctx, CamAudioFrame_t *frame)
{
    AiCtx_t *ctx = (AiCtx_t *)ai_ctx;
    int ret;

    if (!ctx || !ctx->inited || !frame)
        return -1;

    memset(&ctx->stFrame, 0, sizeof(AUDIO_FRAME_S));
    ret = RK_MPI_AI_GetFrame(ctx->dev_id, ctx->chn_id,
                             &ctx->stFrame, NULL, -1);
    if (ret != 0) {
        printf(LOG_TAG " AI GetFrame failed: %#x\n", ret);
        return -1;
    }

    frame->data = RK_MPI_MB_Handle2VirAddr(ctx->stFrame.pMbBlk);
    frame->size = ctx->stFrame.u32Len;
    frame->pts  = ctx->stFrame.u64TimeStamp;
    return 0;
}

int rockchip_ai_release_frame(void *ai_ctx)
{
    AiCtx_t *ctx = (AiCtx_t *)ai_ctx;
    int ret;

    if (!ctx || !ctx->inited)
        return -1;

    ret = RK_MPI_AI_ReleaseFrame(ctx->dev_id, ctx->chn_id,
                                 &ctx->stFrame, NULL);
    if (ret != 0) {
        printf(LOG_TAG " AI ReleaseFrame failed: %#x\n", ret);
        return -1;
    }
    return 0;
}

int rockchip_ai_deinit(void *ai_ctx)
{
    AiCtx_t *ctx = (AiCtx_t *)ai_ctx;

    if (!ctx)
        return -1;

    if (ctx->inited) {
        RK_MPI_AI_DisableChn(ctx->dev_id, ctx->chn_id);
        RK_MPI_AI_Disable(ctx->dev_id);
        ctx->inited = false;
    }

    printf(LOG_TAG " AI deinit: dev=%d chn=%d\n", ctx->dev_id, ctx->chn_id);
    free(ctx);
    return 0;
}

/* ======================== AENC (Audio Encoder) ======================== */

typedef struct {
    int chn_id;
    AENC_CHN_ATTR_S stCodecAttr;
    AUDIO_STREAM_S stStream;
    bool inited;
} AencCtx_t;

int rockchip_aenc_init(void **aenc_ctx, const CamAencCfg_t *cfg)
{
    AencCtx_t *ctx;
    int ret;

    if (!aenc_ctx || !cfg)
        return -1;

    ctx = (AencCtx_t *)calloc(1, sizeof(AencCtx_t));
    if (!ctx)
        return -1;

    ctx->chn_id = cfg->chn_id;
    memset(&ctx->stCodecAttr, 0, sizeof(AENC_CHN_ATTR_S));

    // 配置编码器（对齐 SDK test_mpi_aenc.c）
    ctx->stCodecAttr.u32BufCount = 4;
    ctx->stCodecAttr.u32Depth = 4;

    switch (cfg->codec_type) {
    case CAM_ADAPTER_AUDIO_CODEC_AAC:
        ctx->stCodecAttr.enType = RK_AUDIO_ID_ACC;
        ctx->stCodecAttr.stCodecAttr.enType = RK_AUDIO_ID_ACC;
        ctx->stCodecAttr.stCodecAttr.u32Bitrate = cfg->bitrate;
        ctx->stCodecAttr.stCodecAttr.u32Channels = cfg->channels > 2 ? 2 : cfg->channels;
        ctx->stCodecAttr.stCodecAttr.u32SampleRate = cfg->sample_rate;
        ctx->stCodecAttr.stCodecAttr.enBitwidth = AUDIO_BIT_WIDTH_16;
        break;
    case CAM_ADAPTER_AUDIO_CODEC_G711A:
        ctx->stCodecAttr.enType = RK_AUDIO_ID_PCM_ALAW;
        ctx->stCodecAttr.stCodecAttr.enType = RK_AUDIO_ID_PCM_ALAW;
        ctx->stCodecAttr.stCodecAttr.u32Channels = cfg->channels > 2 ? 2 : cfg->channels;
        ctx->stCodecAttr.stCodecAttr.u32SampleRate = cfg->sample_rate;
        ctx->stCodecAttr.stCodecAttr.enBitwidth = AUDIO_BIT_WIDTH_16;
        break;
    case CAM_ADAPTER_AUDIO_CODEC_G711U:
        ctx->stCodecAttr.enType = RK_AUDIO_ID_PCM_MULAW;
        ctx->stCodecAttr.stCodecAttr.enType = RK_AUDIO_ID_PCM_MULAW;
        ctx->stCodecAttr.stCodecAttr.u32Channels = cfg->channels > 2 ? 2 : cfg->channels;
        ctx->stCodecAttr.stCodecAttr.u32SampleRate = cfg->sample_rate;
        ctx->stCodecAttr.stCodecAttr.enBitwidth = AUDIO_BIT_WIDTH_16;
        break;
    case CAM_ADAPTER_AUDIO_CODEC_G729:
        ctx->stCodecAttr.enType = RK_AUDIO_ID_G729;
        ctx->stCodecAttr.stCodecAttr.enType = RK_AUDIO_ID_G729;
        ctx->stCodecAttr.stCodecAttr.u32Channels = cfg->channels > 2 ? 2 : cfg->channels;
        ctx->stCodecAttr.stCodecAttr.u32SampleRate = cfg->sample_rate;
        ctx->stCodecAttr.stCodecAttr.enBitwidth = AUDIO_BIT_WIDTH_16;
        break;
    default:
        printf(LOG_TAG " unsupported audio codec: %d\n", cfg->codec_type);
        free(ctx);
        return -1;
    }

    ret = RK_MPI_AENC_CreateChn(cfg->chn_id, &ctx->stCodecAttr);
    if (ret != 0) {
        printf(LOG_TAG " AENC CreateChn failed: %#x\n", ret);
        free(ctx);
        return -1;
    }

    ctx->inited = true;
    *aenc_ctx = ctx;
    printf(LOG_TAG " AENC init: chn=%d type=%d\n", cfg->chn_id, cfg->codec_type);
    return 0;
}

int rockchip_aenc_get_stream(void *aenc_ctx, CamAudioFrame_t *frame)
{
    AencCtx_t *ctx = (AencCtx_t *)aenc_ctx;
    int ret;

    if (!ctx || !ctx->inited || !frame)
        return -1;

    memset(&ctx->stStream, 0, sizeof(AUDIO_STREAM_S));
    ret = RK_MPI_AENC_GetStream(ctx->chn_id, &ctx->stStream, 5000);
    if (ret != 0) {
        printf(LOG_TAG " AENC GetStream failed: %#x\n", ret);
        return -1;
    }

    frame->data = RK_MPI_MB_Handle2VirAddr(ctx->stStream.pMbBlk);
    frame->size = ctx->stStream.u32Len;
    frame->pts  = ctx->stStream.u64TimeStamp;
    return 0;
}

int rockchip_aenc_release_stream(void *aenc_ctx)
{
    AencCtx_t *ctx = (AencCtx_t *)aenc_ctx;
    int ret;

    if (!ctx || !ctx->inited)
        return -1;

    ret = RK_MPI_AENC_ReleaseStream(ctx->chn_id, &ctx->stStream);
    if (ret != 0) {
        printf(LOG_TAG " AENC ReleaseStream failed: %#x\n", ret);
        return -1;
    }
    return 0;
}

int rockchip_aenc_deinit(void *aenc_ctx)
{
    AencCtx_t *ctx = (AencCtx_t *)aenc_ctx;

    if (!ctx)
        return -1;

    if (ctx->inited) {
        RK_MPI_AENC_DestroyChn(ctx->chn_id);
        ctx->inited = false;
    }

    printf(LOG_TAG " AENC deinit: chn=%d\n", ctx->chn_id);
    free(ctx);
    return 0;
}

int rockchip_ai_aenc_bind(const CamAiCfg_t *ai_cfg, const CamAencCfg_t *aenc_cfg)
{
    MPP_CHN_S stSrcChn, stDestChn;

    if (!ai_cfg || !aenc_cfg)
        return -1;

    stSrcChn.enModId = RK_ID_AI;
    stSrcChn.s32DevId = ai_cfg->dev_id;
    stSrcChn.s32ChnId = ai_cfg->chn_id;

    stDestChn.enModId = RK_ID_AENC;
    stDestChn.s32DevId = 0;
    stDestChn.s32ChnId = aenc_cfg->chn_id;

    int ret = RK_MPI_SYS_Bind(&stSrcChn, &stDestChn);
    if (ret != RK_SUCCESS) {
        printf(LOG_TAG " AI->AENC bind failed: %x\n", ret);
        return -1;
    }

    printf(LOG_TAG " AI[dev=%d chn=%d] -> AENC[chn=%d] bound\n",
           ai_cfg->dev_id, ai_cfg->chn_id, aenc_cfg->chn_id);
    return 0;
}

void rockchip_ai_aenc_unbind(const CamAiCfg_t *ai_cfg, const CamAencCfg_t *aenc_cfg)
{
    MPP_CHN_S stSrcChn, stDestChn;

    if (!ai_cfg || !aenc_cfg)
        return;

    stSrcChn.enModId = RK_ID_AI;
    stSrcChn.s32DevId = ai_cfg->dev_id;
    stSrcChn.s32ChnId = ai_cfg->chn_id;

    stDestChn.enModId = RK_ID_AENC;
    stDestChn.s32DevId = 0;
    stDestChn.s32ChnId = aenc_cfg->chn_id;

    RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);

    printf(LOG_TAG " AI[dev=%d chn=%d] -> AENC[chn=%d] unbound\n",
           ai_cfg->dev_id, ai_cfg->chn_id, aenc_cfg->chn_id);
}
