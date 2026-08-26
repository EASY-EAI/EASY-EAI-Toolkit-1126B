/**
 * RK_MPI VI + VENC 采集编码模块
 *
 * 使用 RK_MPI_* 接口从 MIPI sensor 采集视频帧，
 * 通过硬件编码器编码为 H.264/H.265 码流，回调交付上层。
 *
 * 3A server 模式：ISP 初始化由 rkaiq_3A.service 负责，
 * 本模块不调用任何 rk_aiq_uapi2_* 接口。
 *
 * 数据流：
 *   VI (sensor) ──bind──> VENC ──> GetStream 回调
 *
 * 用法：
 *   RkCapturer cap("/dev/video23", 1920, 1080, 30, "h264");
 *   cap.setFrameCallback(onFrame, NULL);
 *   cap.start();
 *   ...
 *   cap.stop();
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

    /* 设置帧回调 */
    void setFrameCallback(FrameCallback cb, void *userData);

    /* 启动采集编码 */
    int start();

    /* 停止采集编码 */
    void stop();

    /* 是否正在运行 */
    bool isRunning() const { return mRunning; }

private:
    std::string   mDevice;
    int           mWidth;
    int           mHeight;
    int           mFramerate;
    std::string   mFmt;
    FrameCallback mCb;
    void         *mUserData;

    pthread_t     mStreamTid;
    volatile bool mRunning;

    int mDevId;    /* VI device ID  */
    int mPipeId;   /* VI pipe ID    */
    int mChnId;    /* VI channel ID */
    int mVencChn;  /* VENC channel  */

    bool mSysInited;       /* 是否由本模块初始化了 RK_MPI_SYS */
    bool mViDevEnabled;    /* VI dev 是否已使能 */
    bool mViChnEnabled;    /* VI chn 是否已使能 */
    bool mVencCreated;     /* VENC 是否已创建 */
    bool mBound;           /* VI→VENC 是否已绑定 */

    int viInit();
    int vencInit();
    int bindViVenc();
    void cleanup();

    static void *streamThread(void *para);
};

#endif /* __RK_CAPTURER_H__ */
