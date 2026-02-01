#ifndef __GESTURES_POSE_H__
#define __GESTURES_POSE_H__

#include "opencv2/opencv.hpp"
#include "rknn_api.h"



/**< @brief gestures pose模型结构使*/
typedef struct {
    rknn_context rknn_ctx;
    rknn_input_output_num io_num;
    rknn_tensor_attr* input_attrs;
    rknn_tensor_attr* output_attrs;
    int model_channel;
    int model_width;
    int model_height;
    int input_image_width;
    int input_image_height;
    bool is_quant;
	int  cls_num;
} rknn_gestures_pose_context_t;


/**< @brief gestures pose检测和分割结果 */
typedef struct {
	int		cls_id;
	int		left;
	int		top;
	int		right;
	int		bottom;			
	float	prop;			
	float	keypoints[21][3];	//keypoints x,y,conf
} rknn_gestures_pose_result_t;


#ifdef __cplusplus
extern "C" {
#endif

	/**
	* @brief  rknn gestures pose 初始化函敿
	*
	* @param[in]		p_model_path			gestures pose rknn模型地址
	* @param[i/o]		p_gestures_pose			gestures pose模型上下斿
	* @param[in]		cls_num					类别敿
	* @return									模型初始化结果，0成功，负值失贿
	*/
	int rknn_gestures_pose_init(const char *p_model_path, rknn_gestures_pose_context_t *p_gestures_pose, int cls_num);


	/**
	* @brief  rknn gestures pose计算函数
	*
	* @param[in]		image					待检测图牿
	* @param[in]		p_gestures_pose			gestures pose模型上下斿
	* @param[in]		nms_threshold			NMS阈倿
	* @param[in]		conf_threshold			置信度阈倿
	* @return									检测和姿态估计结枿
	*/
	std::vector<rknn_gestures_pose_result_t> rknn_gestures_pose_calc(cv::Mat image, rknn_gestures_pose_context_t *p_gestures_pose, float nms_threshold, float conf_threshold);


	/**
	* @brief  rknn gestures pose释放函数
	*
	* @param[i/o]		p_gestures_pose			gestures pose模型上下斿
	* @return									模型释放结果
	*/
	int rknn_gestures_pose_deinit(rknn_gestures_pose_context_t* p_gestures_pose);


#ifdef __cplusplus
}
#endif

#endif //__RKNN_gestures_POSE_H__