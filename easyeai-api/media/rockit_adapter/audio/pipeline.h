#ifndef __AUDIO_PIPELINE_H__
#define __AUDIO_PIPELINE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "../platform/audio/rockchip_audio.h"

/*
 * 音频管线帧回调函数类型
 *   data:       编码后音频帧数据指针（由底层 AENC 产生）
 *   data_len:   帧数据长度（字节）
 *   pts:        时间戳（微秒）
 *   route_id:   路由 ID，对应 AudioPipeCfg_t.route_id
 *   user_data:  注册回调时透传的自定义数据
 */
typedef void (*AudioPipeFrameCallback_t)(void *data, int data_len,
                                          unsigned long long pts,
                                          int route_id, void *user_data);

/*
 * 单路音频管线配置（组合 AI + AENC）
 *
 *   route_id:   路由 ID，用于 AudioPipe_RegisterFrameCallback 指定目标
 *   ai_cfg:     AI（音频输入）配置
 *   aenc_cfg:   AENC（音频编码）配置
 *
 *   AI→AENC 通过 MPP 绑定自动连接，管线启动后内部线程从 AENC 获取编码帧。
 *   当 aenc_cfg.codec_type == CAM_ADAPTER_AUDIO_CODEC_PCM 时，管线直接输出原始 PCM 帧。
 */
typedef struct {
    int route_id;
    CamAiCfg_t ai_cfg;
    CamAencCfg_t aenc_cfg;
} AudioPipeCfg_t;

typedef void *AudioPipeHandle;

/*
 * 音频管线生命周期
 */
AudioPipeHandle AudioPipe_Create(AudioPipeCfg_t *cfg);   /* 根据配置创建管线，失败返回 NULL */
int AudioPipe_Destroy(AudioPipeHandle handle);            /* 销毁管线，释放所有资源 */

/*
 * 流控制
 */
int AudioPipe_Start(AudioPipeHandle handle);              /* 启动取流线程，开始推送编码帧 */
int AudioPipe_Stop(AudioPipeHandle handle);               /* 停止取流，反初始化 AI 和 AENC */

/*
 * 注册帧回调
 *
 *   管线在内部线程中采集到一帧音频时调用该回调。
 *
 *   route_id:   目标路由 ID（对应 AudioPipeCfg_t.route_id）
 *   cb:         回调函数指针
 *   user_data:  透传给回调的用户数据
 *
 *   return: 0 成功，-1 失败（route_id 不匹配）
 *
 *   注意：必须在 AudioPipe_Start 之前调用。
 */
int AudioPipe_RegisterFrameCallback(AudioPipeHandle handle, int route_id,
                                     AudioPipeFrameCallback_t cb, void *user_data);

#ifdef __cplusplus
}
#endif

#endif
