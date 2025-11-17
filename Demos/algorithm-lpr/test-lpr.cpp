#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale>
#include <codecvt>
#include <string>
#include <sys/time.h>
#include <opencv2/opencv.hpp>
#include "lpr_detector.h"
#include "lpr_classifier.h"
#include "lpr_recognizer.h"
#include "lpr.h"
#include "put_text_zh.h"


/*******************************************
* 车牌识别demo
********************************************/
int lpr_demo(char *p_det_path, char *p_cls_path, char *p_rec_path, char *p_img_path)
{
	struct timeval start;
    struct timeval end;
	float time_use=0;

	std::vector<std::string> layers = { "单层", "双层"};
	std::vector<std::string> colors = { "蓝色", "绿色", "黄色" };

	PutTextZH put_text = PutTextZH("./simhei.ttf");
	cv::Scalar font_size{ 30, 0.5, 0.1, 0 };	// 字体大小/空白比例/间隔比例/旋转角度
	put_text.setFont(nullptr, &font_size, nullptr, nullptr);

	cv::Mat image = cv::imread(p_img_path);
	rknn_lpr_t lpr;
	
	// 车牌识别初始化
	int ret;
	ret = lpr_init(p_det_path, p_cls_path, p_rec_path, &lpr);

	// 车牌识别
	float conf_thresh = 0.35;
	float nms_thresh = 0.35;

	gettimeofday(&start,NULL); 

	std::vector<rknn_lpr_result_t> results = lpr_run(image, &lpr, conf_thresh, nms_thresh);

	gettimeofday(&end,NULL);
    time_use=(end.tv_sec-start.tv_sec)*1000000+(end.tv_usec-start.tv_usec);//微秒
	printf("车牌识别耗时： %f\n",time_use/1000);

	// 绘制结果
	int num = results.size();
	for (int i = 0; i < num; i++) {
		cv::rectangle(image, results[i].box, CV_RGB(255, 0, 0), 2);
		for (int j = 0; j < 4; j++) {
			cv::circle(image, results[i].key_pts[j], 2, CV_RGB(0, 255, 0), 3);
		}
		//char p_text[128] = "";
		//for (int j = 0; j < results[i].char_list.size(); j++) {
		//	sprintf(p_text, "%s%s", p_text, results[i].char_list[j].c_str());
		//}
		//sprintf(p_text, "%s score: %0.2f", p_text, results[i].det_score);
		//cv::putText(image, p_text, cv::Point(results[i].box.x, results[i].box.y - 5), 1, 2, CV_RGB(255, 0, 0), 3);

		std::string temp_str;
		int num = results[i].char_list.size();
		for (int j = 0; j < num; j++) {
			temp_str += results[i].char_list[j];
		}

		// 转换为宽字符字符串
		std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
		std::wstring wstr = converter.from_bytes(temp_str);

		// 如果需要 wchar_t 数组
		wchar_t w_text[256];
		wcsncpy(w_text, wstr.c_str(), sizeof(w_text) / sizeof(wchar_t));
		w_text[255] = L'\0'; 
		put_text.putText(image, w_text, cv::Point(results[i].box.x, results[i].box.y - 5), CV_RGB(255, 0, 0));
		printf("车牌%d：%s, 颜色: %s, 车牌层数: %s\n", i+1, temp_str.c_str(), colors[results[i].color].c_str(), layers[results[i].layer_num].c_str());
	}

	cv::imwrite("dst.jpg", image);

	// 车牌识别释放
	ret = lpr_release(&lpr);

	return ret;
}


/*******************************************
* 车牌检测demo
********************************************/
int lpr_detector_demo(char *p_det_path, char *p_img_path)
{
	cv::Mat image = cv::imread(p_img_path);

	rknn_lpr_detector_t lpr_det;

	// 车牌检测初始化
	int ret = rknn_lpr_detector_init(p_det_path, &lpr_det);

	// 车牌检测
	float conf_thresh = 0.35;
	float nms_thresh = 0.35;
	std::vector<rknn_lpr_det_result_t> results = rknn_lpr_detector_calc(image, &lpr_det, conf_thresh, nms_thresh);


	// 绘制结果
	int num = results.size();
	for (int i = 0; i < num; i++) {
		cv::rectangle(image, results[i].box, CV_RGB(255, 0, 0), 2);
		for (int j = 0; j < 4; j++) {
			cv::circle(image, results[i].key_pts[j], 2, CV_RGB(0, 255, 0), 3);
		}
		printf("Layer num = %d\n", results[i].layer_num);
	}
	
	cv::imwrite("det.jpg", image);

	// 车牌检测释放
	ret = rknn_lpr_detector_deinit(&lpr_det);

	return ret;
}


/*******************************************
* 车牌分类Demo
********************************************/
int lpr_classifer_demo(char *p_cls_path, char *p_img_path)
{
	int label = 0;
	float score;
	std::vector<std::string> colors = { "blue", "green", "yellow" };
		
	cv::Mat image = cv::imread(p_img_path);

	// 车牌分类初始化
	rknn_lpr_classifer_t lpr_cls;
	int ret = rknn_lpr_classifer_init(p_cls_path, &lpr_cls);

	// 车牌分类计算
	ret = rknn_lpr_classifer_calc(image, &lpr_cls, label, score);
	
	// 车牌分类释放
	ret = rknn_lpr_classifer_deinit(&lpr_cls);
	
	return ret;
}


/*******************************************
* 车牌字符识别Demo
********************************************/
int lpr_recognizer_demo(char *p_rec_path, char *p_img_path)
{
	float score;
	 
	cv::Mat image = cv::imread(p_img_path);

	// 车牌字符识别初始化
	rknn_lpr_recognizer_t lpr_rec;
	int ret = rknn_lpr_recognizer_init(p_rec_path, &lpr_rec);

	//车牌字符识别计算 10ms
	std::vector<std::string> char_list;
	ret = rknn_lpr_recognizer_calc(image, &lpr_rec, char_list, score);

	/*
	char p_text[128] = "";
	int num = char_list.size();
	for (int j = 0; j < num; j++) {
		sprintf(p_text,"%s%s", p_text, char_list[j].c_str());
	}
	
	printf("车牌字符识别耗时：%0.2fms, 车牌号：%s, score = %0.2f\n", run_time, p_text, score);
	*/

	// 车牌字符识别释放
	ret = rknn_lpr_recognizer_deinit(&lpr_rec);

	return ret;
}


/*******************************************
* 主函数
********************************************/
int main(int argc, char **argv)
{
    if (argc != 5) {
        printf("%s <det_model_path> <cls_model_path> <rec_model_path> <image_path>\n", argv[0]);
        return -1;
    }

    char *p_det_path = argv[1];
	char *p_cls_path = argv[2];
	char *p_rec_path = argv[3];
    char *p_img_path = argv[4];

	// 车牌识别demo
	lpr_demo(p_det_path, p_cls_path, p_rec_path, p_img_path);

	//// 车牌检测demo
	//lpr_detector_demo(p_det_path, p_img_path);

	//// 车牌分类Demo
	//lpr_classifer_demo(p_cls_path, p_img_path);

	////// 车牌字符识别Demo
	//lpr_recognizer_demo(p_rec_path, p_img_path);

	return 0;

}
