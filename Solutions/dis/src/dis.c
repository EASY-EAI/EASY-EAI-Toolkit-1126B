/*
 * GDC DIS (Digital Image Stabilization) Solution with RTSP streaming
 *
 * Pipeline: VI -> GDC(DIS) -> VENC -> RTSP
 *
 * Uses lmo_adapter API (LMO_COMM_*) for media pipeline management.
 * FEC hardware acceleration is used for DIS mesh transformation.
 *
 * Usage:
 *   ./dis -w 2688 -h 1520 -a /etc/iqfiles/ -r ./sc450ai_CRK4F4209_dis.json
 *
 * Preview with VLC:
 *   vlc rtsp://<board_ip>:554/live/0
 */
#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* End of #ifdef __cplusplus */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>

#include "lmo_common.h"
#include "rtsp_demo.h"
#include <rk_mpi_sys.h>

#define RTSP_PORT    554
#define RTSP_PATH    "/live/0"

/* VI channel: 3 = VPSS online path (required by FEC on RV1126B) */
#define VI_DEV_ID    0
#define VI_CHN_ID    3
#define GDC_CHN_ID   0
#define VENC_CHN_ID  0

/* ---- Global state ---- */
static volatile int g_bQuit = 0;
static rtsp_demo_handle g_rtsplive = NULL;
static rtsp_session_handle g_rtsp_session = NULL;
pthread_mutex_t g_rtsp_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Context for cleanup */
typedef struct {
    LMO_VI_S   *vi;
    LMO_GDC_S  *gdc;
    LMO_VENC_S *venc;
    int         viDevId;
    int         viChnId;
    int         gdcChnId;
    int         vencChnId;
} MPI_CTX;

/* ---- VENC thread: get encoded stream and send to RTSP ---- */
typedef struct {
    LMO_VENC_S         *venc;
    int                 chnId;
    rtsp_session_handle rtspSession;
} VencThreadArg;

static void *venc_get_stream(void *pArgs) {
    VencThreadArg *ta = (VencThreadArg *)pArgs;
    prctl(PR_SET_NAME, "venc_rtsp_thread");
    void *pData = NULL;
    int loopCount = 0;

    while (!g_bQuit) {
        if (LMO_COMM_VENC_GetStream(ta->venc, &pData) != LMO_SUCCESS) {
            usleep(1000);
            continue;
        }

        LMO_U32 len = LMO_COMM_VENC_GetStreamSize(ta->venc);
        LMO_U64 pts = LMO_COMM_VENC_GetStreamPts(ta->venc);

        pthread_mutex_lock(&g_rtsp_mutex);
        if (ta->rtspSession) {
            rtsp_tx_video(ta->rtspSession, pData, len, pts);
            rtsp_do_event(g_rtsplive);
        }
        pthread_mutex_unlock(&g_rtsp_mutex);

        LMO_COMM_VENC_ReleaseStream(ta->venc);
        loopCount++;

        if (loopCount % 60 == 0) {
            printf("[DIS VENC RTSP] streaming... frame=%d, pts=%llu\n",
                   loopCount, (unsigned long long)pts);
        }
    }

    printf("venc_get_stream exit, total frames: %d\n", loopCount);
    return NULL;
}

static void sigterm_handler(int sig) {
    fprintf(stderr, "signal %d\n", sig);
    g_bQuit = 1;
}

static void handle_pipe(int sig) {
    printf("sigaction will ignore signal %d\n", sig);
}

/* ---- Command-line options ---- */
static char optstr[] = "?a:w:h:f:r:e:b:P:v:";
static const struct option long_options[] = {
    {"aiq",       optional_argument, NULL, 'a'},
    {"width",     required_argument, NULL, 'w'},
    {"height",    required_argument, NULL, 'h'},
    {"fps",       required_argument, NULL, 'f'},
    {"dis_config", required_argument, NULL, 'r'},
    {"encode",    required_argument, NULL, 'e'},
    {"bitrate",   required_argument, NULL, 'b'},
    {"rtsp_port", required_argument, NULL, 'P'},
    {"vi_buffcnt", required_argument, NULL, 'v'},
    {"help",      optional_argument, NULL, '?'},
    {NULL, 0, NULL, 0},
};

static void print_usage(const char *name) {
    printf("usage example:\n");
    printf("\t%s -w 2688 -h 1520 -a /etc/iqfiles/ -r ./sc450ai_CRK4F4209_dis.json\n", name);
    printf("\trtsp://xx.xx.xx.xx:554/live/0\n");
    printf("\t-a | --aiq: enable aiq with dirpath, eg:-a /etc/iqfiles/, Default: /etc/iqfiles/\n");
    printf("\t-w | --width: video width, Default: 2688\n");
    printf("\t-h | --height: video height, Default: 1520\n");
    printf("\t-f | --fps: video fps, Default: 30\n");
    printf("\t-r | --dis_config: DIS config JSON file (REQUIRED)\n");
    printf("\t-e | --encode: encode type, h265cbr/h265vbr/h264cbr/h264vbr, Default: h265cbr\n");
    printf("\t-b | --bitrate: bitrate in Kbps, Default: 10240\n");
    printf("\t-P | --rtsp_port: RTSP port, Default: 554\n");
    printf("\t-v | --vi_buffcnt: VI buffer count, Default: 4\n");
}

int main(int argc, char *argv[]) {
    MPI_CTX ctx;
    memset(&ctx, 0, sizeof(ctx));
    int s32Ret = LMO_SUCCESS;

    /* Default params */
    unsigned int u32Width   = 2688;
    unsigned int u32Height  = 1520;
    unsigned int u32Fps     = 30;
    unsigned int u32BitRate = 10 * 1024;  /* Kbps */
    unsigned int u32ViBufCnt = 12;
    int rtsp_port = RTSP_PORT;
    char *pIqFileDir = "/etc/iqfiles/";
    char *pDisConfigFile = NULL;
    LMO_CODEC_TYPE_E enCodecType = LMO_CODEC_H265;
    LMO_RC_MODE_E enRcMode = LMO_RC_MODE_H265CBR;
    char *pCodecName = "H265";

    if (argc < 2) { print_usage(argv[0]); return 0; }

    signal(SIGINT, sigterm_handler);
    signal(SIGTERM, sigterm_handler);
    struct sigaction action;
    action.sa_handler = handle_pipe;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGPIPE, &action, NULL);

    int c;
    while ((c = getopt_long(argc, argv, optstr, long_options, NULL)) != -1) {
        const char *tmp_optarg = optarg;
        switch (c) {
        case 'a':
            if (!optarg && NULL != argv[optind] && '-' != argv[optind][0])
                tmp_optarg = argv[optind++];
            if (tmp_optarg) pIqFileDir = (char *)tmp_optarg;
            else pIqFileDir = NULL;
            break;
        case 'w': u32Width = atoi(optarg); break;
        case 'h': u32Height = atoi(optarg); break;
        case 'f': u32Fps = atoi(optarg); break;
        case 'r': pDisConfigFile = optarg; break;
        case 'e':
            if (!strcmp(optarg, "h264cbr")) {
                enCodecType = LMO_CODEC_H264; enRcMode = LMO_RC_MODE_H264CBR;
                pCodecName = "H264";
            } else if (!strcmp(optarg, "h264vbr")) {
                enCodecType = LMO_CODEC_H264; enRcMode = LMO_RC_MODE_H264VBR;
                pCodecName = "H264";
            } else if (!strcmp(optarg, "h265cbr")) {
                enCodecType = LMO_CODEC_H265; enRcMode = LMO_RC_MODE_H265CBR;
                pCodecName = "H265";
            } else if (!strcmp(optarg, "h265vbr")) {
                enCodecType = LMO_CODEC_H265; enRcMode = LMO_RC_MODE_H265VBR;
                pCodecName = "H265";
            } else {
                printf("ERROR: Invalid encoder type: %s\n", optarg);
                print_usage(argv[0]);
                return -1;
            }
            break;
        case 'b': u32BitRate = atoi(optarg); break;
        case 'P': rtsp_port = atoi(optarg); break;
        case 'v': u32ViBufCnt = atoi(optarg); break;
        case '?':
        default:
            print_usage(argv[0]);
            return 0;
        }
    }

    if (!pDisConfigFile) {
        printf("ERROR: DIS config file is required (-r option)\n");
        printf("Example: -r ./sc450ai_CRK4F4209_dis.json\n");
        return -1;
    }

    printf("#CodecName: %s\n", pCodecName);
    printf("#Resolution: %ux%u\n", u32Width, u32Height);
    printf("#FPS: %u\n", u32Fps);
    printf("#Bitrate: %u Kbps\n", u32BitRate);
    printf("#DIS Config: %s\n", pDisConfigFile);
    printf("#IQ Path: %s\n", pIqFileDir);
    printf("#RTSP: rtsp://<ip>:%d%s\n\n", rtsp_port, RTSP_PATH);

    /* ---- SYS Init ---- */
    if (RK_MPI_SYS_Init() != LMO_SUCCESS) {
        printf("RK_MPI_SYS_Init failed\n");
        goto __FAILED;
    }

    /* ---- ISP Init ---- */
    if (pIqFileDir) {
        s32Ret = LMO_ISP_Init(0, LMO_AIQ_WORKING_MODE_NORMAL, false, pIqFileDir);
        if (s32Ret != LMO_SUCCESS) {
            printf("ISP init failed: %d\n", s32Ret);
            goto __FAILED;
        }
        s32Ret = LMO_ISP_SetFrameRate(0, u32Fps);
        if (s32Ret != LMO_SUCCESS)
            printf("WARN: ISP set frame rate failed: %d\n", s32Ret);
        s32Ret = LMO_ISP_Run(0);
        if (s32Ret != LMO_SUCCESS) {
            printf("ISP run failed: %d\n", s32Ret);
            goto __FAILED_ISP;
        }
    }

    /* ---- RTSP Init ---- */
    g_rtsplive = create_rtsp_demo(rtsp_port);
    if (!g_rtsplive) {
        printf("create_rtsp_demo failed\n");
        goto __FAILED_ISP;
    }
    g_rtsp_session = rtsp_new_session(g_rtsplive, RTSP_PATH);
    if (!g_rtsp_session) {
        printf("rtsp_new_session failed\n");
        goto __FAILED_RTSP;
    }
    int codec_id = (enCodecType == LMO_CODEC_H264)
                   ? RTSP_CODEC_ID_VIDEO_H264
                   : RTSP_CODEC_ID_VIDEO_H265;
    rtsp_set_video(g_rtsp_session, codec_id, NULL, 0);
    rtsp_sync_video_ts(g_rtsp_session, rtsp_get_reltime(), rtsp_get_ntptime());
    printf("RTSP server started: rtsp://<ip>:%d%s (%s)\n",
           rtsp_port, RTSP_PATH, pCodecName);

    /* ---- VI Init (VPSS online path, chn 3) ----
     * VI_EXT_CHN_MODE is set internally by lmo_adapter (#ifdef RV1126B).
     * TILE_4x4 video format is required by FEC hardware for DIS. */
    LMO_VI_Params viParams = {
        .devId         = VI_DEV_ID,
        .pipeId        = VI_DEV_ID,
        .chnId         = VI_CHN_ID,
        .width         = u32Width,
        .height        = u32Height,
        .pixelFormat   = LMO_FMT_YUV420SP,
        .compressMode  = LMO_COMPRESS_MODE_NONE,
        .videoFormat   = LMO_VIDEO_FORMAT_TILE_4x4,  /* TILE format for FEC */
        .wrapIfEnable  = false,
        .srcFrameRate  = -1,
        .dstFrameRate  = -1,
        .ispBufCount   = u32ViBufCnt,
        .ispMemoryType = LMO_VI_MEM_DMABUF,
        .depth         = 0,
    };
    ctx.vi = LMO_COMM_VI_Create(&viParams);
    if (!ctx.vi) {
        printf("VI create failed\n");
        goto __FAILED_RTSP;
    }
    ctx.viDevId = VI_DEV_ID;
    ctx.viChnId = VI_CHN_ID;
    printf("VI init success: %ux%u (chn=%d, TILE_4x4)\n", u32Width, u32Height, VI_CHN_ID);

    /* ---- GDC DIS Init ---- */
    LMO_GDC_Params gdcParams = {
        .chnId         = GDC_CHN_ID,
        .maxInQueue    = 11,
        .maxOutQueue   = 4,
        .dstWidth      = (LMO_S32)u32Width,
        .dstHeight     = (LMO_S32)u32Height,
        .mode          = LMO_GDC_MODE_DIS,
        .iioDevNo      = 0,
        .dstCompMode   = LMO_COMPRESS_RFBC_64x4,
        .dstPixelFormat = LMO_FMT_YUV420SP,
        .depth         = 0,
        .cfgFile       = pDisConfigFile,
        .sensorCb      = NULL,
        .vframeCb      = NULL,
    };
    ctx.gdc = LMO_COMM_GDC_Create(&gdcParams);
    if (!ctx.gdc) {
        printf("GDC DIS create failed\n");
        goto __FAILED_VI;
    }
    ctx.gdcChnId = GDC_CHN_ID;
    printf("GDC DIS init success: %ux%u\n", u32Width, u32Height);

    /* ---- VENC Init ---- */
    LMO_VENC_Params vencParams = {
        .chnId         = VENC_CHN_ID,
        .width         = u32Width,
        .height        = u32Height,
        .fps           = u32Fps,
        .gop           = u32Fps * 2,
        .bitRate       = u32BitRate,
        .codecType     = enCodecType,
        .rcMode        = enRcMode,
        .pixelFormat   = LMO_FMT_YUV420SP,
        .streamBufCnt  = 5,
        .buffSize      = u32Width * u32Height * 3 / 2,
        .enableBufShare = 0,
        .loopCount     = -1,
        .gopMode       = LMO_VENC_GOP_NORMALP,
        .profile       = (enCodecType == LMO_CODEC_H264) ? 100 : 0,
    };
    ctx.venc = LMO_COMM_VENC_Create(&vencParams);
    if (!ctx.venc) {
        printf("VENC create failed\n");
        goto __FAILED_GDC;
    }
    ctx.vencChnId = VENC_CHN_ID;
    printf("VENC init success: %s %ux%u\n", pCodecName, u32Width, u32Height);

    /* ---- Bind: VI -> GDC -> VENC ---- */
    {
        LMO_CHN_S src = { .modId = LMO_ID_VI,  .devId = VI_DEV_ID,  .chnId = VI_CHN_ID };
        LMO_CHN_S dst = { .modId = LMO_ID_GDC, .devId = 0,           .chnId = GDC_CHN_ID };
        s32Ret = LMO_COMM_Bind(&src, &dst);
        if (s32Ret != LMO_SUCCESS) {
            printf("Bind VI->GDC failed: %d\n", s32Ret);
            goto __FAILED_VENC;
        }
    }
    {
        LMO_CHN_S src = { .modId = LMO_ID_GDC, .devId = 0,           .chnId = GDC_CHN_ID };
        LMO_CHN_S dst = { .modId = LMO_ID_VENC, .devId = 0,          .chnId = VENC_CHN_ID };
        s32Ret = LMO_COMM_Bind(&src, &dst);
        if (s32Ret != LMO_SUCCESS) {
            printf("Bind GDC->VENC failed: %d\n", s32Ret);
            goto __FAILED_BIND_VI_GDC;
        }
    }
    printf("Pipeline bind success: VI -> GDC(DIS) -> VENC\n");

    /* ---- Start VENC thread ---- */
    VencThreadArg ta = {
        .venc = ctx.venc,
        .chnId = VENC_CHN_ID,
        .rtspSession = g_rtsp_session,
    };
    pthread_t venc_tid;
    pthread_create(&venc_tid, NULL, venc_get_stream, &ta);

    printf("\n=== DIS streaming running, press Ctrl+C to stop ===\n");
    printf("RTSP: rtsp://<ip>:%d%s\n\n", rtsp_port, RTSP_PATH);

    while (!g_bQuit) sleep(1);

    printf("\nShutting down...\n");

    /* ---- Cleanup ---- */
    pthread_join(venc_tid, NULL);

    /* Unbind GDC -> VENC */
    {
        LMO_CHN_S src = { .modId = LMO_ID_GDC, .devId = 0,           .chnId = GDC_CHN_ID };
        LMO_CHN_S dst = { .modId = LMO_ID_VENC, .devId = 0,          .chnId = VENC_CHN_ID };
        LMO_COMM_UnBind(&src, &dst);
    }
__FAILED_BIND_VI_GDC:
    /* Unbind VI -> GDC */
    {
        LMO_CHN_S src = { .modId = LMO_ID_VI,  .devId = VI_DEV_ID,  .chnId = VI_CHN_ID };
        LMO_CHN_S dst = { .modId = LMO_ID_GDC, .devId = 0,           .chnId = GDC_CHN_ID };
        LMO_COMM_UnBind(&src, &dst);
    }
__FAILED_VENC:
    if (ctx.venc) { LMO_COMM_VENC_Destroy(ctx.venc); ctx.venc = NULL; }
__FAILED_GDC:
    if (ctx.gdc) { LMO_COMM_GDC_Destroy(ctx.gdc); ctx.gdc = NULL; }
__FAILED_VI:
    if (ctx.vi) { LMO_COMM_VI_Destroy(ctx.vi); ctx.vi = NULL; }
__FAILED_RTSP:
    if (g_rtsp_session) { rtsp_del_session(g_rtsp_session); g_rtsp_session = NULL; }
    if (g_rtsplive) { rtsp_del_demo(g_rtsplive); g_rtsplive = NULL; }
__FAILED_ISP:
    if (pIqFileDir) LMO_ISP_Stop(0);
__FAILED:
    RK_MPI_SYS_Exit();
    printf("DIS solution exited.\n");
    return 0;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */
