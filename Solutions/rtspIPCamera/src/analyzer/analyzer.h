#ifndef __ANALYZER_H__
#define __ANALYZER_H__

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#include <opencv2/opencv.hpp>

#include "algoProcess.h"

typedef struct {
    void *pBuffer;
    int fd;
    int size;
}DmaBuf;

typedef struct {
    DmaBuf dma;
    cv::Mat image;
    pthread_rwlock_t imgLock;
    int chnId;
    ChnResult_t chnResult;
}vChnObject;

extern int analyzer_init(int32_t maxChn);
extern void analyzer_exit();

typedef struct {
    char fmt[16];
    int chnId;
    int width;
    int height;
    int horStride;
    int verStride;
    int dataSize;
}ImgDesc_t;
extern int videoOutHandle(char *imgData, ImgDesc_t imgDesc);

/*
 * 获取指定通道已画 OSD 的 BGR888 帧数据
 * 输出: bgrData 指向帧数据, width/height 输出宽高, dmaFd 输出DMA-BUF文件描述符
 * 返回: 0 成功, <0 失败
 */
extern int analyzer_getOsdFrame(int chnId, uint8_t **bgrData, int *width, int *height, int *dmaFd);

#endif
