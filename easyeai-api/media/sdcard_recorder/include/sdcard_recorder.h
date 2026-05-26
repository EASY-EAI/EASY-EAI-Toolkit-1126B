#ifndef __SDCARD_RECORDER_H__
#define __SDCARD_RECORDER_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 确保目录存在，不存在则创建
 * @param dir 目录绝对路径
 */
void SdRecorder_EnsureDir(const char *dir);

/**
 * @brief 生成录像文件路径（基于时间戳命名）
 *
 * 生成格式：{dir}/{prefix}_YYYYMMDD_HHMMSS.mp4
 *
 * @param dir     存储根目录，例如 "/mnt/sdcard/video"
 * @param prefix  文件名前缀，例如 "cam0"
 * @param buf     输出缓冲区
 * @param max_len 缓冲区长度
 */
void SdRecorder_GeneratePath(const char *dir, const char *prefix,
                             char *buf, size_t max_len);

/**
 * @brief 清理最旧的录像文件直到文件夹总大小低于阈值
 *
 * @param dir            录像目录
 * @param max_size_mb    允许的最大容量(MB)，<=0 表示不限制
 */
void SdRecorder_CleanOldRecords(const char *dir, int max_size_mb);

#ifdef __cplusplus
}
#endif

#endif
