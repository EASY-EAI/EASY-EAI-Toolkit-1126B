#include <stdio.h>
#include <unistd.h>

#include "system_opt.h"
#include "font_engine.h"
#include "rga_wrapper.h"

#include "camera.h"
#include "display.h"
#include "person_detect.h"

using namespace std;
using namespace cv;
static Scalar colorArray[10]={
    Scalar(255, 0, 0, 255),
    Scalar(0, 255, 0, 255),
    Scalar(0,0,139,255),
    Scalar(0,100,0,255),
    Scalar(139,139,0,255),
    Scalar(209,206,0,255),
    Scalar(0,127,255,255),
    Scalar(139,61,72,255),
    Scalar(0,255,0,255),
    Scalar(255,0,0,255),
};

typedef struct{
    int resNumber;
	detect_result_group_t detect_Group;    
}AlgoRes_t;

typedef struct{
	AlgoRes_t algoRes[1];
}Result_t;

Mat algorithm_image;
pthread_mutex_t img_lock;

int plot_one_box(Mat src, int x1, int x2, int y1, int y2, char *label, char colour)
{
    int tl = round(0.002 * (src.rows + src.cols) / 2) + 1;
    rectangle(src, cv::Point(x1, y1), cv::Point(x2, y2), colorArray[(unsigned char)colour], 3);

    int tf = max(tl -1, 1);

    int base_line = 0;
    cv::Size t_size = getTextSize(label, FONT_HERSHEY_SIMPLEX, (float)tl/3, tf, &base_line);
    int x3 = x1 + t_size.width;
    int y3 = y1 - t_size.height - 3;

    rectangle(src, cv::Point(x1, y1), cv::Point(x3, y3), colorArray[(unsigned char)colour], -1);
    putText(src, label, cv::Point(x1, y1 - 2), FONT_HERSHEY_SIMPLEX, (float)tl/3, cv::Scalar(255, 255, 255, 255), tf, 8);
    return 0;
}

// 目标识别线程
void *detect_thread_entry(void *para)
{
	int ret;
	Result_t *pResult = (Result_t *)para;
	
	// 人员检测初始化
	rknn_context ctx;
	person_detect_init(&ctx, "person_detect.model");
	
	Mat image;
	while(1)
	{
        if(algorithm_image.empty()) {
			usleep(5);
            continue;
        }
		
		pthread_mutex_lock(&img_lock);
		image = algorithm_image.clone();
		pthread_mutex_unlock(&img_lock);
		// 算法分析
		ret = person_detect_run(ctx, image, &pResult->algoRes[0].detect_Group);
        if(0 != ret){
			usleep(1000);
			continue;
        }
        
		pResult->algoRes[0].resNumber = pResult->algoRes[0].detect_Group.count;
		if(pResult->algoRes[0].resNumber <= 0){
            memset(&pResult->algoRes[0], 0, sizeof(AlgoRes_t));
			usleep(1000);
			continue;
		}
		
		printf("\n>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
		printf("person number : %d\n", pResult->algoRes[0].resNumber);
		
        usleep(16*1000);
	}
	/* 人员检测释放 */
	person_detect_release(ctx);
	return NULL;
}

int main(int argc, char **argv)
{

	int ret = 0;
    if(1 == argc){
        printf("\nerr: Missing parameter!\n");
        printf("================= [usage] ==================\n");
        printf("example:\n");
        printf("\t%s <23/31>\n", argv[0]);
        printf("--------------------------------------------\n");
        return 0;
    }

    // 目标识别线程相关变量
	pthread_t mTid;
	Result_t Result;

    // camera原始图像相关变量
	int skip = 0;
	char *pbuf = NULL;
	Mat image;
    
#if 0
	// 初始化字体透明度和颜色
	FontColor color = {200, 135, 189, 67};    // {A, R, G, B};
	// 创建全局字体
	global_font_create("./simhei.ttf", CODE_UTF8);
	global_font_set_fontSize(80);
#endif

    // 1.初始化显示屏
    int width, height;
    if (disp_init(&width, &height) < 0) {
        fprintf(stderr, "DRM初始化失败\n");
        return -1;
    }

    char timestamp[64] = {0};
    
    // 2.打开摄像头
#define CAMERA_WIDTH    1920
#define CAMERA_HEIGHT   1080
#define	IMGRATIO        3
#define	IMAGE_SIZE      (CAMERA_WIDTH*CAMERA_HEIGHT*IMGRATIO)
    int cameraIndex = atoi(argv[1]); //通常csi0是video23, csi1是video31
    ret = mipicamera_init(cameraIndex, CAMERA_WIDTH, CAMERA_HEIGHT, 0);
    if (ret) {
        printf("error: %s, %d\n", __func__, __LINE__);
        goto exit3;
    }
    
    pbuf = (char *)malloc(IMAGE_SIZE);
    //pbuf = dma_alloc(&dma_fd, IMAGE_SIZE);
    if (!pbuf) {
        printf("error: %s, %d\n", __func__, __LINE__);
        ret = -1;
        goto exit2;
    }
    
    //跳过前10帧
    skip = 10;
    while(skip--) {
        ret = mipicamera_getframe(cameraIndex, pbuf);
        if (ret) {
            printf("error: %s, %d\n", __func__, __LINE__);
            goto exit1;
        }
    }
	
	// 3.创建识别线程，以及图像互斥锁
	pthread_mutex_init(&img_lock, NULL);
    memset(&Result, 0, sizeof(Result_t));
	CreateNormalThread(detect_thread_entry, &Result, &mTid);

    // 4.(取流 + 显示)循环
    while(1) {
		// 4.1、取流
		pthread_mutex_lock(&img_lock);
        ret = mipicamera_getframe(cameraIndex, pbuf);
        if(ret) {
			printf("error: %s, %d\n", __func__, __LINE__);
			pthread_mutex_unlock(&img_lock);
			continue;
        }
		algorithm_image = Mat(CAMERA_HEIGHT, CAMERA_WIDTH, CV_8UC3, pbuf);
		image = algorithm_image.clone();
		pthread_mutex_unlock(&img_lock);

		// 4.2、显示
		char text[256];
		for (int i = 0; i < Result.algoRes[0].resNumber; i++) {
        	detect_result_t *det_result = &(Result.algoRes[0].detect_Group.results[i]);
        	if( det_result->prop < 0.4) {
        		continue;
        	}
            
			// 标出识别目标框
    		sprintf(text, "%s %.1f%%", det_result->name, det_result->prop * 100);
    		printf("%s @ (%d %d %d %d) %f\n", det_result->name, det_result->box.left, det_result->box.top,
    			   det_result->box.right, det_result->box.bottom, det_result->prop);
    		int x1 = det_result->box.left;
    		int y1 = det_result->box.top;
    		int x2 = det_result->box.right;
    		int y2 = det_result->box.bottom;
			// 标出识别目标定位标记
    		/*
    		rectangle(image, cv::Point(x1, y1), cv::Point(x2, y2), cv::Scalar(255, 0, 0, 255), 3);
    		putText(image, text, cv::Point(x1, y1 + 12), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0));
    		*/
    		plot_one_box(image, x1, x2, y1, y2, text, i%10);
		}
        
        //memset(timestamp, 0, sizeof(timestamp));
        //sprintf(timestamp,"%lu", get_timeval_ms());
        //printf("%s\n",timestamp);
        //putText((uint8_t *)image.data, CAMERA_WIDTH, CAMERA_HEIGHT, timestamp, 210, 540, color);
        
        disp_commit(image.data, CAMERA_WIDTH, CAMERA_HEIGHT, HAL_TRANSFORM_ROT_270);
        usleep(15*1000); //15ms
    }
    
exit1:
    //dma_free(&dma_fd, pbuf, IMAGE_SIZE);
    free(pbuf);
    pbuf = NULL;
    
exit2:
    mipicamera_exit(cameraIndex);
    
exit3:
    disp_exit();
    
    return ret;
}

