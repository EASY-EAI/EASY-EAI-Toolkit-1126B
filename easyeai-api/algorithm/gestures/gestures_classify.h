#ifndef __RKNN_MOBILENET_V4_H__
#define __RKNN_MOBILENET_V4_H__

#include "rknn_api.h"
#include "opencv2/opencv.hpp"

/**< @brief mobilenet v4图像分类 */
typedef struct {
    rknn_context rknn_ctx;
    rknn_input_output_num io_num;
    rknn_tensor_attr* input_attrs;
    rknn_tensor_attr* output_attrs;
    int model_channel;
    int model_width;
    int model_height;
} rknn_gestures_classify_context_t;


#ifdef __cplusplus
extern "C" {
#endif

   /**
	* @brief  rknn gestures_classify模型初始化
	*
	* @param[in]		p_model_path			gestures_classify rknn模型地址
	* @param[i/o]		p_mv4			        gestures_classify 模型上下文
	* @return									模型初始化结果，0成功，负值失败
	*/
    int rknn_gestures_classify_init(const char *p_model_path, rknn_gestures_classify_context_t *p_mv4);


   /**
	* @brief  rknn gestures_classify图像分类
	*
    * @param[in]		p_mv4			        gestures_classify模型上下文
	* @param[in]		image					待分类图片
	* @param[i/o]		label			        分类类别
	* @param[i/o]		score			        分类置信度
	* @return									计算结果状态：0成功，负值失败
	*/
    int rknn_gestures_classify_calc(rknn_gestures_classify_context_t *p_mv4, cv::Mat image, int &label, float &score);


   /**
	* @brief  rknn gestures_classify模型释放
	*
	* @param[i/o]		p_mv4			        gestures_classify模型上下文
	* @return									模型释放结果：0成功，负值失败
	*/
    int rknn_gestures_classify_deinit(rknn_gestures_classify_context_t *p_mv4);


#ifdef __cplusplus
}
#endif

#endif //__RKNN_MOBILENET_V4_H__