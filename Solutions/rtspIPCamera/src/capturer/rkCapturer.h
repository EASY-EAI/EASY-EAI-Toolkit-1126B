/**
 * RK_MPI VI + VENC 采集编码模块
 *
 * 3A server 模式：ISP 初始化由 rkaiq_3A.service（rkaiq_3A_server）负责，
 * 本模块只做 RK_MPI 层面的 VI + VENC。
 *
 * 数据流：
 *   VI chn0 ──bind──> VENC ──> GetStream 回调 (编码后码流)
 *
 * 前置条件：rkaiq_3A.service 必须已启动
 */

#ifndef __RK_CAPTURER_H__
#define __RK_CAPTURER_H__

#include <stdint.h>
#include <stdbool.h>
#include <string>
#include <pthread.h>

#include "frameTypes.h"

class RkCapturer
{
public:
    RkCapturer(const std::string &device, int width, int height,
               int framerate, const std::string &fmt = "h264");
    ~RkCapturer();

    /* 设置编码帧回调 */
    void setFrameCallback(FrameCallback cb, void *userData);

    int start();
    void stop();
    bool isRunning() const { return mRunning; }

private:
    std::string   mDevice;
    int           mWidth;
    int           mHeight;
    int           mFramerate;
    std::string   mFmt;
    FrameCallback mCb;
    void         *mUserData;

    pthread_t     mStreamTid;     /* 编码码流取流线程 */
    volatile bool mRunning;

    int mDevId;
    int mPipeId;
    int mChnId;       /* VI chn0 — 绑定到 VENC */
    int mVencChn;

    bool mSysInited;
    bool mViDevEnabled;
    bool mViChnEnabled;
    bool mVencCreated;
    bool mBound;

    int viInit();
    int vencInit();
    int bindViVenc();
    void cleanup();

    static void *streamThread(void *para);
};

#endif /* __RK_CAPTURER_H__ */
