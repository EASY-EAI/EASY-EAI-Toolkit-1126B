/*
 * Copyright 2025 Rockchip Electronics Co. LTD
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

#ifndef __LMO_COMMON_H__
#define __LMO_COMMON_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>   /* FILE* */

#ifdef __cplusplus
extern "C" {
#endif

/* =================================================================
 *  Return codes
 * ================================================================= */
#define LMO_SUCCESS  0
#define LMO_FAILURE  (-1)

/* =================================================================
 *  Basic type aliases
 * ================================================================= */
typedef int32_t  LMO_S32;
typedef uint32_t LMO_U32;
typedef uint16_t LMO_U16;
typedef uint8_t  LMO_U8;
typedef int64_t  LMO_S64;
typedef uint64_t LMO_U64;
typedef float    LMO_F32;

/*
 * ============================================================
 *  Enums — codec / pixel format / compress / rc / isp
 * ============================================================
 */
typedef enum {
    LMO_CODEC_NONE = -1,
    LMO_CODEC_H264,
    LMO_CODEC_H265,
    LMO_CODEC_JPEG,
    LMO_CODEC_MJPEG,
    LMO_CODEC_NB
} LMO_CODEC_TYPE_E;

typedef enum {
    LMO_RC_MODE_H264CBR = 0,
    LMO_RC_MODE_H264VBR,
    LMO_RC_MODE_H265CBR,
    LMO_RC_MODE_H265VBR,
    LMO_RC_MODE_MJPEGCBR,
    LMO_RC_MODE_MJPEGVBR,
    LMO_RC_MODE_BUTT
} LMO_RC_MODE_E;

typedef enum {
    LMO_COMPRESS_MODE_NONE    = 0,
    LMO_COMPRESS_AFBC_16x16   = 0x1,
    LMO_COMPRESS_RFBC_64x4    = 0x2,
    LMO_COMPRESS_MODE_BUTT
} LMO_COMPRESS_MODE_E;

typedef enum {
    LMO_VIDEO_FORMAT_LINEAR = 0,        /* nature video line */
    LMO_VIDEO_FORMAT_TILE_64x16,        /* tile cell: 64pixel x 16line */
    LMO_VIDEO_FORMAT_TILE_16x8,         /* tile cell: 16pixel x 8line */
    LMO_VIDEO_FORMAT_TILE_4x4,          /* tile cell: 4pixel x 4line */
    LMO_VIDEO_FORMAT_LINEAR_DISCRETE,   /* The data bits are aligned in bytes */
    LMO_VIDEO_FORMAT_BUTT
} LMO_VIDEO_FORMAT_E;

#define LMO_VIDEO_FMT_YUV    0x00000000
#define LMO_VIDEO_FMT_RGB    0x00010000
#define LMO_VIDEO_FMT_BPP    0x00020000
#define LMO_VIDEO_FMT_BAYER  0x00030000

typedef enum {
    LMO_FMT_YUV420SP         = LMO_VIDEO_FMT_YUV,     /* YYYY... UV...            */
    LMO_FMT_YUV420SP_10BIT,
    LMO_FMT_YUV422SP,                                   /* YYYY... UVUV...          */
    LMO_FMT_YUV422SP_10BIT,
    LMO_FMT_YUV420P,                                    /* YYYY... UUUU... VVVV     */
    LMO_FMT_YUV420P_VU,                                 /* YYYY... VVVV... UUUU     */
    LMO_FMT_YUV420SP_VU,                                /* YYYY... VUVUVU...        */
    LMO_FMT_YUV422P,                                    /* YYYY... UUUU... VVVV     */
    LMO_FMT_YUV422SP_VU,                                /* YYYY... VUVUVU...        */
    LMO_FMT_YUV422_YUYV,                                /* YUYVYUYV...              */
    LMO_FMT_YUV422_UYVY,                                /* UYVYUYVY...              */
    LMO_FMT_YUV400SP,                                   /* YYYY...                  */
    LMO_FMT_YUV440SP,                                   /* YYYY... UVUV...          */
    LMO_FMT_YUV411SP,                                   /* YYYY... UV...            */
    LMO_FMT_YUV444,                                     /* YUVYUVYUV...             */
    LMO_FMT_YUV444SP,                                   /* YYYY... UVUVUVUV...      */
    LMO_FMT_YUV444P,                                    /* YYYY... UUUU... VVVV     */
    LMO_FMT_YUV422_YVYU,                                /* YVYUYVYU...              */
    LMO_FMT_YUV422_VYUY,                                /* VYUYVYUY...              */
    LMO_FMT_YUV_BUTT,

    LMO_FMT_RGB565          = LMO_VIDEO_FMT_RGB,       /* 16-bit RGB               */
    LMO_FMT_BGR565,                                     /* 16-bit RGB               */
    LMO_FMT_RGB555,                                     /* 15-bit RGB               */
    LMO_FMT_BGR555,                                     /* 15-bit RGB               */
    LMO_FMT_RGB444,                                     /* 12-bit RGB               */
    LMO_FMT_BGR444,                                     /* 12-bit RGB               */
    LMO_FMT_RGB888,                                     /* 24-bit RGB               */
    LMO_FMT_BGR888,                                     /* 24-bit RGB               */
    LMO_FMT_RGB101010,                                  /* 30-bit RGB               */
    LMO_FMT_BGR101010,                                  /* 30-bit RGB               */
    LMO_FMT_ARGB1555,                                   /* 16-bit RGB               */
    LMO_FMT_ABGR1555,                                   /* 16-bit RGB               */
    LMO_FMT_ARGB4444,                                   /* 16-bit RGB               */
    LMO_FMT_ABGR4444,                                   /* 16-bit RGB               */
    LMO_FMT_ARGB8565,                                   /* 24-bit RGB               */
    LMO_FMT_ABGR8565,                                   /* 24-bit RGB               */
    LMO_FMT_ARGB8888,                                   /* 32-bit RGB               */
    LMO_FMT_ABGR8888,                                   /* 32-bit RGB               */
    LMO_FMT_BGRA8888,                                   /* 32-bit RGB               */
    LMO_FMT_RGBA8888,                                   /* 32-bit RGB               */
    LMO_FMT_RGBA5551,                                   /* 16-bit RGB               */
    LMO_FMT_BGRA5551,                                   /* 16-bit RGB               */
    LMO_FMT_BGRA4444,                                   /* 16-bit RGB               */
    LMO_FMT_RGBA4444,                                   /* 16-bit RGB               */
    LMO_FMT_XBGR8888,                                   /* 32-bit RGB               */
    LMO_FMT_RGB_BUTT,

    LMO_FMT_2BPP            = LMO_VIDEO_FMT_BPP,
    LMO_FMT_8BPP,
    LMO_FMT_1BPP,

    LMO_FMT_RGB_BAYER_SBGGR_8BPP = LMO_VIDEO_FMT_BAYER,  /* 8-bit raw                */
    LMO_FMT_RGB_BAYER_SGBRG_8BPP,
    LMO_FMT_RGB_BAYER_SGRBG_8BPP,
    LMO_FMT_RGB_BAYER_SRGGB_8BPP,
    LMO_FMT_RGB_BAYER_SBGGR_10BPP,
    LMO_FMT_RGB_BAYER_SGBRG_10BPP,
    LMO_FMT_RGB_BAYER_SGRBG_10BPP,
    LMO_FMT_RGB_BAYER_SRGGB_10BPP,
    LMO_FMT_RGB_BAYER_SBGGR_12BPP,
    LMO_FMT_RGB_BAYER_SGBRG_12BPP,
    LMO_FMT_RGB_BAYER_SGRBG_12BPP,
    LMO_FMT_RGB_BAYER_SRGGB_12BPP,
    LMO_FMT_RGB_BAYER_SBGGR_16BPP,
    LMO_FMT_RGB_BAYER_SGBRG_16BPP,
    LMO_FMT_RGB_BAYER_SGRBG_16BPP,
    LMO_FMT_RGB_BAYER_SRGGB_16BPP,
    LMO_FMT_RGB_BAYER_BUTT,
    LMO_FMT_BUTT            = LMO_FMT_RGB_BAYER_BUTT,
} LMO_PIXEL_FORMAT_E;

typedef enum {
    LMO_AIQ_WORKING_MODE_NORMAL   = 0,
    LMO_AIQ_WORKING_MODE_ISP_HDR2 = 0x10,
    LMO_AIQ_WORKING_MODE_ISP_HDR3 = 0x20,
} LMO_AIQ_WORKING_MODE_E;

typedef enum {
    LMO_ROTATION_0   = 0,
    LMO_ROTATION_90  = 1,
    LMO_ROTATION_180 = 2,
    LMO_ROTATION_270 = 3,
} LMO_ROTATION_E;

/*
 * ============================================================
 *  Opaque handle types (forward declarations — NO struct body)
 * ============================================================
 */
typedef struct LMO_VI_IMPL     LMO_VI_S;
typedef struct LMO_VENC_IMPL   LMO_VENC_S;
typedef struct LMO_VPSS_IMPL   LMO_VPSS_S;
typedef struct LMO_AVS_IMPL    LMO_AVS_S;
typedef struct LMO_AI_IMPL     LMO_AI_S;
typedef struct LMO_AO_IMPL     LMO_AO_S;
typedef struct LMO_AENC_IMPL   LMO_AENC_S;
typedef struct LMO_VDEC_IMPL   LMO_VDEC_S;
typedef struct LMO_VO_IMPL     LMO_VO_S;
typedef struct LMO_RGN_IMPL    LMO_RGN_S;
typedef struct LMO_TDE_IMPL    LMO_TDE_S;
typedef struct LMO_IVS_IMPL    LMO_IVS_S;
typedef struct LMO_GDC_IMPL    LMO_GDC_S;

#ifdef ROCKIVA
typedef struct LMO_IVA_IMPL    LMO_IVA_S;
#endif

/* ISP — camgroup 配置句柄 */
typedef struct LMO_AIQ_CAMGROUP_CFG_IMPL  LMO_AIQ_CAMGROUP_CFG_S;

/*
 * ============================================================
 *  Public structs (not opaque — callers need field access)
 * ============================================================
 */

/* MPP module ID — 用于 Bind / UnBind */
#define LMO_ID_VENC  4
#define LMO_ID_VPSS  6
#define LMO_ID_VI    8
#define LMO_ID_AVS   17
#define LMO_ID_GDC   18

/* VI memory type */
#define LMO_VI_MEM_DMABUF  4

/* VPSS device type */
#define LMO_VPSS_DEV_RGA   1

/* VENC GOP mode */
#define LMO_VENC_GOP_NORMALP  0
#define LMO_VENC_GOP_SMARTP   1

/* MPP channel descriptor — 用于 Bind / UnBind */
typedef struct {
    LMO_S32 modId;
    LMO_S32 devId;
    LMO_S32 chnId;
} LMO_CHN_S;

/* LDCH mesh path */
typedef struct {
    char *cLdchPath;
    char *cLdchName;
} LMO_ISP_LDCH_PATH;

/* Frame info — 解码/编码返回的数据描述 */
typedef struct {
    LMO_U8  *pVirAddr;
    LMO_U32  u32Size;
    LMO_U32  u32Width;
    LMO_U32  u32Height;
    LMO_U32  u32VirWidth;
    LMO_U32  u32VirHeight;
    LMO_U64  u64Pts;
    LMO_S32  enPixelFormat;
} LMO_FRAME_INFO_S;

/*
 * ============================================================
 *  Params structs — 各模块 Create 时的参数
 * ============================================================
 */

/* ---- VI ---- */
typedef struct {
    LMO_S32 devId;
    LMO_S32 pipeId;
    LMO_S32 chnId;
    LMO_U32 width;
    LMO_U32 height;
    LMO_S32 pixelFormat;          /* LMO_PIXEL_FORMAT_E */
    LMO_S32 compressMode;         /* LMO_COMPRESS_MODE_E, default NONE */
    LMO_S32 videoFormat;          /* LMO_VIDEO_FORMAT_E, default LINEAR(0) */
    bool    wrapIfEnable;
    bool    openEptz;
    bool    ispGroupInit;
    LMO_U32 bufferLine;
    LMO_S32 srcFrameRate;         /* -1 = follow source */
    LMO_S32 dstFrameRate;         /* -1 = follow source */
    LMO_U32 ispBufCount;          
    LMO_S32 ispMemoryType;        
    LMO_U32 depth;                
} LMO_VI_Params;

/* ---- VPSS 子通道参数 ---- */
typedef struct {
    LMO_U32 width;
    LMO_U32 height;
    LMO_S32 compressMode;
    LMO_S32 pixelFormat;
    LMO_S32 srcFrameRate;
    LMO_S32 dstFrameRate;
} LMO_VPSS_ChnParams;

/* ---- VENC ---- */
typedef struct {
    LMO_S32 chnId;
    LMO_U32 width;
    LMO_U32 height;
    LMO_U32 fps;
    LMO_U32 gop;
    LMO_U32 bitRate;
    LMO_S32 codecType;            /* LMO_CODEC_TYPE_E */
    LMO_S32 rcMode;               /* LMO_RC_MODE_E */
    LMO_S32 pixelFormat;          /* LMO_PIXEL_FORMAT_E */
    LMO_U32 streamBufCnt;
    LMO_U32 buffSize;
    bool    svcIfEnable;
    bool    wrapIfEnable;
    LMO_U32 bufferLine;           /* wrap buffer line count, valid when wrapIfEnable */
    bool    comboIfEnable;
    LMO_U32 comboChnId;
    LMO_U32 enableBufShare;
    LMO_S32 loopCount;            /* <0 = forever */
    LMO_S32 gopMode;              /* 0=normalP, 1=smartP */
    LMO_U32 profile;              /* H264: 66=Baseline 77=Main 100=High */
    LMO_U32 qfactor;              /* JPEG quality factor [1,99] */
} LMO_VENC_Params;

/* ---- VPSS ---- */
typedef struct {
    LMO_S32 grpId;
    LMO_S32 chnId;
    LMO_U32 width;
    LMO_U32 height;
    LMO_S32 pixelFormat;          /* LMO_PIXEL_FORMAT_E */
    LMO_S32 compressMode;         /* LMO_COMPRESS_MODE_E */
    LMO_S32 rotation;             /* LMO_ROTATION_E */
    LMO_S32 vProcDevType;         /* hardware VProc device type */
    LMO_S32 srcFrameRate;
    LMO_S32 dstFrameRate;
    LMO_VPSS_ChnParams chn[2];    /* 最多 2 个子通道 */
} LMO_VPSS_Params;

/* ---- AVS 子通道 ---- */
typedef struct {
    LMO_U32 width;
    LMO_U32 height;
    LMO_S32 compressMode;
    LMO_S32 srcFrameRate;
    LMO_S32 dstFrameRate;
    LMO_U32 frameBufCnt;
    LMO_U32 depth;
    LMO_S32 dynamicRange;         /* -1=默认(SDR8), 0=SDR8 */
} LMO_AVS_ChnParams;

/* ---- AVS ---- */
typedef struct {
    LMO_S32 grpId;
    LMO_S32 chnId;
    LMO_U32 srcWidth;
    LMO_U32 srcHeight;
    LMO_F32 distance;
    LMO_S32 loopCount;
    LMO_S32 modeBlend;            /* 0=blend */
    LMO_U32 pipeNum;              /* camera count */
    LMO_U32 outWidth;             /* output width */
    LMO_U32 outHeight;            /* output height */
    bool    syncPipe;
    LMO_S32 srcFrameRate;
    LMO_S32 dstFrameRate;
    char   *calibFilePath;
    char   *jsonFilePath;
    LMO_S32 ldchMode;             /* GET_LDCH_MODE_E */
    void    *ldchMeshData;        /* LdchMesh pointer array */
    LMO_S32 projMode;             /* 投影模式, -1=默认(equirectangular) */
    LMO_S32 gainMode;             /* 增益模式, -1=默认(auto) */
    LMO_S32 paramSource;          /* 参数来源, -1=默认(calib) */
    LMO_AVS_ChnParams chn[2];     /* 最多 2 个子通道 */
} LMO_AVS_Params;

/* ---- AI ---- */
typedef struct {
    LMO_S32 devId;
    LMO_S32 chnId;
    LMO_S32 chnSampleRate;        /* channel resample rate */
    LMO_S32 loopCount;
    /* sound card / AIO attributes */
    LMO_S32 soundCardSampleRate;  /* device hardware sample rate */
    LMO_S32 soundCardChannels;    /* device hardware channels */
    LMO_S32 bitWidth;             /* AUDIO_BIT_WIDTH_E: 0=8bit 1=16bit 2=24bit */
    LMO_S32 soundMode;            /* AUDIO_SOUND_MODE_E: 0=mono 1=stereo */
    LMO_U32 frmNum;               /* frame num in buf [2, MAX] */
    LMO_U32 ptNumPerFrm;          /* point num per frame */
    LMO_U32 chnCnt;               /* channel count on FS: 1/2/4/8 */
} LMO_AI_Params;

/* ---- AO ---- */
typedef struct {
    LMO_S32 devId;
    LMO_S32 chnId;
    LMO_S32 chnSampleRate;        /* channel resample rate */
    /* sound card / AIO attributes */
    LMO_S32 soundCardSampleRate;  /* device hardware sample rate */
    LMO_S32 soundCardChannels;    /* device hardware channels */
    LMO_S32 bitWidth;             /* AUDIO_BIT_WIDTH_E: 0=8bit 1=16bit 2=24bit */
    LMO_S32 soundMode;            /* AUDIO_SOUND_MODE_E: 0=mono 1=stereo */
    LMO_U32 frmNum;               /* frame num in buf [2, MAX] */
    LMO_U32 ptNumPerFrm;          /* point num per frame */
    LMO_U32 chnCnt;               /* channel count on FS: 1/2/4/8 */
} LMO_AO_Params;

/* ---- AENC ---- */
typedef struct {
    LMO_S32 chnId;
    LMO_S32 loopCount;
} LMO_AENC_Params;

/* ---- VDEC ---- */
typedef struct {
    LMO_S32 chnId;
    LMO_U32 width;
    LMO_U32 height;
    LMO_S32 codecType;            /* LMO_CODEC_TYPE_E */
    LMO_S32 pixelFormat;          /* LMO_PIXEL_FORMAT_E */
    bool    enableMbPool;
} LMO_VDEC_Params;

/* ---- VO ---- */
typedef struct {
    LMO_S32 devId;
    LMO_S32 chnId;
    LMO_S32 layerId;
    LMO_U32 dispWidth;
    LMO_U32 dispHeight;
    LMO_U32 dispBufLen;
} LMO_VO_Params;

/* ---- RGN ---- */
/* RGN type: 0=OVERLAY 1=OVERLAY_EX 2=COVER 3=MOSAIC */
#define LMO_RGN_TYPE_OVERLAY    0
#define LMO_RGN_TYPE_OVERLAY_EX 1
#define LMO_RGN_TYPE_COVER      2
#define LMO_RGN_TYPE_MOSAIC     3

typedef struct {
    LMO_U32 rgnHandle;            /* region handle */
    LMO_S32 rgnType;              /* LMO_RGN_TYPE_* */
    LMO_S32 modId;
    LMO_S32 devId;
    LMO_S32 chnId;
    LMO_S32 regionX;
    LMO_S32 regionY;
    LMO_U32 regionW;
    LMO_U32 regionH;
    LMO_U32 layer;
    LMO_U32 color;
    LMO_U32 bgAlpha;
    LMO_U32 fgAlpha;
    LMO_U32 bmpFormat;
    const char *srcFileBmpName;   /* BMP file path for overlay regions */
} LMO_RGN_Params;

/* ---- TDE ---- */
typedef struct {
    LMO_S32 chnId;
    LMO_U32 tdeWidth;
    LMO_U32 tdeHeight;
    LMO_U32 srcWidth;
    LMO_U32 srcHeight;
    LMO_S32 srcPixelFormat;       /* LMO_PIXEL_FORMAT_E */
    LMO_S32 srcCompMode;          /* LMO_COMPRESS_MODE_E */
} LMO_TDE_Params;

/* ---- IVS ---- */
typedef struct {
    LMO_S32 chnId;
} LMO_IVS_Params;

/* GDC channel mode */
typedef enum {
    LMO_GDC_MODE_EIS = 0,
    LMO_GDC_MODE_FEC = 1,
    LMO_GDC_MODE_DIS = 2,
} LMO_GDC_MODE_E;

typedef enum {
    LMO_GDC_INFO_CB_SUCCESS = 0,
    LMO_GDC_INFO_CB_CANCEL,
    LMO_GDC_INFO_CB_ERROR,
} LMO_GDC_INFO_CB_RESULT_E;

typedef struct {
    LMO_S64  s64Timestamp;
    double   dTemp;
    double   dGyroData[3];
    double   dAccData[3];
} LMO_GDC_SENSOR_INFO_S;

typedef struct {
    LMO_U64  u64ExtraPts;
    LMO_U32  u32RsSkew;
    LMO_U32  u32ExpTime;
    LMO_U32  u32Again;
    LMO_U32  u32Dgain;
    LMO_U32  u32Ispgain;
    double   dIso;
    void    *pMbBlk;               /* MB_BLK */
    LMO_U32  u32Width;
    LMO_U32  u32Height;
    LMO_U32  u32VirWidth;
    LMO_U32  u32VirHeight;
    LMO_S32  enPixelFormat;        /* LMO_PIXEL_FORMAT_E */
    LMO_S32  enCompressMode;       /* LMO_COMPRESS_MODE_E */
    LMO_U32  u32Seq;
    LMO_U64  u64PTS;
} LMO_GDC_VFAME_INFO_S;

typedef LMO_S32 (*LMO_GDC_SensorCB)(void *pUsr, LMO_GDC_SENSOR_INFO_S *pInfo);
typedef LMO_S32 (*LMO_GDC_VframeCB)(void *pUsr, LMO_GDC_VFAME_INFO_S  *pInfo);

/* ---- GDC ---- */
typedef struct {
    LMO_S32 chnId;
    LMO_U32 maxInQueue;
    LMO_U32 maxOutQueue;
    LMO_S32 dstWidth;
    LMO_S32 dstHeight;
    LMO_S32 mode;               /* LMO_GDC_MODE_E, default EIS */
    LMO_S32 iioDevNo;           /* EIS sensor (IIO) device number */
    LMO_S32 dstCompMode;        /* LMO_COMPRESS_MODE_E */
    LMO_S32 dstPixelFormat;     /* LMO_PIXEL_FORMAT_E */
    LMO_S32 depth;              /* 0 = default */
    const char *cfgFile;        /* EIS/FEC config file path */
    LMO_GDC_SensorCB sensorCb;  /* NULL = no sensor callback */
    LMO_GDC_VframeCB vframeCb;  /* NULL = no vframe callback */
} LMO_GDC_Params;

/*
 * ============================================================
 *  VI (Video Input)
 * ============================================================
 */
LMO_VI_S *LMO_COMM_VI_Create(const LMO_VI_Params *params);
void      LMO_COMM_VI_Destroy(LMO_VI_S *ctx);
LMO_S32   LMO_COMM_VI_GetChnFrame(LMO_VI_S *ctx, void **pdata);
LMO_S32   LMO_COMM_VI_ReleaseChnFrame(LMO_VI_S *ctx);
LMO_S32   LMO_COMM_VI_GetWidth(const LMO_VI_S *ctx);
LMO_S32   LMO_COMM_VI_GetHeight(const LMO_VI_S *ctx);
LMO_S32   LMO_COMM_VI_GetPixelFormat(const LMO_VI_S *ctx);

/*
 * ============================================================
 *  VENC (Video Encoder)
 * ============================================================
 */
LMO_VENC_S *LMO_COMM_VENC_Create(const LMO_VENC_Params *params);
void        LMO_COMM_VENC_Destroy(LMO_VENC_S *ctx);
LMO_S32     LMO_COMM_VENC_GetStream(LMO_VENC_S *ctx, void **pdata);
LMO_S32     LMO_COMM_VENC_ReleaseStream(LMO_VENC_S *ctx);
LMO_U32     LMO_COMM_VENC_GetStreamSize(const LMO_VENC_S *ctx);
LMO_U64     LMO_COMM_VENC_GetStreamPts(const LMO_VENC_S *ctx);
LMO_S32     LMO_COMM_VENC_GetChnId(const LMO_VENC_S *ctx);
LMO_S32     LMO_COMM_VENC_GetLoopCount(const LMO_VENC_S *ctx);

/*
 * ============================================================
 *  VPSS (Video Process Sub-System)
 * ============================================================
 */
LMO_VPSS_S *LMO_COMM_VPSS_Create(const LMO_VPSS_Params *params);
void        LMO_COMM_VPSS_Destroy(LMO_VPSS_S *ctx);
LMO_S32     LMO_COMM_VPSS_GetChnFrame(LMO_VPSS_S *ctx, void **pdata);
LMO_S32     LMO_COMM_VPSS_ReleaseChnFrame(LMO_VPSS_S *ctx);
LMO_S32     LMO_COMM_VPSS_GetFrameSize(const LMO_VPSS_S *ctx);

/*
 * ============================================================
 *  AVS (Around View System)
 * ============================================================
 */
LMO_AVS_S *LMO_COMM_AVS_Create(const LMO_AVS_Params *params);
void       LMO_COMM_AVS_Destroy(LMO_AVS_S *ctx);
LMO_S32    LMO_COMM_AVS_Start(LMO_AVS_S *ctx);
LMO_S32    LMO_COMM_AVS_Stop(LMO_AVS_S *ctx);
LMO_S32    LMO_COMM_AVS_GetChnFrame(LMO_AVS_S *ctx, void **pdata);
LMO_S32    LMO_COMM_AVS_ReleaseChnFrame(LMO_AVS_S *ctx);

/*
 * ============================================================
 *  AI / AO (Audio Input / Output)
 * ============================================================
 */
LMO_AI_S *LMO_COMM_AI_Create(const LMO_AI_Params *params);
void      LMO_COMM_AI_Destroy(LMO_AI_S *ctx);
LMO_S32   LMO_COMM_AI_GetFrame(LMO_AI_S *ctx, void **pdata);
LMO_S32   LMO_COMM_AI_ReleaseFrame(LMO_AI_S *ctx);

LMO_AO_S *LMO_COMM_AO_Create(const LMO_AO_Params *params);
void      LMO_COMM_AO_Destroy(LMO_AO_S *ctx);

/*
 * ============================================================
 *  VO (Video Output)
 * ============================================================
 */
LMO_VO_S *LMO_COMM_VO_Create(const LMO_VO_Params *params);
void      LMO_COMM_VO_Destroy(LMO_VO_S *ctx);
LMO_S32   LMO_COMM_VO_GetFrameCount(LMO_VO_S *ctx);

/*
 * ============================================================
 *  VDEC / AENC / RGN / TDE / IVS / IVA
 * ============================================================
 */
LMO_VDEC_S *LMO_COMM_VDEC_Create(const LMO_VDEC_Params *params);
void        LMO_COMM_VDEC_Destroy(LMO_VDEC_S *ctx);
LMO_S32     LMO_COMM_VDEC_SendStream(LMO_VDEC_S *ctx, FILE *fp);
LMO_S32     LMO_COMM_VDEC_SendFrame(LMO_VDEC_S *ctx, FILE *fp,
                                    LMO_U8 *buffer, int *nalStart);

LMO_AENC_S *LMO_COMM_AENC_Create(const LMO_AENC_Params *params);
void        LMO_COMM_AENC_Destroy(LMO_AENC_S *ctx);
LMO_S32     LMO_COMM_AENC_GetStream(LMO_AENC_S *ctx, void **pdata);
LMO_S32     LMO_COMM_AENC_ReleaseStream(LMO_AENC_S *ctx);

LMO_RGN_S *LMO_COMM_RGN_Create(const LMO_RGN_Params *params);
void       LMO_COMM_RGN_Destroy(LMO_RGN_S *ctx);
LMO_S32    LMO_COMM_RGN_Show(LMO_RGN_S *ctx, LMO_S32 show);
LMO_S32    LMO_COMM_RGN_SetPos(LMO_RGN_S *ctx, LMO_S32 x, LMO_S32 y);
LMO_S32    LMO_COMM_RGN_UpdateBmp(LMO_RGN_S *ctx, const char *bmpPath);

LMO_TDE_S *LMO_COMM_TDE_Create(const LMO_TDE_Params *params);
LMO_S32    LMO_COMM_TDE_Handle(LMO_TDE_S *ctx, void *pVideoFrame);
LMO_S32    LMO_COMM_TDE_GetMB(LMO_TDE_S *ctx);
LMO_S32    LMO_COMM_TDE_ReleaseMB(LMO_TDE_S *ctx);
void       LMO_COMM_TDE_Destroy(LMO_TDE_S *ctx);

LMO_IVS_S *LMO_COMM_IVS_Create(const LMO_IVS_Params *params);
void       LMO_COMM_IVS_Destroy(LMO_IVS_S *ctx);

/*
 * ============================================================
 *  GDC (Geometric Distortion Correction / EIS)
 * ============================================================
 */
LMO_GDC_S *LMO_COMM_GDC_Create(const LMO_GDC_Params *params);
void       LMO_COMM_GDC_Destroy(LMO_GDC_S *ctx);
LMO_S32    LMO_COMM_GDC_GetFrame(LMO_GDC_S *ctx, void **pdata, LMO_S32 s32MilliSec);
LMO_S32    LMO_COMM_GDC_ReleaseFrame(LMO_GDC_S *ctx);
LMO_S32    LMO_COMM_GDC_SendFrame(LMO_GDC_S *ctx, const void *pstFrame,
                                  LMO_S32 s32MilliSec);
LMO_S32    LMO_COMM_GDC_RegisterInfoCB(LMO_GDC_S *ctx);
LMO_S32    LMO_COMM_GDC_GetFd(LMO_GDC_S *ctx);
LMO_S32    LMO_COMM_GDC_GetUpdateAttr(LMO_GDC_S *ctx, void *pstAttr);
LMO_S32    LMO_COMM_GDC_Update(LMO_GDC_S *ctx, const void *pstAttr);
LMO_S32    LMO_COMM_GDC_GetChnId(const LMO_GDC_S *ctx);


LMO_S32 LMO_COMM_GDC_BeginJob(LMO_S32 *phHandle);
LMO_S32 LMO_COMM_GDC_EndJob(LMO_S32 hHandle);
LMO_S32 LMO_COMM_GDC_CancelJob(LMO_S32 hHandle);
LMO_S32 LMO_COMM_GDC_StopJob(LMO_S32 hHandle);
LMO_S32 LMO_COMM_GDC_SetConfig(LMO_S32 hHandle, const void *pstJobConfig);

LMO_S32 LMO_COMM_GDC_AddCorrectionTask(LMO_S32 hHandle, const void *pstTask,
                                        const void *pstFisheyeAttr);
LMO_S32 LMO_COMM_GDC_AddCorrectionExTask(LMO_S32 hHandle, const void *pstTask,
                                         const void *pstFishEyeAttrEx,
                                         LMO_S32 bCheckMode);
LMO_S32 LMO_COMM_GDC_AddPMFTask(LMO_S32 hHandle, const void *pstTask,
                                const void *pstGdcPmfAttr);

LMO_S32 LMO_COMM_GDC_FisheyePosQueryDst2Src(const void *pstAttr,
                                            const void *pstVideoInfo,
                                            const void *pstDstPoint,
                                            void *pstSrcPoint);
LMO_S32 LMO_COMM_GDC_FisheyePosQueryDst2SrcArray(const void *pstAttr,
                                                 const void *pstVideoInfo,
                                                 LMO_U32 u32PointNum,
                                                 const void *pastDstPoint,
                                                 void *pastSrcPoint);
LMO_S32 LMO_COMM_GDC_FisheyePosQueryDst2Pano(const void *pstAttr,
                                             const void *pstVideoInfo,
                                             LMO_U32 u32PanoRegionIndex,
                                             const void *pstDstPoint,
                                             void *pstPanoPoint);
LMO_S32 LMO_COMM_GDC_FisheyePosQueryDst2PanoArray(const void *pstAttr,
                                                  const void *pstVideoInfo,
                                                  LMO_U32 u32PanoRegionIndex,
                                                  LMO_U32 u32PointNum,
                                                  const void *pastDstPoint,
                                                  void *pastPanoPoint);

LMO_S32 LMO_COMM_GDC_GetAttrFromFile(void *pstAttr, const char *pFile);

#ifdef ROCKIVA
LMO_IVA_S *LMO_COMM_IVA_Create(void);
void       LMO_COMM_IVA_Destroy(LMO_IVA_S *ctx);
#endif

/*
 * ============================================================
 *  通用工具函数
 * ============================================================
 */
LMO_S32 LMO_COMM_Bind(const LMO_CHN_S *pstSrcChn, const LMO_CHN_S *pstDestChn);
LMO_S32 LMO_COMM_UnBind(const LMO_CHN_S *pstSrcChn, const LMO_CHN_S *pstDestChn);
void    LMO_PrintStreamDetails(int chnId, int frameSize);
LMO_S32 LMO_COMM_FillImage(LMO_U8 *buf, LMO_U32 width, LMO_U32 height,
                           LMO_U32 horStride, LMO_U32 verStride,
                           LMO_S32 pixelFormat, LMO_U32 frameCount);
LMO_S32 LMO_COMM_GetLdchMesh(char *cam0LdchPath, char *cam1LdchPath,
                             LMO_S32 meshDataSize, LMO_U16 **pLdchMesh);

/*
 * ============================================================
 *  ISP (Image Signal Processor)
 * ============================================================
 */
LMO_AIQ_CAMGROUP_CFG_S *LMO_ISP_CreateCamGroupCfg(void);
void                     LMO_ISP_DestroyCamGroupCfg(LMO_AIQ_CAMGROUP_CFG_S *cfg);

LMO_S32 LMO_ISP_ShouldQuit(void);
LMO_S32 LMO_ISP_GetSofCnt(void);
LMO_S32 LMO_ISP_PreInitBufCnt(LMO_S32 camId, LMO_S32 bufCnt);
LMO_S32 LMO_ISP_Init(LMO_S32 camId, LMO_S32 wdrMode, bool multiCam,
                     const char *iqFileDir);
LMO_S32 LMO_ISP_CamGroup_Init(LMO_S32 camGroupId, LMO_S32 wdrMode, bool multiCam,
                               int openLdch, void *ldchMesh[],
                               LMO_AIQ_CAMGROUP_CFG_S *camGroupCfg);
LMO_S32 LMO_ISP_Stop(LMO_S32 camId);
LMO_S32 LMO_ISP_CamGroup_Stop(LMO_S32 camGroupId);
LMO_S32 LMO_ISP_Run(LMO_S32 camId);
LMO_S32 LMO_ISP_SetFrameRate(LMO_S32 camId, LMO_U32 fps);
LMO_S32 LMO_ISP_CamGroup_SetFrameRate(LMO_S32 camId, LMO_U32 fps);
LMO_S32 LMO_ISP_SetMirrorFlip(int camId, int mirror, int flip);
LMO_S32 LMO_ISP_SetLDCH(LMO_U32 camId, LMO_U32 level, bool bIfEnable);
LMO_S32 LMO_ISP_CamGroup_SetLDCH(LMO_U32 camId, LMO_U32 level, bool bIfEnable);
LMO_S32 LMO_ISP_GetAINrParams(LMO_S32 camId, void *ainrParam);
LMO_S32 LMO_ISP_EnablsAiisp(LMO_S32 camId);

#ifdef __cplusplus
}
#endif

#endif /* __LMO_COMMON_H__ */
