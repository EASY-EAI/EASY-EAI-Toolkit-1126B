//=====================  C++  =====================
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
//=====================   C   =====================
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/prctl.h>
//=====================  PRJ  =====================
#include "logHandle.h"
#include "capturer/capturer.h"
#include "analyzer/analyzer.h"
#include "streamer/rtspStreamer.h"
#include "streamer/fileSaver.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/* ======================== 全局日志句柄 ======================== */
log_mgr_t    g_logMgr  = LOG_MGR_INVALID;
log_handle_t g_hMain    = LOG_HANDLE_INVALID;
log_handle_t g_hCapture  = LOG_HANDLE_INVALID;
log_handle_t g_hAnalyze  = LOG_HANDLE_INVALID;
log_handle_t g_hRtsp     = LOG_HANDLE_INVALID;
log_handle_t g_hFile     = LOG_HANDLE_INVALID;

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
        .bOsdEnabled  = true,   /* true=OSD叠加模式, false=直通模式 */
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
        .bOsdEnabled  = false,
    },
    */
};

/* ======================== RTSP / 文件保存配置 ======================== */
typedef struct {
    int         rtspPort;     /* RTSP 端口 */
    const char *rtspPath;    /* RTSP URL 路径 */
    const char *savePath;    /* 文件保存路径，NULL 表示不保存 */
} StreamCfg_t;

static StreamCfg_t StreamCfg_tab[] = {
    {
        .rtspPort  = 8554,
        .rtspPath  = "/live/0",
        .savePath  = "/userdata/output.h264",
    },
};

/* ======================== 信号处理 ======================== */
static volatile sig_atomic_t g_exitRequested = 0;
static void handleSigInt(int signo)
{
    (void)signo;
    g_exitRequested = 1;
}

/* ======================== 帧回调上下文 ======================== */
typedef struct {
    RtspStreamer *streamer;
    FileSaver    *saver;
} FrameCtx_t;

/* 编码后码流回调：推送到 RTSP 和文件保存 */
static void onEncodedFrame(const uint8_t *data, uint32_t len,
                           const FrameDesc_t *desc, void *userData)
{
    (void)desc;
    FrameCtx_t *ctx = (FrameCtx_t *)userData;
    if (!ctx || !data || len == 0) return;

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t timestamp = (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;

    /* 推送到 RTSP */
    if (ctx->streamer && ctx->streamer->isInited()) {
        ctx->streamer->pushFrame(data, (int)len, timestamp);
    }

    /* 保存到文件 */
    if (ctx->saver && ctx->saver->isInited()) {
        ctx->saver->writeFrame(data, len);
    }
}

/* YUV 原始帧回调：送到算法分析器 */
static void onYuvFrame(const uint8_t *data, uint32_t len,
                       const YuvDesc_t *desc, void *userData)
{
    (void)len;
    int *chnId = (int *)userData;
    if (!data || !desc || !chnId) return;

    ImgDesc_t imgDesc = {0};
    imgDesc.chnId = *chnId;
    imgDesc.width = desc->width;
    imgDesc.height = desc->height;
    imgDesc.horStride = desc->horStride;
    imgDesc.verStride = desc->verStride;
    imgDesc.dataSize = (int)len;
    strncpy(imgDesc.fmt, desc->fmt, sizeof(imgDesc.fmt) - 1);

    videoOutHandle((char *)data, imgDesc);
}

/* ======================== OSD → 编码线程上下文 ======================== */
typedef struct {
    Capturer *capturer;
    int       chnId;
} OsdEncCtx_t;

/*
 * OSD 模式专用线程：
 *   循环从 analyzer 获取已画 OSD 的 BGR888 帧
 *   送入 Capturer::sendFrame() 进行编码
 *   编码后的码流通过 onEncodedFrame 回调推送到 RTSP/文件
 */
static void *osdEncThread(void *para)
{
    OsdEncCtx_t *ctx = (OsdEncCtx_t *)para;
    prctl(PR_SET_NAME, "osd_enc");

    PRINT_INFO(g_hMain, "osdEnc thread started (chn=%d)\n", ctx->chnId);

    uint32_t send_cnt = 0;
    uint32_t fail_cnt = 0;

    while (!g_exitRequested) {
        uint8_t *bgrData = NULL;
        int width = 0, height = 0;
        int dmaFd = -1;

        int ret = analyzer_getOsdFrame(ctx->chnId, &bgrData, &width, &height, &dmaFd);
        if (ret == 0 && bgrData && width > 0 && height > 0) {
            ctx->capturer->sendFrame(bgrData, width, height, dmaFd);
            send_cnt++;
            if ((send_cnt % 30) == 0) {
                PRINT_INFO(g_hMain, "osdEnc alive: sent=%u fail=%u\n", send_cnt, fail_cnt);
            }
        } else {
            fail_cnt++;
        }

        usleep(1000); /* 1ms, 避免空转; sendFrame内部会阻塞控制节奏 */
    }

    PRINT_INFO(g_hMain, "osdEnc thread exiting (chn=%d)\n", ctx->chnId);
    return NULL;
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
    g_hAnalyze = log_register(g_logMgr, LOG_MODULE_ANALYZE);
    g_hRtsp    = log_register(g_logMgr, LOG_MODULE_RTSP);
    g_hFile    = log_register(g_logMgr, LOG_MODULE_FILE);

    signal(SIGINT, handleSigInt);
    signal(SIGTERM, handleSigInt);

    int ret = -1;
    int chnNums = ARRAY_SIZE(SrcCfg_tab);
    int streamNums = ARRAY_SIZE(StreamCfg_tab);
    if (chnNums <= 0) {
        PRINT_ERROR(g_hMain, "No channel configured!\n");
        return -1;
    }

    /* ===== 1. 检查是否有通道启用 OSD ===== */
    bool anyOsdEnabled = false;
    for (int i = 0; i < chnNums; i++) {
        if (SrcCfg_tab[i].bOsdEnabled) {
            anyOsdEnabled = true;
            break;
        }
    }

    /* 仅在需要 OSD 时初始化算法模型 */
    if (anyOsdEnabled) {
        ret = analyzer_init(chnNums);
        if (0 != ret) {
            PRINT_ERROR(g_hMain, "Initialize algorithm model failed! ret = %d\n", ret);
            return ret;
        }
        PRINT_INFO(g_hMain, "Algorithm model initialized.\n");
    } else {
        PRINT_INFO(g_hMain, "OSD disabled, skip algorithm init.\n");
    }

    /* ===== 2. 初始化 RTSP 推流器和文件保存器 ===== */
    std::vector<RtspStreamer*> streamers(streamNums);
    std::vector<FileSaver*>   savers(streamNums);
    std::vector<FrameCtx_t>   frameCtxs(streamNums);
    std::vector<int>          chnIds(chnNums);

    for (int i = 0; i < streamNums; i++) {
        streamers[i] = new RtspStreamer();
        savers[i]    = new FileSaver();

        /* 初始化 RTSP 推流 */
        if (0 != streamers[i]->init(StreamCfg_tab[i].rtspPort,
                                     StreamCfg_tab[i].rtspPath,
                                     SrcCfg_tab[i].videoEncType)) {
            PRINT_ERROR(g_hRtsp, "RtspStreamer[%d] init failed\n", i);
        }

        /* 初始化文件保存 */
        if (StreamCfg_tab[i].savePath) {
            if (0 != savers[i]->init(StreamCfg_tab[i].savePath,
                                      SrcCfg_tab[i].videoEncType)) {
                PRINT_ERROR(g_hFile, "FileSaver[%d] init failed\n", i);
            }
        }

        frameCtxs[i].streamer = streamers[i];
        frameCtxs[i].saver    = savers[i];
    }

    /* ===== 3. 创建采集器并设置帧回调 ===== */
    Capturer *pCapturer[32] = {NULL};
    std::vector<pthread_t> osdEncTids(chnNums, 0);
    std::vector<OsdEncCtx_t> osdEncCtxs(chnNums);

    for (int i = 0; i < chnNums && i < streamNums; i++) {
        pCapturer[i] = new Capturer(i, SrcCfg_tab[i]);
        if (pCapturer[i]) {
            chnIds[i] = i;

            /* 设置编码帧回调 → 推送 RTSP 和保存文件 */
            pCapturer[i]->setFrameCallback(onEncodedFrame, &frameCtxs[i]);

            /* 仅 OSD 模式需要 YUV 原始帧回调 → 送到算法分析器 */
            if (SrcCfg_tab[i].bOsdEnabled) {
                pCapturer[i]->setYuvCallback(onYuvFrame, &chnIds[i]);
            }

            if (0 != pCapturer[i]->init()) {
                PRINT_ERROR(g_hCapture, "Capturer[%d] init failed\n", i);
                delete pCapturer[i];
                pCapturer[i] = NULL;
            }
        }
    }

    /* ===== 4. OSD 模式：启动 OSD→编码 线程 ===== */
    for (int i = 0; i < chnNums; i++) {
        if (pCapturer[i] && SrcCfg_tab[i].bOsdEnabled) {
            osdEncCtxs[i].capturer = pCapturer[i];
            osdEncCtxs[i].chnId = i;
            pthread_create(&osdEncTids[i], NULL, osdEncThread, &osdEncCtxs[i]);
            PRINT_INFO(g_hMain, "OSD encoding thread started for channel %d\n", i);
        }
    }

    /* ===== 5. 主循环等待退出信号 ===== */
    PRINT_INFO(g_hMain, "=== rtspIPCamera running, press Ctrl+C to exit ===\n");
    PRINT_INFO(g_hMain, "    RTSP: rtsp://<IP>:%d%s\n", StreamCfg_tab[0].rtspPort, StreamCfg_tab[0].rtspPath);
    if (StreamCfg_tab[0].savePath) {
        PRINT_INFO(g_hMain, "    FILE: %s\n", StreamCfg_tab[0].savePath);
    }
    for (int i = 0; i < chnNums; i++) {
        PRINT_INFO(g_hMain, "    CHN[%d] OSD: %s\n", i, SrcCfg_tab[i].bOsdEnabled ? "ON" : "OFF");
    }
    PRINT_INFO(g_hMain, "\n");

    while (!g_exitRequested) {
        sleep(1);
    }

    PRINT_INFO(g_hMain, "SIGINT received, shutting down...\n");

    /* ===== 6. 清理资源 ===== */
    /* 等待 OSD 编码线程退出 */
    for (int i = 0; i < chnNums; i++) {
        if (osdEncTids[i]) {
            pthread_join(osdEncTids[i], NULL);
            osdEncTids[i] = 0;
        }
    }

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
        if (savers[i]) {
            delete savers[i];
            savers[i] = NULL;
        }
    }

    if (anyOsdEnabled) {
        analyzer_exit();
    }

    return 0;
}
