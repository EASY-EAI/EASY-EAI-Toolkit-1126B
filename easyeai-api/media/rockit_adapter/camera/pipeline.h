#ifndef __CAMERA_PIPELINE_H__
#define __CAMERA_PIPELINE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "../platform/cam/rockchip_cam.h"

/*
 * 帧元数据头魔数，用于 DataQueue 中 FrameMeta_t 的有效性校验
 */
#define FRAME_META_MAGIC 0x464D4554

/*
 * 帧元数据头，与编码帧数据体拼成连续 buffer 存入 DataQueue
 *
 * 数据格式：
 *   [FrameMeta_t][编码帧 payload ...]
 *                 ^ 长度 = data_len
 *
 *   magic:      FRAME_META_MAGIC，Pop 时校验完整性
 *   data_len:   编码帧数据长度 (字节)
 *   timestamp:  PTS 时间戳 (微秒)
 *   tag:        路由 ID (route_id)，用于区分多路输出
 *   is_keyframe: 是否为 IDR 关键帧 (1=是)
 */
typedef struct {
    unsigned int magic;
    unsigned int data_len;
    unsigned long long timestamp;
    int tag;
    int is_keyframe;
} FrameMeta_t;

/*
 * 解码后的媒体帧，由调用方从 DataQueue Pop 后自行解包得到
 *   data:        编码帧数据 (调用方负责 free)
 *   data_len:    数据长度 (字节)
 *   timestamp:   时间戳 (微秒)
 *   tag:         路由 ID
 *   is_keyframe: 是否为 IDR 关键帧
 */
typedef struct {
    void *data;
    unsigned int data_len;
    unsigned long long timestamp;
    int tag;
    int is_keyframe;
} Frame_t;

/*
 * 管线下各模块最大数量
 */
#define CAM_PIPE_MAX_CAMERAS 4
#define CAM_PIPE_MAX_AVS 2
#define CAM_PIPE_MAX_OUTPUTS 8
#define CAM_PIPE_MAX_BINDS 24

/*
 * 管线帧回调函数类型
 *   data:        编码后帧数据指针 (由底层 VENC 产生)
 *   data_len:    帧数据长度 (字节)
 *   pts:         时间戳 (微秒)
 *   route_id:    路由 ID，对应 CamPipeOutputCfg_t.route_id
 *   is_keyframe: 是否为 IDR 关键帧 (1=是)
 *   user_data:   注册回调时透传的自定义数据
 */
typedef void (*CamPipeFrameCallback_t)(void *data, unsigned int data_len,
                                       unsigned long long pts,
                                       int route_id, int is_keyframe,
                                       void *user_data);

/*
 * 单 camera 配置 (组合 ISP + VI + VPSS)
 *
 *   isp_cfg:     ISP 配置
 *   vi_cfg:      VI 配置
 *   enable_vpss: 是否启用 VPSS
 *   vpss_cfg:    VPSS 配置 (enable_vpss 为 true 时有效)
 */
typedef struct {
    CamIspCfg_t isp_cfg;
    CamViCfg_t vi_cfg;
    bool enable_vpss;
    CamVpssCfg_t vpss_cfg;
} CamPipeCameraCfg_t;

/*
 * 单 AVS 组配置
 */
typedef struct {
    CamAvsCfg_t avs_cfg;
} CamPipeAvsCfg_t;

/*
 * 单路输出配置 (VENC 编码 + 路由 ID)
 *
 *   route_id:  路由 ID，用于 CamPipe_RegisterFrameCallback 指定目标输出
 *   venc_cfg:  VENC 编码配置
 */
typedef struct {
    int route_id;
    CamVencCfg_t venc_cfg;
} CamPipeOutputCfg_t;

/*
 * 单条 MPP 绑定配置
 *
 *   camera_index: 所属 camera 编号 (0, 1, ...)
 *   bind_cfg:     MPP 源/目标模块绑定关系
 */
typedef struct {
    int camera_index;
    CamBindCfg_t bind_cfg;
} CamPipeBindCfg_t;

/*
 * 管线总配置
 *
 *   使用者填充 cameras / avss / outputs / binds 数组并设置对应 count，
 *   由 CamPipe_Create 一次性创建所有模块并建立拓扑。
 *
 *   典型拓扑：
 *     camera -> ISP -> VI -> (VPSS) -> VENC -> [回调推送]
 *     camera -> ISP -> VI -> AVS -> VENC -> [回调推送]
 *
 *   camera_count:  camera 数量
 *   avs_count:     AVS 组数量 (0 表示无拼接)
 *   output_count:  VENC 输出通道数量
 *   bind_count:    MPP 绑定关系数量
 *   cameras[]:     各 camera 配置
 *   avss[]:        各 AVS 组配置
 *   outputs[]:     各输出通道配置
 *   binds[]:       各绑定关系
 */
typedef struct {
    int camera_count;
    int avs_count;
    int output_count;
    int bind_count;
    CamPipeCameraCfg_t cameras[CAM_PIPE_MAX_CAMERAS];
    CamPipeAvsCfg_t avss[CAM_PIPE_MAX_AVS];
    CamPipeOutputCfg_t outputs[CAM_PIPE_MAX_OUTPUTS];
    CamPipeBindCfg_t binds[CAM_PIPE_MAX_BINDS];
} CamPipeCfg_t;

typedef void *CamPipeHandle;

/*
 * 管线生命周期
 */
CamPipeHandle CamPipe_Create(CamPipeCfg_t *cfg);   /* 根据配置创建完整管线，失败返回 NULL */
int CamPipe_Destroy(CamPipeHandle handle);         /* 销毁管线，释放所有硬件资源 */

/*
 * 流控制
 */
int CamPipe_Start(CamPipeHandle handle);           /* 启动所有模块，开始编码推流 */
int CamPipe_Stop(CamPipeHandle handle);            /* 停止推流，反初始化所有模块 */

/*
 * 注册帧回调
 *
 *   每个 output (由 route_id 标识) 可注册一个回调。
 *   当 VENC 编码出一帧时，管线会在 VENC 内部线程中调用该回调。
 *
 *   route_id:   目标输出路由 ID (对应 CamPipeOutputCfg_t.route_id)
 *   cb:         回调函数指针
 *   user_data:  透传给回调的用户数据
 *
 *   return: 0 成功，-1 失败 (route_id 未找到)
 *
 *   注意：必须在 CamPipe_Start 之前调用。
 */
int CamPipe_RegisterFrameCallback(CamPipeHandle handle, int route_id,
                                  CamPipeFrameCallback_t cb, void *user_data);

#ifdef __cplusplus
}
#endif

#endif
