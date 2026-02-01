#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <opencv2/opencv.hpp>
#include "gestures.h"


/// 分类名称
char gpp_gestures[27][100] = { "00-call", "01-dislike", "02-fist", "03-four", "04-grabbing", "05-grip", "06-like", "07-little_finger", "08-middle_finger",
	"09-no_gesture", "10-ok", "11-one", "12-palm", "13-peace", "14-peace_inverted", "15-point", "16-rock", "17-stop", "18-stop_inverted", "19-three",
	 "20-three_gun", "21-three2", "22-three3", "23-thumb_index", "24-two_up", "25-two_up_inverted", "26-gestures_bg" };


/// 绘制手势
cv::Mat draw_image(cv::Mat image, std::vector<rknn_gestures_result_t> results)
{
	long unsigned int i =0;
	for (i = 0; i < results.size(); i++) {
		cv::Rect rect(results[i].left, results[i].top, (results[i].right - results[i].left), (results[i].bottom - results[i].top));
		cv::rectangle(image, rect, CV_RGB(255, 0, 0), 2);
		char p_text[128];
		sprintf(p_text, "%s-%0.2f", gpp_gestures[results[i].gesture], results[i].score);
		cv::putText(image, p_text, cv::Point(rect.x, rect.y - 5), 1, 1.2, CV_RGB(255, 0, 0), 2);

		cv::Point pt0(results[i].keypoints[0][0], results[i].keypoints[0][1]);
		cv::circle(image, pt0, 2, CV_RGB(0, 255, 0), 3);
		for (int m = 0; m < 5; m++) {
			int idx = 4 * m + 1;
			cv::Point pt1(results[i].keypoints[idx][0], results[i].keypoints[idx][1]);
			cv::Point pt2(results[i].keypoints[idx + 1][0], results[i].keypoints[idx + 1][1]);
			cv::Point pt3(results[i].keypoints[idx + 2][0], results[i].keypoints[idx + 2][1]);
			cv::Point pt4(results[i].keypoints[idx + 3][0], results[i].keypoints[idx + 3][1]);
			cv::circle(image, pt1, 2, CV_RGB(0, 255, 0), 3);
			cv::circle(image, pt2, 2, CV_RGB(0, 255, 0), 3);
			cv::circle(image, pt3, 2, CV_RGB(0, 255, 0), 3);
			cv::circle(image, pt4, 2, CV_RGB(0, 255, 0), 3);
			cv::line(image, pt0, pt1, CV_RGB(255, 0, 0), 2);
			cv::line(image, pt1, pt2, CV_RGB(255, 0, 0), 2);
			cv::line(image, pt2, pt3, CV_RGB(255, 0, 0), 2);
			cv::line(image, pt3, pt4, CV_RGB(255, 0, 0), 2);
		}
	}
	return image;
}

/// 主函数
int main(int argc, char **argv)
{
    if (argc != 4) {
        printf("%s <gesture_pose_path> <gesture_classify_path> <image_path>\n", argv[0]);
        return -1;
    }

    const char *p_gesture_pose_path = argv[1];
	const char *p_gesture_classify_path = argv[2];
    const char *p_img_path = argv[3];
	printf("gesture pose model path = %s, gesture classify path = %s, image path = %s\n", p_gesture_pose_path, p_gesture_classify_path, p_img_path);

	// 读取图片
	cv::Mat image = cv::imread(p_img_path);
	  if(image.empty()){
        printf("Image is empty.\n");
        return 0;
    }
	printf("Image size = (%d, %d)\n", image.rows, image.cols);

	// 手势识别初始化
	int ret;
	rknn_gestures_context_t gestures;
	gestures_init(p_gesture_pose_path, p_gesture_classify_path, gestures, 1);

	// 手势识别函数
	double start = static_cast<double>(cv::getTickCount());
	
	std::vector<rknn_gestures_result_t> results = gestures_run(gestures, image, 0.35, 0.35);

	double end = static_cast<double>(cv::getTickCount());
	double runtime = (end - start) / cv::getTickFrequency() * 1000;
	std::cout << "Gestures run time: " << runtime << " ms" << std::endl;

	// 绘制结果
	image = draw_image(image, results);
	
	cv::imwrite("result.jpg", image);
	printf("Detect size = %ld\n", results.size());

	// 内存释放
    ret = gestures_release(gestures);

    return ret;
}
