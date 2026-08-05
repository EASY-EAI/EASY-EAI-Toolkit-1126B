#ifndef __GESTURES_H__
#define __GESTURES_H__

#include "gestures_classify.h"
#include "gestures_pose.h"

/**< @brief 手势识别结构体 */
typedef struct {
    rknn_gestures_pose_context_t  *p_gestures;
    rknn_gestures_classify_context_t  *p_mv4;
}rknn_gestures_context_t;


/**< @brief 手势识别结果 */
typedef struct {
	int		gesture;            // 手势类别
	int		left;               // 手部检测框
	int		top;
	int		right;
	int		bottom;			
	float	score;			    // 置信度
	float	keypoints[21][2];	// keypoints x,y
} rknn_gestures_result_t;


#ifdef __cplusplus
extern "C" {
#endif

   /**
	* @brief  手势识别初始化
	*
    * @param[in]		p_gesture_pose_path			手势关键点模型地址
	* @param[in]		p_gesture_classify_path		手势姿态模型地址
	* @param[i/o]		gestures			    手势识别上下文
	* @param[in]		cls_num			类别数
	* @return									模型初始化结果，0成功，负值失败
	*/
    int gestures_init(const char *p_gestures_path, const char *p_mobilenet_path, rknn_gestures_context_t &gestures, int cls_num);


   /**
	* @brief  手势识别函数
	*
    * @param[in]		gestures			    手势识别上下文
    * @param[in]		image			        待识别图片
	* @param[in]		conf_threshold		    gestures目标检测置信度
	* @param[in]		nms_threshold		    gestures非最大抑制阈值
	* @return									模型初始化结果，0成功，负值失败
	*/
    std::vector<rknn_gestures_result_t> gestures_run(rknn_gestures_context_t &gestures, cv::Mat image, float conf_threshold, float nms_threshold);


    /**
	* @brief  手势识别释放
	*
	* @param[i/o]		p_gestures			    手势识别上下文
	* @return									模型释放结果：0成功，负值失败
	*/
    int gestures_release(rknn_gestures_context_t &gestures);


#ifdef __cplusplus
}
#endif

#endif // __RKNN_GESTURES_H__