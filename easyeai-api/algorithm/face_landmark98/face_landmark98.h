#ifndef __RKNN_FACE_LANDMARK_H__
#define __RKNN_FACE_LANDMARK_H__

#include "opencv2/opencv.hpp"
#include "rknn_api.h"


/**< @brief face_landmarkÄ£ÐÍ½á¹¹Ìå */
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
} rknn_face_landmark_context_t;


#ifdef __cplusplus
extern "C" {
#endif

	/**
	* @brief  face_landmark98_init
	*
	* @param[i/o]		p_face_landmark			face_landmark模型上下文
	* @param[in]		p_model_path			face_landmark rknn模型地址
	* @return									模型初始化结果，0成功，负值失败
	*/
	int face_landmark98_init(rknn_face_landmark_context_t *p_face_landmark, const char *p_model_path);


	/**
	* @brief  face_landmark98_run
	*
	* @param[in]		p_face_landmark			face_landmark模型上下文
	* @param[in]		image					待检测图片
	* @return									人脸关键点
	*/
	std::vector<cv::Point> face_landmark98_run(rknn_face_landmark_context_t *p_face_landmark, cv::Mat image);


	/**
	* @brief face_landmark_release
	*
	* @param[i/o]		p_face_landmark			face_landmark模型上下文
	* @return									模型释放结果
	*/
	int face_landmark98_release(rknn_face_landmark_context_t* p_face_landmark);


#ifdef __cplusplus
}
#endif

#endif //__RKNN_FACE_LANDMARK_H__