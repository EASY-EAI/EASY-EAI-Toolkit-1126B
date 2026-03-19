#ifndef __ALGOPROCESS_H__
#define __ALGOPROCESS_H__

#include <stdbool.h>
#include <stdint.h>

#include <opencv2/opencv.hpp>

#include "person_detect.h"

using namespace cv;

#define ALGOMAXNUM 1

typedef struct{
    int resNumber;
	detect_result_group_t detect_Group;    
}AlgoRes_t;


typedef struct {
	AlgoRes_t algoRes[ALGOMAXNUM];
}ChnResult_t;


extern int algorithm_init();
extern ChnResult_t algorithm_process(int chnId, Mat image);

#endif

