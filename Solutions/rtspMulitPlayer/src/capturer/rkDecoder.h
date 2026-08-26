/**
 * RK_MPI VDEC 硬件解码模块
 *
 * 使用 RK_MPI_VDEC 接口将 H.264/H.265 码流解码为 NV12 帧，
 * 直接以 DMA-BUF fd 形式回调上层，由 display 库 zero-copy 显示。
 *
 * 用法：
 *   RkDecoder dec("h264");
 *   dec.setDecodedCallback(onDecoded, userData);
 *   dec.start();
 *   dec.pushData(h264Data, len);
 *   ...
 *   dec.stop();
 */

#ifndef __RK_DECODER_H__
#define __RK_DECODER_H__

#include <stdint.h>
#include <stdbool.h>
#include <string>
#include <queue>
#include <pthread.h>
#include <semaphore.h>

/* 解码回调：交付 NV12 DMA-BUF fd（zero-copy，调用方须在回调返回前使用完毕） */
typedef void (*DecodedCallback)(int dmabuf_fd, int width, int height,
                                int hor_stride, int ver_stride,
                                void *userData);

class RkDecoder
{
public:
    RkDecoder(const std::string &fmt = "h264",
              int width = 1920, int height = 1080);
    ~RkDecoder();

    /* 设置解码回调 */
    void setDecodedCallback(DecodedCallback cb, void *userData);

    /* 启动解码器 */
    int start();

    /* 停止解码器 */
    void stop();

    /* 推送 H.264/H.265 数据到解码器 */
    int pushData(const uint8_t *data, uint32_t len);

    /* 是否正在运行 */
    bool isRunning() const { return mRunning; }

private:
    std::string      mFmt;
    int              mWidth;
    int              mHeight;
    DecodedCallback  mCb;
    void            *mUserData;

    pthread_t        mGetTid;
    pthread_t        mPushTid;
    volatile bool    mRunning;

    bool mSysInited;
    bool mVdecCreated;

    /* ---- 生产者-消费者队列（解耦 VCIC 回调与 VDEC 送流）---- */
    struct PushItem {
        uint8_t *data;
        uint32_t len;
    };
    std::queue<PushItem> m_pushQueue;
    pthread_mutex_t      m_pushMutex;
    sem_t                m_pushSem;
    static const int     PUSH_QUEUE_MAX = 4;  /* 队列上限，超出丢旧帧 */

    int vdecInit();
    void cleanup();
    void flushPushQueue();

    static void *getFrameThread(void *para);
    static void *pushThread(void *para);
};

#endif /* __RK_DECODER_H__ */
