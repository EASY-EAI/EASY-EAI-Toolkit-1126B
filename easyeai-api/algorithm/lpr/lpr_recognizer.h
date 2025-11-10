#ifndef __LPR_RECOGNIZER_H__
#define __LPR_RECOGNIZER_H__

#include "rknn_api.h"
#include "opencv2/opencv.hpp"

/**< @brief 车牌识别器 */
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
	int output_width;
	int output_height;
	bool is_quant;
}rknn_lpr_recognizer_t;


#ifdef __cplusplus
extern "C" {
#endif

	/**
	* @brief  rknn 车牌字符识别初始化函数
	*
	* @param[in]		p_model_path			车牌字符识别模型地址
	* @param[i/o]		p_rec					车牌字符识别模型上下文
	* @return									模型初始化结果，0成功，负值失败
	*/
	int rknn_lpr_recognizer_init(const char *p_model_path, rknn_lpr_recognizer_t *p_rec);


	/**
	* @brief  rknn 车牌字符识别函数
	*
	* @param[in]		image					待识别车牌图片（BGR格式）
	* @param[i/o]		p_rec					车牌字符识别模型上下文
	* @param[i/o]		char_list				车牌号
	* @param[i/o]		score					置信度评分
	* @return									模型初始化结果，0成功，负值失败
	*/
	int rknn_lpr_recognizer_calc(cv::Mat image, rknn_lpr_recognizer_t *p_rec, std::vector<std::string> &char_list, float &score);


	/**
	* @brief  rknn  车牌字符识别释放函数
	*
	* @param[i/o]		p_rec					车牌字符识别模型上下文
	* @return									模型释放结果
	*/
	int rknn_lpr_recognizer_deinit(rknn_lpr_recognizer_t *p_rec);



#ifdef __cplusplus
}
#endif

#endif //__LPR_RECOGNIZER_H__