/*
 * aov_record_queue.h — 编码帧缓存队列接口
 *
 * 提供编码帧的缓存管理，支持延迟写入 SD 卡的场景。
 */

#ifndef AOV_RECORD_QUEUE_H
#define AOV_RECORD_QUEUE_H

#include <stddef.h>
#include <stdint.h>

#include "aov_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 帧数据结构体 */
typedef struct {
    unsigned char schn;        /**< 码流通道 */
    unsigned char vchn;        /**< 视频通道 */
    unsigned char mode;        /**< 编码模式 */
    unsigned char frame_type;  /**< 帧子类型 */
    uint32_t      data_size;   /**< 数据长度 (bytes) */
    uint64_t      pts;         /**< 时间戳 (microseconds) */
    unsigned char *data;       /**< 编码帧数据指针 */
} AOV_RECORD_QUEUE_FRAME_T;

/** @brief 队列句柄（不透明类型） */
typedef struct AOV_RECORD_QUEUE_S AOV_RECORD_QUEUE_T;

/**
 * @brief 初始化队列
 * @param queue          [out] 返回队列句柄
 * @param pool_size      缓冲池总大小 (bytes)
 * @param frame_capacity 队列最大帧数容量
 * @return 0 成功，< 0 失败
 */
int aov_record_queue_init(AOV_RECORD_QUEUE_T **queue,
                          size_t pool_size,
                          int frame_capacity);

/**
 * @brief 销毁队列，释放所有资源
 * @param queue 队列句柄
 */
void aov_record_queue_deinit(AOV_RECORD_QUEUE_T *queue);

/**
 * @brief 压入一帧
 * @param queue      队列句柄
 * @param schn       码流通道
 * @param vchn       视频通道
 * @param mode       编码模式
 * @param frame_type 帧子类型
 * @param pts        时间戳 (microseconds)
 * @param data       编码帧数据指针
 * @param data_size  数据长度 (bytes)
 * @return 0 成功，< 0 失败（队列满时返回负值）
 */
int aov_record_queue_push(AOV_RECORD_QUEUE_T *queue,
                          unsigned char schn,
                          unsigned char vchn,
                          unsigned char mode,
                          unsigned char frame_type,
                          uint64_t pts,
                          const unsigned char *data,
                          uint32_t data_size);

/**
 * @brief 查看队首帧（不移除）
 *
 * 返回的 data 指针由队列管理，调用者不应 free。
 * @param queue 队列句柄
 * @param frame [out] 返回帧数据
 * @return 0 成功，< 0 队列为空
 */
int aov_record_queue_peek(AOV_RECORD_QUEUE_T *queue,
                          AOV_RECORD_QUEUE_FRAME_T *frame);

/**
 * @brief 弹出队首帧
 * @param queue 队列句柄
 */
void aov_record_queue_pop(AOV_RECORD_QUEUE_T *queue);

/**
 * @brief 获取当前帧数
 * @return 当前队列中的帧数量
 */
int aov_record_queue_count(AOV_RECORD_QUEUE_T *queue);

/**
 * @brief 获取已使用的缓冲池大小
 * @return 已占用的字节数
 */
size_t aov_record_queue_used(AOV_RECORD_QUEUE_T *queue);

/**
 * @brief 获取缓冲池总容量
 * @return 缓冲池总大小 (bytes)
 */
size_t aov_record_queue_size(AOV_RECORD_QUEUE_T *queue);

/**
 * @brief 计算缓冲池使用率
 * @return 百分比值 (0~100)，-1 表示输入无效
 */
int aov_record_queue_usage_percent(AOV_RECORD_QUEUE_T *queue);

/**
 * @brief 获取累计丢弃帧数
 *
 * 当缓冲池满时无法压入新帧，该计数器累加。
 * @return 自初始化以来丢弃的总帧数
 */
uint64_t aov_record_queue_dropped(AOV_RECORD_QUEUE_T *queue);

#ifdef __cplusplus
}
#endif

#endif /* AOV_RECORD_QUEUE_H */
