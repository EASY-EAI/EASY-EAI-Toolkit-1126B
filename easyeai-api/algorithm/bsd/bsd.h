#ifndef BSD_H
#define BSD_H

#include "bsd_postprocess.h"
#include "rknn_api.h"
#include <opencv2/opencv.hpp>




/* 
 * bsd初始化函数
 * ctx:输入参数,rknn_context句柄
 * path:输入参数,算法模型路径
 */
int bsd_init(rknn_context *ctx, const char * path);


/* 
 * bsd执行函数
 * ctx:输入参数,rknn_context句柄
 * input_image:输入参数,图像数据输入(cv::Mat是Opencv的类型)
 * output_dets:输出参数，目标检测框输出
 */
int bsd_run(rknn_context ctx, cv::Mat input_image, detect_result_group_t *detect_result_group);


/* 
 * bsd释放函数
 * ctx:输入参数,rknn_context句柄
 */
int bsd_release(rknn_context ctx);




#endif
