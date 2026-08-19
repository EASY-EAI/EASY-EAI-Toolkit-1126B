#include <stdio.h>
#include <cstring>

#include <opencv2/opencv.hpp>

#include "rga_wrapper.h"

int main()
{
	cv::Mat src = cv::imread("test.jpeg", cv::IMREAD_COLOR);
	if (src.empty()) {
		fprintf(stderr, "imread test.jpeg failed\n");
		return 1;
	}

	printf("src width=%d height=%d\n", src.cols, src.rows);

	if (!src.isContinuous())
		src = src.clone();

	int dst_w = src.cols / 2;
	int dst_h = src.rows / 2;
	cv::Mat dst(dst_h, dst_w, CV_8UC3);

	Image srcImage;
	memset(&srcImage, 0, sizeof(srcImage));
	Image dstImage;
	memset(&dstImage, 0, sizeof(dstImage));

	srcImage.fmt = RK_FORMAT_BGR_888;
	srcImage.width = src.cols;
	srcImage.height = src.rows;
	srcImage.hor_stride = src.cols;
	srcImage.ver_stride = src.rows;
	srcImage.rotation = HAL_TRANSFORM_ROT_0;
	srcImage.fd = -1;
	srcImage.pBuf = src.data;

	dstImage.fmt = RK_FORMAT_BGR_888;
	dstImage.width = dst.cols;
	dstImage.height = dst.rows;
	dstImage.hor_stride = dst.cols;
	dstImage.ver_stride = dst.rows;
	dstImage.rotation = HAL_TRANSFORM_ROT_0;
	dstImage.fd = -1;
	dstImage.pBuf = dst.data;

	if (srcImg_ConvertTo_dstImg(&dstImage, &srcImage) != 0) {
		fprintf(stderr, "srcImg_ConvertTo_dstImg failed\n");
		return 1;
	}
    
	if (!cv::imwrite("resize.jpeg", dst)) {
		fprintf(stderr, "imwrite resize,jpeg failed\n");
		return 1;
	}
    
	return 0;
}
