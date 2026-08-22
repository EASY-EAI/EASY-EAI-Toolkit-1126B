#ifndef __CAPTURER_H__
#define __CAPTURER_H__

#include <stdbool.h>
#include <stdint.h>
#include <string>

#include "frameTypes.h"
#include "rkCapturer.h"

typedef struct {
    const char *srcType;       /* "MIPI" */
    const char *loaction;      /* 设备节点, 如 "/dev/video23" */
    int         width;
    int         height;
    int         framerate;
    const char *videoEncType;  /* "h264" / "h265" */
    const char *audioEncType;  /* "null" */
    bool        bOsdEnabled;   /* 是否启用 OSD 叠加模式 */
}SrcCfg_t;

class Capturer
{
public:
    Capturer(int chnId, SrcCfg_t config);
    ~Capturer();

    int32_t init();
    int32_t IsInited();
    int32_t channelId();

    /* 设置编码帧回调（需在 init() 之前调用） */
    void setFrameCallback(FrameCallback cb, void *userData);

    /* 设置 YUV 原始帧回调（需在 init() 之前调用） */
    void setYuvCallback(YuvCallback cb, void *userData);

    /*
     * 发送 BGR888 帧到 VENC 编码（仅 OSD 模式有效）
     * bgrData 指向 BGR888 像素数据，width/height 为图像宽高
     * srcFd 为 BGR888 数据的 DMA-BUF fd，>=0 时启用零拷贝模式
     */
    int sendFrame(const uint8_t *bgrData, int width, int height, int srcFd = -1);

private:
    int          mi32ChnId;
    int          mbObjIsInited;
    SrcCfg_t     mConfig;
    RkCapturer  *mpRkCapturer;
    FrameCallback mUserCb;
    void         *mUserData;
    YuvCallback   mYuvCb;
    void         *mYuvUserData;
};

#endif
