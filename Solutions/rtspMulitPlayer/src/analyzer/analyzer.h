#ifndef __ANALYZER_H__
#define __ANALYZER_H__

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#include <opencv2/opencv.hpp>

#include "algoProcess.h"

typedef struct {
    //char fmt[16];
    cv::Mat image;
    pthread_rwlock_t imgLock;
    int chnId;
    ChnResult_t chnResult;
}vChnObject;

extern int analyzer_init(int32_t maxChn);

/* DMA-BUF 帧描述（零拷贝路径） */
typedef struct {
    int chnId;
    int dmabuf_fd;     /* DMA-BUF fd (NV12) */
    int width;
    int height;
    int horStride;     /* 水平步长 */
    int verStride;     /* 垂直步长 */
}DmaFrame_t;

/* 旧的 CPU 内存帧接口（保留兼容，不再用于主要路径） */
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

/* 新的 DMA-BUF 零拷贝帧接口 */
extern int videoDmaHandle(DmaFrame_t frame);

#endif
