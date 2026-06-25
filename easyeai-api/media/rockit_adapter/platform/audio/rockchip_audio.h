/*
 * Rockchip RV1126B 平台音频适配层
 *
 * 基于 Rockchip MPP 多媒体接口，提供：
 *   - AI（音频输入）初始化、获取帧、释放帧
 *   - AENC（音频编码）创建、获取码流、销毁
 *
 * 对齐 SDK sample_comm_ai.c / sample_comm_aenc.c
 */

#ifndef __ROCKCHIP_AUDIO_H__
#define __ROCKCHIP_AUDIO_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/*
 * 音频编码类型
 */
typedef enum {
    CAM_ADAPTER_AUDIO_CODEC_NONE  = 0,
    CAM_ADAPTER_AUDIO_CODEC_AAC   = 1,
    CAM_ADAPTER_AUDIO_CODEC_G711A = 2,
    CAM_ADAPTER_AUDIO_CODEC_G711U = 3,
    CAM_ADAPTER_AUDIO_CODEC_G729  = 4,
    CAM_ADAPTER_AUDIO_CODEC_PCM   = 5,
} CamAudioCodecType_e;

/*
 * AI (Audio Input) 配置
 *   dev_id:      音频设备 ID，通常为 0
 *   chn_id:      音频通道 ID，通常为 0
 *   sample_rate: 采样率，如 8000 / 16000 / 44100 / 48000
 *   channels:    声道数，1=单声道 2=立体声
 *   bit_width:   位宽，16 / 24 / 32
 *   frame_samples: 每帧采样点数（建议 1024），0 表示默认
 *   enable_resample: 是否启用重采样
 *   dst_sample_rate: 重采样目标采样率（enable_resample 时有效）
 */
typedef struct {
    int dev_id;
    int chn_id;
    int sample_rate;
    int channels;
    int bit_width;
    int frame_samples;
    bool enable_resample;
    int dst_sample_rate;
} CamAiCfg_t;

/*
 * AENC (Audio Encoder) 配置
 *   chn_id:      编码通道 ID
 *   codec_type:  编码类型 (CamAudioCodecType_e)
 *   sample_rate: 采样率
 *   channels:    声道数
 *   bitrate:     码率 (bps)，仅 AAC 有效
 *   bit_width:   位宽，仅 G711 有效
 */
typedef struct {
    int chn_id;
    int codec_type;
    int sample_rate;
    int channels;
    int bitrate;
    int bit_width;
} CamAencCfg_t;

/*
 * 音频帧信息
 */
typedef struct {
    void *data;
    int size;
    unsigned long long pts;
} CamAudioFrame_t;

/*
 * ======================== AI (Audio Input) 接口 ========================
 */

/**
 * @brief 初始化 AI 音频输入通道
 * @param ai_ctx  AI 上下文（输出）
 * @param cfg     AI 配置参数
 * @return 成功返回 0，失败返回 -1
 */
int rockchip_ai_init(void **ai_ctx, const CamAiCfg_t *cfg);

/**
 * @brief 获取一帧音频 PCM 数据
 * @param ai_ctx AI 上下文
 * @param frame  音频帧（输出）
 * @return 成功返回 0，失败返回 -1
 */
int rockchip_ai_get_frame(void *ai_ctx, CamAudioFrame_t *frame);

/**
 * @brief 释放一帧音频数据
 * @param ai_ctx AI 上下文
 * @return 成功返回 0，失败返回 -1
 */
int rockchip_ai_release_frame(void *ai_ctx);

/**
 * @brief 反初始化 AI 音频输入通道
 * @param ai_ctx AI 上下文
 * @return 成功返回 0，失败返回 -1
 */
int rockchip_ai_deinit(void *ai_ctx);

/*
 * ======================== AENC (Audio Encoder) 接口 ========================
 */

/**
 * @brief 创建音频编码通道
 * @param aenc_ctx 编码上下文（输出）
 * @param cfg      编码配置参数
 * @return 成功返回 0，失败返回 -1
 */
int rockchip_aenc_init(void **aenc_ctx, const CamAencCfg_t *cfg);

/**
 * @brief 获取一帧编码后的音频码流
 * @param aenc_ctx 编码上下文
 * @param frame    编码帧（输出）
 * @return 成功返回 0，失败返回 -1
 */
int rockchip_aenc_get_stream(void *aenc_ctx, CamAudioFrame_t *frame);

/**
 * @brief 释放一帧编码后的音频码流
 * @param aenc_ctx 编码上下文
 * @return 成功返回 0，失败返回 -1
 */
int rockchip_aenc_release_stream(void *aenc_ctx);

/**
 * @brief 销毁音频编码通道
 * @param aenc_ctx 编码上下文
 * @return 成功返回 0，失败返回 -1
 */
int rockchip_aenc_deinit(void *aenc_ctx);

/*
 * ======================== AI→AENC 绑定接口 ========================
 */

/**
 * @brief 绑定 AI 设备到 AENC 编码通道
 * @param ai_cfg    AI 配置（用于 dev_id / chn_id）
 * @param aenc_cfg  AENC 配置（用于 chn_id）
 * @return 成功返回 0，失败返回 -1
 */
int rockchip_ai_aenc_bind(const CamAiCfg_t *ai_cfg, const CamAencCfg_t *aenc_cfg);

/**
 * @brief 解绑 AI 设备与 AENC 编码通道
 * @param ai_cfg    AI 配置
 * @param aenc_cfg  AENC 配置
 */
void rockchip_ai_aenc_unbind(const CamAiCfg_t *ai_cfg, const CamAencCfg_t *aenc_cfg);

#ifdef __cplusplus
}
#endif

#endif
