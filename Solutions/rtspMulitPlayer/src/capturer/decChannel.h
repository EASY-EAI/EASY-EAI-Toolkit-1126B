#ifndef __DECODECHANNEL_H__
#define __DECODECHANNEL_H__

#include <stdbool.h>
#include <stdint.h>
#include <string>
#include <pthread.h>
#include <semaphore.h>

/* easyeai-api rtsp client */
#include "rtsp_client.h"

/* forward declaration to avoid pulling rk_mpi headers into the header */
class RkDecoder;

typedef struct {
    const char *srcType;
    const char *loaction;
    const char *videoEncType;
    const char *audioEncType;
} SrcCfg_t;

class DecChannel
{
public:
    DecChannel(int chnId, std::string strUrl, std::string strVedioFmt = "h264");
    ~DecChannel();

    int init();
    int32_t IsInited() { return bObjIsInited; }
    int32_t channelId() { return mChnId; }

private:
    /* rtsp client frame callback → push to RkDecoder */
    static void onRtspFrame(const uint8_t *nalu, int size,
                            uint64_t timestamp, void *userdata);
    /* rtsp client event callback */
    static void onRtspEvent(RtspClient *client,
                            RtspClientEvent event,
                            void *userdata);
    /* rkdecoder decoded callback → convert NV12 to BGR888 → videoOutHandle */
    static void onDecoded(int dmabuf_fd, int width, int height,
                          int hor_stride, int ver_stride, void *userData);

    int  mChnId;
    bool bObjIsInited;

    std::string mStrUrl;
    std::string mStrVideoFmt;

    RtspClient *mpRtspClient;
    RkDecoder  *mpDecoder;
};

#endif // __DECODECHANNEL_H__
