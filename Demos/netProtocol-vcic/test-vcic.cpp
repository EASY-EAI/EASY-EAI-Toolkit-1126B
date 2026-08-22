/**
 * VCIC (ISO 17215) 简单 Demo
 *
 * 演示如何使用 VCIC 统一接口创建 Server / Client，
 * 通过 SOME/IP 控制面 + AVTP 数据面进行视频帧传输。
 *
 * 模式一（Server）：模拟摄像头 SoC 侧，提供合成视频帧
 *   ./test-vcic server [ifname] [local_ip]
 *
 * 模式二（Client）：模拟域控侧，接收视频帧并打印统计
 *   ./test-vcic client <server_ip> [ifname] [local_ip]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <vcic.h>

/* ======================== 通用配置 ======================== */
static const uint8_t STREAM_ID[8] = {0x00, 0x1B, 0x21, 0xFF, 0xFE, 0x00, 0x00, 0x01};
static const uint8_t MCAST_MAC[6] = {0x91, 0xE0, 0xF0, 0x00, 0x0E, 0x80};

static volatile int g_running = 1;

static void on_signal(int sig) { (void)sig; g_running = 0; }

/* ======================== Server 模式 ======================== */

static VCIC_Server *g_server = NULL;
static uint32_t g_srv_frame_cnt = 0;

/* feed 回调：生成合成帧（1280x720 H.264 模拟数据） */
static int32_t on_feed(void *pObj, uint8_t **data, uint32_t *len, uint8_t *isLastFrag)
{
    (void)pObj;
    /* 生成一帧模拟数据（实际应用中从编码器获取） */
    static uint8_t fake_frame[4096];
    memset(fake_frame, g_srv_frame_cnt & 0xFF, sizeof(fake_frame));

    *data = fake_frame;
    *len = sizeof(fake_frame);
    *isLastFrag = 1;
    g_srv_frame_cnt++;
    return 0;
}

static int run_server(const char *ifname, const char *local_ip)
{
    VCIC_ServerConfig_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.ifname, ifname, sizeof(cfg.ifname) - 1);
    if (local_ip[0] != '\0')
        strncpy(cfg.local_ip, local_ip, sizeof(cfg.local_ip) - 1);
    cfg.service_id  = VCIC_SERVICE_ID;
    cfg.instance_id = VCIC_INSTANCE_ID;
    cfg.app_port    = VCIC_APP_PORT;
    memcpy(cfg.stream_id, STREAM_ID, 8);
    memcpy(cfg.dst_mac, MCAST_MAC, 6);
    cfg.codec       = VCIC_CODEC_H264;

    g_server = VCIC_server_create(&cfg);
    if (!g_server) {
        fprintf(stderr, "[server]: VCIC_server_create failed\n");
        return -1;
    }

    VCIC_server_register_feed(g_server, NULL, on_feed);

    printf("[server]: VCIC Server ready on %s (svc=0x%04x port=%u)\n",
           ifname, cfg.service_id, cfg.app_port);
    printf("[server]: waiting for client to call START_STREAM...\n\n");

    while (g_running) {
        sleep(2);
        if (g_srv_frame_cnt > 0) {
            printf("[server]: frames sent: %u\n", g_srv_frame_cnt);
        }
    }

    printf("[server]: shutting down, total frames=%u\n", g_srv_frame_cnt);
    VCIC_server_destroy(g_server);
    g_server = NULL;
    return 0;
}

/* ======================== Client 模式 ======================== */

static VCIC_Client *g_client = NULL;
static uint32_t g_cli_frag_cnt = 0;
static uint32_t g_cli_frame_cnt = 0;
static uint32_t g_cli_byte_cnt = 0;

/* 帧回调：收到视频帧分片 */
static int32_t on_frame(void *pObj, const uint8_t *data, uint32_t len,
                        uint32_t avtp_ts, uint8_t isLastFrag, uint8_t codec)
{
    (void)pObj; (void)avtp_ts; (void)codec;
    g_cli_frag_cnt++;
    g_cli_byte_cnt += len;

    if (isLastFrag) {
        g_cli_frame_cnt++;
        printf("[client]: frame %u complete (frags=%u bytes=%u codec=%s)\n",
               g_cli_frame_cnt, g_cli_frag_cnt, g_cli_byte_cnt,
               codec == VCIC_CODEC_H265 ? "H265" : "H264");
    }

    if ((g_cli_frag_cnt % 50) == 1) {
        printf("[client]: frag#%u len=%u ts=0x%08x last=%u\n",
               g_cli_frag_cnt, len, avtp_ts, isLastFrag);
    }

    return 0;
}

/* 事件回调：CAM_STATUS */
static int32_t on_cam_status(void *pObj, uint16_t eventId,
                              const uint8_t *data, uint32_t len)
{
    (void)pObj;
    printf("[client]: event 0x%04x len=%u", eventId, len);
    if (len >= 2) printf(" streaming=%u status=0x%02x", data[0], data[1]);
    printf("\n");
    return 0;
}

/* 事件回调：FRAME_LOST */
static int32_t on_frame_lost(void *pObj, uint16_t eventId,
                              const uint8_t *data, uint32_t len)
{
    (void)pObj;
    fprintf(stderr, "[client]: event FRAME_LOST(0x%04x) len=%u\n", eventId, len);
    return 0;
}

static int run_client(const char *server_ip, const char *ifname, const char *local_ip)
{
    VCIC_ClientConfig_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.ifname, ifname, sizeof(cfg.ifname) - 1);
    if (local_ip[0] != '\0')
        strncpy(cfg.local_ip, local_ip, sizeof(cfg.local_ip) - 1);
    cfg.service_id   = VCIC_SERVICE_ID;
    cfg.instance_id  = VCIC_INSTANCE_ID;
    cfg.local_port   = 30492;
    strncpy(cfg.server_ip, server_ip, sizeof(cfg.server_ip) - 1);
    cfg.server_port  = VCIC_APP_PORT;
    memcpy(cfg.stream_id, STREAM_ID, 8);
    memcpy(cfg.multicast_mac, MCAST_MAC, 6);

    g_client = VCIC_client_create(&cfg);
    if (!g_client) {
        fprintf(stderr, "[client]: VCIC_client_create failed\n");
        return -1;
    }

    /* 订阅事件 */
    VCIC_client_subscribe_event(g_client, NULL, VCIC_EVENT_CAM_STATUS, on_cam_status);
    VCIC_client_subscribe_event(g_client, NULL, VCIC_EVENT_FRAME_LOST, on_frame_lost);

    printf("[client]: connected to %s:%u\n", server_ip, VCIC_APP_PORT);

    /* 启动取流 */
    if (VCIC_client_start_stream(g_client, NULL, on_frame) != 0) {
        fprintf(stderr, "[client]: start_stream failed\n");
        VCIC_client_destroy(g_client);
        return -1;
    }

    printf("[client]: stream started, waiting for frames...\n\n");

    while (g_running) {
        sleep(2);
        printf("[client]: stats: frags=%u frames=%u bytes=%u\n",
               g_cli_frag_cnt, g_cli_frame_cnt, g_cli_byte_cnt);
    }

    printf("[client]: stopping...\n");
    VCIC_client_stop_stream(g_client);
    VCIC_client_unsubscribe_event(g_client, VCIC_EVENT_CAM_STATUS);
    VCIC_client_unsubscribe_event(g_client, VCIC_EVENT_FRAME_LOST);
    VCIC_client_destroy(g_client);
    g_client = NULL;
    printf("[client]: stopped, total frags=%u frames=%u bytes=%u\n",
           g_cli_frag_cnt, g_cli_frame_cnt, g_cli_byte_cnt);
    return 0;
}

/* ======================== main ======================== */
int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("Usage:\n");
        printf("  Server: %s server [ifname] [local_ip]\n", argv[0]);
        printf("  Client: %s client <server_ip> [ifname] [local_ip]\n", argv[0]);
        printf("\nExamples:\n");
        printf("  %s server eth0 192.168.1.20\n", argv[0]);
        printf("  %s client 192.168.1.20 eth0 192.168.1.30\n", argv[0]);
        return -1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    const char *mode = argv[1];

    if (strcmp(mode, "server") == 0) {
        const char *ifname   = (argc >= 3) ? argv[2] : "eth0";
        const char *local_ip = (argc >= 4) ? argv[3] : "";
        return run_server(ifname, local_ip);
    } else if (strcmp(mode, "client") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Client mode requires <server_ip>\n");
            return -1;
        }
        const char *server_ip = argv[2];
        const char *ifname    = (argc >= 4) ? argv[3] : "eth0";
        const char *local_ip  = (argc >= 5) ? argv[4] : "";
        return run_client(server_ip, ifname, local_ip);
    } else {
        fprintf(stderr, "Unknown mode: %s (use 'server' or 'client')\n", mode);
        return -1;
    }
}
