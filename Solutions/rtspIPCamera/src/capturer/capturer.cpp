//=====================  C++  =====================
#include <string>
//=====================  C   =====================
#include "system.h"
//=====================  PRJ  =====================
#include "logHandle.h"
#include "capturer.h"

Capturer::Capturer(int chnId, SrcCfg_t config) :
    mi32ChnId(chnId),
    mbObjIsInited(false),
    mConfig(config),
    mpRkCapturer(NULL),
    mUserCb(NULL),
    mUserData(NULL),
    mYuvCb(NULL),
    mYuvUserData(NULL)
{
}

Capturer::~Capturer()
{
    if(mpRkCapturer) {
        delete mpRkCapturer;
        mpRkCapturer = NULL;
    }
    mbObjIsInited = false;
}

int32_t Capturer::init()
{
    /* 创建 RK_MPI 采集编码器（3A server 模式，不调用 ISP 初始化） */
    mpRkCapturer = new RkCapturer(mConfig.loaction,
                                 mConfig.width,
                                 mConfig.height,
                                 mConfig.framerate,
                                 mConfig.videoEncType,
                                 mConfig.bOsdEnabled);
    if(!mpRkCapturer) {
        PRINT_ERROR(g_hCapture, "error: %s, %d\n", __func__, __LINE__);
        return -1;
    }

    /* 设置用户在 init 之前注册的帧回调 */
    if(mUserCb) {
        mpRkCapturer->setFrameCallback(mUserCb, mUserData);
    }
    if(mYuvCb) {
        mpRkCapturer->setYuvCallback(mYuvCb, mYuvUserData);
    }

    if(0 != mpRkCapturer->start()) {
        PRINT_ERROR(g_hCapture, "error: %s, %d\n", __func__, __LINE__);
        delete mpRkCapturer;
        mpRkCapturer = NULL;
        return -1;
    }

    mbObjIsInited = true;
    return 0;
}

int32_t Capturer::IsInited()
{
    return mbObjIsInited;
}

int32_t Capturer::channelId()
{
    return mi32ChnId;
}

void Capturer::setFrameCallback(FrameCallback cb, void *userData)
{
    mUserCb = cb;
    mUserData = userData;
    if(mpRkCapturer) {
        mpRkCapturer->setFrameCallback(cb, userData);
    }
}

void Capturer::setYuvCallback(YuvCallback cb, void *userData)
{
    mYuvCb = cb;
    mYuvUserData = userData;
    if(mpRkCapturer) {
        mpRkCapturer->setYuvCallback(cb, userData);
    }
}

int Capturer::sendFrame(const uint8_t *bgrData, int width, int height, int srcFd)
{
    if(!mpRkCapturer) {
        return -1;
    }
    return mpRkCapturer->sendFrame(bgrData, width, height, srcFd);
}
