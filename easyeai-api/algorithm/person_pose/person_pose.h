#ifndef __PERSON_POSE_H__
#define __PERSON_POSE_H__

#include "opencv2/opencv.hpp"
#include "rknn_api.h"



/**< @brief person pose模型结构体 */
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
} person_pose_context_t;


/**< @brief person pose检测和分割结果 */
typedef struct {
	int		cls_id;
	int		left;
	int		top;
	int		right;
	int		bottom;			
	float	prop;			
	float	keypoints[17][3];	//keypoints x,y,conf
} person_pose_result_t;


#ifdef __cplusplus
extern "C" {
#endif

	/**
	* @brief  person pose 初始化函数
	*
	* @param[in]		p_model_path			pose init rknn模型地址
	* @param[i/o]		p_person_pose			person pose模型上下文
	* @param[in]		cls_num					类别数
	* @return									模型初始化结果，0成功，负值失败
	*/
	int person_pose_init(const char *p_model_path, person_pose_context_t *p_person_pose, int cls_num);


	/**
	* @brief  person pose 运行函数
	*
	* @param[in]		image					待检测图片
	* @param[in]		p_person_pose			person pose模型上下文
	* @param[in]		nms_threshold			NMS阈值
	* @param[in]		conf_threshold			置信度阈值
	* @return									检测和姿态估计结果
	*/
	std::vector<person_pose_result_t> person_pose_run(cv::Mat image, person_pose_context_t *p_person_pose, float nms_threshold, float conf_threshold);


	/**
	* @brief  rknn person pose释放函数
	*
	* @param[i/o]		p_person_pose			person pose模型上下文
	* @return									模型释放结果
	*/
	int person_pose_release(person_pose_context_t* p_person_pose);


#ifdef __cplusplus
}
#endif

#endif //__PERSON_POSE_H__