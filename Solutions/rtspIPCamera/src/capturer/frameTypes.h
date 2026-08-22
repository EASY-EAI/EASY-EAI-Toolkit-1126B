/**
 * 共享帧类型定义
 *
 * 将 FrameDesc_t 和 FrameCallback 类型定义抽出为共享头文件，
 * 供 capturer / streamer 共用。
 */

#ifndef __FRAME_TYPES_H__
#define __FRAME_TYPES_H__

#include <stdint.h>

/* 帧描述信息 */
typedef struct {
    char strFmt[32];
    int  width;
    int  height;
    int  horStride;
    int  verStride;
} FrameDesc_t;

/* 帧数据回调（编码后码流） */
typedef void (*FrameCallback)(const uint8_t *data, uint32_t len,
                              const FrameDesc_t *desc, void *userData);

#endif /* __FRAME_TYPES_H__ */
