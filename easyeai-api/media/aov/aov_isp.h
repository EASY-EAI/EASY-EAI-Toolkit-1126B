/*
 * aov_isp.h — AOV ISP 接口
 *
 * 提供 sensor 初始化、帧率控制、单帧/多帧模式切换及 AE 稳定等待功能。
 */

#ifndef __AOV_ISP_H__
#define __AOV_ISP_H__

#ifdef __cplusplus
extern "C" {
#endif

/************************** 常量 ********************************/

#define AOV_ISP_SENSOR_NUM_MAX  2

/* HDR 工作模式 */
typedef enum {
    AOV_ISP_HDR_MODE_NORMAL = 0,
    AOV_ISP_HDR_MODE_HDR2   = 0x10,
    AOV_ISP_HDR_MODE_HDR3   = 0x20,
} AOV_ISP_HDR_MODE_E;

/************************** ISP 属性 ****************************/

typedef struct {
    int               sensor_num;    /* sensor 数量 */
    AOV_ISP_HDR_MODE_E hdr_mode;     /* HDR 模式: NORMAL / HDR2 / HDR3 */
    int               bMultictx;     /* 多上下文模式 (0=关闭 1=开启) */
    int               fps;           /* 目标帧率 */
    const char       *iq_file_dir;   /* IQ 配置文件目录 (如 "/etc/iqfiles") */
} AOV_ISP_ATTR_T;

/************************** 接口 *********************************/

/*
 * ISP 初始化
 *
 * 必须在 RK_MPI_SYS_Init() 之前调用。
 * @param attr ISP 属性配置
 * @return 0 成功，-1 失败
 */
int  aov_isp_init(AOV_ISP_ATTR_T *attr);

/*
 * ISP 反初始化
 *
 * 应在所有媒体模块关闭后调用。
 * @return 0 成功
 */
int  aov_isp_deinit(void);

/*
 * 进入 AOV 单帧模式（暂停 ISP pipeline）
 *
 * @param cam_id sensor 索引 (0 ~ AOV_ISP_SENSOR_NUM_MAX-1)
 * @return 0 成功，-1 失败
 */
int  aov_isp_single_frame(int cam_id);

/*
 * 恢复多帧模式（恢复 ISP pipeline）
 *
 * @param cam_id sensor 索引 (0 ~ AOV_ISP_SENSOR_NUM_MAX-1)
 * @return 0 成功，-1 失败
 */
int  aov_isp_multi_frame(int cam_id);

/*
 * 设置 ISP 帧率
 *
 * @param cam_id sensor 索引
 * @param fps    目标帧率
 * @return 0 成功，-1 失败
 */
int  aov_isp_set_frame_rate(int cam_id, int fps);

/*
 * 获取 SOF (Start of Frame) 中断计数
 *
 * @return >=0 当前帧计数
 */
int  aov_isp_get_sof_cnt(void);

/*
 * 等待 AE (自动曝光) 收敛稳定
 *
 * 在从单帧模式恢复到连续帧模式后调用，
 * 阻塞直至 AE 收敛或超时。
 */
void aov_isp_wait_ae_stabilize(void);

#ifdef __cplusplus
}
#endif

#endif /* __AOV_ISP_H__ */
