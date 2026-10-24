#ifndef __LANE_DETECT_H__
#define __LANE_DETECT_H__

#include <vector>

#include <opencv2/opencv.hpp>
#include <rknn_api.h>

/**
 * @brief 车道线检测结果结构体
 * @param score 置信度
 * @param y0 1 - 起始点的纵坐标(归一化)
 * @param x0 起始点的横坐标(归一化)
 * @param theta 车道线的角度
 * @param length 车道线的长度
 * @param xs 车道线在每个采样点的横坐标(推理坐标系下的像素值)，长度为 72
 */
typedef struct {
    float score;
    float y0;
    float x0;
    float theta;
    float length;
    std::vector<float> xs;
} detect_result;

/**
 * @brief 车道线结构体
 * @param points 车道线上的点, 原图坐标系
 */
typedef struct {
    std::vector<cv::Point> points;
} lane;

/**
 * @brief 车道线检测后处理信息结构体
 * @param ratio_y 图像缩放比例（纵向）
 * @param ratio_x 图像缩放比例（横向）
 * @param croped_y 图像裁剪的纵向偏移量
 */
typedef struct {
    float ratio_y;
    float ratio_x;
    int croped_y;
} postprocess_info;

/**
 * @brief 车道线检测上下文结构体
 * @param rknn_ctx RKNN上下文
 * @param io_num 输入输出数量
 * @param input_attrs 输入张量属性
 * @param output_attrs 输出张量属性
 * @param inference_width 推理图像宽度
 * @param inference_height 推理图像高度
 */
typedef struct {
    rknn_context rknn_ctx;
    rknn_input_output_num io_num;
    rknn_tensor_attr *input_attrs;
    rknn_tensor_attr *output_attrs;
    int inference_width;
    int inference_height;
} lane_detect_context;

/**
 * @brief 初始化车道线检测模型
 * @param ctx 上下文指针
 * @param model_path 模型文件路径
 * @return 0表示成功，非0表示失败
 */
int lane_detect_init(lane_detect_context *ctx, const char *model_path);

/**
 * @brief 运行车道线检测
 * @param ctx 上下文指针
 * @param image 输入图像
 * @param results 检测到的原始输出结果
 * @param info 后处理信息
 * @param score_threshold 置信度阈值，低于该值的候选线将被丢弃（默认 0.41）
 * @param nms_threshold 非极大值抑制的重叠距离阈值（像素，默认 50）
 * @return 0表示成功，非0表示失败
 */
int lane_detect_run(lane_detect_context *ctx, const cv::Mat &image, std::vector<detect_result> &results,
    postprocess_info &info, float score_threshold = 0.41f, int nms_threshold = 50);

/**
 * @brief 释放车道线检测模型
 * @param ctx 上下文指针
 */
int lane_detect_release(lane_detect_context *ctx);

#endif  // __LANE_DETECT_H__
