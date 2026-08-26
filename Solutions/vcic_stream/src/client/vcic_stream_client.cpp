/**
 * SOME/IP + AVTP 融合方案 —— Client 端（接收端 / 显示端）
 *
 * 使用 VCIC 统一接口（ISO 17215）实现：
 *   - VCIC_client_create()           创建客户端
 *   - VCIC_client_subscribe_event()  订阅事件通知
 *   - VCIC_client_start_stream()     请求取流
 *   - VCIC_client_stop_stream()       停止取流
 *
 * 数据面：VCIC 帧回调 → RK_MPI VDEC 解码 → NV12 DMA-BUF → display zero-copy 显示
 *
 * 用法：
 *   sudo ./vcic_stream_client <server_ip> [ifname] [local_ip] [fmt]
 * 示例：
 *   sudo ./vcic_stream_client 192.168.1.20 eth0 192.168.1.30 h264
 *
 * 需要权限：CAP_NET_RAW（AVTP 使用裸以太网 socket）
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>
#include <vector>
#include <time.h>
#include <semaphore.h>
#include <pthread.h>

#include "log_manager_pro.h"
#include "vcic.h"
#include "rkDecoder.h"
#include "display_pro.h"      /* screen_init, disp_init_pro, add_window_to, window_commit_pro, window_refresh_pro */
#include <rga/rga.h>          /* RK_FORMAT_YCbCr_420_SP (NV12) */

/* ======================== 日志句柄 ======================== */
static log_mgr_t    g_logMgr   = LOG_MGR_INVALID;
static log_handle_t h_client   = LOG_HANDLE_INVALID;
static log_handle_t h_dec      = LOG_HANDLE_INVALID;
static log_handle_t h_display  = LOG_HANDLE_INVALID;

/* ======================== 全局状态 ======================== */
static volatile sig_atomic_t g_running = 1;
/* 是否已进入关闭流程（用于二次 Ctrl+C 强制退出） */
static volatile sig_atomic_t g_shuttingDown = 0;

/* AVTP / VCIC 配置（必须与 Server 一致） */
static const uint8_t STREAM_ID[8] = {0x00, 0x1B, 0x21, 0xFF, 0xFE, 0x00, 0x00, 0x01};
static const uint8_t MCAST_MAC[6] = {0x91, 0xE0, 0xF0, 0x00, 0x0E, 0x80};

/* 帧重组缓冲区 */
static std::vector<uint8_t> g_reassemblyBuf;
static pthread_mutex_t g_reassemblyMutex = PTHREAD_MUTEX_INITIALIZER;

/* RkDecoder 解码器 */
static RkDecoder *g_pDecoder = NULL;

/* VCIC Client 实例 */
static VCIC_Client *g_vcicClient = NULL;

/* 显示窗口 */
static int g_winChn = -1;

/* 统计计数 */
static uint32_t g_frag_cnt  = 0;
static uint32_t g_byte_cnt  = 0;
static uint32_t g_frame_cnt = 0;
static uint32_t g_disp_cnt  = 0;

/* ======================== 独立显示线程 ======================== */
/* window_refresh_pro 可能阻塞（等 VSYNC），如果在解码回调中同步调用，
 * 会导致 VDEC 输出缓冲区耗尽、画面冻结。
 *
 * 解决方案：onDecoded 只做轻量的 window_commit_pro，
 * 由独立显示线程异步调用 window_refresh_pro。
 * 若显示线程仍在刷新上一帧，新帧只 commit 不 refresh，
 * 等 display 空闲后自动刷新最新帧。 */
static volatile sig_atomic_t g_displayBusy = 0;
static sem_t   g_displaySem;
static pthread_t g_displayTid = 0;

static uint32_t g_refresh_cnt = 0;

static void *display_thread(void *para)
{
    (void)para;
    PRINT_INFO(h_display, "display thread started\n");

    while (g_running) {
        /* 等待刷新信号，超时 500ms 以便检查 g_running */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 500 * 1000 * 1000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000;
        }
        if (sem_timedwait(&g_displaySem, &ts) != 0) {
            PRINT_DEBUG(h_display, "display thread: sem timeout (alive, refresh_cnt=%u)\n", g_refresh_cnt);
            continue;
        }
        if (!g_running) break;

        /* 执行 RGA 合成 + DRM commit（可能阻塞，但在独立线程中不影响解码） */
        window_refresh_pro();
        g_refresh_cnt++;

        __sync_synchronize();
        g_displayBusy = 0;

        if ((g_refresh_cnt % 30) == 0) {
            PRINT_DEBUG(h_display, "display thread alive: refresh_cnt=%u disp_cnt=%u\n",
                        g_refresh_cnt, g_disp_cnt);
        }
    }

    PRINT_INFO(h_display, "display thread exiting (refresh_cnt=%u)\n", g_refresh_cnt);
    return NULL;
}

/* ======================== 信号处理 ======================== */
/* 第一次信号 → 优雅关闭（设置 g_running=0）
 * 第二次信号 → 强制退出（说明清理阶段卡死，直接 _exit） */
static void on_signal(int sig)
{
    (void)sig;
    if (g_shuttingDown) {
        /* 已经在关闭流程中，再次收到信号 → 强制退出 */
        _exit(128 + sig);
    }
    g_shuttingDown = 1;
    g_running = 0;
}

/* ======================== 解码回调 → zero-copy 屏幕显示 ======================== */
/* 仅做轻量的 window_commit_pro，不调用 window_refresh_pro。
 * 由独立 display_thread 异步刷新，避免阻塞解码线程。 */
static void onDecoded(int dmabuf_fd, int width, int height,
                      int hor_stride, int ver_stride, void *userData)
{
    (void)userData;
    (void)ver_stride;

    if (dmabuf_fd < 0 || width <= 0 || height <= 0 || g_winChn < 0) {
        return;
    }

    /* 将 VDEC 输出的 NV12 DMA-BUF fd 交给 display 库 */
    display_dmabuf_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.dmabuf_fd  = dmabuf_fd;
    frame.width      = width;
    frame.height     = height;
    frame.pitch_bytes = hor_stride;
    frame.rotation   = 0;
    frame.rga_format = RK_FORMAT_YCbCr_420_SP;  /* NV12 */

    window_commit_pro(g_winChn, &frame);

    /* 通知显示线程刷新（如果它空闲的话） */
    __sync_synchronize();
    if (!g_displayBusy) {
        g_displayBusy = 1;
        sem_post(&g_displaySem);
    }

    g_disp_cnt++;
    PRINT_DEBUG(h_dec, "onDecoded: frame=%u %dx%d fd=%d dispBusy=%d refresh_cnt=%u\n",
               g_disp_cnt, width, height, dmabuf_fd, (int)g_displayBusy, g_refresh_cnt);
    if ((g_disp_cnt % 300) == 1) {
        PRINT_INFO(h_client, "[client]: displayed frame %u (%dx%d fd=%d)\n",
                   g_disp_cnt, width, height, dmabuf_fd);
    }
}

/* ======================== VCIC 帧回调 → 重组 → 解码 ======================== */
static int32_t on_vcic_frame(void *pObj, const uint8_t *data, uint32_t len,
                              uint32_t avtp_ts, uint8_t isLastFrag, uint8_t codec)
{
    (void)pObj;
    (void)avtp_ts;
    (void)codec;

    g_frag_cnt++;
    g_byte_cnt += len;

    pthread_mutex_lock(&g_reassemblyMutex);

    /* 追加分片到重组缓冲区 */
    if (data && len > 0) {
        g_reassemblyBuf.insert(g_reassemblyBuf.end(), data, data + len);
    }

    /* 收到最后一帧分片 → 重组完成，推送解码 */
    if (isLastFrag) {
        g_frame_cnt++;

        if (g_pDecoder && g_pDecoder->isRunning() && !g_reassemblyBuf.empty()) {
            g_pDecoder->pushData(g_reassemblyBuf.data(),
                                 (uint32_t)g_reassemblyBuf.size());
        }

        g_reassemblyBuf.clear();
    }

    pthread_mutex_unlock(&g_reassemblyMutex);

    /* 每 30 个分片打印一次统计 */
    if ((g_frag_cnt % 30) == 1) {
        PRINT_DEBUG(h_client, "on_vcic_frame: frag#%u len=%u ts=0x%08x last=%u | "
               "frames=%u bytes=%u displayed=%u\n",
               g_frag_cnt, len, avtp_ts, isLastFrag,
               g_frame_cnt, g_byte_cnt, g_disp_cnt);
    }

    return 0;
}

/* ======================== VCIC 事件回调 ======================== */
static int32_t on_cam_status(void *pObj, uint16_t eventId,
                             const uint8_t *data, uint32_t len)
{
    (void)pObj;
    if (len >= 2) {
        PRINT_DEBUG(h_client, "[client]: event CAM_STATUS(0x%04x) len=%u streaming=%u status=0x%02x\n",
                    eventId, len, data[0], data[1]);
    } else {
        PRINT_DEBUG(h_client, "[client]: event CAM_STATUS(0x%04x) len=%u\n", eventId, len);
    }
    return 0;
}

static int32_t on_frame_lost(void *pObj, uint16_t eventId,
                             const uint8_t *data, uint32_t len)
{
    (void)pObj;
    if (data && len > 0) {
        char hexbuf[64] = {0};
        int pos = 0;
        for (uint32_t i = 0; i < len && i < 16; i++) {
            pos += snprintf(hexbuf + pos, sizeof(hexbuf) - pos, " %02x", data[i]);
        }
        PRINT_ERROR(h_client, "[client]: event FRAME_LOST(0x%04x) len=%u raw:%s\n", eventId, len, hexbuf);
    } else {
        PRINT_ERROR(h_client, "[client]: event FRAME_LOST(0x%04x) len=%u\n", eventId, len);
    }

    /* 丢帧时清空重组缓冲区，避免残留数据导致解码错误 */
    pthread_mutex_lock(&g_reassemblyMutex);
    g_reassemblyBuf.clear();
    pthread_mutex_unlock(&g_reassemblyMutex);

    return 0;
}

/* ======================== main ======================== */
int main(int argc, char *argv[])
{
    const char *server_ip = NULL;
    const char *ifname    = "eth0";
    const char *local_ip  = "";
    const char *fmt       = "h264";

    if (argc < 2) {
        printf("Usage: %s <server_ip> [ifname] [local_ip] [fmt]\n", argv[0]);
        printf("  server_ip : VCIC server address\n");
        printf("  ifname    : network interface for AVTP (default eth0)\n");
        printf("  local_ip  : optional local bind IP (default any)\n");
        printf("  fmt       : h264|h265 (default h264, must match server)\n");
        return -1;
    }
    server_ip = argv[1];
    if (argc >= 3) ifname = argv[2];
    if (argc >= 4) local_ip = argv[3];
    if (argc >= 5) fmt = argv[4];

    /* 确保 stdout/stderr 无缓冲，便于调试 */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    /* 初始化日志系统 */
    g_logMgr = log_manager_init("/userdata/logs/vcic_stream_client.ini");
    h_client  = log_register(g_logMgr, "client");
    h_dec     = log_register(g_logMgr, "dec");
    h_display = log_register(g_logMgr, "display");

    /* 使用 sigaction 替代 signal，不设 SA_RESTART 以确保中断系统调用 */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  /* 不设 SA_RESTART，让 poll/sleep 被信号中断 */
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* 屏蔽 SIGINT/SIGTERM，使后续子线程继承屏蔽，
     * 信号只投递到主线程 */
    sigset_t blockMask;
    sigemptyset(&blockMask);
    sigaddset(&blockMask, SIGINT);
    sigaddset(&blockMask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &blockMask, NULL);

    /* ---------------- 初始化屏幕显示（Pro 接口，支持 zero-copy NV12） ---------------- */
    fprintf(stderr, "[client]: initializing display (pro)...\n");
    PRINT_INFO(h_client, "[client]: initializing display (pro)...\n");

    /* 获取屏幕信息 */
    int screenW = 0, screenH = 0, refresh = 0;
    if (screen_init() != 0) {
        fprintf(stderr, "[client]: screen_init failed\n");
        PRINT_ERROR(h_client, "[client]: screen_init failed\n");
        return -1;
    }
    screen_info(&screenW, &screenH, &refresh);
    fprintf(stderr, "[client]: screen: %dx%d@%d\n", screenW, screenH, refresh);

    /* 初始化 display */
    display_t disp;
    memset(&disp, 0, sizeof(disp));
    disp.width  = screenW;
    disp.height = screenH;
    if (disp_init_pro(&disp) != 0) {
        fprintf(stderr, "[client]: disp_init_pro failed\n");
        PRINT_ERROR(h_client, "[client]: disp_init_pro failed\n");
        return -1;
    }

    /* 创建一个全屏窗口 */
    window_t win;
    memset(&win, 0, sizeof(win));
    win.zpos  = 1;
    win.win_x = 0;
    win.win_y = 0;
    win.win_w = screenW;
    win.win_h = screenH;
    g_winChn = add_window_to(DISPLAY, &win);
    if (g_winChn < 0) {
        fprintf(stderr, "[client]: add_window_to failed\n");
        PRINT_ERROR(h_client, "[client]: add_window_to failed\n");
        disp_release_pro();
        return -1;
    }
    fprintf(stderr, "[client]: display window %d created (%dx%d)\n", g_winChn, screenW, screenH);

    /* 初始化显示线程信号量 */
    sem_init(&g_displaySem, 0, 0);

    /* ---------------- 初始化 RkDecoder 解码器 ---------------- */
    fprintf(stderr, "[client]: initializing RK_MPI decoder (%s)...\n", fmt);
    PRINT_INFO(h_client, "[client]: initializing RK_MPI decoder (%s)...\n", fmt);
    g_pDecoder = new RkDecoder(fmt);
    g_pDecoder->setDecodedCallback(onDecoded, NULL);
    if (g_pDecoder->start() != 0) {
        fprintf(stderr, "[client]: RkDecoder start failed\n");
        PRINT_ERROR(h_client, "[client]: RkDecoder start failed\n");
        delete g_pDecoder;
        g_pDecoder = NULL;
        remove_window_from(DISPLAY, g_winChn);
        disp_release_pro();
        screen_exit();
        return -1;
    }
    fprintf(stderr, "[client]: RkDecoder started OK\n");

    /* 启动独立显示线程 */
    pthread_create(&g_displayTid, NULL, display_thread, NULL);
    fprintf(stderr, "[client]: display thread started\n");

    /* ---------------- 创建 VCIC Client ---------------- */
    VCIC_ClientConfig_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.ifname, ifname, sizeof(cfg.ifname) - 1);
    if (local_ip[0] != '\0')
        strncpy(cfg.local_ip, local_ip, sizeof(cfg.local_ip) - 1);
    cfg.service_id   = VCIC_SERVICE_ID;
    cfg.instance_id  = VCIC_INSTANCE_ID;
    cfg.local_port   = 30492;  /* 客户端本地端口，避免与 server 冲突 */
    strncpy(cfg.server_ip, server_ip, sizeof(cfg.server_ip) - 1);
    cfg.server_port  = VCIC_APP_PORT;
    memcpy(cfg.stream_id, STREAM_ID, 8);
    memcpy(cfg.multicast_mac, MCAST_MAC, 6);

    fprintf(stderr, "[client]: creating VCIC client (server=%s:%u if=%s local=%s)...\n",
            server_ip, VCIC_APP_PORT, ifname, local_ip[0] ? local_ip : "auto");
    g_vcicClient = VCIC_client_create(&cfg);
    if (!g_vcicClient) {
        fprintf(stderr, "[client]: VCIC_client_create failed\n");
        PRINT_ERROR(h_client, "[client]: VCIC_client_create failed\n");
        goto cleanup_decoder;
    }
    fprintf(stderr, "[client]: VCIC client created OK\n");

    /* 订阅事件 */
    VCIC_client_subscribe_event(g_vcicClient, NULL, VCIC_EVENT_CAM_STATUS, on_cam_status);
    VCIC_client_subscribe_event(g_vcicClient, NULL, VCIC_EVENT_FRAME_LOST, on_frame_lost);

    PRINT_INFO(h_client, "[client]: VCIC Client connected to %s:%u\n", server_ip, VCIC_APP_PORT);

    /* 启动取流 */
    fprintf(stderr, "[client]: starting stream...\n");
    if (VCIC_client_start_stream(g_vcicClient, NULL, on_vcic_frame) != 0) {
        fprintf(stderr, "[client]: VCIC_client_start_stream failed\n");
        PRINT_ERROR(h_client, "[client]: VCIC_client_start_stream failed\n");
        goto cleanup_vcic;
    }

    fprintf(stderr, "[client]: stream started, waiting for video frames...\n\n");
    PRINT_INFO(h_client, "[client]: stream started, waiting for video frames...\n\n");

    /* 主线程解除屏蔽 —— 确保信号只投递到主线程 */
    pthread_sigmask(SIG_UNBLOCK, &blockMask, NULL);

    /* ---------------- 主循环 ---------------- */
    while (g_running) {
        /* 用 poll 替代 sleep，信号到达时立即返回 */
        poll(NULL, 0, 2000);
        if (g_frame_cnt > 0) {
            PRINT_INFO(h_client, "[client]: stats: frags=%u frames=%u bytes=%u displayed=%u\n",
                   g_frag_cnt, g_frame_cnt, g_byte_cnt, g_disp_cnt);
            PRINT_DEBUG(h_client, "main loop alive: frags=%u frames=%u bytes=%u displayed=%u refresh=%u\n",
                   g_frag_cnt, g_frame_cnt, g_byte_cnt, g_disp_cnt, g_refresh_cnt);
        } else {
            PRINT_DEBUG(h_client, "main loop alive: waiting for frames...\n");
        }
    }

    /* ---------------- 清理 ---------------- */
    fprintf(stderr, "[client]: shutting down... (再按 Ctrl+C 可强制退出)\n");
    PRINT_INFO(h_client, "[client]: shutting down...\n");
    VCIC_client_stop_stream(g_vcicClient);

    /* 停止显示线程 */
    if (g_displayTid) {
        sem_post(&g_displaySem);  /* 唤醒线程使其退出 */
        pthread_join(g_displayTid, NULL);
        g_displayTid = 0;
    }
    sem_destroy(&g_displaySem);

cleanup_vcic:
    if (g_vcicClient) {
        VCIC_client_unsubscribe_event(g_vcicClient, VCIC_EVENT_CAM_STATUS);
        VCIC_client_unsubscribe_event(g_vcicClient, VCIC_EVENT_FRAME_LOST);
        VCIC_client_destroy(g_vcicClient);
        g_vcicClient = NULL;
    }
    fprintf(stderr, "[client]: VCIC Client stopped\n");
    PRINT_INFO(h_client, "[client]: VCIC Client stopped\n");

cleanup_decoder:
    if (g_pDecoder) {
        g_pDecoder->stop();
        delete g_pDecoder;
        g_pDecoder = NULL;
    }

    if (g_winChn >= 0) {
        remove_window_from(DISPLAY, g_winChn);
        g_winChn = -1;
    }
    disp_release_pro();
    screen_exit();
    fprintf(stderr, "[client]: display released\n");
    PRINT_INFO(h_client, "[client]: display released\n");

    return 0;
}
