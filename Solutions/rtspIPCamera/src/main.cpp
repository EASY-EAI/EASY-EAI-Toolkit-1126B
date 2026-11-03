//=====================  C++  =====================
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
//=====================   C   =====================
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
//=====================  PRJ  =====================
#include "logHandle.h"
#include "capturer/capturer.h"
#include "streamer/rtspStreamer.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/* ======================== 全局日志句柄 ======================== */
log_mgr_t    g_logMgr    = LOG_MGR_INVALID;
log_handle_t g_hMain     = LOG_HANDLE_INVALID;
log_handle_t g_hCapture  = LOG_HANDLE_INVALID;
log_handle_t g_hRtsp     = LOG_HANDLE_INVALID;

/* ======================== 通道配置表 ======================== */
/* 参考 app-bsd/src/main.cpp 使用数组进行配置 */
static SrcCfg_t SrcCfg_tab[] = {
    {
        .srcType      = "MIPI",
        .loaction     = "/dev/video23",
        .width        = 1920,
        .height       = 1080,
        .framerate    = 30,
        .videoEncType = "h264",
        .audioEncType = "null",
    },
    /* 可追加更多通道
    {
        .srcType      = "MIPI",
        .loaction     = "/dev/video31",
        .width        = 1920,
        .height       = 1080,
        .framerate    = 30,
        .videoEncType = "h264",
        .audioEncType = "null",
    },
    */
};

/* ======================== RTSP 配置 ======================== */
typedef struct {
    int         rtspPort;     /* RTSP 端口 */
    const char *rtspPath;     /* RTSP URL 路径 */
} StreamCfg_t;

static StreamCfg_t StreamCfg_tab[] = {
    {
        .rtspPort  = 8554,
        .rtspPath  = "/live/0",
    },
};

/* ======================== 信号处理 ======================== */
static volatile sig_atomic_t g_exitRequested = 0;
static void handleSigInt(int signo)
{
    (void)signo;
    g_exitRequested = 1;
}

/* 编码后码流回调：推送到 RTSP */
static void onEncodedFrame(const uint8_t *data, uint32_t len,
                           const FrameDesc_t *desc, void *userData)
{
    (void)desc;
    RtspStreamer *streamer = (RtspStreamer *)userData;
    if (!streamer || !data || len == 0) return;

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t timestamp = (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;

    if (streamer->isInited()) {
        streamer->pushFrame(data, (int)len, timestamp);
    }
}

/* ======================== 主函数 ======================== */
int main(int argc, char **argv)
{
    /* 确保 stdout/stderr 无缓冲 */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    /* ===== 0. 初始化日志系统 ===== */
    g_logMgr   = log_manager_init("/userdata/logs/rtspIPCamera.ini");
    g_hMain    = log_register(g_logMgr, LOG_MODULE_MAIN);
    g_hCapture = log_register(g_logMgr, LOG_MODULE_CAPTURE);
    g_hRtsp    = log_register(g_logMgr, LOG_MODULE_RTSP);

    signal(SIGINT, handleSigInt);
    signal(SIGTERM, handleSigInt);

    int chnNums = ARRAY_SIZE(SrcCfg_tab);
    int streamNums = ARRAY_SIZE(StreamCfg_tab);
    if (chnNums <= 0) {
        PRINT_ERROR(g_hMain, "No channel configured!\n");
        return -1;
    }

    /* ===== 1. 初始化 RTSP 推流器 ===== */
    std::vector<RtspStreamer*> streamers(streamNums);

    for (int i = 0; i < streamNums; i++) {
        streamers[i] = new RtspStreamer();

        if (0 != streamers[i]->init(StreamCfg_tab[i].rtspPort,
                                     StreamCfg_tab[i].rtspPath,
                                     SrcCfg_tab[i].videoEncType)) {
            PRINT_ERROR(g_hRtsp, "RtspStreamer[%d] init failed\n", i);
        }
    }

    /* ===== 2. 创建采集器并把编码码流接到推流器 ===== */
    Capturer *pCapturer[32] = {NULL};

    for (int i = 0; i < chnNums && i < streamNums; i++) {
        pCapturer[i] = new Capturer(i, SrcCfg_tab[i]);
        if (pCapturer[i]) {
            pCapturer[i]->setFrameCallback(onEncodedFrame, streamers[i]);

            if (0 != pCapturer[i]->init()) {
                PRINT_ERROR(g_hCapture, "Capturer[%d] init failed\n", i);
                delete pCapturer[i];
                pCapturer[i] = NULL;
            }
        }
    }

    /* ===== 3. 主循环等待退出信号 ===== */
    PRINT_INFO(g_hMain, "=== rtspIPCamera running, press Ctrl+C to exit ===\n");
    PRINT_INFO(g_hMain, "    RTSP: rtsp://<IP>:%d%s\n", StreamCfg_tab[0].rtspPort, StreamCfg_tab[0].rtspPath);
    PRINT_INFO(g_hMain, "\n");

    while (!g_exitRequested) {
        sleep(1);
    }

    PRINT_INFO(g_hMain, "SIGINT received, shutting down...\n");

    /* ===== 4. 清理资源 ===== */
    for (int i = 0; i < chnNums; i++) {
        if (pCapturer[i]) {
            delete pCapturer[i];
            pCapturer[i] = NULL;
        }
    }

    for (int i = 0; i < streamNums; i++) {
        if (streamers[i]) {
            delete streamers[i];
            streamers[i] = NULL;
        }
    }

    return 0;
}
