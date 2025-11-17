#ifndef __ALGOPROCESS_H__
#define __ALGOPROCESS_H__

#include <stdbool.h>
#include <stdint.h>

#include <opencv2/opencv.hpp>
using namespace cv;

extern int algorithm_init();
extern int algorithm_process(int chnId, Mat image);

#endif

