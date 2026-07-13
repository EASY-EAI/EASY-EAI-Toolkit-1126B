/*
 * Copyright 2023 Rockchip Electronics Co. LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
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
#include <rk_mpi_vi.h>

#define VI_NUM_MAX 8
#define VENC_NUM_MAX 2

rtsp_demo_handle g_rtsplive = NULL;
static rtsp_session_handle g_rtsp_session[VENC_NUM_MAX] = {NULL};

typedef struct {
    LMO_VI_S   *vi[VI_NUM_MAX];
    LMO_AVS_S  *avs;
    LMO_VENC_S *venc[VENC_NUM_MAX];
    LMO_RGN_S  *rgn;
    int         viCount;          /* number of VI instances */
    int         viDevId[VI_NUM_MAX];  /* remembered for bind */
    int         viPipeId[VI_NUM_MAX];
    int         viChnId[VI_NUM_MAX];
    int         viIspGroupInit[VI_NUM_MAX];
    int         avsGrpId;
    int         vencChnId[VENC_NUM_MAX];
    int         vencLoopCount[VENC_NUM_MAX];
    char       *vencDstPath;      /* copied for venc thread */
} MPI_CTX;

/* VENC thread argument */
typedef struct {
    LMO_VENC_S *venc;
    int         chnId;
    int         loopCount;
    char       *dstPath;
    rtsp_session_handle rtspSession;
} VencThreadArg;

static volatile int g_bMainThreadQuit  = 0;
static volatile int g_bVencThreadQuit[VENC_NUM_MAX] = {0};
static pthread_t    g_vencTid[VENC_NUM_MAX];
pthread_mutex_t g_rtsp_mutex = PTHREAD_MUTEX_INITIALIZER;

static void sigterm_handler(int sig) {
    fprintf(stderr, "signal %d\n", sig);
    g_bMainThreadQuit = 1;
}

static RK_CHAR optstr[] = "?::a::A:n:b:l:o:e:F:h:m:d:L:v:s:e:i:c:J:";
static const struct option long_options[] = {
    {"aiq", optional_argument, NULL, 'a'},
    {"calib_file_path", required_argument, NULL, 'A'},
    {"camera_num", required_argument, NULL, 'n'},
    {"bitrate", required_argument, NULL, 'b'},
    {"vi_size", required_argument, NULL, 'v' + 's'},
    {"avs_chn0_size", required_argument, NULL, 'a' + 's'},
    {"avs_chn1_size", required_argument, NULL, 'a' + 'v'},
    {"loop_count", required_argument, NULL, 'l'},
    {"output_path", required_argument, NULL, 'o'},
    {"encode", required_argument, NULL, 'e'},
    {"dstfps", required_argument, NULL, 'F'},
    {"hdr_mode", required_argument, NULL, 'h' + 'm'},
    {"stitch_distance", required_argument, NULL, 'd'},
    {"cam0_ldch_path", required_argument, NULL, 'L'},
    {"cam1_ldch_path", required_argument, NULL, 'L' + 'm'},
    {"set_ldch", required_argument, NULL, 'l' + 'd'},
    {"ispLaunchMode", required_argument, NULL, 'i'},
    {"input_bmp_path", required_argument, NULL, 'i' + 'b'},
    {"osd_display", required_argument, NULL, 'o' + 'd'},
    {"vi_chnid", required_argument, NULL, 'v' + 'i'},
    {"vi_buffcnt", required_argument, NULL, 'v' + 'c'},
    {"avs_mode_blend", required_argument, NULL, 'a' + 'm' + 'b'},
    {"avs_json_path", required_argument, NULL, 'J'},
    {"help", optional_argument, NULL, '?'},
    {NULL, 0, NULL, 0},
};

static void print_usage(const char *name) {
    printf("usage example:\n");
    printf("\t%s --vi_size 1920x1080 --avs_chn0_size 3840x1080 --avs_chn1_size 1920x544 "
           "-a /etc/iqfiles/ -e h265cbr -b 4096 -n 2\n", name);
    printf("\trtsp://xx.xx.xx.xx/live/0, Default OPEN\n");
#ifdef RKAIQ
    printf("\t-a | --aiq: enable aiq with dirpath provided, eg:-a /etc/iqfiles/\n");
#endif
    printf("\t-A | --calib_file_path: input file path of xxx.xml\n");
    printf("\t-n | --camera_num: camera number, Default 2\n");
    printf("\t-b | --bitrate: encode bitrate, Default 4096\n");
    printf("\t-l | --loop_count: loop count, Default -1\n");
    printf("\t-o | --output_path: encode output file path, Default NULL\n");
    printf("\t-e | --encode: encode type, Default:h264cbr\n");
    printf("\t-F | --dstfps: set venc output fps, Default: 15\n");
    printf("\t-i | --ispLaunchMode: 0:single 1:group, default:1\n");
    printf("\t--vi_size: set vi resolution WidthxHeight, default:1920x1080\n");
    printf("\t--avs_chn0_size: set avs chn0 resolution, default:3840x1080\n");
    printf("\t--avs_chn1_size: set avs chn1 resolution, default:1920x544\n");
    printf("\t--hdr_mode: 0:normal 1:HDR2 2:HDR3, Default:0\n");
    printf("\t--stitch_distance: set stitch distance, default:5.0(m)\n");
    printf("\t--cam0_ldch_path: cam0 ldch mesh path\n");
    printf("\t--cam1_ldch_path: cam1 ldch mesh path\n");
    printf("\t--set_ldch: -1:disable, 1:read_file, 2:read_buff, Default:2\n");
    printf("\t--input_bmp_path: set bmp path for osd, default:NULL\n");
    printf("\t--osd_display: 0:no-display, 1:display, default:1\n");
    printf("\t--vi_chnid: set vi channel id, default:1\n");
    printf("\t--vi_buffcnt: set vi buff cnt, default:2\n");
    printf("\t--avs_mode_blend: 0:blend, 1:no-blend-ver, 2:no-blend-hor, default:0\n");
    printf("\t-J | --avs_json_path: AVS json config path\n");
}

/* ---- VENC thread (LMO style) ---- */
static void *venc_get_stream(void *pArgs) {
    VencThreadArg *ta = (VencThreadArg *)pArgs;
    LMO_VENC_S *venc = ta->venc;
    void *pData = NULL;
    int loopCount = 0;
    FILE *fp = NULL;

    if (ta->dstPath) {
        char name[256];
        snprintf(name, sizeof(name), "/%s/venc_%d.bin", ta->dstPath, ta->chnId);
        fp = fopen(name, "wb");
    }

    while (!g_bVencThreadQuit[ta->chnId]) {
        if (LMO_COMM_VENC_GetStream(venc, &pData) != LMO_SUCCESS) {
            usleep(1000);
            continue;
        }

        LMO_U32 len = LMO_COMM_VENC_GetStreamSize(venc);
        LMO_U64 pts = LMO_COMM_VENC_GetStreamPts(venc);

        if (ta->loopCount > 0 && loopCount >= ta->loopCount) {
            LMO_COMM_VENC_ReleaseStream(venc);
            break;
        }

        if (fp) {
            fwrite(pData, 1, len, fp);
            fflush(fp);
        }
        pthread_mutex_lock(&g_rtsp_mutex);
        rtsp_tx_video(ta->rtspSession, pData, len, pts);
        rtsp_do_event(g_rtsplive);
        pthread_mutex_unlock(&g_rtsp_mutex);

        LMO_COMM_VENC_ReleaseStream(venc);
        loopCount++;
    }

    if (fp) fclose(fp);
    return NULL;
}

static void handle_pipe(int sig) {
    printf("sigaction will ignore signal %d\n", sig);
}

int main(int argc, char *argv[]) {
    MPI_CTX ctx;
    memset(&ctx, 0, sizeof(ctx));
    int s32Ret = LMO_SUCCESS;
    int bIfOsdDisplay = 0;
    unsigned int u32ViWidth = 1920;
    unsigned int u32ViHeight = 1080;
    unsigned int u32AvsChn0Width = 3840;
    unsigned int u32AvsChn0Height = 1080;
    unsigned int u32AvsChn1Width = 1920;
    unsigned int u32AvsChn1Height = 544;
    char *pAvsCalibFilePath = "/oem/usr/share/avs_calib/calib_file.xml";
    char *pAvsJsonFilePath = NULL;
    char *pOutPathVenc = NULL;
    char *pCam0LdchMeshPath = "/oem/usr/share/iqfiles/cam0_ldch_mesh.bin";
    char *pCam1LdchMeshPath = "/oem/usr/share/iqfiles/cam1_ldch_mesh.bin";
    char *pBmpPath = NULL;
    LMO_CODEC_TYPE_E enCodecType = LMO_CODEC_H264;
    LMO_RC_MODE_E enRcMode = LMO_RC_MODE_H264CBR;
    int hdr_mode = LMO_AIQ_WORKING_MODE_NORMAL;
    char *pCodecName = "H264";
    int s32CamNum = 2;
    int s32DstFps = 15;
    int s32loopCnt = -1;
    int s32BitRate = 4 * 1024;
    int s32CamGrpId = 0;
    int s32ViChnid = 1;
    int s32ViBuffCnt = 2;
    int s32AvsModeBlend = 0;
    int eGetLdchMode = 2;  /* RK_GET_LDCH_BY_BUFF */
    float fStitchDistance = 5;
    void *pLdchMeshData[VI_NUM_MAX] = {NULL};
    int bIfIspGroupInit = 1;

    if (argc < 2) { print_usage(argv[0]); return 0; }

    signal(SIGINT, sigterm_handler);
    struct sigaction action;
    action.sa_handler = handle_pipe;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGPIPE, &action, NULL);

#ifdef RKAIQ
    int bMultictx = 0;
#endif
    int c;
    char *iq_file_dir = "/etc/iqfiles/";
    while ((c = getopt_long(argc, argv, optstr, long_options, NULL)) != -1) {
        const char *tmp_optarg = optarg;
        switch (c) {
        case 'a':
            if (!optarg && NULL != argv[optind] && '-' != argv[optind][0])
                tmp_optarg = argv[optind++];
            if (tmp_optarg) iq_file_dir = (char *)tmp_optarg;
            else iq_file_dir = NULL;
            break;
        case 'A': pAvsCalibFilePath = optarg; break;
        case 'b': s32BitRate = atoi(optarg); break;
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
            }
            break;
        case 'n': s32CamNum = atoi(optarg); break;
        case 'l': s32loopCnt = atoi(optarg); break;
        case 'o': pOutPathVenc = optarg; break;
        case 'F': s32DstFps = atoi(optarg); break;
        case 'd': fStitchDistance = atof(optarg); break;
        case 'L': pCam0LdchMeshPath = optarg; break;
        case 'L' + 'm': pCam1LdchMeshPath = optarg; break;
        case 'v' + 's':
            u32ViWidth = atoi(optarg);
            tmp_optarg = strstr(optarg, "x");
            u32ViHeight = atoi(tmp_optarg + 1);
            break;
        case 'a' + 's':
            u32AvsChn0Width = atoi(optarg);
            tmp_optarg = strstr(optarg, "x");
            u32AvsChn0Height = atoi(tmp_optarg + 1);
            break;
        case 'a' + 'v':
            u32AvsChn1Width = atoi(optarg);
            tmp_optarg = strstr(optarg, "x");
            u32AvsChn1Height = atoi(tmp_optarg + 1);
            break;
        case 'h' + 'm':
            if (atoi(optarg) == 0) hdr_mode = LMO_AIQ_WORKING_MODE_NORMAL;
            else if (atoi(optarg) == 1) hdr_mode = LMO_AIQ_WORKING_MODE_ISP_HDR2;
            else if (atoi(optarg) == 2) hdr_mode = LMO_AIQ_WORKING_MODE_ISP_HDR3;
            break;
        case 'l' + 'd': eGetLdchMode = atoi(optarg); break;
        case 'i': bIfIspGroupInit = atoi(optarg); break;
        case 'i' + 'b': pBmpPath = optarg; break;
        case 'o' + 'd': bIfOsdDisplay = atoi(optarg); break;
        case 'v' + 'i': s32ViChnid = atoi(optarg); break;
        case 'v' + 'c': s32ViBuffCnt = atoi(optarg); break;
        case 'a' + 'm' + 'b': s32AvsModeBlend = atoi(optarg); break;
        case 'J': pAvsJsonFilePath = optarg; break;
        default: print_usage(argv[0]); return 0;
        }
    }

    printf("#CameraGrpIdx: %d\n", s32CamGrpId);
    printf("#CodecName:%s\n", pCodecName);
    printf("#Output Path: %s\n", pOutPathVenc);
    printf("#IQ Path: %s\n", iq_file_dir);
    printf("#fStitchDistance: %f\n", fStitchDistance);

    if (eGetLdchMode == 1 && pCam0LdchMeshPath && pCam1LdchMeshPath) {
        pLdchMeshData[0] = pCam0LdchMeshPath;
        pLdchMeshData[1] = pCam1LdchMeshPath;
    }

    if (RK_MPI_SYS_Init() != 0) goto __FAILED;

    /* ---- AVS ---- */
    LMO_AVS_Params avsParams = {
        .grpId = 0, .chnId = 0,
        .srcWidth = u32ViWidth, .srcHeight = u32ViHeight,
        .distance = fStitchDistance,
        .loopCount = s32loopCnt,
        .modeBlend = s32AvsModeBlend,
        .pipeNum = s32CamNum,
        .outWidth = u32AvsChn0Width, .outHeight = u32AvsChn0Height,
        .syncPipe = true,
        .srcFrameRate = -1, .dstFrameRate = -1,
        .calibFilePath = pAvsCalibFilePath,
        .jsonFilePath = pAvsJsonFilePath,
        .ldchMode = eGetLdchMode,
        .ldchMeshData = (void *)pLdchMeshData,
        .projMode = -1,             /* 使用默认 equirectangular */
        .gainMode = -1,             /* 使用默认 auto */
        .paramSource = -1,          /* 使用默认 calib */
        .chn = {
            { .width = u32AvsChn0Width, .height = u32AvsChn0Height,
              .compressMode = LMO_COMPRESS_MODE_NONE,
              .srcFrameRate = -1, .dstFrameRate = -1,
              .frameBufCnt = 2, .depth = 0 },
            { .width = u32AvsChn1Width, .height = u32AvsChn1Height,
              .compressMode = LMO_COMPRESS_MODE_NONE,
              .srcFrameRate = -1, .dstFrameRate = -1,
              .frameBufCnt = 2, .depth = 0 },
        },
    };
#ifdef RV1126
    avsParams.chn[0].frameBufCnt = 4;
    avsParams.chn[1].frameBufCnt = 4;
#endif
    ctx.avs = LMO_COMM_AVS_Create(&avsParams);
    ctx.avsGrpId = 0;
    if (!ctx.avs) { printf("AVS create failed\n"); goto __FAILED; }

    /* ---- ISP ---- */
    if (iq_file_dir) {
#ifdef RKAIQ
        if (bIfIspGroupInit == 0) {
            for (int i = 0; i < s32CamNum; i++) {
                s32Ret = LMO_ISP_Init(i, hdr_mode, bMultictx, iq_file_dir);
                s32Ret |= LMO_ISP_Run(i);
                if (s32Ret != LMO_SUCCESS) {
                    printf("ISP init failure camid:%d\n", i);
                    goto __AVS_FAILED;
                }
            }
        } else {
#ifdef RKAIQ_GRP
            LMO_AIQ_CAMGROUP_CFG_S *camgroup_cfg = LMO_ISP_CreateCamGroupCfg();
            s32Ret = LMO_ISP_CamGroup_Init(s32CamGrpId, hdr_mode, bMultictx,
                                           eGetLdchMode, pLdchMeshData,
                                           camgroup_cfg);
            LMO_ISP_DestroyCamGroupCfg(camgroup_cfg);
            if (s32Ret != LMO_SUCCESS) {
                printf("ISP CamGroup_Init failure\n");
                goto __AVS_FAILED;
            }
            LMO_ISP_CamGroup_SetFrameRate(s32CamGrpId, s32DstFps);
#endif
        }
#endif
    }

    /* ---- RTSP ---- */
    g_rtsplive = create_rtsp_demo(554);
    for (int i = 0; i < VENC_NUM_MAX; i++) {
        char str_buff[255];
        sprintf(str_buff, "/live/%d", i);
        g_rtsp_session[i] = rtsp_new_session(g_rtsplive, str_buff);
        int codec_id = (enCodecType == LMO_CODEC_H264)
                        ? RTSP_CODEC_ID_VIDEO_H264
                        : RTSP_CODEC_ID_VIDEO_H265;
        rtsp_set_video(g_rtsp_session[i], codec_id, NULL, 0);
        rtsp_sync_video_ts(g_rtsp_session[i], rtsp_get_reltime(), rtsp_get_ntptime());
    }

    /* ---- VI ---- */
    for (int i = 0; i < s32CamNum; i++) {
        LMO_VI_Params vp = {
            .devId = i, .pipeId = i, .chnId = s32ViChnid,
            .width = u32ViWidth, .height = u32ViHeight,
            .pixelFormat  = LMO_FMT_YUV420SP,
            .compressMode = LMO_COMPRESS_MODE_NONE,
            .srcFrameRate = -1, .dstFrameRate = -1,
            .ispBufCount  = s32ViBuffCnt,
            .ispMemoryType = LMO_VI_MEM_DMABUF,
            .ispGroupInit = bIfIspGroupInit,
        };
#ifdef RV1126
        vp.ispGroupInit = 0;
#endif
        ctx.vi[i] = LMO_COMM_VI_Create(&vp);
        ctx.viDevId[i]  = i;
        ctx.viPipeId[i] = i;
        ctx.viChnId[i]  = s32ViChnid;
        ctx.viIspGroupInit[i] = vp.ispGroupInit;
        if (!ctx.vi[i]) { printf("VI[%d] create failed\n", i); goto __VI_INITFAIL; }
    }
    ctx.viCount = s32CamNum;

    for (int i = 0; i < s32CamNum && ctx.viIspGroupInit[i]; i++) {
        s32Ret = RK_MPI_VI_StartPipe(ctx.viPipeId[i]);
        if (s32Ret != 0) {
            printf("RK_MPI_VI_StartPipe failure pipe:%d\n", ctx.viPipeId[i]);
            goto __VI_INITFAIL;
        }
    }

    /* ---- AVS start ---- */
    if (LMO_COMM_AVS_Start(ctx.avs) != LMO_SUCCESS) {
        printf("AVS start failed\n");
        goto __AVS_FAILED;
    }

    /* ---- VENC[0] ---- */
    LMO_VENC_Params vp0 = {
        .chnId = 0, .width = u32AvsChn0Width, .height = u32AvsChn0Height,
        .fps = s32DstFps, .gop = s32DstFps * 2, .bitRate = s32BitRate,
        .codecType = enCodecType, .rcMode = enRcMode,
        .pixelFormat  = LMO_FMT_YUV420SP,
        .streamBufCnt = 3,
        .buffSize     = u32AvsChn0Width * u32AvsChn0Height / 3,
        .enableBufShare = 1, .loopCount = s32loopCnt,
        .profile = (enCodecType == LMO_CODEC_H264) ? 66 : 0,
    };
#ifdef RV1126
    vp0.streamBufCnt = 4;
#endif
    ctx.venc[0] = LMO_COMM_VENC_Create(&vp0);
    ctx.vencChnId[0] = 0;
    ctx.vencLoopCount[0] = s32loopCnt;
    if (!ctx.venc[0]) { printf("VENC[0] create failed\n"); goto __AVS_FAILED; }

    /* ---- VENC[1] ---- */
    LMO_VENC_Params vp1 = {
        .chnId = 1, .width = u32AvsChn1Width, .height = u32AvsChn1Height,
        .fps = s32DstFps, .gop = s32DstFps * 2, .bitRate = 1024,
        .codecType = enCodecType, .rcMode = enRcMode,
        .pixelFormat  = LMO_FMT_YUV420SP,
        .streamBufCnt = 3,
        .buffSize     = u32AvsChn1Width * u32AvsChn1Height / 3,
        .enableBufShare = 1, .loopCount = s32loopCnt,
        .profile = (enCodecType == LMO_CODEC_H264) ? 66 : 0,
    };
#ifdef RV1126
    vp1.streamBufCnt = 4;
#endif
    ctx.venc[1] = LMO_COMM_VENC_Create(&vp1);
    ctx.vencChnId[1] = 1;
    ctx.vencLoopCount[1] = s32loopCnt;
    if (!ctx.venc[1]) { printf("VENC[1] create failed\n"); goto __VENC0_FAILED; }

    /* ---- RGN ---- */
    if (pBmpPath && bIfOsdDisplay) {
        LMO_RGN_Params rp = {
            .rgnHandle = 0,
            .rgnType   = LMO_RGN_TYPE_OVERLAY,
            .modId = LMO_ID_VENC, .devId = 0, .chnId = 0,
            .regionX = (int)u32AvsChn0Width / 2,
            .regionY = 0,
            .regionW = 128, .regionH = 128,
            .layer = 1, .bgAlpha = 128, .fgAlpha = 128,
            .bmpFormat = LMO_FMT_BGRA5551,
            .srcFileBmpName = pBmpPath,
        };
        ctx.rgn = LMO_COMM_RGN_Create(&rp);
        if (!ctx.rgn) { printf("RGN create failed\n"); }
    }

    /* ---- Bind VI → AVS ---- */
    for (int i = 0; i < s32CamNum; i++) {
        LMO_CHN_S src = { .modId = LMO_ID_VI,  .devId = ctx.viDevId[i], .chnId = ctx.viChnId[i] };
        LMO_CHN_S dst = { .modId = LMO_ID_AVS, .devId = ctx.avsGrpId,   .chnId = i };
        LMO_COMM_Bind(&src, &dst);
    }

    /* Bind AVS → VENC[0] */
    {
        LMO_CHN_S src = { .modId = LMO_ID_AVS,  .devId = ctx.avsGrpId, .chnId = 0 };
        LMO_CHN_S dst = { .modId = LMO_ID_VENC, .devId = 0,            .chnId = 0 };
        LMO_COMM_Bind(&src, &dst);
    }
    /* Bind AVS → VENC[1] */
    {
        LMO_CHN_S src = { .modId = LMO_ID_AVS,  .devId = ctx.avsGrpId, .chnId = 1 };
        LMO_CHN_S dst = { .modId = LMO_ID_VENC, .devId = 0,            .chnId = 1 };
        LMO_COMM_Bind(&src, &dst);
    }

    /* ---- VENC threads ---- */
    VencThreadArg ta[VENC_NUM_MAX];
    for (int i = 0; i < VENC_NUM_MAX; i++) {
        ta[i].venc    = ctx.venc[i];
        ta[i].chnId   = ctx.vencChnId[i];
        ta[i].loopCount = ctx.vencLoopCount[i];
        ta[i].dstPath = pOutPathVenc;
        ta[i].rtspSession = g_rtsp_session[i];
        pthread_create(&g_vencTid[i], NULL, venc_get_stream, &ta[i]);
    }

    printf("%s initial finish\n", __func__);

    while (!g_bMainThreadQuit) sleep(1);

    printf("%s exit!\n", __func__);

    /* ---- Cleanup ---- */
    if (ctx.rgn) { LMO_COMM_RGN_Destroy(ctx.rgn); ctx.rgn = NULL; }

    for (int i = 0; i < VENC_NUM_MAX; i++) {
        g_bVencThreadQuit[i] = 1;
        pthread_join(g_vencTid[i], NULL);
        {
            LMO_CHN_S src = { .modId = LMO_ID_AVS,  .devId = ctx.avsGrpId, .chnId = i };
            LMO_CHN_S dst = { .modId = LMO_ID_VENC, .devId = 0,            .chnId = i };
            LMO_COMM_UnBind(&src, &dst);
        }
        LMO_COMM_VENC_Destroy(ctx.venc[i]);
        ctx.venc[i] = NULL;
    }

    if (g_rtsplive) rtsp_del_demo(g_rtsplive);

    for (int i = 0; i < s32CamNum; i++) {
        LMO_CHN_S src = { .modId = LMO_ID_VI,  .devId = ctx.viDevId[i], .chnId = ctx.viChnId[i] };
        LMO_CHN_S dst = { .modId = LMO_ID_AVS, .devId = ctx.avsGrpId,   .chnId = i };
        LMO_COMM_UnBind(&src, &dst);
    }

__VENC0_FAILED:
    if (ctx.venc[0]) { LMO_COMM_VENC_Destroy(ctx.venc[0]); ctx.venc[0] = NULL; }

__AVS_FAILED:
    if (ctx.avs) { LMO_COMM_AVS_Stop(ctx.avs); LMO_COMM_AVS_Destroy(ctx.avs); ctx.avs = NULL; }

    for (int i = 0; i < ctx.viCount && ctx.viIspGroupInit[i]; i++)
        RK_MPI_VI_StopPipe(ctx.viPipeId[i]);

__VI_INITFAIL:
    for (int i = 0; i < ctx.viCount; i++)
        if (ctx.vi[i]) { LMO_COMM_VI_Destroy(ctx.vi[i]); ctx.vi[i] = NULL; }

__FAILED:
    RK_MPI_SYS_Exit();
    if (iq_file_dir) {
#ifdef RKAIQ
        if (bIfIspGroupInit == 0) {
            for (int i = 0; i < s32CamNum; i++)
                LMO_ISP_Stop(i);
        } else {
#ifdef RKAIQ_GRP
            LMO_ISP_CamGroup_Stop(s32CamGrpId);
#endif
        }
#endif
    }
    return 0;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */
