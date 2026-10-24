#ifndef __UTILS_H__
#define __UTILS_H__

#include <opencv2/opencv.hpp>

#include "lane_detect.h"

/**
 * @brief 将检测结果转换为车道线列表
 * @param results 检测结果列表
 * @param lanes 车道线列表
 * @param info 后处理信息
 * @return 0表示成功，非0表示失败
 */
int results_to_lanes(const std::vector<detect_result> &results, std::vector<lane> &lanes, const postprocess_info &info);

/**
 * @brief 在图像上绘制车道线
 * @param image 输入图像
 * @param lanes 车道线列表
 */
void draw_line(cv::Mat &image, const std::vector<lane> &lanes);

#endif  // __UTILS_H__