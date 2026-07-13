/*
 * aiisp_rtsp.c — rv1126b 双摄 AIISP 验证 + RTSP 预览（主码流 + 子码流）
 *
 * 管线：
 *   Sensor 0 (camId=0):
 *     VI(dev=0,pipe=0,chn=0) -> VPSS(grp=0) -> chn0 -> VENC(0)  RTSP: rtsp://<ip>:554/live/0
 *                                             -> chn1 -> VENC(1)  RTSP: rtsp://<ip>:554/live/1
 *     监控节点: /proc/rkaiisp-vir0
 *
 *   Sensor 1 (camId=1):
 *     VI(dev=1,pipe=1,chn=0) -> VPSS(grp=1) -> chn0 -> VENC(2)  RTSP: rtsp://<ip>:554/live/2
 *                                             -> chn1 -> VENC(3)  RTSP: rtsp://<ip>:554/live/3
 *     监控节点: /proc/rkaiisp-vir1
 *
 * AIBNR 受 ISO 阈值(isoIdx3)控制，遮住镜头/暗光下才激活。
 */

#include "lmo_common.h"
#include "rtsp_demo.h"
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <rk_mpi_sys.h>

#define MAX_SENSOR       2
#define VENC_PER_SENSOR  2                    /* 每路 sensor 的主码流 + 子码流 */

/* ---------- AIISP proc 节点 ---------- */
static const char *g_aiisp_proc[MAX_SENSOR] = {
    "/proc/rkaiisp-vir0",
    "/proc/rkaiisp-vir1",
};

/* ---------- RTSP ---------- */
static rtsp_demo_handle    g_rtsplive              = NULL;
static rtsp_session_handle g_rtsp_session[MAX_SENSOR * VENC_PER_SENSOR];
static pthread_mutex_t     g_rtsp_mutex            = PTHREAD_MUTEX_INITIALIZER;
static int                 g_rtsp_ready            = 0;

/* ---------- 状态 ---------- */
static volatile int g_quit      = 0;
static volatile int g_venc_quit = 0;
static pthread_t    g_monitor_tid;
static pthread_t    g_venc_tid  [MAX_SENSOR * VENC_PER_SENSOR];

/* ---- VENC 线程参数 ---- */
typedef struct {
    LMO_VENC_S *venc;
    rtsp_session_handle session;
} VencThreadArg;

/* ---- 单路 sensor 参数 ---- */
typedef struct {
    unsigned int          u32Width;
    unsigned int          u32Height;
    unsigned int          u32SubWidth;
    unsigned int          u32SubHeight;
    unsigned int          u32Fps;
    int                   s32BitRate;
    int                   s32SubBitRate;
} SensorParams;

typedef struct {
    SensorParams          stSensor[MAX_SENSOR];
    int                   s32CamIdBase;       /* 通常为 0 */
    char                 *pIqFileDir;
    int                   s32LoopCnt;
    LMO_CODEC_TYPE_E      enCodecType;
    LMO_RC_MODE_E         enRcMode;
    LMO_S32               eHdrMode;
    int                   s32SensorCnt;       /* 实际启用 sensor 数量 */
} TestArgs;

static void sigterm_handler(int sig) {
    fprintf(stderr, "\ncatch signal %d, exiting...\n", sig);
    g_quit = 1;
    g_venc_quit = 1;
}

/* ---- RTSP ---- */
static int rtsp_init(LMO_CODEC_TYPE_E enCodecType, int sensorCnt) {
    g_rtsplive = create_rtsp_demo(554);
    if (!g_rtsplive) {
        printf("[rtsp] create_rtsp_demo failed\n");
        return -1;
    }

    int codec_id = (enCodecType == LMO_CODEC_H264)
                   ? RTSP_CODEC_ID_VIDEO_H264
                   : RTSP_CODEC_ID_VIDEO_H265;

    int total = sensorCnt * VENC_PER_SENSOR;
    for (int i = 0; i < total; i++) {
        char url[32];
        snprintf(url, sizeof(url), "/live/%d", i);
        g_rtsp_session[i] = rtsp_new_session(g_rtsplive, url);
        rtsp_set_video(g_rtsp_session[i], codec_id, NULL, 0);
        rtsp_sync_video_ts(g_rtsp_session[i], rtsp_get_reltime(), rtsp_get_ntptime());
    }

    g_rtsp_ready = 1;
    printf("[rtsp] server ready on port 554\n");
    for (int s = 0; s < sensorCnt; s++) {
        printf("[rtsp]   sensor %d  main: rtsp://<board-ip>:554/live/%d\n",
               s, s * VENC_PER_SENSOR + 0);
        printf("[rtsp]   sensor %d  sub:  rtsp://<board-ip>:554/live/%d\n",
               s, s * VENC_PER_SENSOR + 1);
    }
    return 0;
}

static void rtsp_deinit(void) {
    g_rtsp_ready = 0;
    if (g_rtsplive)
        rtsp_del_demo(g_rtsplive);
    g_rtsplive = NULL;
    for (int i = 0; i < MAX_SENSOR * VENC_PER_SENSOR; i++)
        g_rtsp_session[i] = NULL;
}

/* ---- 监控线程 ---- */
static void *monitor_thread(void *arg) {
    int sensorCnt = *(int *)arg;
    int last_frame_id[MAX_SENSOR] = {-1, -1};
    int loop = 0;
    prctl(PR_SET_NAME, "aiisp_monitor");

    while (!g_quit) {
        for (int s = 0; s < sensorCnt; s++) {
            FILE *fp = fopen(g_aiisp_proc[s], "r");
            if (!fp) {
                printf("[monitor_s%d] %s 打不开 — 内核未注册 aiisp 设备\n",
                       s, g_aiisp_proc[s]);
                continue;
            }
            char line[256];
            int run_idx = -1, frame_id = -1, frm_rate = -1, exe_algo = -1;
            int hw_state = -1, img_w = -1, img_h = -1;
            while (fgets(line, sizeof(line), fp)) {
                int v;
                if      (sscanf(line, "run idx %d",      &v) == 1) run_idx   = v;
                else if (sscanf(line, "frame id %d",     &v) == 1) frame_id  = v;
                else if (sscanf(line, "frm rate %d",     &v) == 1) frm_rate  = v;
                else if (sscanf(line, "execute algo %d", &v) == 1) exe_algo  = v;
                else if (sscanf(line, "hw state %d",     &v) == 1) hw_state  = v;
                else if (sscanf(line, "image width %d",  &v) == 1) img_w     = v;
                else if (sscanf(line, "image height %d", &v) == 1) img_h     = v;
            }
            fclose(fp);

            const char *algo_name;
            switch (exe_algo) {
                case 0:  algo_name = "AIBNR"; break;
                case 1:  algo_name = "AIRMS"; break;
                case 2:  algo_name = "AIYNR"; break;
                default: algo_name = "none";  break;
            }
            int running = (frame_id != last_frame_id[s] && frame_id > 0);
            printf("[monitor_s%d #%02d] run_idx=%d frame_id=%d frm_rate=%d "
                   "algo=%d(%s) hw_state=%d img=%dx%d  => %s\n",
                   s, loop, run_idx, frame_id, frm_rate,
                   exe_algo, algo_name, hw_state, img_w, img_h,
                   running ? "AIISP 正在推理 ✓" : "未推理(ISO未达阈值)");
            last_frame_id[s] = frame_id;
        }
        loop++;
        sleep(2);
    }
    printf("[monitor] exit\n");
    return NULL;
}

/* ---- VENC 取流线程 ---- */
static void *venc_get_stream(void *arg) {
    VencThreadArg *targ = (VencThreadArg *)arg;
    LMO_VENC_S *venc = targ->venc;
    rtsp_session_handle session = targ->session;
    prctl(PR_SET_NAME, "venc_get_stream");

    int cnt = 0;
    int loopCount = LMO_COMM_VENC_GetLoopCount(venc);
    while (!g_venc_quit) {
        void *pData = NULL;
        if (LMO_COMM_VENC_GetStream(venc, &pData) != LMO_SUCCESS)
            continue;

        LMO_U32 len = LMO_COMM_VENC_GetStreamSize(venc);
        LMO_U64 pts = LMO_COMM_VENC_GetStreamPts(venc);

        if (g_rtsp_ready && pData) {
            pthread_mutex_lock(&g_rtsp_mutex);
            rtsp_tx_video(session, pData, len, pts);
            rtsp_do_event(g_rtsplive);
            pthread_mutex_unlock(&g_rtsp_mutex);
        }

        LMO_COMM_VENC_ReleaseStream(venc);
        cnt++;
        if (loopCount > 0 && cnt >= loopCount) {
            g_quit = 1;
            break;
        }
    }
    printf("venc_get_stream[%d] exit, got %d frames\n",
           LMO_COMM_VENC_GetChnId(venc), cnt);
    return NULL;
}

static void print_usage(const char *name) {
    printf("Usage: %s [options]\n", name);
    printf("\n  === Sensor 0 (camId=0) ===\n");
    printf("  -w <width>   sensor0 width     (default 2688)\n");
    printf("  -h <height>  sensor0 height    (default 1520)\n");
    printf("  -W <width>   sensor0 sub width (default 640)\n");
    printf("  -H <height>  sensor0 sub height(default 360)\n");
    printf("  -b <kbps>    sensor0 main br   (default 8192)\n");
    printf("  -B <kbps>    sensor0 sub br    (default 768)\n");
    printf("\n  === Sensor 1 (camId=1) ===\n");
    printf("  --w1 <w>     sensor1 width     (default 2688)\n");
    printf("  --h1 <h>     sensor1 height    (default 1520)\n");
    printf("  --W1 <w>     sensor1 sub width (default 640)\n");
    printf("  --H1 <h>     sensor1 sub height(default 360)\n");
    printf("  --b1 <kbps>  sensor1 main br   (default 8192)\n");
    printf("  --B1 <kbps>  sensor1 sub br    (default 768)\n");
    printf("\n  === 通用 ===\n");
    printf("  -a <iqdir>   iqfiles dir       (default /etc/iqfiles/)\n");
    printf("  -e h264cbr|h265cbr             (default h265cbr)\n");
    printf("  -l <N>       exit after N frames (-1=forever)\n");
    printf("  -n <1|2>     sensor count      (default 2)\n");
    printf("  -f <fps>     frame rate        (default 25)\n");
    printf("\nRTSP:\n");
    printf("  sensor0 main: rtsp://<board-ip>:554/live/0\n");
    printf("  sensor0 sub:  rtsp://<board-ip>:554/live/1\n");
    printf("  sensor1 main: rtsp://<board-ip>:554/live/2\n");
    printf("  sensor1 sub:  rtsp://<board-ip>:554/live/3\n");
    printf("AIBNR 激活条件: ISO > isoIdx3 (遮住镜头或暗光)\n");
}

int main(int argc, char *argv[]) {
    TestArgs args;
    memset(&args, 0, sizeof(args));

    /* ---- 默认值 ---- */
    for (int s = 0; s < MAX_SENSOR; s++) {
        args.stSensor[s].u32Width     = 2688;
        args.stSensor[s].u32Height    = 1520;
        args.stSensor[s].u32Fps       = 25;
        args.stSensor[s].s32BitRate   = 8192;
        args.stSensor[s].u32SubWidth  = 640;
        args.stSensor[s].u32SubHeight = 360;
        args.stSensor[s].s32SubBitRate = 768;
    }
    args.pIqFileDir   = "/etc/iqfiles/";
    args.s32LoopCnt   = -1;
    args.enCodecType  = LMO_CODEC_H265;
    args.enRcMode     = LMO_RC_MODE_H265CBR;
    args.eHdrMode     = LMO_AIQ_WORKING_MODE_NORMAL;
    args.s32SensorCnt = 2;   /* 默认双摄 */

    /* ---- 命令行解析 ---- */
    int c;
    int option_index = 0;
    enum { OPT_S1_W = 256, OPT_S1_H, OPT_S1_SUBW, OPT_S1_SUBH,
           OPT_S1_BR, OPT_S1_SUBBR };
    static struct option long_options[] = {
        {"w1",  required_argument, 0, OPT_S1_W},
        {"h1",  required_argument, 0, OPT_S1_H},
        {"W1",  required_argument, 0, OPT_S1_SUBW},
        {"H1",  required_argument, 0, OPT_S1_SUBH},
        {"b1",  required_argument, 0, OPT_S1_BR},
        {"B1",  required_argument, 0, OPT_S1_SUBBR},
        {0, 0, 0, 0}
    };

    while ((c = getopt_long(argc, argv,
             "w:h:a:e:b:W:H:B:l:n:f:?", long_options, &option_index)) != -1) {
        switch (c) {
        /* ---- Sensor 0 ---- */
        case 'w': args.stSensor[0].u32Width  = (unsigned)atoi(optarg); break;
        case 'h': args.stSensor[0].u32Height = (unsigned)atoi(optarg); break;
        case 'W': args.stSensor[0].u32SubWidth  = (unsigned)atoi(optarg); break;
        case 'H': args.stSensor[0].u32SubHeight = (unsigned)atoi(optarg); break;
        case 'b': args.stSensor[0].s32BitRate    = atoi(optarg); break;
        case 'B': args.stSensor[0].s32SubBitRate = atoi(optarg); break;

        /* ---- Sensor 1 (long options) ---- */
        case OPT_S1_W:    args.stSensor[1].u32Width  = (unsigned)atoi(optarg); break;
        case OPT_S1_H:    args.stSensor[1].u32Height = (unsigned)atoi(optarg); break;
        case OPT_S1_SUBW: args.stSensor[1].u32SubWidth  = (unsigned)atoi(optarg); break;
        case OPT_S1_SUBH: args.stSensor[1].u32SubHeight = (unsigned)atoi(optarg); break;
        case OPT_S1_BR:   args.stSensor[1].s32BitRate    = atoi(optarg); break;
        case OPT_S1_SUBBR:args.stSensor[1].s32SubBitRate = atoi(optarg); break;

        /* ---- 通用 ---- */
        case 'a': args.pIqFileDir  = optarg;           break;
        case 'l': args.s32LoopCnt  = atoi(optarg);     break;
        case 'f':
            for (int s = 0; s < MAX_SENSOR; s++)
                args.stSensor[s].u32Fps = (unsigned)atoi(optarg);
            break;
        case 'n': args.s32SensorCnt = atoi(optarg);    break;
        case 'e':
            if (!strcmp(optarg, "h264cbr")) {
                args.enCodecType = LMO_CODEC_H264;
                args.enRcMode    = LMO_RC_MODE_H264CBR;
            } else {
                args.enCodecType = LMO_CODEC_H265;
                args.enRcMode    = LMO_RC_MODE_H265CBR;
            }
            break;
        default:
            print_usage(argv[0]);
            return 0;
        }
    }

    if (args.s32SensorCnt < 1 || args.s32SensorCnt > MAX_SENSOR) {
        printf("sensor count must be 1-%d\n", MAX_SENSOR);
        return -1;
    }

    printf("==== aiisp (双摄 AIISP + RTSP) ====\n");
    printf("sensor count: %d, fps: %u, iq: %s, codec: %s, loop: %d\n",
           args.s32SensorCnt,
           args.stSensor[0].u32Fps,
           args.pIqFileDir,
           args.enCodecType == LMO_CODEC_H264 ? "H264" : "H265",
           args.s32LoopCnt);
    for (int s = 0; s < args.s32SensorCnt; s++) {
        printf("  sensor[%d]: %ux%u  sub=%ux%u  main_br=%ukbps sub_br=%ukbps\n",
               s,
               args.stSensor[s].u32Width, args.stSensor[s].u32Height,
               args.stSensor[s].u32SubWidth, args.stSensor[s].u32SubHeight,
               args.stSensor[s].s32BitRate, args.stSensor[s].s32SubBitRate);
    }

    signal(SIGINT,  sigterm_handler);
    signal(SIGTERM, sigterm_handler);

    /* ============================================================
     *  1. ISP init — 两路 sensor
     * ============================================================ */
    bool bMultiCam = (args.s32SensorCnt > 1);
    for (int s = 0; s < args.s32SensorCnt; s++) {
        printf("\n--- ISP[%d] init ---\n", s);
        if (LMO_ISP_Init(s, args.eHdrMode, bMultiCam, args.pIqFileDir) != LMO_SUCCESS) {
            printf("ISP[%d] init failed\n", s);
            /* 回滚已初始化的 ISP */
            for (int i = 0; i < s; i++)
                LMO_ISP_Stop(i);
            return -1;
        }
        LMO_ISP_SetFrameRate(s, args.stSensor[s].u32Fps);
        if (LMO_ISP_Run(s) != LMO_SUCCESS) {
            printf("ISP[%d] run failed\n", s);
            for (int i = 0; i <= s; i++)
                LMO_ISP_Stop(i);
            return -1;
        }
        /* 启用 AIISP */
        if (LMO_ISP_EnablsAiisp(s) != LMO_SUCCESS) {
            printf("ISP[%d] EnablsAiisp failed (可能已启用或固件不存在)\n", s);
        }
        printf("ISP[%d] init/run OK, AIISP enabled\n", s);
    }

    /* ---- 系统初始化 ---- */
    if (RK_MPI_SYS_Init() != 0) {
        printf("RK_MPI_SYS_Init failed\n");
        for (int s = 0; s < args.s32SensorCnt; s++)
            LMO_ISP_Stop(s);
        return -1;
    }

    /* ============================================================
     *  2. RTSP
     * ============================================================ */
    rtsp_init(args.enCodecType, args.s32SensorCnt);

    /* ============================================================
     *  3. VI — 每路 sensor 一个 VI pipe
     * ============================================================ */
    LMO_VI_S *vi[MAX_SENSOR] = {NULL};
    for (int s = 0; s < args.s32SensorCnt; s++) {
        LMO_VI_Params vip = {
            .devId = s, .pipeId = s, .chnId = 0,
            .width  = args.stSensor[s].u32Width,
            .height = args.stSensor[s].u32Height,
            .pixelFormat  = LMO_FMT_YUV420SP,
            .compressMode = LMO_COMPRESS_MODE_NONE,
            .srcFrameRate = args.stSensor[s].u32Fps,
            .dstFrameRate = args.stSensor[s].u32Fps,
            .ispBufCount  = 2,
            .ispMemoryType = LMO_VI_MEM_DMABUF,
            .ispGroupInit  = false,
        };
        vi[s] = LMO_COMM_VI_Create(&vip);
        if (!vi[s]) {
            printf("VI[%d] create failed\n", s);
            /* 回滚 */
            for (int i = 0; i < s; i++)
                LMO_COMM_VI_Destroy(vi[i]);
            goto exit_rtsp;
        }
        printf("VI[%d] pipe=%d OK\n", s, s);
    }

    /* ============================================================
     *  4. VPSS — 每路 sensor 一个 VPSS 组，每组 2 个子通道
     * ============================================================ */
    LMO_VPSS_S *vpss[MAX_SENSOR] = {NULL};
    for (int s = 0; s < args.s32SensorCnt; s++) {
        LMO_VPSS_Params vp = {
            .grpId = s, .chnId = 0,
            .pixelFormat   = LMO_FMT_YUV420SP,
            .compressMode  = LMO_COMPRESS_MODE_NONE,
            .vProcDevType  = LMO_VPSS_DEV_RGA,
            .srcFrameRate  = -1, .dstFrameRate = -1,
            .chn = {
                { .width  = args.stSensor[s].u32Width,
                  .height = args.stSensor[s].u32Height,
                  .pixelFormat = LMO_FMT_YUV420SP,
                  .compressMode = LMO_COMPRESS_MODE_NONE,
                  .srcFrameRate = -1, .dstFrameRate = -1 },
                { .width  = args.stSensor[s].u32SubWidth,
                  .height = args.stSensor[s].u32SubHeight,
                  .pixelFormat = LMO_FMT_YUV420SP,
                  .compressMode = LMO_COMPRESS_MODE_NONE,
                  .srcFrameRate = -1, .dstFrameRate = -1 },
            },
        };
        vpss[s] = LMO_COMM_VPSS_Create(&vp);
        if (!vpss[s]) {
            printf("VPSS[%d] create failed\n", s);
            for (int i = 0; i < s; i++)
                LMO_COMM_VPSS_Destroy(vpss[i]);
            goto exit_vi;
        }
        printf("VPSS[%d] OK (chn0=%ux%u chn1=%ux%u)\n",
               s,
               args.stSensor[s].u32Width, args.stSensor[s].u32Height,
               args.stSensor[s].u32SubWidth, args.stSensor[s].u32SubHeight);
    }

    /* ============================================================
     *  5. VENC — 每路 sensor 2 个编码通道（主 + 子）
     *     索引: sensor N → VENC chn = N*2 + 0 (主), N*2 + 1 (子)
     * ============================================================ */
    LMO_VENC_S *venc[MAX_SENSOR * VENC_PER_SENSOR] = {NULL};
    for (int s = 0; s < args.s32SensorCnt; s++) {
        /* 主码流 */
        int chn_main = s * VENC_PER_SENSOR + 0;
        LMO_VENC_Params vencp_main = {
            .chnId = chn_main,
            .width  = args.stSensor[s].u32Width,
            .height = args.stSensor[s].u32Height,
            .fps    = args.stSensor[s].u32Fps,
            .gop    = 50,
            .bitRate    = args.stSensor[s].s32BitRate,
            .codecType  = args.enCodecType,
            .rcMode     = args.enRcMode,
            .pixelFormat  = LMO_FMT_YUV420SP,
            .buffSize     = args.stSensor[s].u32Width * args.stSensor[s].u32Height,
            .enableBufShare = 1,
            .loopCount   = args.s32LoopCnt,
            .gopMode     = LMO_VENC_GOP_NORMALP,
            .profile     = (args.enCodecType == LMO_CODEC_H264) ? 100 : 0,
        };
        venc[chn_main] = LMO_COMM_VENC_Create(&vencp_main);
        if (!venc[chn_main]) {
            printf("VENC[%d] (s%d main) create failed\n", chn_main, s);
            for (int i = 0; i < chn_main; i++)
                LMO_COMM_VENC_Destroy(venc[i]);
            goto exit_vpss;
        }

        /* 子码流 */
        int chn_sub = s * VENC_PER_SENSOR + 1;
        LMO_VENC_Params vencp_sub = {
            .chnId = chn_sub,
            .width  = args.stSensor[s].u32SubWidth,
            .height = args.stSensor[s].u32SubHeight,
            .fps    = args.stSensor[s].u32Fps,
            .gop    = 50,
            .bitRate    = args.stSensor[s].s32SubBitRate,
            .codecType  = args.enCodecType,
            .rcMode     = args.enRcMode,
            .pixelFormat  = LMO_FMT_YUV420SP,
            .buffSize     = args.stSensor[s].u32SubWidth * args.stSensor[s].u32SubHeight,
            .enableBufShare = 1,
            .loopCount   = args.s32LoopCnt,
            .gopMode     = LMO_VENC_GOP_NORMALP,
            .profile     = (args.enCodecType == LMO_CODEC_H264) ? 100 : 0,
        };
        venc[chn_sub] = LMO_COMM_VENC_Create(&vencp_sub);
        if (!venc[chn_sub]) {
            printf("VENC[%d] (s%d sub) create failed\n", chn_sub, s);
            for (int i = 0; i <= chn_main; i++)
                LMO_COMM_VENC_Destroy(venc[i]);
            goto exit_vpss;
        }
        printf("VENC[%d] s%d main OK  VENC[%d] s%d sub OK\n",
               chn_main, s, chn_sub, s);
    }

    /* ============================================================
     *  6. 绑定
     *      VI(s) -> VPSS(s, chn0) -> VENC(s*2+0) 主
     *                              -> VENC(s*2+1) 子
     * ============================================================ */
    for (int s = 0; s < args.s32SensorCnt; s++) {
        /* VI(s, chn0) -> VPSS(s, chn0) */
        LMO_CHN_S vi_src   = { .modId = LMO_ID_VI,   .devId = s, .chnId = 0 };
        LMO_CHN_S vpss_dst = { .modId = LMO_ID_VPSS, .devId = s, .chnId = 0 };
        LMO_COMM_Bind(&vi_src, &vpss_dst);

        /* VPSS(s, chn0) -> VENC(s*2+0) 主码流 */
        LMO_CHN_S vpss0_src = { .modId = LMO_ID_VPSS, .devId = s, .chnId = 0 };
        LMO_CHN_S venc0_dst = { .modId = LMO_ID_VENC, .devId = 0, .chnId = s * VENC_PER_SENSOR + 0 };
        LMO_COMM_Bind(&vpss0_src, &venc0_dst);

        /* VPSS(s, chn1) -> VENC(s*2+1) 子码流 */
        LMO_CHN_S vpss1_src = { .modId = LMO_ID_VPSS, .devId = s, .chnId = 1 };
        LMO_CHN_S venc1_dst = { .modId = LMO_ID_VENC, .devId = 0, .chnId = s * VENC_PER_SENSOR + 1 };
        LMO_COMM_Bind(&vpss1_src, &venc1_dst);

        printf("Bind: VI[%d] -> VPSS[%d] -> VENC[%d/%d] done\n",
               s, s, s * VENC_PER_SENSOR + 0, s * VENC_PER_SENSOR + 1);
    }

    /* ============================================================
     *  7. 启动取流线程 + 监控线程
     * ============================================================ */
    for (int s = 0; s < args.s32SensorCnt; s++) {
        int chn_main = s * VENC_PER_SENSOR + 0;
        int chn_sub  = s * VENC_PER_SENSOR + 1;

        VencThreadArg targ_main = { .venc = venc[chn_main],
                                    .session = g_rtsp_session[chn_main] };
        pthread_create(&g_venc_tid[chn_main], NULL, venc_get_stream, &targ_main);

        VencThreadArg targ_sub  = { .venc = venc[chn_sub],
                                    .session = g_rtsp_session[chn_sub] };
        pthread_create(&g_venc_tid[chn_sub], NULL, venc_get_stream, &targ_sub);
    }

    /* 仅一个监控线程轮流查所有 sensor 的 proc 节点 */
    pthread_create(&g_monitor_tid, NULL, monitor_thread, &args.s32SensorCnt);

    printf("\n==== 双摄 AIISP 运行中 ====\n");
    for (int s = 0; s < args.s32SensorCnt; s++) {
        printf("  sensor%d main: rtsp://<板子IP>:554/live/%d\n",
               s, s * VENC_PER_SENSOR + 0);
        printf("  sensor%d sub:  rtsp://<板子IP>:554/live/%d\n",
               s, s * VENC_PER_SENSOR + 1);
    }
    printf("遮住镜头或暗光，观察 [monitor_sX] frame_id 递增确认 AIBNR 激活\n");
    printf("Ctrl-C 退出\n\n");

    while (!g_quit)
        sleep(1);

    /* ============================================================
     *  8. 清理
     * ============================================================ */
    printf("\n--- cleaning up ---\n");
    g_venc_quit = 1;

    /* join 所有线程 */
    pthread_join(g_monitor_tid, NULL);
    for (int s = 0; s < args.s32SensorCnt; s++) {
        pthread_join(g_venc_tid[s * VENC_PER_SENSOR + 0], NULL);
        pthread_join(g_venc_tid[s * VENC_PER_SENSOR + 1], NULL);
    }

    /* 解绑定 */
    for (int s = 0; s < args.s32SensorCnt; s++) {
        LMO_CHN_S vpss1_src = { .modId = LMO_ID_VPSS, .devId = s, .chnId = 1 };
        LMO_CHN_S venc1_dst = { .modId = LMO_ID_VENC, .devId = 0, .chnId = s * VENC_PER_SENSOR + 1 };
        LMO_COMM_UnBind(&vpss1_src, &venc1_dst);

        LMO_CHN_S vpss0_src = { .modId = LMO_ID_VPSS, .devId = s, .chnId = 0 };
        LMO_CHN_S venc0_dst = { .modId = LMO_ID_VENC, .devId = 0, .chnId = s * VENC_PER_SENSOR + 0 };
        LMO_COMM_UnBind(&vpss0_src, &venc0_dst);

        LMO_CHN_S vi_src   = { .modId = LMO_ID_VI,   .devId = s, .chnId = 0 };
        LMO_CHN_S vpss_dst = { .modId = LMO_ID_VPSS, .devId = s, .chnId = 0 };
        LMO_COMM_UnBind(&vi_src, &vpss_dst);
    }

    /* 销毁 VENC */
    for (int i = 0; i < args.s32SensorCnt * VENC_PER_SENSOR; i++)
        LMO_COMM_VENC_Destroy(venc[i]);

exit_vpss:
    /* 销毁 VPSS */
    for (int s = 0; s < args.s32SensorCnt; s++)
        LMO_COMM_VPSS_Destroy(vpss[s]);

exit_vi:
    /* 销毁 VI */
    for (int s = 0; s < args.s32SensorCnt; s++)
        LMO_COMM_VI_Destroy(vi[s]);

exit_rtsp:
    rtsp_deinit();
    RK_MPI_SYS_Exit();

    /* 停止 ISP */
    for (int s = 0; s < args.s32SensorCnt; s++) {
        LMO_ISP_Stop(s);
        printf("ISP[%d] stopped\n", s);
    }

    printf("aiisp dual-sensor exit\n");
    return 0;
}
