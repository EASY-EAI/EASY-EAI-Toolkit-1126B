/*
 * aov_video.h — AOV 视频接口
 *
 * 定义视频通道、帧数据、编码配置、回调类型和统一对外接口。
 * 上层应用通过 aov_video_func_init() 选择后端模式，
 * 后续只依赖本头文件中的接口即可完成视频采集与编码。
 */

#ifndef __AOV_VIDEO_H__
#define __AOV_VIDEO_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ======================== 常量 ======================== */

#define AOV_VIDEO_GROUP_NUM_MAX 2  /* 最多传感器数量 */
#define AOV_VIDEO_CHN_NUM_MAX   3  /* 主/子/第三通道 */

/* ======================== 枚举 ======================== */

/** @brief 视频通道 ID */
typedef enum {
    AOV_VIDEO_MAIN_CHN  = 0,       /* 主码流 */
    AOV_VIDEO_SUB_CHN   = 1,       /* 子码流 */
    AOV_VIDEO_THIRD_CHN = 2,       /* 第三通道 */
    AOV_VIDEO_SVP_CHN   = AOV_VIDEO_THIRD_CHN,  /* 兼容别名 */
} AOV_VIDEO_CHN_E;

/** @brief 视频后端模式 */
typedef enum {
    AOV_VIDEO_BACKEND_SPLICE     = 0, /* 拼接模式 */
    AOV_VIDEO_BACKEND_SPLICE_AVS = 1, /* AVS 硬件拼接模式 */
    AOV_VIDEO_BACKEND_MULTI      = 2, /* 多目非拼接模式 */
} AOV_VIDEO_BACKEND_E;

/** @brief 编码类型 */
typedef enum {
    AOV_VIDEO_ENCODE_H264  = 5,
    AOV_VIDEO_ENCODE_H265  = 6,
    AOV_VIDEO_ENCODE_JPEG  = 7,
    AOV_VIDEO_ENCODE_MJPEG = 8,
} AOV_VIDEO_ENCODE_E;

/* AVS 拼接模式（仅拼接后端使用） */
#define AVS_SPLICE_BLEND       0  /* 融合拼接（默认） */
#define AVS_SPLICE_HORIZONTAL  2  /* 水平拼接（左右并排） */
#define AVS_SPLICE_VERTICAL    1  /* 垂直拼接（上下堆叠） */

/* ======================== 结构体 ======================== */

/** @brief 视频分辨率 */
typedef struct {
    int width;
    int height;
} AOV_VIDEO_INFO_T;

/** @brief 多目公共信息 */
typedef struct {
    int   sensor_num;              /* sensor 数量 */
    char *avs_json_path;           /* JSON 配置文件路径，非 NULL 时从 JSON 读取硬件参数 */
    char *calib_file_path;         /* 标定文件路径 */
} AOV_COMM_INFO_T;

/** @brief 拼接模式多目属性（传入 aov_video_vi_init） */
typedef struct {
    AOV_VIDEO_INFO_T  vi_info[AOV_VIDEO_CHN_NUM_MAX]; /* 各通道分辨率 */
    AOV_COMM_INFO_T   comm_info;                      /* 公共信息 */
    char              avs_mode;  /* AVS 拼接模式: AVS_SPLICE_BLEND / HORIZONTAL / VERTICAL */
    unsigned short    vi_frame_rate;  /* VI/ISP 采集帧率 (fps)，0 表示使用默认值 15 */
} AOV_SPLICE_MULTI_ATTR_T;

/** @brief VENC 码率控制参数 */
typedef struct {
    unsigned short fps;            /* 目标帧率 */
    unsigned short gop_len;        /* GOP 长度 */
    unsigned short target_kbps;    /* 目标码率 (kbps) */
    unsigned short max_kbps;       /* 最大码率 (kbps) */
    unsigned short min_qp;         /* 最小 QP */
    unsigned short max_qp;         /* 最大 QP */
    unsigned short min_framesize;  /* 最小帧尺寸 */
    unsigned short max_framesize;  /* 最大帧尺寸 */
} AOV_VENC_RC_PARAM_T;

/** @brief 通用视频帧
 *
 * frame_type 区分帧类型，对应 union 的不同成员:
 *   'I'/'P'/'B' — 编码帧 (H264/H265), 使用 enc 成员
 *   'J'         — JPEG 单帧, 使用 jpeg 成员
 *   'M'         — MJPEG 多帧拼接, 使用 jpeg 成员
 *   'Y'         — YUV 原始帧, 使用 yuv 成员
 *
 * 各帧类型互斥，不应同时赋值。
 */
typedef struct {
    unsigned char      frame_type; /* 帧类型: 'I'/'P'/'B'=编码帧, 'J'=JPEG, 'M'=MJPEG, 'Y'=YUV原始帧 */
    unsigned int       data_size;  /* 数据总大小（字节） */
    unsigned long long pts;        /* 时间戳 (us) */
    union {
        /* ---- 编码帧 (frame_type == 'I'/'P'/'B') ---- */
        struct {
            void *data_vaddr; /* 编码数据虚拟地址 */
        } enc;

        /* ---- JPEG/MJPEG 帧 (frame_type == 'J'/'M') ---- */
        struct {
            void         *data_vaddr; /* JPEG/MJPEG 码流虚拟地址 (仅本帧回调期间有效) */
            unsigned int  width;      /* 编码图像宽度 */
            unsigned int  height;     /* 编码图像高度 */
            unsigned int  frame_num;  /* 拼接的 JPEG 帧数 (1=静态 JPEG, >1=MJPEG) */
        } jpeg;

        /* ---- YUV 原始帧 (frame_type == 'Y') ---- */
        struct {
            void          *data_vaddr; /* YUV 数据虚拟地址 */
            void          *mb_blk;     /* 硬件 DMA 缓冲句柄 (MB_BLK) */
            unsigned int   width;      /* 帧宽度 (像素) */
            unsigned int   height;     /* 帧高度 (像素) */
            unsigned int   vir_width;  /* 虚拟宽度 / stride (像素) */
            unsigned int   vir_height; /* 虚拟高度 (像素) */
        } yuv;
    };
} AOV_VIDEO_FRAME_T;

/** @brief VENC 通道属性（传入 aov_video_venc_init） */
typedef struct {
    char                br_mode;        /* 码率模式 (CBR/VBR 等) */
    AOV_VIDEO_ENCODE_E  encode_type;     /* 编码类型, 值与 rk_mpi CODEC_TYPE_E 匹配, 无需映射 */
    int                 smart_mode;      /* 智能编码 */
    int                 jpeg_enc_type;   /* JPEG 编码子类型 */
    AOV_VIDEO_INFO_T    venc_res;        /* 编码输出分辨率 */
    AOV_VENC_RC_PARAM_T bit_rate_attr;   /* 码率控制参数 */
} AOV_VENC_ATTR_T;

/* ======================== 回调类型 ======================== */

/**
 * @brief 编码后码流回调
 * @param schn  传感器通道 (0-based)
 * @param vchn  编码通道 (MAIN / SUB / THIRD)
 * @param frame 编码帧:
 *              'I'/'P'/'B' — MAIN/SUB 的 H264/H265 码流
 *              'J'/'M'     — THIRD 通道的 JPEG / MJPEG 截图 (frame->jpeg)
 * @return 0 成功，非 0 失败
 */
typedef int (*VIDEO_GET_VENC_DATA)(unsigned char schn, unsigned char vchn,
                                  AOV_VIDEO_FRAME_T *frame);

/**
 * @brief 原始 YUV 数据回调（编码前）
 * @param schn  传感器通道
 * @param vchn  视频通道
 * @param frame 原始 YUV 帧 (NV12 格式)
 * @return 0 成功，非 0 失败
 */
typedef int (*VIDEO_GET_RAW_DATA)(unsigned char schn, unsigned char vchn,
                                  AOV_VIDEO_FRAME_T *frame);

/* ======================== 对外接口 ======================== */

/**
 * @brief 选择视频后端模式
 *
 * 必须在 aov_isp_init() 之后、所有其他 aov_video_* 之前调用。
 * @param backend 后端模式 (SPLICE / SPLICE_AVS / MULTI)
 */
void aov_video_func_init(AOV_VIDEO_BACKEND_E backend);

/* ---------- 生命周期 ---------- */

/**
 * @brief 初始化 VI + AVS 管线
 * @param attr 拼接多目属性
 * @return 0 成功，负值失败
 */
int  aov_video_vi_init(AOV_SPLICE_MULTI_ATTR_T *attr);

/**
 * @brief 反初始化 VI + AVS 管线
 * @return 0 成功，负值失败
 */
int  aov_video_vi_uninit(void);

/**
 * @brief 初始化 VENC 编码通道
 * @param vchn 通道 ID (MAIN / SUB / THIRD)
 * @param attr 编码属性
 * @return 0 成功，负值失败
 */
int  aov_video_venc_init(AOV_VIDEO_CHN_E vchn, AOV_VENC_ATTR_T *attr);

/**
 * @brief 反初始化 VENC 编码通道
 * @param vchn 通道 ID
 * @return 0 成功，负值失败
 */
int  aov_video_venc_uninit(AOV_VIDEO_CHN_E vchn);

/**
 * @brief 启动编码线程
 * @param raw_cb  原始 YUV 帧回调（可为 NULL）
 * @param venc_cb 编码后码流回调（可为 NULL）
 * @return 0 成功，负值失败
 */
int  aov_video_venc_start(VIDEO_GET_RAW_DATA raw_cb, VIDEO_GET_VENC_DATA venc_cb);

/**
 * @brief 停止编码线程
 */
void aov_video_venc_stop(void);

/* ---------- 通道控制 ---------- */

/**
 * @brief 启用指定编码通道
 * @param vchn 通道 ID
 * @return 0 成功，负值失败
 */
int  aov_video_venc_chn_enable(AOV_VIDEO_CHN_E vchn);

/**
 * @brief 禁用指定编码通道
 * @param vchn 通道 ID
 * @return 0 成功，负值失败
 */
int  aov_video_venc_chn_disable(AOV_VIDEO_CHN_E vchn);

/* ---------- 编码控制 ---------- */

/**
 * @brief 请求编码器立即输出 IDR 帧
 * @param vchn 通道 ID
 * @return 0 成功，负值失败
 */
int  aov_video_request_idr_frame(AOV_VIDEO_CHN_E vchn);

/**
 * @brief 清空内部帧数量统计
 *
 * 通常在模式切换后调用。
 */
void aov_video_aov_info_clear(void);

/* ---------- 截图 ---------- */

/**
 * @brief 触发一次 MJPEG 动态截图 (异步)
 *
 * 结果经 VIDEO_GET_VENC_DATA 回调上报, 由上层自行处理。
 *
 * @param schn       传感器通道号 (选择哪个 sensor)
 * @param vchn       视频通道号 (0=主通道, 1=子通道)
 * @param frame_num  拼接帧数 (1=单帧 JPEG, >1=多帧 MJPEG)
 * @return 0 请求已受理, 负值失败
 */
int  aov_video_mjpeg_save(unsigned char schn, unsigned char vchn,
                           unsigned char frame_num);

/**
 * @brief 触发一次 JPEG 静态截图 (异步)
 *
 * 结果经 VIDEO_GET_VENC_DATA 回调上报, 由上层自行处理。
 *
 * @param schn  传感器通道号 (选择哪个 sensor)
 * @param vchn  视频通道号 (0=主通道, 1=子通道)
 * @return 0 请求已受理, 负值失败
 */
int  aov_video_jpeg_save(unsigned char schn, unsigned char vchn);

#ifdef __cplusplus
}
#endif

#endif /* __AOV_VIDEO_H__ */
