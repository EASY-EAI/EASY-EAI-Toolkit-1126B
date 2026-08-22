/**
 * RTSP 推流模块 —— 实现
 *
 * 基于 easyeai-api/netProtocol/rtsp/server 的 RtspServer 封装。
 * 接收编码后的 H264/H265 码流并通过 RTSP 协议推流。
 */

#include "rtspStreamer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "rtsp_server.h"

#include "logHandle.h"

RtspStreamer::RtspStreamer()
    : mInited(false)
    , mServer(NULL)
{
}

RtspStreamer::~RtspStreamer()
{
    deinit();
}

int RtspStreamer::init(int port, const std::string &path, const std::string &fmt)
{
    RtspServerCodec codec;
    if (fmt == "h265") {
        codec = RTSP_SERVER_CODEC_H265;
    } else {
        codec = RTSP_SERVER_CODEC_H264;
    }

    mServer = RtspServer_Create(port, path.c_str(), codec);
    if (!mServer) {
        PRINT_ERROR(g_hRtsp, "RtspServer_Create failed (port=%d path=%s fmt=%s)\n",
               port, path.c_str(), fmt.c_str());
        return -1;
    }

    mInited = true;
    PRINT_INFO(g_hRtsp, "RTSP server ready: rtsp://0.0.0.0:%d%s (%s)\n",
           port, path.c_str(), fmt.c_str());
    return 0;
}

int RtspStreamer::pushFrame(const uint8_t *data, int size, uint64_t timestamp)
{
    if (!mInited || !mServer || !data || size <= 0) {
        return -1;
    }

    return RtspServer_PushFrame((RtspServer*)mServer, data, size, timestamp);
}

void RtspStreamer::deinit()
{
    if (mServer) {
        RtspServer_Destroy((RtspServer*)mServer);
        mServer = NULL;
    }
    mInited = false;
    PRINT_INFO(g_hRtsp, "RTSP server stopped\n");
}
