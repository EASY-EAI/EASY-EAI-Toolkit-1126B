#ifndef __MEDIA_MUXER_H__
#define __MEDIA_MUXER_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 音视频封装格式
 */
typedef enum {
    MEDIA_MUXER_FMT_MP4 = 0,    // MP4 封装格式
    MEDIA_MUXER_FMT_BUTT
} MediaMuxerFmt_e;

/**
 * @brief 视频编码类型
 */
typedef enum {
    MEDIA_MUXER_VCODEC_H264 = 0, // H.264 视频编码
    MEDIA_MUXER_VCODEC_H265,     // H.265 视频编码
    MEDIA_MUXER_VCODEC_BUTT
} MediaMuxerVCodec_e;

/**
 * @brief 音频编码类型
 */
typedef enum {
    MEDIA_MUXER_ACODEC_G711A = 0, // G.711A 音频编码
    MEDIA_MUXER_ACODEC_G711U,     // G.711U 音频编码
    MEDIA_MUXER_ACODEC_AAC,       // AAC 音频编码
    MEDIA_MUXER_ACODEC_MP2,       // MP2 音频编码
    MEDIA_MUXER_ACODEC_BUTT
} MediaMuxerACodec_e;

/**
 * @brief 视频流配置参数
 */
typedef struct {
    MediaMuxerVCodec_e codec; // 视频编码格式 (H264/H265)
    int width;                // 视频宽度
    int height;               // 视频高度
    int fps;                  // 视频帧率
    int bitrate;              // 码率 (bps)，若不知可填 0
} MediaMuxerVideoParam_t;

/**
 * @brief 音频流配置参数
 */
typedef struct {
    MediaMuxerACodec_e codec; // 音频编码格式
    int channels;             // 声道数
    int sample_rate;          // 采样率 (Hz)
    int frame_size;           // 每帧采样点数
} MediaMuxerAudioParam_t;

/**
 * @brief 音视频混合器不透明句柄
 */
typedef void* MediaMuxerHandle;

/**
 * @brief 创建并初始化音视频混合器
 * 
 * @param file_path 混合输出的文件绝对路径（如 /mnt/sdcard/record.mp4）
 * @param format 封装格式（目前主要支持 MP4）
 * @param v_param 视频参数指针。如果只录音频，可传入 NULL。
 * @param a_param 音频参数指针。如果只录视频，可传入 NULL。
 * @return 成功返回混合器句柄，失败返回 NULL。
 */
MediaMuxerHandle MediaMuxer_Create(const char* file_path, MediaMuxerFmt_e format, 
                                   MediaMuxerVideoParam_t *v_param, 
                                   MediaMuxerAudioParam_t *a_param);

/**
 * @brief 写入一帧视频数据
 * 
 * @param handle 混合器句柄
 * @param data 视频帧数据指针
 * @param len 视频帧数据长度
 * @param pts_us 呈现时间戳（单位：微秒）
 * @param is_keyframe 是否为关键帧 (1: 是, 0: 否)
 * @return 0 成功，非 0 失败
 */
int MediaMuxer_PushVideo(MediaMuxerHandle handle, uint8_t *data, unsigned int len, 
                         uint64_t pts_us, int is_keyframe);

/**
 * @brief 写入一帧音频数据
 * 
 * @param handle 混合器句柄
 * @param data 音频帧数据指针
 * @param len 音频帧数据长度
 * @param pts_us 呈现时间戳（单位：微秒）
 * @return 0 成功，非 0 失败
 */
int MediaMuxer_PushAudio(MediaMuxerHandle handle, uint8_t *data, unsigned int len, 
                         uint64_t pts_us);

/**
 * @brief 销毁混合器，停止录制并保存文件结尾信息
 * 
 * @param handle 混合器句柄
 */
void MediaMuxer_Destroy(MediaMuxerHandle handle);

#ifdef __cplusplus
}
#endif

#endif // __MEDIA_MUXER_H__