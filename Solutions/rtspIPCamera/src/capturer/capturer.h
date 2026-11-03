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

private:
    int          mi32ChnId;
    int          mbObjIsInited;
    SrcCfg_t     mConfig;
    RkCapturer  *mpRkCapturer;
    FrameCallback mUserCb;
    void         *mUserData;
};

#endif
