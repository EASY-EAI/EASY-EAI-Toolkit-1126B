//=====================  C++  =====================
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
//=====================   C   =====================
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
//=====================  SDK  =====================
#include "system_opt.h"
#include <rga/rga.h>
#include "rga_wrapper.h"
#include "rk_mpi_sys.h"
#include "rk_mpi_mmz.h"
#include "rk_mpi_mb.h"
//=====================  PRJ  =====================
#include "decChannel.h"
#include "rkDecoder.h"
#include "../analyzer/analyzer.h"

/* ======================== DecChannel Implementation ======================== */

DecChannel::DecChannel(int chnId, std::string strUrl, std::string strVedioFmt)
    : mChnId(chnId)
    , bObjIsInited(false)
    , mStrUrl(strUrl)
    , mStrVideoFmt(strVedioFmt)
    , mpRtspClient(NULL)
    , mpDecoder(NULL)
{
}

DecChannel::~DecChannel()
{
    if (mpRtspClient) {
        RtspClient_Destroy(mpRtspClient);
        mpRtspClient = NULL;
    }
    if (mpDecoder) {
        mpDecoder->stop();
        delete mpDecoder;
        mpDecoder = NULL;
    }
}

/* ======================== RTSP frame callback ======================== */
/* RtspClient delivers assembled NALUs (with start code prefix).
 * Forward them directly to RkDecoder for hardware decoding. */
void DecChannel::onRtspFrame(const uint8_t *nalu, int size,
                             uint64_t timestamp, void *userdata)
{
    (void)timestamp;
    DecChannel *self = (DecChannel *)userdata;
    if (!self || !self->mpDecoder || !self->mpDecoder->isRunning())
        return;
    self->mpDecoder->pushData(nalu, (uint32_t)size);
}

/* ======================== RTSP event callback ======================== */
void DecChannel::onRtspEvent(RtspClient *client,
                              RtspClientEvent event,
                              void *userdata)
{
    (void)client;
    DecChannel *self = (DecChannel *)userdata;
    const char *evtStr[] = {"CONNECTED", "PLAYING", "DISCONNECTED",
                            "RECONNECTING", "ERROR"};
    int idx = (int)event;
    if (idx < 0 || idx > 4) idx = 4;
    fprintf(stderr, "[decChn:%d] RTSP event: %s\n",
            self ? self->mChnId : -1, evtStr[idx]);
}

/* ======================== Decoder callback（零拷贝） ======================== */
/* RkDecoder delivers NV12 frames as DMA-BUF fd。
 * 直接将 fd 传递给 analyzer 的零拷贝接口 videoDmaHandle，
 * 由 analyzer 内部：
 *   - 显示路径：window_commit_pro 直接用 fd 做 RGA+DRM（零拷贝）
 *   - 分析路径：仅做一次 NV12→BGR888 RGA 转换供模型推理
 * 不再做任何 malloc/memcpy/munmap。 */
void DecChannel::onDecoded(int dmabuf_fd, int width, int height,
                           int hor_stride, int ver_stride, void *userData)
{
    DecChannel *self = (DecChannel *)userData;
    if (!self || dmabuf_fd < 0 || width <= 0 || height <= 0)
        return;

    DmaFrame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.chnId     = self->mChnId;
    frame.dmabuf_fd = dmabuf_fd;
    frame.width     = width;
    frame.height    = height;
    frame.horStride = hor_stride;
    frame.verStride = ver_stride;

    videoDmaHandle(frame);
}

/* ======================== init ======================== */
int DecChannel::init()
{
    /* 1. Create RkDecoder */
    mpDecoder = new RkDecoder(mStrVideoFmt);
    mpDecoder->setDecodedCallback(onDecoded, this);
    if (mpDecoder->start() != 0) {
        fprintf(stderr, "[decChn:%d] RkDecoder start failed\n", mChnId);
        delete mpDecoder;
        mpDecoder = NULL;
        return -1;
    }
    fprintf(stderr, "[decChn:%d] RkDecoder started (fmt=%s)\n",
            mChnId, mStrVideoFmt.c_str());

    /* 2. Create RtspClient with auto-reconnect enabled */
    RtspClientConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.auto_reconnect      = 1;
    cfg.reconnect_interval_ms = 3000;
    cfg.recv_timeout_ms    = 5000;

    RtspClientCodec codec = RTSP_CLIENT_CODEC_H264;
    if (mStrVideoFmt == "h265" || mStrVideoFmt == "H265") {
        codec = RTSP_CLIENT_CODEC_H265;
    }

    mpRtspClient = RtspClient_CreateEx(mStrUrl.c_str(), codec, &cfg,
                                        onRtspFrame, this,
                                        onRtspEvent, this);
    if (!mpRtspClient) {
        fprintf(stderr, "[decChn:%d] RtspClient_Create failed\n", mChnId);
        mpDecoder->stop();
        delete mpDecoder;
        mpDecoder = NULL;
        return -1;
    }

    fprintf(stderr, "[decChn:%d] RtspClient created (url=%s)\n",
            mChnId, mStrUrl.c_str());
    bObjIsInited = true;
    return 0;
}
