/**
 * SOME/IP + AVTP 融合方案 —— Server 端（摄像头 / 推流端）
 *
 * 使用 VCIC 统一接口（ISO 17215）实现：
 *   - VCIC_server_create()          创建服务
 *   - VCIC_server_register_feed()   注册视频帧数据源回调
 *   - VCIC_server_send_event()      推送事件通知
 *
 * 数据面（二选一）：
 *   [RK_MPI 模式]    MIPI sensor → RK_MPI_VI → RK_MPI_VENC → 码流回调
 *                   (ISP 3A 由 rkaiq_3A.service 负责)
 *   [文件模式]        H.264/H.265 AnnexB 文件 → NAL 解析 → Access Unit 回调
 *
 * 终端控制：
 *   - start   → 开始推流
 *   - stop    → 停止推流
 *   - status  → 打印当前状态
 *   - quit    → 退出程序
 *
 * 用法：
 *   [RK_MPI 模式] (默认)
 *     sudo ./vcic_stream_server [ifname] [local_ip] [width] [height] [framerate] [fmt]
 *   示例：
 *     sudo ./vcic_stream_server eth0 192.168.1.20 1920 1080 30 h264
 *
 *   前置条件：rkaiq_3A.service 必须已启动（systemctl start rkaiq_3A.service）
 *
 *   [文件模式]
 *     sudo ./vcic_stream_server --file <h264_file> [ifname] [local_ip] [framerate] [fmt]
 *   示例：
 *     sudo ./vcic_stream_server --file miaobiao.h264 eth0 192.168.1.20 30 h264
 *
 * 需要权限：CAP_NET_RAW（AVTP 使用裸以太网 socket）
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>
#include <pthread.h>

#include "log_manager_pro.h"
#include "vcic.h"
#include "rkCapturer.h"
#include "fileCapturer.h"

/* ======================== 日志句柄 ======================== */
static log_mgr_t    g_logMgr  = LOG_MGR_INVALID;
static log_handle_t h_server  = LOG_HANDLE_INVALID;
static log_handle_t h_capture = LOG_HANDLE_INVALID;

/* ======================== 数据源模式 ======================== */
typedef enum {
    SOURCE_RK = 0,    /* RK_MPI MIPI sensor 采集 + 硬件编码 */
    SOURCE_FILE       /* H.264/H.265 文件直接读取 */
} SourceMode;

/* ======================== 全局状态 ======================== */
static volatile sig_atomic_t g_running  = 1;
static volatile sig_atomic_t g_streaming = 0;
/* 是否已进入关闭流程（用于二次 Ctrl+C 强制退出） */
static volatile sig_atomic_t g_shuttingDown = 0;
static SourceMode   g_sourceMode   = SOURCE_RK;
static char         g_ifname[16]   = "eth0";
static char         g_filePath[256] = "";
static char         g_localIp[16]  = "";
static int          g_width        = 1920;
static int          g_height       = 1080;
static int          g_framerate    = 30;
static char         g_videoFmt[8]  = "h264";

/* 线程安全锁 */
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

/* AVTP / VCIC 配置 */
static const uint8_t STREAM_ID[8] = {0x00, 0x1B, 0x21, 0xFF, 0xFE, 0x00, 0x00, 0x01};
static const uint8_t MCAST_MAC[6] = {0x91, 0xE0, 0xF0, 0x00, 0x0E, 0x80};

/* 数据源捕获器（二选一） */
static RkCapturer   *g_pRkCapturer   = NULL;
static FileCapturer *g_pFileCapturer = NULL;

/* VCIC Server 实例 */
static VCIC_Server *g_vcicServer = NULL;

/* 帧缓冲（feed callback 用） */
static uint8_t *g_frameBuf = NULL;
static uint32_t g_frameLen = 0;
static uint8_t  g_frameIsLast = 0;
static pthread_mutex_t g_frameLock = PTHREAD_MUTEX_INITIALIZER;

/* 统计计数 */
static uint32_t g_frame_cnt = 0;
static uint32_t g_frag_cnt  = 0;
static uint32_t g_byte_cnt  = 0;

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

/* ======================== 帧回调 → 缓存到帧缓冲 ======================== */
/* RK_MPI 模式和文件模式共用同一回调 */
static void onFrame(const uint8_t *data, uint32_t len,
                    const FrameDesc_t *desc, void *userData)
{
    (void)userData;
    (void)desc;

    if (!g_streaming || !data || len == 0) {
        return;
    }

    pthread_mutex_lock(&g_frameLock);
    /* 缓存当前帧，等待 VCIC feed callback 取走 */
    if (g_frameBuf) free(g_frameBuf);
    g_frameBuf = (uint8_t *)malloc(len);
    if (g_frameBuf) {
        memcpy(g_frameBuf, data, len);
        g_frameLen = len;
        g_frameIsLast = 1;
        g_frame_cnt++;
        g_byte_cnt += len;
    }
    pthread_mutex_unlock(&g_frameLock);

    if ((g_frame_cnt % 30) == 0) {
        PRINT_DEBUG(h_capture, "onFrame alive: frame_cnt=%u len=%u frag_cnt=%u\n",
                    g_frame_cnt, len, g_frag_cnt);
    }
}

/* ======================== VCIC Feed 回调 → 提供视频帧 ======================== */
static int32_t on_vcic_feed(void *pObj, uint8_t **data, uint32_t *len, uint8_t *isLastFrag)
{
    (void)pObj;

    pthread_mutex_lock(&g_frameLock);
    if (g_frameBuf && g_frameLen > 0) {
        *data = g_frameBuf;
        *len = g_frameLen;
        *isLastFrag = g_frameIsLast;
        g_frag_cnt++;
        /* 清空指针（VCIC 取走后不释放，下次替换时释放） */
        g_frameBuf = NULL;
        g_frameLen = 0;
    } else {
        *data = NULL;
        *len = 0;
        *isLastFrag = 0;
    }
    pthread_mutex_unlock(&g_frameLock);

    if ((g_frag_cnt % 30) == 0) {
        PRINT_DEBUG(h_server, "on_vcic_feed alive: frag_cnt=%u frame_cnt=%u buf=%s\n",
                    g_frag_cnt, g_frame_cnt, (*data) ? "has" : "empty");
    }

    return 0;
}

/* ======================== 推流控制函数 ======================== */

/* 启动推流：启动数据源采集即可。
 * 返回 0=成功, 1=已在推流, 2=失败 */
static int do_start_stream(void)
{
    pthread_mutex_lock(&g_mutex);

    if (g_streaming) {
        PRINT_DEBUG(h_server, "[server]: START_STREAM — already streaming\n");
        pthread_mutex_unlock(&g_mutex);
        return 1;
    }

    if (g_sourceMode == SOURCE_FILE) {
        /* ---- 文件模式 ---- */
        g_pFileCapturer = new FileCapturer(g_filePath, g_framerate, g_videoFmt);
        g_pFileCapturer->setFrameCallback(onFrame, NULL);
        if (g_pFileCapturer->start() != 0) {
            PRINT_ERROR(h_server, "[server]: FileCapturer start failed (file=%s)\n",
                   g_filePath);
            delete g_pFileCapturer;
            g_pFileCapturer = NULL;
            pthread_mutex_unlock(&g_mutex);
            return 2;
        }
        PRINT_INFO(h_server, "[server]: START_STREAM — file %s (%zu frames @%d fps %s)\n",
               g_filePath, g_pFileCapturer->frameCount(), g_framerate, g_videoFmt);
    } else {
        /* ---- RK_MPI 模式 ---- */
        g_pRkCapturer = new RkCapturer("", g_width, g_height, g_framerate, g_videoFmt);
        g_pRkCapturer->setFrameCallback(onFrame, NULL);
        if (g_pRkCapturer->start() != 0) {
            PRINT_ERROR(h_capture, "[server]: RkCapturer start failed (%dx%d@%d)\n",
                   g_width, g_height, g_framerate);
            delete g_pRkCapturer;
            g_pRkCapturer = NULL;
            pthread_mutex_unlock(&g_mutex);
            return 2;
        }
        PRINT_INFO(h_server, "[server]: START_STREAM — RK_MPI (%dx%d@%d %s)\n",
               g_width, g_height, g_framerate, g_videoFmt);
    }

    /* 重置统计 */
    g_frame_cnt = 0;
    g_frag_cnt  = 0;
    g_byte_cnt  = 0;

    g_streaming = 1;

    pthread_mutex_unlock(&g_mutex);
    return 0;
}

/* 停止推流 */
static int do_stop_stream(void)
{
    pthread_mutex_lock(&g_mutex);

    if (!g_streaming) {
        PRINT_DEBUG(h_server, "[server]: STOP_STREAM — not streaming\n");
        pthread_mutex_unlock(&g_mutex);
        return 1;
    }

    g_streaming = 0;

    /* 停止数据源 */
    if (g_pRkCapturer) {
        g_pRkCapturer->stop();
        delete g_pRkCapturer;
        g_pRkCapturer = NULL;
    }
    if (g_pFileCapturer) {
        g_pFileCapturer->stop();
        delete g_pFileCapturer;
        g_pFileCapturer = NULL;
    }

    /* 清理帧缓冲 */
    pthread_mutex_lock(&g_frameLock);
    if (g_frameBuf) { free(g_frameBuf); g_frameBuf = NULL; }
    g_frameLen = 0;
    pthread_mutex_unlock(&g_frameLock);

    PRINT_DEBUG(h_server, "[server]: STOP_STREAM — stopped (frames=%u frags=%u bytes=%u)\n",
           g_frame_cnt, g_frag_cnt, g_byte_cnt);

    pthread_mutex_unlock(&g_mutex);
    return 0;
}

/* 打印状态 */
static void do_print_status(void)
{
    pthread_mutex_lock(&g_mutex);
    if (g_sourceMode == SOURCE_FILE) {
        PRINT_INFO(h_server, "[server]: status: streaming=%d source=FILE file=%s @%d %s "
               "frames=%u frags=%u bytes=%u\n",
               g_streaming, g_filePath, g_framerate, g_videoFmt,
               g_frame_cnt, g_frag_cnt, g_byte_cnt);
    } else {
        PRINT_INFO(h_server, "[server]: status: streaming=%d source=RK %dx%d@%d %s "
               "frames=%u frags=%u bytes=%u\n",
               g_streaming, g_width, g_height, g_framerate, g_videoFmt,
               g_frame_cnt, g_frag_cnt, g_byte_cnt);
    }
    pthread_mutex_unlock(&g_mutex);
}

/* ======================== 终端命令线程 ======================== */
static void *terminal_thread(void *para)
{
    (void)para;
    char cmd[64];
    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;

    printf("\n命令: start | stop | status | quit\n> ");
    fflush(stdout);

    while (g_running) {
        int ret = poll(&pfd, 1, 1000);
        if (ret <= 0) {
            PRINT_DEBUG(h_server, "terminal_thread alive: waiting for command...\n");
            continue;
        }

        if (fgets(cmd, sizeof(cmd), stdin) == NULL) break;

        cmd[strcspn(cmd, "\r\n")] = '\0';

        if (strcmp(cmd, "start") == 0) {
            do_start_stream();
        } else if (strcmp(cmd, "stop") == 0) {
            do_stop_stream();
        } else if (strcmp(cmd, "status") == 0) {
            do_print_status();
        } else if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "q") == 0) {
            g_running = 0;
            break;
        } else if (cmd[0] != '\0') {
            printf("未知命令: %s\n", cmd);
            printf("可用命令: start | stop | status | quit");
        }

        printf("> ");
        fflush(stdout);
    }

    return NULL;
}

/* ======================== main ======================== */
int main(int argc, char *argv[])
{
    /* 解析参数 —— 支持两种模式 */
    if (argc >= 2 && strcmp(argv[1], "--file") == 0) {
        /* ---- 文件模式 ---- */
        g_sourceMode = SOURCE_FILE;
        if (argc >= 3) strncpy(g_filePath, argv[2], sizeof(g_filePath) - 1);
        if (argc >= 4) strncpy(g_ifname, argv[3], sizeof(g_ifname) - 1);
        if (argc >= 5) strncpy(g_localIp, argv[4], sizeof(g_localIp) - 1);
        if (argc >= 6) g_framerate = atoi(argv[5]);
        if (argc >= 7) strncpy(g_videoFmt, argv[6], sizeof(g_videoFmt) - 1);

        printf("Usage: %s --file <h264_file> [ifname] [local_ip] [framerate] [fmt]\n", argv[0]);
        printf("  h264_file : H.264/H.265 AnnexB bitstream file\n");
        printf("  ifname    : network interface for AVTP (default %s)\n", g_ifname);
        printf("  local_ip  : VCIC bind IP (default any)\n");
        printf("  framerate : playback fps (default %d)\n", g_framerate);
        printf("  fmt       : h264|h265 (default %s)\n\n", g_videoFmt);
    } else {
        /* ---- RK_MPI 模式（默认） ---- */
        g_sourceMode = SOURCE_RK;
        if (argc >= 2) strncpy(g_ifname, argv[1], sizeof(g_ifname) - 1);
        if (argc >= 3) strncpy(g_localIp, argv[2], sizeof(g_localIp) - 1);
        if (argc >= 4) g_width = atoi(argv[3]);
        if (argc >= 5) g_height = atoi(argv[4]);
        if (argc >= 6) g_framerate = atoi(argv[5]);
        if (argc >= 7) strncpy(g_videoFmt, argv[6], sizeof(g_videoFmt) - 1);
        printf("Usage: %s [ifname] [local_ip] [width] [height] [framerate] [fmt]\n", argv[0]);
        printf("  ifname    : network interface for AVTP (default %s)\n", g_ifname);
        printf("  local_ip  : VCIC bind IP (default any)\n");
        printf("  width     : capture width (default %d)\n", g_width);
        printf("  height    : capture height (default %d)\n", g_height);
        printf("  framerate : capture fps (default %d)\n", g_framerate);
        printf("  fmt       : h264|h265 (default %s)\n", g_videoFmt);
        printf("  * ISP 3A 由 rkaiq_3A.service 负责，请确保已启动\n");
        printf("\n  * 文件模式: %s --file <h264_file> [ifname] [local_ip] [framerate] [fmt]\n\n",
               argv[0]);
    }

    /* 初始化日志系统 */
    g_logMgr  = log_manager_init("/userdata/logs/vcic_stream_server.ini");
    h_server  = log_register(g_logMgr, "server");
    h_capture = log_register(g_logMgr, "capture");

    if (g_sourceMode == SOURCE_FILE) {
        PRINT_INFO(h_server, "[server]: file mode\n");
    }

    /* ---- 信号处理：sigaction + 线程信号屏蔽 ----
     * 关键：在创建任何子线程之前，先屏蔽 SIGINT/SIGTERM，
     * 这样后续创建的子线程都会继承屏蔽，
     * 信号只会投递到主线程（主线程稍后解除屏蔽）。
     * 这确保 sleep/poll 一定被信号中断。
     */
    sigset_t blockMask;
    sigemptyset(&blockMask);
    sigaddset(&blockMask, SIGINT);
    sigaddset(&blockMask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &blockMask, NULL);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  /* 不设 SA_RESTART，确保 sleep/poll 被中断 */
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* ---------------- 创建 VCIC Server ---------------- */
    VCIC_ServerConfig_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.ifname, g_ifname, sizeof(cfg.ifname) - 1);
    if (g_localIp[0] != '\0')
        strncpy(cfg.local_ip, g_localIp, sizeof(cfg.local_ip) - 1);
    cfg.service_id  = VCIC_SERVICE_ID;
    cfg.instance_id = VCIC_INSTANCE_ID;
    cfg.app_port    = VCIC_APP_PORT;
    memcpy(cfg.stream_id, STREAM_ID, 8);
    memcpy(cfg.dst_mac, MCAST_MAC, 6);
    cfg.codec = (strcmp(g_videoFmt, "h265") == 0) ? VCIC_CODEC_H265 : VCIC_CODEC_H264;

    g_vcicServer = VCIC_server_create(&cfg);
    if (!g_vcicServer) {
        PRINT_ERROR(h_server, "VCIC_server_create failed\n");
        return -1;
    }

    /* 注册视频帧数据源回调 */
    VCIC_server_register_feed(g_vcicServer, NULL, on_vcic_feed);

    PRINT_INFO(h_server, "[server]: VCIC Server started\n");
    PRINT_INFO(h_server, "[server]:   svc=0x%04x inst=0x%04x port=%u\n", cfg.service_id, cfg.instance_id, cfg.app_port);
    PRINT_INFO(h_server, "[server]:   AVTP: if=%s stream_id=01 mcast=%02x:%02x:%02x:%02x:%02x:%02x\n",
           g_ifname, MCAST_MAC[0], MCAST_MAC[1], MCAST_MAC[2],
           MCAST_MAC[3], MCAST_MAC[4], MCAST_MAC[5]);
    if (g_sourceMode == SOURCE_FILE) {
        PRINT_INFO(h_server, "[server]:   Source: FILE  file=%s @%d %s\n",
               g_filePath, g_framerate, g_videoFmt);
    } else {
        PRINT_INFO(h_server, "[server]:   Source: RK    %dx%d@%d %s (3A: rkaiq_3A.service)\n",
               g_width, g_height, g_framerate, g_videoFmt);
    }
    PRINT_INFO(h_server, "[server]:   终端命令: start | stop | status | quit\n\n");

    /* ---------------- 启动终端命令线程 ----------------
     * 终端线程继承了 SIGINT/SIGTERM 屏蔽，不会抢夺信号
     */
    pthread_t termTid;
    pthread_create(&termTid, NULL, terminal_thread, NULL);

    /* 主线程解除屏蔽 —— 确保信号只投递到主线程 */
    pthread_sigmask(SIG_UNBLOCK, &blockMask, NULL);

    /* ---------------- 主循环 ----------------
     * 用 poll 替代 sleep：poll 在信号到达时立即返回 -1(EINTR)，
     * 比 sleep 更可靠地响应信号
     */
    uint32_t tick = 0;

    while (g_running) {
        if (g_streaming) {
            /* 每 5 秒主动发一次 CAM_STATUS 事件 */
            if ((tick % 5) == 0 && tick > 0) {
                uint8_t evt[4];
                evt[0] = 0x01;  /* streaming */
                evt[1] = 0x00;  /* ok */
                evt[2] = (uint8_t)(g_frame_cnt & 0xFF);
                evt[3] = (uint8_t)((g_frame_cnt >> 8) & 0xFF);
                VCIC_server_send_event(g_vcicServer, VCIC_EVENT_CAM_STATUS, evt, sizeof(evt));
                PRINT_INFO(h_server, "[server]: event CAM_STATUS sent (frames=%u)\n", g_frame_cnt);
            }
            poll(NULL, 0, 1000);  /* 1 秒，信号到达时立即返回 */
            tick++;
            if ((tick % 5) == 0) {
                PRINT_DEBUG(h_server, "main loop alive: tick=%u streaming=%u frames=%u frags=%u\n",
                            tick, (uint32_t)g_streaming, g_frame_cnt, g_frag_cnt);
            }
        } else {
            poll(NULL, 0, 1000);
            tick = 0;
        }
    }

    /* ---------------- 清理 ---------------- */
    PRINT_INFO(h_server, "[server]: shutting down... (再按 Ctrl+C 可强制退出)\n");
    if (g_streaming) do_stop_stream();
    VCIC_server_destroy(g_vcicServer);
    g_vcicServer = NULL;

    pthread_join(termTid, NULL);

    PRINT_INFO(h_server, "[server]: stopped, total frames=%u frags=%u bytes=%u\n",
           g_frame_cnt, g_frag_cnt, g_byte_cnt);
    return 0;
}
