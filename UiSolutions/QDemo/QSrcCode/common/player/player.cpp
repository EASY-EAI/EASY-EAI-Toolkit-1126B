/**
 *
 * Copyright 2026 by Guangzhou Easy EAI Technologny Co.,Ltd.
 * website: www.easy-eai.com
 *
 * Author: ZJH <zhongjiehao@easy-eai.com>
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * License file for more details.
 *
 */

#include <opencv2/opencv.hpp>

#include "system_opt.h"
#include "player.h"

using namespace cv;

typedef struct{
    int resNumber;
//    detect_result_group_t detect_Group;
}AlgoRes_t;

typedef struct{
    AlgoRes_t algoRes[1];
}Result_t;

static bool mThreadWorking = false;
static pthread_t mTid;
static Result_t Result;

static Mat algorithm_image;
static pthread_mutex_t img_lock;
// 目标识别线程
void *main_thread_entry(void *para)
{
    int ret;
    Result_t *pResult = (Result_t *)para;

    // 人员检测初始化
//    rknn_context ctx;
//    person_detect_init(&ctx, "person_detect.model");

    Mat image = cv::imread("./background.jpg", 1);

    // 初始化屏幕
    bool bScreenInites = false;
    int width=0, height=0, refresh=0;
    if (0 == screen_init()) {
        bScreenInites = true;
        screen_info(&width, &height, &refresh);
        printf("成功初始化屏幕，分辨率: %dx%d@%dHz\n", width, height, refresh);

        set_uiLayer_on_top(true);
        set_alpha_blend_mode(1);
    }else{
        fprintf(stderr, "屏幕初始化失败\n");
    }

    display_t dispArea = {0, 0, width, height};
    if(bScreenInites){
        disp_init(&dispArea);
        window_commit(image.data, image.cols, image.rows);
    }

    mThreadWorking = true;
    while(1) {
        if(!mThreadWorking){
            msleep(5);
            break;
        }

        if(algorithm_image.empty()) {
            msleep(5);
            continue;
        }

        pthread_mutex_lock(&img_lock);
        image = algorithm_image.clone();
        pthread_mutex_unlock(&img_lock);
#if 0
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
#endif
        msleep(16);
    }

    /* 人员检测释放 */
//    person_detect_release(ctx);
    /* 释放显示资源 */
    if(bScreenInites){
        screen_exit();
    }
    return NULL;
}

void mainThread_start()
{
    pthread_mutex_init(&img_lock, NULL);
    memset(&Result, 0, sizeof(Result_t));

    CreateNormalThread(main_thread_entry, &Result, &mTid);

    return;
}

void mainThread_stop()
{
    /*回收线程*/
    // 1，等待取流线程跑起来
    int timeOut_ms = 1000; //设置n(ms)超时，超时就不等了
    while(1){
        if((true == mThreadWorking)||(timeOut_ms <= 0)){
            break;
        }
        timeOut_ms--;
        usleep(1000);
    }
    // 2，退出线程并等待其结束
    mThreadWorking = false;
    // --[等待分析线程结束]--
    while(1) {
        usleep(20*1000);
        int32_t exitCode = pthread_join(mTid, NULL);
        if(0 == exitCode){
            break;
        }else if(0 != exitCode){
            switch (exitCode) {
                case ESRCH:  // 没有找到线程ID
                    printf("imgAnalyze_thread exit: No thread with the given ID was found.");
                    break;
                case EINVAL: // 线程不可连接或已经有其他线程在等待它
                    printf("imgAnalyze_thread exit: Thread is detached or already being waited on.");
                    break;
                case EDEADLK: // 死锁 - 线程尝试join自己
                    printf("imgAnalyze_thread exit: Deadlock detected - thread is trying to join itself.");
                    break;
            }
            continue;
        }
    }
    pthread_mutex_destroy(&img_lock);

    return ;
}
