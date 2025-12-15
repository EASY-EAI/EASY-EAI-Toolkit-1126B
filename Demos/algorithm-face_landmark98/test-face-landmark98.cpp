#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <fstream>
#include <iostream>
#include <vector>
#include <fstream>
#include <atomic>
#include <queue>
#include <thread>
#include <mutex>
#include <chrono>
#include <sys/time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include <opencv2/opencv.hpp>

#include <unistd.h>
#include <sys/syscall.h>
#include "face_detect.h"
#include "face_landmark98.h"

using namespace std;
using namespace cv;



int main(int argc, char **argv)
{

	if( argc != 2) {
		printf("./test-face-landmark98 xxx.jpg \n");
		return -1;
	}

	struct timeval start;
	struct timeval end;
	float time_use=0;

	rknn_context detect_ctx;
	std::vector<det> result;
	int ret;

	cv::Mat src;
	src = cv::imread(argv[1], 1);


	face_detect_init(&detect_ctx, "./face_detect.model");

	/* 人脸关键点定位初始化 */
	rknn_face_landmark_context_t face_landmark;

	ret = face_landmark98_init(&face_landmark, "./face_landmark98.model");
	if( ret < 0) {
		printf("face_mask_judgement_init fail! ret=%d\n", ret);
		return -1;
	}


	gettimeofday(&start,NULL); 
	face_detect_run(detect_ctx, src, result);

	gettimeofday(&end,NULL);
	time_use=(end.tv_sec-start.tv_sec)*1000000+(end.tv_usec-start.tv_usec);//微秒
	printf("face_detect time_use is %f\n",time_use/1000);

	printf("face num:%d\n", (int)result.size());

	for (int i = 0; i < (int)result.size(); i++) 
	{
		int x = (int)(result[i].box.x);
		int y = (int)(result[i].box.y);
		int w = (int)(result[i].box.width);
		int h = (int)(result[i].box.height);
		int max = (w > h)?w:h;

		// 判断图像裁剪是否越界
		if( (x < 0) || (y < 0) || ((x +max) > src.cols) || ((y +max) > src.rows) ) {
			continue;
		}
		
		// 人脸位置
		cv::Rect rect(x, y, w, h);
		cv::Mat face_roi =src(rect).clone();
		
		gettimeofday(&start,NULL); 
		std::vector<cv::Point> keys = face_landmark98_run(&face_landmark, face_roi);

		gettimeofday(&end,NULL);
		time_use=(end.tv_sec-start.tv_sec)*1000000+(end.tv_usec-start.tv_usec);//微秒
		printf("face_landmark time_use is %f\n",time_use/1000);

		// 原图位置
		for (int i = 0; i < (int)keys.size(); i++) {
			keys[i].x += rect.x;
			keys[i].y += rect.y;
		}

		for (int i = 0; i < (int)keys.size(); i++) 
		{
			printf("keyPoints %d :[%d, %d]\n", i, keys[i].x, keys[i].y);
			cv::circle(src, keys[i], 2, CV_RGB(0, 255, 0), 2);
		}
	
		//cv::rectangle(src, rect, CV_RGB(0, 255, 0), 2);

	}

	cv::imwrite("result.jpg", src);

	/* 人脸检测释放 */
	face_detect_release(detect_ctx);

	/* 人脸关键点定位释放 */
	face_landmark98_release(&face_landmark);

	return 0;
}

