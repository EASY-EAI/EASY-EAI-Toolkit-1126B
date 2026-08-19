//=====================  C++  =====================
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
//=====================   C   =====================
//#include <gst/video/video-info.h>
#include "system.h"
//=====================  SDK  =====================
#include "system_opt.h"
//#include "gst_opt.h"
//=====================  PRJ  =====================
#include "decChannel.h"

#include "../analyzer/analyzer.h"


#define CAMERA_WIDTH    1920
#define CAMERA_HEIGHT   1080
#define	IMGRATIO        3
#define	IMAGE_SIZE      (CAMERA_WIDTH*CAMERA_HEIGHT*IMGRATIO)
// =======================================================================================================
void *getFrameThread(void *para)
{
    int ret;
    GstChannel_t *pChannel = (GstChannel_t *)para;

    pChannel->bGetFrameRunning = true;
    while(pChannel->bGetFrameRunning) {
        SrcFrame_t srcFrame = {0};
        ret = mipicamera_getSrcframe(pChannel->cameraIndex, &srcFrame);
        if (ret) {
            /* DQBUF 失败时不能 QBUF，否则会损坏队列或崩溃 */
            printf("error: %s, %d\n", __func__, __LINE__);
            continue;
        }

        ImgDesc_t imgDesc = {0};
        imgDesc.chnId = pChannel->chnId;
        imgDesc.width = srcFrame.width;
        imgDesc.height = srcFrame.height;
        imgDesc.horStride = srcFrame.horStride;
        imgDesc.verStride = srcFrame.verStride;
        imgDesc.dataSize = srcFrame.dataSize;
        strcpy(imgDesc.fmt, "NV12");
        videoOutHandle((char *)srcFrame.pMapBuff, imgDesc);

        
        mipicamera_putSrcframe(pChannel->cameraIndex, &srcFrame);
        usleep(10*1000);
    }
    mipicamera_exit(pChannel->cameraIndex);
    
    pthread_exit(NULL);
}

DecChannel::DecChannel(int chnId, std::string strUrl, std::string strVedioFmt) :
    mTid(0),
	bObjIsInited(false),
    mStrUrl(strUrl),
    mStrVideoFmt(strVedioFmt)
{
    memset(&mGstChn, 0, sizeof(mGstChn));
    mGstChn.chnId = chnId;
}
DecChannel::~DecChannel()
{
    /*回收线程*/
    // 1，等待取流线程跑起来
    int timeOut_ms = 1500; //设置n(ms)超时，超时就不等了
    while(1){
        if((true == mGstChn.bGetFrameRunning)||(timeOut_ms <= 0)){
            break;
        }
        timeOut_ms--;
        usleep(1000);
    }
    // 2，退出线程并等待其结束
    if(mTid){
        mGstChn.bGetFrameRunning = false;
        // --[等待取流线程结束]--
        while(1) {
            usleep(20*1000);
            int32_t exitCode = pthread_join(mTid, NULL);
            if(0 == exitCode){
                break;
            }else if(0 != exitCode){
                switch (exitCode) {
                    case ESRCH:  // 没有找到线程ID
                        printf("getFrameThread exit: No thread with the given ID was found.\n");
                        break;
                    case EINVAL: // 线程不可连接或已经有其他线程在等待它
                        printf("getFrameThread exit: Thread is detached or already being waited on.\n");
                        break;
                    case EDEADLK: // 死锁 - 线程尝试join自己
                        printf("getFrameThread exit: Deadlock detected - thread is trying to join itself.\n");
                        break;
                }
                continue;
            }
        }
    }

    if(bObjIsInited){
        bObjIsInited = false;
    }
}
int DecChannel::init()
{
    mGstChn.cameraIndex = atoi(mStrUrl.c_str()); //通常csi0是video23, csi1是video31
    
    // 1.打开摄像头
    int ret = mipicamera_init(mGstChn.cameraIndex, CAMERA_WIDTH, CAMERA_HEIGHT, 0);
    if (ret) {
        printf("error: %s, %d\n", __func__, __LINE__);
        return -1;
    }
    
    // 2.跳过前10帧
    char *pbuf = (char *)malloc(IMAGE_SIZE);
    if(pbuf){
        int skip = 10;
        while(skip--) {
            ret = mipicamera_getframe(mGstChn.cameraIndex, pbuf);
            if (ret) {
                printf("error: %s, %d\n", __func__, __LINE__);
                mipicamera_exit(mGstChn.cameraIndex);
                return -1;
            }
        }
        free(pbuf);
    }

	if(0 != CreateJoinThread(getFrameThread, &mGstChn, &mTid)){
        mipicamera_exit(mGstChn.cameraIndex);
        return -1;
    }
    bObjIsInited = true;
    return 0;
}
