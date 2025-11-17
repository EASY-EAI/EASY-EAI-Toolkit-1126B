#ifndef __LPR_H__
#define __LPR_H__

#include "lpr_detector.h"
#include "lpr_classifier.h"
#include "lpr_recognizer.h"

/**< @brief lpr 车牌识别 */
typedef struct {
	rknn_lpr_detector_t		det;	// 车牌检测模型
	rknn_lpr_classifer_t	cls;	// 车牌分类模型
	rknn_lpr_recognizer_t   rec;	// 车牌识别模型
}rknn_lpr_t;


/**< @brief lpr 车牌识别结果 */
typedef struct {
	cv::Rect					box;				// 检测框位置
	int                         color;				// 车牌颜色：0蓝色，1绿色，2黄色
	int							layer_num;			// 车牌行数0表一行，1表2行
	float                       det_score;			// 车牌检测置信度
	float                       cls_score;			// 车牌分类置信度
	float                       reg_score;			// 字符识别置信度
	std::vector<std::string>    char_list;			// 车牌字符
	cv::Point					key_pts[4];			// 车牌四个角点
}rknn_lpr_result_t;



#ifdef __cplusplus
extern "C" {
#endif

	/**
	* @brief  rknn lpr 车牌识别初始化函数
	*
	* @param[in]		p_det_model				车牌检测模型地址
	* @param[in]		p_cls_model				车牌分类模型地址
	* @param[in]		p_rec_model				车牌识别模型地址
	* @param[i/o]		p_lpr					lpr车牌识别
	* @return									模型初始化结果，0成功，负值失败
	*/
	int lpr_init(const char *p_det_model, const char *p_cls_model, const char *p_rec_model, rknn_lpr_t *p_lpr);


	/**
	* @brief  LPR 车牌识别函数
	*
	* @param[in]		image					待分类图片（BGR格式）
	* @param[i/o]		p_lpr					lpr车牌识别
	* @param[in]		conf_thresh				置信度阈值
	* @param[in]		nms_thresh				nms阈值
	* @return									返回车牌识别结果
	*/
	std::vector<rknn_lpr_result_t> lpr_run(cv::Mat image, rknn_lpr_t *p_lpr, float conf_thresh, float nms_thresh);


	/**
	* @brief  LPR 车牌识别释放函数
	*
	* @param[i/o]		p_lpr					车牌分类器模型上下文
	* @return									模型释放结果
	*/
	int lpr_release(rknn_lpr_t *p_lpr);

#ifdef __cplusplus
}
#endif


#endif