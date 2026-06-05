#ifndef __rockchip_IMPL_H__
#define __rockchip_IMPL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/*
 * 模块数量上限
 */
#define CAM_ADAPTER_MAX_CAMERA_NUM 4
#define CAM_ADAPTER_MAX_VENC_CHN 8
#define CAM_ADAPTER_MAX_VPSS_CHN 3
#define CAM_ADAPTER_MAX_AVS_CHN 3

/*
 * 编码类型
 */
typedef enum {
    CAM_ADAPTER_CODEC_H264 = 0,
    CAM_ADAPTER_CODEC_H265,
    CAM_ADAPTER_CODEC_MJPEG
} CamAdapterCodecType_e;

/*
 * 码率控制模式
 */
typedef enum {
    CAM_ADAPTER_RC_CBR = 0
} CamAdapterRcMode_e;

/*
 * MPP 模块类型，用于描述绑定的源/目标模块
 */
typedef enum {
    CAM_MODULE_VI = 0,
    CAM_MODULE_VPSS,
    CAM_MODULE_VENC,
    CAM_MODULE_AVS
} CamModuleType_e;

/*
 * HDR 模式
 */
typedef enum {
    CAM_ADAPTER_HDR_NORMAL = 0,
    CAM_ADAPTER_HDR_HDR2,
    CAM_ADAPTER_HDR_HDR3
} CamAdapterHdrMode_e;

/*
 * VPSS 通道工作模式
 *   PASSTHROUGH: 直通模式，不改变分辨率
 *   AUTO:        自动模式，可做缩放/裁剪
 */
typedef enum {
    CAM_VPSS_CHN_MODE_PASSTHROUGH = 0,
    CAM_VPSS_CHN_MODE_AUTO
} CamVpssChnMode_e;

/*
 * ISP 配置
 *   cam_id:        摄像头 ID
 *   iq_file_dir:   IQ 调试文件目录路径
 *   hdr_mode:      HDR 模式 (CamAdapterHdrMode_e)
 *   fps:           帧率
 *   is_multi_cam:  是否多目模式
 */
typedef struct {
    int cam_id;
    const char *iq_file_dir;
    int hdr_mode;
    int fps;
    bool is_multi_cam;
} CamIspCfg_t;

/*
 * VI (Video Input) 配置
 *   pipe_id:       VI Pipe ID
 *   chn_id:        VI Channel ID
 *   width:         输入宽度
 *   height:        输入高度
 *   buf_cnt:       buffer 数量
 *   pix_fmt:       像素格式
 *   buf_wrap_enable: 是否开启 buffer wrap（按行分配缓冲，节省内存）
 *   buf_line:      buffer wrap 行大小（64 ~ height 之间）
 */
typedef struct {
    int pipe_id;
    int chn_id;
    int width;
    int height;
    int buf_cnt;
    int pix_fmt;
    bool buf_wrap_enable;
    int buf_line;
} CamViCfg_t;

/*
 * AVS 单通道配置
 */
typedef struct {
    int chn_id;
    int width;
    int height;
    bool enabled;
} CamAvsChannelCfg_t;

/*
 * AVS (全景拼接) 配置
 *   grp_id:       AVS Group ID
 *   chn_id:       通道 ID（channel_count==0 时的 fallback）
 *   cam_num:      相机数量
 *   src_width:    单路输入宽度
 *   src_height:   单路输入高度
 *   dst_width:    拼接输出宽度
 *   dst_height:   拼接输出高度
 *   distance:     拼接距离（融合模式）
 *   calib_file:   标定文件路径（融合模式需要）
 *   blend_mode:   拼接模式: 0=融合, 1=上下无融合, 2=左右无融合
 *   channel_count:输出通道数
 *   channels:     各输出通道配置
 */
typedef struct {
    int grp_id;
    int chn_id;
    int cam_num;
    int src_width;
    int src_height;
    int dst_width;
    int dst_height;
    float distance;
    const char *calib_file;
    int blend_mode;
    int channel_count;
    CamAvsChannelCfg_t channels[CAM_ADAPTER_MAX_AVS_CHN];
} CamAvsCfg_t;

/*
 * VPSS 单通道配置
 *   chn_id:    通道 ID
 *   width:     输出宽度
 *   height:    输出高度
 *   pix_fmt:   像素格式
 *   chn_mode:  通道模式 (CamVpssChnMode_e)
 *   enabled:   是否启用
 */
typedef struct {
    int chn_id;
    int width;
    int height;
    int pix_fmt;
    int chn_mode;
    bool enabled;
} CamVpssChannelCfg_t;

/*
 * VPSS (视频处理子系统) 配置
 *   grp_id:           Group ID
 *   channel_count:    启用的通道数
 *   channels:         各通道配置
 *   enable_aiisp:     是否启用 AI-ISP 黑光增强
 *   aiisp_model_path: AI-ISP 模型文件路径
 *   aiisp_buf_cnt:    AI-ISP buffer 数量
 */
typedef struct {
    int grp_id;
    int channel_count;
    CamVpssChannelCfg_t channels[CAM_ADAPTER_MAX_VPSS_CHN];
    bool enable_aiisp;
    const char *aiisp_model_path;
    int aiisp_buf_cnt;
} CamVpssCfg_t;

/*
 * VENC (视频编码) 配置
 *   chn_id:               编码通道 ID
 *   width:                编码宽度
 *   height:               编码高度
 *   fps:                  帧率
 *   gop:                  I 帧间隔（帧数）
 *   bitrate:              码率 (kbps)
 *   codec_type:           编码类型 (CamAdapterCodecType_e)
 *   rc_mode:              码控模式 (CamAdapterRcMode_e)
 *   ref_buf_share:        是否开启参考帧缓冲区共享
 *   buf_wrap_enable:      是否开启 buffer wrap（按行分配缓冲区）
 *   buf_line:             buffer wrap 行大小
 *   svc_enable:           是否启用 SVC（可伸缩视频编码）
 *   motion_deblur_enable: 是否启用运动去模糊
 */
typedef struct {
    int chn_id;
    int width;
    int height;
    int fps;
    int gop;
    int bitrate;
    int codec_type;
    int rc_mode;
    bool ref_buf_share;
    bool buf_wrap_enable;
    int buf_line;
    bool svc_enable;
    bool motion_deblur_enable;
} CamVencCfg_t;

/*
 * MPP 通道描述符，用于绑定/解绑操作
 *   mod_type: 模块类型 (CamModuleType_e)
 *   dev_id:   设备 ID
 *   chn_id:   通道 ID
 */
typedef struct {
    int mod_type;
    int dev_id;
    int chn_id;
} CamMppChn_t;

/*
 * MPP 绑定关系
 *   src:  源模块通道
 *   dst:  目标模块通道
 */
typedef struct {
    CamMppChn_t src;
    CamMppChn_t dst;
} CamBindCfg_t;

typedef struct Rk1126bCtx Rk1126bCtx_t;

/*
 * 生命周期管理
 */
Rk1126bCtx_t *rockchip_create(void);                    /* 创建上下文 */
int rockchip_destroy(Rk1126bCtx_t *ctx);                /* 销毁上下文，释放所有资源 */

/*
 * ISP (图像信号处理) 模块
 */
int rockchip_isp_init(Rk1126bCtx_t *ctx, CamIspCfg_t *cfg); /* 初始化 ISP */
int rockchip_isp_run(Rk1126bCtx_t *ctx, int cam_id);         /* 启动 ISP 运行 */
int rockchip_isp_stop(Rk1126bCtx_t *ctx, int cam_id);        /* 停止 ISP */
int rockchip_isp_pause(Rk1126bCtx_t *ctx);                   /* 暂停 ISP 3A 算法（AOV 休眠前调用，保留硬件） */
int rockchip_isp_resume(Rk1126bCtx_t *ctx);                  /* 恢复 ISP 3A 算法（AOV 唤醒后调用，恢复硬件） */

/*
 * VI (视频输入) 模块
 */
int rockchip_vi_init(Rk1126bCtx_t *ctx, CamViCfg_t *cfg);                      /* 初始化 VI */
int rockchip_vi_deinit(Rk1126bCtx_t *ctx, int vi_pipe, int vi_chn);             /* 去初始化 VI */

/*
 * VPSS (视频处理子系统) 模块 — 黑光增强、缩放等
 */
int rockchip_vpss_init(Rk1126bCtx_t *ctx, CamVpssCfg_t *cfg);     /* 初始化 VPSS */
int rockchip_vpss_deinit(Rk1126bCtx_t *ctx, int grp_id);          /* 去初始化 VPSS */

/*
 * AVS (多目全景拼接) 模块
 */
int rockchip_avs_init(Rk1126bCtx_t *ctx, CamAvsCfg_t *cfg);      /* 初始化 AVS */
int rockchip_avs_deinit(Rk1126bCtx_t *ctx, int grp_id);           /* 去初始化 AVS */

/*
 * VENC (视频编码) 模块
 */
int rockchip_venc_init(Rk1126bCtx_t *ctx, CamVencCfg_t *cfg);     /* 初始化 VENC */
int rockchip_venc_deinit(Rk1126bCtx_t *ctx, int venc_chn);        /* 去初始化 VENC */
int rockchip_venc_request_idr(Rk1126bCtx_t *ctx, int chn_id);     /* 请求 VENC 立即输出 IDR 关键帧 */

/*
 * 编码帧数据，由 rockchip_get_venc_stream 填充
 *   调用方在处理完数据后须调用 rockchip_release_venc_stream 释放
 */
typedef struct {
    void *data;             /* 编码帧数据指针（有效期内只读） */
    unsigned int data_len;  /* 数据长度（字节） */
    unsigned long long pts; /* 时间戳（微秒） */
    int is_keyframe;        /* 是否为关键帧（1=是，0=否） */
} CamFrameData_t;

/*
 * 取流接口（封装 RK_MPI_VENC_GetStream / MB_Handle2VirAddr / VENC_ReleaseStream）
 *   chn_id:     VENC 通道 ID
 *   codec_type: 编码类型（CAM_ADAPTER_CODEC_H264 / H265）
 *   frame:      [out] 帧数据
 *   timeout_ms: 等待超时（毫秒）
 *   返回：0=成功拿到一帧，-1=超时或出错
 */
int rockchip_get_venc_stream(Rk1126bCtx_t *ctx, int chn_id, int codec_type,
                              CamFrameData_t *frame, int timeout_ms);

/*
 * 释放由 rockchip_get_venc_stream 获取的帧（实际调用 RK_MPI_VENC_ReleaseStream）
 */
void rockchip_release_venc_stream(Rk1126bCtx_t *ctx, int chn_id);

/*
 * MPP 绑定 / 解绑
 * bind_modules:   通用绑定，通过 CamBindCfg_t 描述拓扑
 * unbind_modules: 通用解绑
 */
int rockchip_bind_modules(Rk1126bCtx_t *ctx, CamBindCfg_t *cfg);
int rockchip_unbind_modules(Rk1126bCtx_t *ctx, CamBindCfg_t *cfg);

#ifdef __cplusplus
}
#endif

#endif
