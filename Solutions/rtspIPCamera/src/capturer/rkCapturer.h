/**
 * RK_MPI VI + VENC 采集编码模块
 *
 * 3A server 模式：ISP 初始化由 rkaiq_3A.service（rkaiq_3A_server）负责，
 * 本模块只做 RK_MPI 层面的 VI + VENC。
 *
 * 两种工作模式：
 *   1) 直通模式 (bOsdEnabled=false)：
 *      VI chn0 ──bind──> VENC ──> GetStream 回调 (编码后码流)
 *      VI chn1 ──> GetChnFrame 回调 (原始 YUV 帧，供算法分析，不影响推流)
 *
 *   2) OSD 模式 (bOsdEnabled=true)：
 *      VI chn1 ──> GetChnFrame 回调 (原始 YUV 帧)
 *      外部 BGR888 帧送入 sendFrame() ──> RGA 转 NV12 ──> VENC SendFrame ──> GetStream 回调
 *      （不绑定 VI chn0 → VENC）
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

/* YUV 帧描述信息 */
typedef struct {
    int  width;
    int  height;
    int  horStride;
    int  verStride;
    char fmt[16];     /* "NV12" */
} YuvDesc_t;

/* YUV 帧回调（原始未编码帧，供算法分析使用） */
typedef void (*YuvCallback)(const uint8_t *data, uint32_t len,
                            const YuvDesc_t *desc, void *userData);

class RkCapturer
{
public:
    RkCapturer(const std::string &device, int width, int height,
               int framerate, const std::string &fmt = "h264",
               bool bOsdEnabled = false);
    ~RkCapturer();

    /* 设置编码帧回调 */
    void setFrameCallback(FrameCallback cb, void *userData);

    /* 设置 YUV 原始帧回调（供算法分析） */
    void setYuvCallback(YuvCallback cb, void *userData);

    /*
     * 发送 BGR888 帧到 VENC 编码（仅 OSD 模式有效）
     * bgrData 指向 BGR888 像素数据，width/height 为图像宽高
     * srcFd 为 BGR888 数据的 DMA-BUF fd，>=0 时启用零拷贝模式
     */
    int sendFrame(const uint8_t *bgrData, int width, int height, int srcFd = -1);

    int start();
    void stop();
    bool isRunning() const { return mRunning; }

private:
    std::string   mDevice;
    int           mWidth;
    int           mHeight;
    int           mFramerate;
    std::string   mFmt;
    bool          mOsdEnabled;  /* OSD 模式开关 */
    FrameCallback mCb;
    void         *mUserData;
    YuvCallback   mYuvCb;
    void         *mYuvUserData;

    pthread_t     mStreamTid;     /* 编码码流取流线程 */
    pthread_t     mYuvTid;       /* YUV 原始帧取帧线程 */
    volatile bool mRunning;

    int mDevId;
    int mPipeId;
    int mChnId;       /* VI chn0 — 直通模式绑定到 VENC */
    int mYuvChnId;   /* VI chn1 — 供算法分析 */
    int mVencChn;

    /* OSD 模式专用：RGA 转换所需的 NV12 DMA-BUF */
    void *mNv12Buf;
    int   mNv12Fd;
    int   mNv12Size;
    /* OSD 模式专用：MB pool 用于 VENC SendFrame */
    uint32_t mMbPool;

    bool mSysInited;
    bool mViDevEnabled;
    bool mViChnEnabled;
    bool mViYuvChnEnabled;
    bool mVencCreated;
    bool mBound;

    int viInit();
    int viYuvInit();
    int vencInit();
    int bindViVenc();
    int nv12BufInit();   /* OSD 模式：分配 NV12 DMA-BUF */
    void nv12BufDeinit();
    void cleanup();

    static void *streamThread(void *para);
    static void *yuvThread(void *para);
};

#endif /* __RK_CAPTURER_H__ */
