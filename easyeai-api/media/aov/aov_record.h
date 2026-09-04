/*
 * aov_record.h — MP4 录像接口
 *
 * 调用方负责创建录像上下文、输入编码帧、触发写入事件和释放上下文。
 */

#ifndef AOV_RECORD_H
#define AOV_RECORD_H

#include <stdint.h>

#include "aov_error.h"

#define AOV_MAX_FRAME_PER_SESSION 10 /* 每个唤醒周期最大帧数 */

/*
 * 录像配置默认值宏
 *
 * 用法: AOV_RECORD_CFG_T cfg = AOV_RECORD_CFG_DEFAULT("/tmp", "front_main");
 * 用户可在初始化后按需覆盖个别字段。
 */
#define AOV_RECORD_CFG_DEFAULT(_dir, _name) { \
    .output_dir          = (_dir), \
    .file_name           = (_name), \
    .video_width         = 1920, \
    .video_height        = 1080, \
    .fps                 = 15, \
    .gop_size            = 15, \
    .video_kbps          = 2048, \
    .audio_enable        = 0, \
    .audio_sample_rate   = 48000, \
    .audio_channels      = 1, \
    .audio_samples_per_frame = 1024, \
    .max_frames          = AOV_MAX_FRAME_PER_SESSION, \
    .encode_type         = 0, /* H.264 */ \
    .record_fps          = 15, \
    .aov_frame_duration_us = 1000000, /* 1 秒 */ \
    .skip_initial_frames = 0, \
    .venc_chn            = 0, \
    .on_need_idr         = NULL, \
    .idr_userdata        = NULL, \
    .container_format    = {0}, \
}

/*
 * 首帧非 I 帧回调 — 录像文件刚创建但首帧不是关键帧时触发。
 * 用户应在回调中请求 VENC 立即生成 IDR。
 *
 * @param vchn     VENC 通道号
 * @param userdata 注册时透传的用户数据
 */
typedef void (*aov_record_need_idr_cb)(int vchn, void *userdata);

typedef struct {
    char             output_dir[256];       /* 输出目录，如 "/tmp" */
    char             file_name[128];        /* MP4 文件名前缀，如 "front_main" */
    int              video_width;           /* 编码视频宽度 */
    int              video_height;          /* 编码视频高度 */
    int              fps;                   /* 帧率 */
    int              gop_size;              /* GOP 长度 */
    int              video_kbps;            /* 视频码率 (kbps) */
    int              audio_enable;          /* 音频使能 */
    int              audio_sample_rate;     /* 音频采样率 */
    int              audio_channels;        /* 音频通道数 */
    int              audio_samples_per_frame; /* 每帧音频采样数 */
    int              max_frames;            /* 每个文件的帧数上限 */
    int              encode_type;           /* 0=H.264, 1=H.265 */
    int              record_fps;            /* MP4 播放帧率 */
    uint32_t         aov_frame_duration_us; /* AOV 单帧播放间隔，0 表示默认 1 秒 */
    int              skip_initial_frames;   /* 建文件前跳过的预热帧数 */
    int              venc_chn;              /* VENC 通道号，用于 IDR 回调 */
    aov_record_need_idr_cb on_need_idr;     /* 首帧非 I 帧回调 */
    void            *idr_userdata;          /* 回调透传 */
    char             container_format[8];   /* 保留 */
} AOV_RECORD_CFG_T;

typedef struct {
    void             *data_vaddr;           /* 编码数据地址 */
    uint32_t          data_size;            /* 编码数据长度 */
    uint64_t          pts;                  /* 时间戳 */
    char              frame_type;           /* 'I' 表示关键帧 */
} AOV_RECORD_FRAME_T;

typedef struct {
    void             *data_vaddr;           /* 音频编码数据地址 */
    uint32_t          data_size;            /* 音频编码数据长度 */
    uint64_t          pts;                  /* 时间戳 */
} AOV_RECORD_AUDIO_FRAME_T;

/* 录像句柄（不透明类型） */
typedef struct AOV_RECORD_CTX_S AOV_RECORD_CTX_T;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 初始化录像上下文
 * @param cfg 录像配置
 * @return 非 NULL 成功，NULL 失败
 */
AOV_RECORD_CTX_T* aov_record_init(const AOV_RECORD_CFG_T* cfg);

/*
 * 反初始化录像上下文，释放资源
 * @return AOV_ERR_SUCCESS 或 AOV_ERR_* 错误码
 */
int aov_record_deinit(AOV_RECORD_CTX_T* ctx);

/*
 * 输入一帧编码后视频数据
 *
 * 首次输入或 reset 后会自动创建新 MP4 文件。
 * @return AOV_ERR_SUCCESS 或 AOV_ERR_* 错误码
 */
int aov_record_input_frame(AOV_RECORD_CTX_T* ctx, AOV_RECORD_FRAME_T* frame);

/*
 * 输入一帧音频编码数据
 * @return AOV_ERR_SUCCESS 或 AOV_ERR_* 错误码
 */
int aov_record_input_audio_frame(AOV_RECORD_CTX_T* ctx,
                                 AOV_RECORD_AUDIO_FRAME_T* frame);

/*
 * 触发写入事件，将当前周期内已输入的编码帧写入 MP4 文件
 * @return AOV_ERR_SUCCESS 或 AOV_ERR_* 错误码
 */
int aov_record_write_event(AOV_RECORD_CTX_T* ctx);

/*
 * 暂停文件 IO（用于 SD 卡解绑期间保留 muxer 状态）
 * @return AOV_ERR_SUCCESS 或 AOV_ERR_* 错误码
 */
int aov_record_suspend_io(AOV_RECORD_CTX_T* ctx);

/*
 * 恢复文件 IO
 * @return AOV_ERR_SUCCESS 或 AOV_ERR_* 错误码
 */
int aov_record_resume_io(AOV_RECORD_CTX_T* ctx);

/*
 * 重置录像上下文，后续 input_frame 将创建新文件
 */
void aov_record_reset(AOV_RECORD_CTX_T* ctx);

/*
 * ==================== 调用示例 ====================
 *
 * // 1. 定义 IDR 回调（首帧非 I 帧时触发）
 * static void on_need_idr(int vchn, void *userdata)
 * {
 *     aov_video_request_idr_frame(vchn);
 * }
 *
 * // 2. 初始化录像上下文（使用默认值宏，按需覆盖）
 * AOV_RECORD_CFG_T cfg = AOV_RECORD_CFG_DEFAULT("/tmp", "front_main");
 * cfg.encode_type  = 0;          // 0=H.264, 1=H.265
 * cfg.on_need_idr  = on_need_idr;
 * cfg.venc_chn     = 0;
 *
 * AOV_RECORD_CTX_T *ctx = aov_record_init(&cfg);
 * if (!ctx) {
 *     printf("record init failed\n");
 *     return -1;
 * }
 *
 * // 3. 在 VENC 回调中输入编码帧
 * //    首次 input_frame 会自动创建新 MP4 文件
 * AOV_RECORD_FRAME_T frame = {0};
 * frame.data_vaddr = enc_data;
 * frame.data_size  = enc_size;
 * frame.pts        = pts_us;
 * frame.frame_type = 'I';        // 'I' / 'P' / 'B'
 * aov_record_input_frame(ctx, &frame);
 *
 * // 4. 累计达到阈值后触发写入（如每 10 帧）
 * if (++frame_cnt >= 10) {
 *     aov_record_write_event(ctx);
 *     frame_cnt = 0;
 * }
 *
 * // 5. SD 卡解绑前暂停 IO（休眠场景）
 * aov_record_suspend_io(ctx);
 * // ... 系统休眠 / SD 卡解绑 ...
 * // ... 唤醒 / SD 卡重新绑定 ...
 * aov_record_resume_io(ctx);
 *
 * // 6. 反初始化，释放资源
 * aov_record_deinit(ctx);
 */

#ifdef __cplusplus
}
#endif

#endif /* AOV_RECORD_H */
