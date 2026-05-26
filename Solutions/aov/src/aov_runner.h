/*
 * AOV (Always On Video) 运行器头文件
 * 定义 AOV 录制器的配置结构体和运行接口
 */

#ifndef __AOV_RUNNER_H__
#define __AOV_RUNNER_H__

#include <stdbool.h>

#include "pipeline.h"

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief AOV 运行器配置结构体
 */
typedef struct {
    CamPipeCfg_t pipe_cfg;          ///< 摄像头管道配置
    int fps;                        ///< 录制帧率
    char rec_dir[128];              ///< 录制文件保存目录
    char rec_prefix[32];            ///< 录制文件名前缀
    int suspend_time_ms;            ///< 暂停时间（毫秒）
    int loop_count;                 ///< 循环次数
    int loop_duration_sec;          ///< 单次循环持续时间（秒）
    bool enable_aov;                ///< 是否启用 AOV 功能
    bool use_h265;                  ///< 是否使用 H.265 编码格式
} AovRunnerCfg_t;

/**
 * @brief 运行 AOV 录制器
 * @param cfg AOV 运行器配置结构体指针
 * @param running 运行状态标志指针，用于控制录制过程
 * @return 成功返回 0，失败返回负值
 */
int AovRunner_Run(const AovRunnerCfg_t *cfg, volatile bool *running);

#ifdef __cplusplus
}
#endif

#endif
