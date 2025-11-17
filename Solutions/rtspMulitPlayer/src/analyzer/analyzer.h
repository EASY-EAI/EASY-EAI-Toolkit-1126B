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
    int result;
}vChnObject;

extern int analyzer_init();

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

#endif

