/*
 * main.c — rv1126b 单摄 AIISP 验证 + RTSP 预览（主码流 + 子码流）
 *
 * 路径B：纯 rkaiq AIBNR，不调用 RK_MPI_VPSS_SetGrpAIISPAttr
 * 管线：VI(0) -> VPSS(0, chn0) -> VENC(0)  RTSP: rtsp://<ip>:554/live/0
 *                (chn1) -> VENC(1)  RTSP: rtsp://<ip>:554/live/1
 *
 * AIBNR 受 ISO 阈值(isoIdx3)控制，遮住镜头/暗光下才激活。
 * 激活标志：rkaiq 日志 "switch to aiisp mode" + monitor frame_id 递增。
 */

#include "rtsp_demo.h"
#include "sample_comm.h"
#include "sample_comm_isp.h"
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>

#define AIISP_PROC_NODE "/proc/rkaiisp-vir0"

/* ---------- RTSP ---------- */
static rtsp_demo_handle    g_rtsplive         = NULL;
static rtsp_session_handle g_rtsp_session     = NULL;
static rtsp_session_handle g_rtsp_session_sub = NULL;
static pthread_mutex_t     g_rtsp_mutex       = PTHREAD_MUTEX_INITIALIZER;
static int                 g_rtsp_ready       = 0;

/* ---------- 状态 ---------- */
static volatile int g_quit      = 0;
static volatile int g_venc_quit = 0;
static pthread_t    g_monitor_tid;
static pthread_t    g_venc_tid;
static pthread_t    g_venc_tid_sub;

/* ---- VENC 线程参数（区分主/子码流 RTSP session） ---- */
typedef struct {
    SAMPLE_VENC_CTX_S *ctx;
    rtsp_session_handle session;
} VencThreadArg;

typedef struct {
    unsigned int          u32Width;
    unsigned int          u32Height;
    unsigned int          u32Fps;
    int                   s32BitRate;
    unsigned int          u32SubWidth;
    unsigned int          u32SubHeight;
    int                   s32SubBitRate;
    int                   s32CamId;
    char                 *pIqFileDir;
    int                   s32LoopCnt;
    CODEC_TYPE_E          enCodecType;
    VENC_RC_MODE_E        enRcMode;
    rk_aiq_working_mode_t eHdrMode;
} TestArgs;

static void sigterm_handler(int sig) {
    fprintf(stderr, "\ncatch signal %d, exiting...\n", sig);
    g_quit = 1;
    g_venc_quit = 1;
}

/* ---- RTSP ---- */
static int rtsp_init(CODEC_TYPE_E enCodecType) {
    g_rtsplive = create_rtsp_demo(554);
    if (!g_rtsplive) {
        printf("[rtsp] create_rtsp_demo failed\n");
        return -1;
    }
    /* 主码流 /live/0 */
    g_rtsp_session = rtsp_new_session(g_rtsplive, "/live/0");
    int codec_id = (enCodecType == RK_CODEC_TYPE_H264)
                   ? RTSP_CODEC_ID_VIDEO_H264
                   : RTSP_CODEC_ID_VIDEO_H265;
    rtsp_set_video(g_rtsp_session, codec_id, NULL, 0);
    rtsp_sync_video_ts(g_rtsp_session, rtsp_get_reltime(), rtsp_get_ntptime());

    /* 子码流 /live/1 */
    g_rtsp_session_sub = rtsp_new_session(g_rtsplive, "/live/1");
    rtsp_set_video(g_rtsp_session_sub, codec_id, NULL, 0);
    rtsp_sync_video_ts(g_rtsp_session_sub, rtsp_get_reltime(), rtsp_get_ntptime());

    g_rtsp_ready = 1;
    printf("[rtsp] server ready  rtsp://<board-ip>:554/live/0 (main)\n");
    printf("[rtsp]                rtsp://<board-ip>:554/live/1 (sub)\n");
    return 0;
}

static void rtsp_deinit(void) {
    g_rtsp_ready = 0;
    if (g_rtsplive)
        rtsp_del_demo(g_rtsplive);
    g_rtsplive = NULL;
    g_rtsp_session = NULL;
    g_rtsp_session_sub = NULL;
}

/* ---- 监控线程 ---- */
static void *monitor_thread(void *arg) {
    (void)arg;
    prctl(PR_SET_NAME, "aiisp_monitor");
    int last_frame_id = -1, loop = 0;
    while (!g_quit) {
        FILE *fp = fopen(AIISP_PROC_NODE, "r");
        if (!fp) {
            printf("[monitor] %s 打不开 — 内核未注册 aiisp 设备\n", AIISP_PROC_NODE);
            sleep(2);
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
        int running = (frame_id != last_frame_id && frame_id > 0);
        printf("[monitor #%02d] run_idx=%d frame_id=%d frm_rate=%d "
               "algo=%d(%s) hw_state=%d img=%dx%d  => %s\n",
               loop, run_idx, frame_id, frm_rate,
               exe_algo, algo_name, hw_state, img_w, img_h,
               running ? "AIISP 正在推理 ✓" : "未推理(ISO未达阈值)");
        last_frame_id = frame_id;
        loop++;
        sleep(2);
    }
    printf("[monitor] exit\n");
    return NULL;
}

/* ---- VENC 取流线程（主/子码流共用，通过 VencThreadArg 区分 RTSP session） ---- */
static void *venc_get_stream(void *arg) {
    VencThreadArg *targ = (VencThreadArg *)arg;
    SAMPLE_VENC_CTX_S *ctx = targ->ctx;
    rtsp_session_handle session = targ->session;
    prctl(PR_SET_NAME, "venc_get_stream");

    int cnt = 0;
    while (!g_venc_quit) {
        RK_S32 ret = RK_MPI_VENC_GetStream(ctx->s32ChnId, &ctx->stFrame, 500);
        if (ret != RK_SUCCESS)
            continue;

        RK_VOID *pData = RK_MPI_MB_Handle2VirAddr(ctx->stFrame.pstPack->pMbBlk);
        RK_U32   len   = ctx->stFrame.pstPack->u32Len;
        RK_U64   pts   = ctx->stFrame.pstPack->u64PTS;

        if (g_rtsp_ready && pData) {
            pthread_mutex_lock(&g_rtsp_mutex);
            rtsp_tx_video(session, pData, len, pts);
            rtsp_do_event(g_rtsplive);
            pthread_mutex_unlock(&g_rtsp_mutex);
        }

        RK_MPI_VENC_ReleaseStream(ctx->s32ChnId, &ctx->stFrame);
        cnt++;
        if (ctx->s32loopCount > 0 && cnt >= ctx->s32loopCount) {
            g_quit = 1;
            break;
        }
    }
    RK_LOGI("venc_get_stream[%d] exit, got %d frames", ctx->s32ChnId, cnt);
    return NULL;
}

static void print_usage(const char *name) {
    printf("Usage: %s [options]\n", name);
    printf("  -w <width>   sensor width     (default 2688)\n");
    printf("  -h <height>  sensor height    (default 1520)\n");
    printf("  -a <iqdir>   iqfiles dir      (default /etc/iqfiles/)\n");
    printf("  -e h264cbr|h265cbr (default h265cbr)\n");
    printf("  -b <kbps>    main bitrate     (default 4096)\n");
    printf("  -W <width>   sub width        (default 640)\n");
    printf("  -H <height>  sub height       (default 360)\n");
    printf("  -B <kbps>    sub bitrate      (default 512)\n");
    printf("  -l <N>       exit after N frames (-1=forever)\n");
    printf("\nRTSP:\n");
    printf("  main: rtsp://<board-ip>:554/live/0  (main stream)\n");
    printf("  sub:  rtsp://<board-ip>:554/live/1  (sub stream)\n");
    printf("AIBNR 激活条件: ISO > isoIdx3 (遮住镜头或暗光)\n");
}

int main(int argc, char *argv[]) {
    TestArgs args = {
        .u32Width     = 2688,
        .u32Height    = 1520,
        .u32Fps       = 25,
        .s32BitRate   = 4096,
        .u32SubWidth  = 640,
        .u32SubHeight = 360,
        .s32SubBitRate = 512,
        .s32CamId     = 0,
        .pIqFileDir   = "/etc/iqfiles/",
        .s32LoopCnt   = -1,
        .enCodecType  = RK_CODEC_TYPE_H265,
        .enRcMode     = VENC_RC_MODE_H265CBR,
        .eHdrMode     = RK_AIQ_WORKING_MODE_NORMAL,
    };

    int c;
    while ((c = getopt(argc, argv, "w:h:a:e:b:W:H:B:l:?")) != -1) {
        switch (c) {
        case 'w': args.u32Width    = (unsigned)atoi(optarg); break;
        case 'h': args.u32Height   = (unsigned)atoi(optarg); break;
        case 'a': args.pIqFileDir  = optarg;                 break;
        case 'b': args.s32BitRate  = atoi(optarg);           break;
        case 'W': args.u32SubWidth  = (unsigned)atoi(optarg); break;
        case 'H': args.u32SubHeight = (unsigned)atoi(optarg); break;
        case 'B': args.s32SubBitRate = atoi(optarg);          break;
        case 'l': args.s32LoopCnt  = atoi(optarg);           break;
        case 'e':
            if (!strcmp(optarg, "h264cbr")) {
                args.enCodecType = RK_CODEC_TYPE_H264;
                args.enRcMode    = VENC_RC_MODE_H264CBR;
            } else {
                args.enCodecType = RK_CODEC_TYPE_H265;
                args.enRcMode    = VENC_RC_MODE_H265CBR;
            }
            break;
        default:
            print_usage(argv[0]);
            return 0;
        }
    }

    printf("==== aiisp (单摄 路径B / 纯 rkaiq AIBNR + RTSP) ====\n");
    printf("main=%ux%u  sub=%ux%u  fps=%u  iq=%s  codec=%s  "
           "main_br=%ukbps  sub_br=%ukbps\n",
           args.u32Width, args.u32Height,
           args.u32SubWidth, args.u32SubHeight,
           args.u32Fps, args.pIqFileDir,
           args.enCodecType == RK_CODEC_TYPE_H264 ? "H264" : "H265",
           args.s32BitRate, args.s32SubBitRate);

    signal(SIGINT,  sigterm_handler);
    signal(SIGTERM, sigterm_handler);

    /* 1. ISP init */
    RK_S32 ret = SAMPLE_COMM_ISP_Init(args.s32CamId, args.eHdrMode,
                                      RK_FALSE, args.pIqFileDir);
    if (ret != RK_SUCCESS) {
        printf("SAMPLE_COMM_ISP_Init failed: %#x\n", ret);
        return -1;
    }
    SAMPLE_COMM_ISP_SetFrameRate(args.s32CamId, args.u32Fps);
    ret = SAMPLE_COMM_ISP_Run(args.s32CamId);
    if (ret != RK_SUCCESS) {
        printf("SAMPLE_COMM_ISP_Run failed: %#x\n", ret);
        SAMPLE_COMM_ISP_Stop(args.s32CamId);
        return -1;
    }
    printf("ISP init/run OK\n");

    if (RK_MPI_SYS_Init() != RK_SUCCESS) {
        printf("RK_MPI_SYS_Init failed\n");
        SAMPLE_COMM_ISP_Stop(args.s32CamId);
        return -1;
    }

    /* 2. RTSP */
    rtsp_init(args.enCodecType);

    /* 3. VI */
    SAMPLE_VI_CTX_S vi;
    memset(&vi, 0, sizeof(vi));
    vi.u32Width  = args.u32Width;
    vi.u32Height = args.u32Height;
    vi.s32DevId  = 0;
    vi.u32PipeId = 0;
    vi.s32ChnId  = 0;
    vi.stChnAttr.stIspOpt.stMaxSize.u32Width  = args.u32Width;
    vi.stChnAttr.stIspOpt.stMaxSize.u32Height = args.u32Height;
    vi.stChnAttr.stIspOpt.u32BufCount         = 2;
    vi.stChnAttr.stIspOpt.enMemoryType        = VI_V4L2_MEMORY_TYPE_DMABUF;
    vi.stChnAttr.enPixelFormat                = RK_FMT_YUV420SP;
    vi.stChnAttr.enCompressMode               = COMPRESS_MODE_NONE;
    vi.stChnAttr.stFrameRate.s32SrcFrameRate  = args.u32Fps;
    vi.stChnAttr.stFrameRate.s32DstFrameRate  = args.u32Fps;
    if (SAMPLE_COMM_VI_CreateChn(&vi) != RK_SUCCESS) {
        printf("VI create failed\n");
        goto exit_rtsp;
    }

    /* 4. VPSS */
    SAMPLE_VPSS_CTX_S vpss;
    memset(&vpss, 0, sizeof(vpss));
    vpss.s32GrpId = 0;
    vpss.s32ChnId = 0;
    vpss.enVProcDevType = VIDEO_PROC_DEV_RGA;
    vpss.stGrpVpssAttr.enPixelFormat  = RK_FMT_YUV420SP;
    vpss.stGrpVpssAttr.enCompressMode = COMPRESS_MODE_NONE;
    /* VPSS chn 0 — 主码流（原始分辨率） */
    vpss.stVpssChnAttr[0].enChnMode      = VPSS_CHN_MODE_USER;
    vpss.stVpssChnAttr[0].enCompressMode = COMPRESS_MODE_NONE;
    vpss.stVpssChnAttr[0].enDynamicRange = DYNAMIC_RANGE_SDR8;
    vpss.stVpssChnAttr[0].enPixelFormat  = RK_FMT_YUV420SP;
    vpss.stVpssChnAttr[0].stFrameRate.s32SrcFrameRate = -1;
    vpss.stVpssChnAttr[0].stFrameRate.s32DstFrameRate = -1;
    vpss.stVpssChnAttr[0].u32Width  = args.u32Width;
    vpss.stVpssChnAttr[0].u32Height = args.u32Height;
    /* VPSS chn 1 — 子码流（缩小分辨率） */
    vpss.stVpssChnAttr[1].enChnMode      = VPSS_CHN_MODE_USER;
    vpss.stVpssChnAttr[1].enCompressMode = COMPRESS_MODE_NONE;
    vpss.stVpssChnAttr[1].enDynamicRange = DYNAMIC_RANGE_SDR8;
    vpss.stVpssChnAttr[1].enPixelFormat  = RK_FMT_YUV420SP;
    vpss.stVpssChnAttr[1].stFrameRate.s32SrcFrameRate = -1;
    vpss.stVpssChnAttr[1].stFrameRate.s32DstFrameRate = -1;
    vpss.stVpssChnAttr[1].u32Width  = args.u32SubWidth;
    vpss.stVpssChnAttr[1].u32Height = args.u32SubHeight;
    if (SAMPLE_COMM_VPSS_CreateChn(&vpss) != RK_SUCCESS) {
        printf("VPSS create failed\n");
        goto exit_vi;
    }

    /* 5. VENC — 主码流 (chn 0) */
    SAMPLE_VENC_CTX_S venc;
    memset(&venc, 0, sizeof(venc));
    venc.s32ChnId    = 0;
    venc.u32Width    = args.u32Width;
    venc.u32Height   = args.u32Height;
    venc.u32Fps      = args.u32Fps;
    venc.u32Gop      = 50;
    venc.u32BitRate  = args.s32BitRate;
    venc.enCodecType = args.enCodecType;
    venc.enRcMode    = args.enRcMode;
    venc.s32loopCount = args.s32LoopCnt;
    venc.u32BuffSize  = args.u32Width * args.u32Height / 2;
    venc.enable_buf_share = RK_TRUE;
    venc.stChnAttr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
    venc.stChnAttr.stVencAttr.u32Profile =
        (args.enCodecType == RK_CODEC_TYPE_H264) ? 100 : 0;
    if (SAMPLE_COMM_VENC_CreateChn(&venc) != RK_SUCCESS) {
        printf("VENC[0] create failed\n");
        goto exit_vpss;
    }
    /* getStreamCbFunc 为 NULL，CreateChn 不分配 pstPack，手动补 */
    venc.stFrame.pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S));
    if (!venc.stFrame.pstPack) {
        printf("malloc pstPack[0] failed\n");
        SAMPLE_COMM_VENC_DestroyChn(&venc);
        goto exit_vpss;
    }

    /* 6. VENC — 子码流 (chn 1) */
    SAMPLE_VENC_CTX_S venc_sub;
    memset(&venc_sub, 0, sizeof(venc_sub));
    venc_sub.s32ChnId    = 1;
    venc_sub.u32Width    = args.u32SubWidth;
    venc_sub.u32Height   = args.u32SubHeight;
    venc_sub.u32Fps      = args.u32Fps;
    venc_sub.u32Gop      = 50;
    venc_sub.u32BitRate  = args.s32SubBitRate;
    venc_sub.enCodecType = args.enCodecType;
    venc_sub.enRcMode    = args.enRcMode;
    venc_sub.s32loopCount = args.s32LoopCnt;
    venc_sub.u32BuffSize  = args.u32SubWidth * args.u32SubHeight / 2;
    venc_sub.enable_buf_share = RK_TRUE;
    venc_sub.stChnAttr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
    venc_sub.stChnAttr.stVencAttr.u32Profile =
        (args.enCodecType == RK_CODEC_TYPE_H264) ? 100 : 0;
    if (SAMPLE_COMM_VENC_CreateChn(&venc_sub) != RK_SUCCESS) {
        printf("VENC[1] create failed\n");
        goto exit_venc_main;
    }
    venc_sub.stFrame.pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S));
    if (!venc_sub.stFrame.pstPack) {
        printf("malloc pstPack[1] failed\n");
        SAMPLE_COMM_VENC_DestroyChn(&venc_sub);
        goto exit_venc_main;
    }

    /* 7. 绑定 VI -> VPSS(chn0|chn1) -> VENC(0|1) */
    MPP_CHN_S src, dst;
    src.enModId = RK_ID_VI;   src.s32DevId = 0; src.s32ChnId = 0;
    dst.enModId = RK_ID_VPSS; dst.s32DevId = 0; dst.s32ChnId = 0;
    SAMPLE_COMM_Bind(&src, &dst);

    src.enModId = RK_ID_VPSS; src.s32DevId = 0; src.s32ChnId = 0;
    dst.enModId = RK_ID_VENC; dst.s32DevId = 0; dst.s32ChnId = 0;
    SAMPLE_COMM_Bind(&src, &dst);

    src.enModId = RK_ID_VPSS; src.s32DevId = 0; src.s32ChnId = 1;
    dst.enModId = RK_ID_VENC; dst.s32DevId = 0; dst.s32ChnId = 1;
    SAMPLE_COMM_Bind(&src, &dst);

    /* 8. 启动取流线程 */
    VencThreadArg main_arg = { .ctx = &venc,     .session = g_rtsp_session };
    VencThreadArg sub_arg  = { .ctx = &venc_sub, .session = g_rtsp_session_sub };
    pthread_create(&g_venc_tid,    NULL, venc_get_stream, &main_arg);
    pthread_create(&g_venc_tid_sub, NULL, venc_get_stream, &sub_arg);
    pthread_create(&g_monitor_tid, NULL, monitor_thread,  NULL);

    printf("\n==== 运行中 ====\n");
    printf("VLC 预览:\n");
    printf("  main: rtsp://<板子IP>:554/live/0\n");
    printf("  sub:  rtsp://<板子IP>:554/live/1\n");
    printf("遮住镜头或暗光，观察 [monitor] frame_id 递增确认 AIBNR 激活\n");
    printf("Ctrl-C 退出\n\n");

    while (!g_quit)
        sleep(1);

    /* 9. 清理 */
    g_venc_quit = 1;
    pthread_join(g_venc_tid_sub, NULL);
    pthread_join(g_venc_tid,    NULL);
    pthread_join(g_monitor_tid, NULL);

    /* 解绑定（逆序） */
    src.enModId = RK_ID_VPSS; src.s32DevId = 0; src.s32ChnId = 1;
    dst.enModId = RK_ID_VENC; dst.s32DevId = 0; dst.s32ChnId = 1;
    SAMPLE_COMM_UnBind(&src, &dst);

    src.enModId = RK_ID_VPSS; src.s32DevId = 0; src.s32ChnId = 0;
    dst.enModId = RK_ID_VENC; dst.s32DevId = 0; dst.s32ChnId = 0;
    SAMPLE_COMM_UnBind(&src, &dst);

    src.enModId = RK_ID_VI;   src.s32DevId = 0; src.s32ChnId = 0;
    dst.enModId = RK_ID_VPSS; dst.s32DevId = 0; dst.s32ChnId = 0;
    SAMPLE_COMM_UnBind(&src, &dst);

    /* 子码流 VENC */
    SAMPLE_COMM_VENC_DestroyChn(&venc_sub);
    free(venc_sub.stFrame.pstPack);

exit_venc_main:
    /* 主码流 VENC */
    SAMPLE_COMM_VENC_DestroyChn(&venc);
    free(venc.stFrame.pstPack);
    venc.stFrame.pstPack = NULL;
exit_vpss:
    SAMPLE_COMM_VPSS_DestroyChn(&vpss);
exit_vi:
    SAMPLE_COMM_VI_DestroyChn(&vi);
exit_rtsp:
    rtsp_deinit();
    RK_MPI_SYS_Exit();
    SAMPLE_COMM_ISP_Stop(args.s32CamId);
    printf("aiisp exit\n");
    return 0;
}
