/**
 * RK_MPI VI + VENC 采集编码模块 —— 实现
 *
 * 3A server 模式：ISP 初始化由 rkaiq_3A.service（rkaiq_3A_server）负责，
 * 本模块只做 RK_MPI 层面的 VI + VENC。
 *
 * 两种工作模式：
 *   1) 直通模式 (bOsdEnabled=false)：
 *      VI chn0 ──bind──> VENC ──> GetStream 回调
 *      VI chn1 ──> GetChnFrame 回调 (YUV)
 *
 *   2) OSD 模式 (bOsdEnabled=true)：
 *      VI chn1 ──> GetChnFrame 回调 (YUV 供分析)
 *      外部 BGR888 → sendFrame() → RGA 转 NV12 → VENC SendFrame → GetStream 回调
 *
 * 前置条件：rkaiq_3A.service 必须已启动（systemctl start rkaiq_3A.service）
 */

#include "rkCapturer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/ioctl.h>

#include "rk_mpi_sys.h"
#include "rk_mpi_vi.h"
#include "rk_mpi_venc.h"
#include "rk_mpi_mb.h"
#include "rga_wrapper.h"
#include "logHandle.h"

#define RK_ALIGN(x, a) (((x) + (a)-1) & ~((a)-1))
#define RK_ALIGN_2(x) RK_ALIGN(x, 2)
#define SEND_FRAME_TIMEOUT 200

/* ======================== 构造 / 析构 ======================== */

RkCapturer::RkCapturer(const std::string &device, int width, int height,
                       int framerate, const std::string &fmt, bool bOsdEnabled)
    : mDevice(device)
    , mWidth(width)
    , mHeight(height)
    , mFramerate(framerate > 0 ? framerate : 30)
    , mFmt(fmt)
    , mOsdEnabled(bOsdEnabled)
    , mCb(NULL)
    , mUserData(NULL)
    , mYuvCb(NULL)
    , mYuvUserData(NULL)
    , mStreamTid(0)
    , mYuvTid(0)
    , mRunning(false)
    , mDevId(0)
    , mPipeId(0)
    , mChnId(0)
    , mYuvChnId(1)
    , mVencChn(0)
    , mNv12Buf(NULL)
    , mNv12Fd(-1)
    , mNv12Size(0)
    , mMbPool((uint32_t)-1)
    , mSysInited(false)
    , mViDevEnabled(false)
    , mViChnEnabled(false)
    , mViYuvChnEnabled(false)
    , mVencCreated(false)
    , mBound(false)
{
}

RkCapturer::~RkCapturer()
{
    stop();
}

void RkCapturer::setFrameCallback(FrameCallback cb, void *userData)
{
    mCb = cb;
    mUserData = userData;
}

void RkCapturer::setYuvCallback(YuvCallback cb, void *userData)
{
    mYuvCb = cb;
    mYuvUserData = userData;
}

/* ======================== NV12 DMA-BUF 分配（OSD 模式） ======================== */
int RkCapturer::nv12BufInit()
{
    /* NV12: width * height * 3 / 2 */
    mNv12Size = mWidth * mHeight * 3 / 2;
    if (alloc_dmabuf((size_t)mNv12Size, &mNv12Fd, &mNv12Buf) != 0) {
        PRINT_ERROR(g_hCapture, "nv12BufInit: alloc_dmabuf failed (%d bytes)\n", mNv12Size);
        return -1;
    }
    PRINT_DEBUG(g_hCapture, "nv12BufInit: allocated %d bytes, fd=%d\n", mNv12Size, mNv12Fd);
    return 0;
}

void RkCapturer::nv12BufDeinit()
{
    if (mNv12Buf && mNv12Size > 0) {
        munmap(mNv12Buf, mNv12Size);
        mNv12Buf = NULL;
    }
    if (mNv12Fd >= 0) {
        close(mNv12Fd);
        mNv12Fd = -1;
    }
    mNv12Size = 0;
}

/* ======================== VI 初始化（设备 + chn0） ======================== */
int RkCapturer::viInit()
{
    int devId = mDevId;
    int pipeId = mPipeId;
    int channelId = mChnId;

    VI_DEV_ATTR_S stDevAttr;
    VI_DEV_BIND_PIPE_S stBindPipe;
    VI_CHN_ATTR_S stChnAttr;

    /* ---- 设备配置 ---- */
    memset(&stDevAttr, 0, sizeof(stDevAttr));
    int ret = RK_MPI_VI_GetDevAttr(devId, &stDevAttr);
    if (ret == RK_ERR_VI_NOT_CONFIG) {
        ret = RK_MPI_VI_SetDevAttr(devId, &stDevAttr);
        if (ret != RK_SUCCESS) {
            PRINT_ERROR(g_hCapture, "RK_MPI_VI_SetDevAttr %x\n", ret);
            return -1;
        }
    }

    /* ---- 使能设备 ---- */
    ret = RK_MPI_VI_GetDevIsEnable(devId);
    if (ret != RK_SUCCESS) {
        ret = RK_MPI_VI_EnableDev(devId);
        if (ret != RK_SUCCESS) {
            PRINT_ERROR(g_hCapture, "RK_MPI_VI_EnableDev %x\n", ret);
            return -1;
        }
        memset(&stBindPipe, 0, sizeof(stBindPipe));
        stBindPipe.u32Num = 1;
        stBindPipe.PipeId[0] = pipeId;
        ret = RK_MPI_VI_SetDevBindPipe(devId, &stBindPipe);
        if (ret != RK_SUCCESS) {
            PRINT_ERROR(g_hCapture, "RK_MPI_VI_SetDevBindPipe %x\n", ret);
            return -1;
        }
    }

    mViDevEnabled = true;

    /* ---- chn0 配置（直通模式绑定到 VENC，u32Depth=0） ---- */
    memset(&stChnAttr, 0, sizeof(stChnAttr));
    stChnAttr.stSize.u32Width = mWidth;
    stChnAttr.stSize.u32Height = mHeight;
    stChnAttr.stIspOpt.stMaxSize.u32Width = mWidth;
    stChnAttr.stIspOpt.stMaxSize.u32Height = mHeight;
    stChnAttr.stIspOpt.u32BufCount = 3;
    stChnAttr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
    stChnAttr.u32Depth = 0;
    stChnAttr.enPixelFormat = RK_FMT_YUV420SP;
    stChnAttr.enCompressMode = COMPRESS_MODE_NONE;
    stChnAttr.stFrameRate.s32SrcFrameRate = -1;
    stChnAttr.stFrameRate.s32DstFrameRate = -1;

    ret = RK_MPI_VI_SetChnAttr(pipeId, channelId, &stChnAttr);
    if (ret != RK_SUCCESS) {
        PRINT_ERROR(g_hCapture, "RK_MPI_VI_SetChnAttr chn0 %x\n", ret);
        return -1;
    }

    ret = RK_MPI_VI_EnableChn(pipeId, channelId);
    if (ret != RK_SUCCESS) {
        PRINT_ERROR(g_hCapture, "RK_MPI_VI_EnableChn chn0 %x\n", ret);
        return -1;
    }

    mViChnEnabled = true;
    return 0;
}

/* ======================== VI chn1 初始化（供算法分析） ======================== */
int RkCapturer::viYuvInit()
{
    int pipeId = mPipeId;
    int channelId = mYuvChnId;

    VI_CHN_ATTR_S stChnAttr;
    memset(&stChnAttr, 0, sizeof(stChnAttr));

    stChnAttr.stSize.u32Width = mWidth;
    stChnAttr.stSize.u32Height = mHeight;
    stChnAttr.stIspOpt.stMaxSize.u32Width = mWidth;
    stChnAttr.stIspOpt.stMaxSize.u32Height = mHeight;
    stChnAttr.stIspOpt.u32BufCount = 2;
    stChnAttr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
    stChnAttr.u32Depth = 1; /* 允许应用获取帧 */
    stChnAttr.enPixelFormat = RK_FMT_YUV420SP;
    stChnAttr.enCompressMode = COMPRESS_MODE_NONE;
    stChnAttr.stFrameRate.s32SrcFrameRate = -1;
    stChnAttr.stFrameRate.s32DstFrameRate = -1;

    int ret = RK_MPI_VI_SetChnAttr(pipeId, channelId, &stChnAttr);
    if (ret != RK_SUCCESS) {
        PRINT_ERROR(g_hCapture, "RK_MPI_VI_SetChnAttr chn1 %x\n", ret);
        return -1;
    }

    ret = RK_MPI_VI_EnableChn(pipeId, channelId);
    if (ret != RK_SUCCESS) {
        PRINT_ERROR(g_hCapture, "RK_MPI_VI_EnableChn chn1 %x\n", ret);
        return -1;
    }

    mViYuvChnEnabled = true;
    return 0;
}

/* ======================== VENC 初始化 ======================== */
int RkCapturer::vencInit()
{
    int chnId = mVencChn;
    RK_CODEC_ID_E enType;

    if (mFmt == "h265") {
        enType = RK_VIDEO_ID_HEVC;
    } else {
        enType = RK_VIDEO_ID_AVC;
    }

    VENC_CHN_ATTR_S stAttr;
    memset(&stAttr, 0, sizeof(VENC_CHN_ATTR_S));

    stAttr.stVencAttr.enType = enType;
    stAttr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
    stAttr.stVencAttr.u32MaxPicWidth = mWidth;
    stAttr.stVencAttr.u32MaxPicHeight = mHeight;
    stAttr.stVencAttr.u32PicWidth = mWidth;
    stAttr.stVencAttr.u32PicHeight = mHeight;
    stAttr.stVencAttr.u32VirWidth = RK_ALIGN_2(mWidth);
    stAttr.stVencAttr.u32VirHeight = RK_ALIGN_2(mHeight);
    stAttr.stVencAttr.u32StreamBufCnt = 5;
    stAttr.stVencAttr.u32BufSize = mWidth * mHeight / 2;
    stAttr.stVencAttr.enMirror = MIRROR_NONE;

    if (enType == RK_VIDEO_ID_AVC) {
        stAttr.stVencAttr.u32Profile = H264E_PROFILE_HIGH;
        stAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
        stAttr.stRcAttr.stH264Cbr.u32Gop = 50;
        stAttr.stRcAttr.stH264Cbr.u32BitRate = 4 * 1024;
        stAttr.stRcAttr.stH264Cbr.u32SrcFrameRateNum = mFramerate;
        stAttr.stRcAttr.stH264Cbr.u32SrcFrameRateDen = 1;
        stAttr.stRcAttr.stH264Cbr.fr32DstFrameRateNum = mFramerate;
        stAttr.stRcAttr.stH264Cbr.fr32DstFrameRateDen = 1;
        stAttr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
    } else {
        stAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
        stAttr.stRcAttr.stH265Cbr.u32Gop = 50;
        stAttr.stRcAttr.stH265Cbr.u32BitRate = 4 * 1024;
        stAttr.stRcAttr.stH265Cbr.u32SrcFrameRateNum = mFramerate;
        stAttr.stRcAttr.stH265Cbr.u32SrcFrameRateDen = 1;
        stAttr.stRcAttr.stH265Cbr.fr32DstFrameRateNum = mFramerate;
        stAttr.stRcAttr.stH265Cbr.fr32DstFrameRateDen = 1;
        stAttr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
    }

    int ret = RK_MPI_VENC_CreateChn(chnId, &stAttr);
    if (ret != RK_SUCCESS) {
        PRINT_ERROR(g_hCapture, "RK_MPI_VENC_CreateChn %x\n", ret);
        return -1;
    }

    VENC_RECV_PIC_PARAM_S stRecvParam;
    memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
    stRecvParam.s32RecvPicNum = -1;
    ret = RK_MPI_VENC_StartRecvFrame(chnId, &stRecvParam);
    if (ret != RK_SUCCESS) {
        PRINT_ERROR(g_hCapture, "RK_MPI_VENC_StartRecvFrame %x\n", ret);
        RK_MPI_VENC_DestroyChn(chnId);
        return -1;
    }

    mVencCreated = true;
    return 0;
}

/* ======================== 绑定 VI chn0 → VENC ======================== */
int RkCapturer::bindViVenc()
{
    MPP_CHN_S stSrcChn, stDestChn;
    stSrcChn.enModId = RK_ID_VI;
    stSrcChn.s32DevId = mPipeId;
    stSrcChn.s32ChnId = mChnId;

    stDestChn.enModId = RK_ID_VENC;
    stDestChn.s32DevId = 0;
    stDestChn.s32ChnId = mVencChn;

    int ret = RK_MPI_SYS_Bind(&stSrcChn, &stDestChn);
    if (ret != RK_SUCCESS) {
        PRINT_ERROR(g_hCapture, "RK_MPI_SYS_Bind vi[%d,%d]→venc[%d,%d] failed %x\n",
               mPipeId, mChnId, 0, mVencChn, ret);
        return -1;
    }

    mBound = true;
    return 0;
}

/* ======================== 发送 BGR888 帧到 VENC 编码（OSD 模式） ======================== */
int RkCapturer::sendFrame(const uint8_t *bgrData, int width, int height, int srcFd)
{
    if (!mOsdEnabled || !mRunning || !bgrData || width <= 0 || height <= 0) {
        return -1;
    }

    /* 使用 MB pool 获取一个 MB_BLK */
    MB_POOL pool = (MB_POOL)mMbPool;
    if (pool == MB_INVALID_POOLID) {
        PRINT_ERROR(g_hCapture, "sendFrame: MB pool not initialized\n");
        return -1;
    }

    int blkSize = width * height * 3 / 2;
    MB_BLK blk = RK_MPI_MB_GetMB(pool, blkSize, RK_TRUE);
    if (!blk) {
        PRINT_ERROR(g_hCapture, "sendFrame: RK_MPI_MB_GetMB failed\n");
        return -1;
    }

    /* RGA: BGR888 → NV12，使用 fd 模式零拷贝 */
    Image srcImage, dstImage;
    memset(&srcImage, 0, sizeof(srcImage));
    memset(&dstImage, 0, sizeof(dstImage));

    srcImage.fmt = RK_FORMAT_BGR_888;
    srcImage.width = width;
    srcImage.height = height;
    srcImage.hor_stride = width;
    srcImage.ver_stride = height;
    srcImage.rotation = HAL_TRANSFORM_ROT_0;
    srcImage.fd = srcFd;       /* >=0 时 RGA 用 fd 模式直接访问 DMA-BUF */
    srcImage.pBuf = (void *)bgrData;

    dstImage.fmt = RK_FORMAT_YCbCr_420_SP;
    dstImage.width = width;
    dstImage.height = height;
    dstImage.hor_stride = width;
    dstImage.ver_stride = height;
    dstImage.rotation = HAL_TRANSFORM_ROT_0;
    dstImage.fd = RK_MPI_MB_Handle2Fd(blk);  /* MB_BLK 的 fd，RGA 直接输出到 MB_BLK */
    dstImage.pBuf = RK_MPI_MB_Handle2VirAddr(blk);

    if (srcImg_ConvertTo_dstImg(&dstImage, &srcImage) != 0) {
        PRINT_ERROR(g_hCapture, "sendFrame: RGA BGR→NV12 failed\n");
        RK_MPI_MB_ReleaseMB(blk);
        return -1;
    }

    RK_MPI_SYS_MmzFlushCache(blk, RK_FALSE);

    /* 构造 VIDEO_FRAME_INFO_S 并发送到 VENC */
    VIDEO_FRAME_INFO_S stFrame;
    memset(&stFrame, 0, sizeof(stFrame));
    stFrame.stVFrame.u32Width = width;
    stFrame.stVFrame.u32Height = height;
    stFrame.stVFrame.u32VirWidth = width;
    stFrame.stVFrame.u32VirHeight = height;
    stFrame.stVFrame.enPixelFormat = RK_FMT_YUV420SP;
    stFrame.stVFrame.enCompressMode = COMPRESS_MODE_NONE;
    stFrame.stVFrame.pMbBlk = blk;

    int ret = RK_MPI_VENC_SendFrame(mVencChn, &stFrame, SEND_FRAME_TIMEOUT);
    if (ret != RK_SUCCESS) {
        PRINT_ERROR(g_hCapture, "RK_MPI_VENC_SendFrame fail %x\n", ret);
    }

    RK_MPI_MB_ReleaseMB(blk);
    return ret;
}

/* ======================== 编码码流取流线程 ======================== */
void *RkCapturer::streamThread(void *para)
{
    RkCapturer *self = (RkCapturer *)para;
    prctl(PR_SET_NAME, "rkcap_stream");

    int s32Ret;
    VENC_STREAM_S stFrame;
    stFrame.pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S));
    uint32_t stream_cnt = 0;
    uint32_t buf_empty_cnt = 0;

    PRINT_INFO(g_hCapture, "stream thread started (venc chn=%d)\n", self->mVencChn);

    while (self->mRunning) {
        s32Ret = RK_MPI_VENC_GetStream(self->mVencChn, &stFrame, 200);
        if (s32Ret == RK_SUCCESS) {
            void *pData = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
            uint32_t len = stFrame.pstPack->u32Len;

            if (pData && len > 0 && self->mCb) {
                FrameDesc_t desc;
                memset(&desc, 0, sizeof(desc));
                desc.width = self->mWidth;
                desc.height = self->mHeight;
                strncpy(desc.strFmt, self->mFmt.c_str(), sizeof(desc.strFmt) - 1);
                self->mCb((const uint8_t *)pData, len, &desc, self->mUserData);
            }

            s32Ret = RK_MPI_VENC_ReleaseStream(self->mVencChn, &stFrame);
            if (s32Ret != RK_SUCCESS) {
                PRINT_ERROR(g_hCapture, "RK_MPI_VENC_ReleaseStream fail %x\n", s32Ret);
            }

            stream_cnt++;
            if ((stream_cnt % 30) == 0) {
                PRINT_DEBUG(g_hCapture, "streamThread alive: cnt=%u len=%u buf_empty=%u\n",
                       stream_cnt, len, buf_empty_cnt);
            }
        } else if (s32Ret == RK_ERR_VENC_BUF_EMPTY) {
            buf_empty_cnt++;
            usleep(1000);
        } else {
            if (self->mRunning) {
                PRINT_ERROR(g_hCapture, "RK_MPI_VENC_GetStream fail %x\n", s32Ret);
                usleep(5000);
            }
        }
    }

    free(stFrame.pstPack);
    PRINT_INFO(g_hCapture, "stream thread exiting (cnt=%u buf_empty=%u)\n", stream_cnt, buf_empty_cnt);
    return NULL;
}

/* ======================== YUV 原始帧取帧线程 ======================== */
void *RkCapturer::yuvThread(void *para)
{
    RkCapturer *self = (RkCapturer *)para;
    prctl(PR_SET_NAME, "rkcap_yuv");

    int s32Ret;
    uint32_t frame_cnt = 0;

    PRINT_INFO(g_hCapture, "yuv thread started (vi chn=%d)\n", self->mYuvChnId);

    while (self->mRunning) {
        VIDEO_FRAME_INFO_S stFrame;
        memset(&stFrame, 0, sizeof(stFrame));

        s32Ret = RK_MPI_VI_GetChnFrame(self->mPipeId, self->mYuvChnId,
                                       &stFrame, 200);
        if (s32Ret == RK_SUCCESS) {
            void *pData = RK_MPI_MB_Handle2VirAddr(stFrame.stVFrame.pMbBlk);
            uint32_t len = stFrame.stVFrame.u32VirWidth * stFrame.stVFrame.u32VirHeight * 3 / 2;

            if (pData && len > 0 && self->mYuvCb) {
                YuvDesc_t desc;
                memset(&desc, 0, sizeof(desc));
                desc.width = stFrame.stVFrame.u32Width;
                desc.height = stFrame.stVFrame.u32Height;
                desc.horStride = stFrame.stVFrame.u32VirWidth;
                desc.verStride = stFrame.stVFrame.u32VirHeight;
                strncpy(desc.fmt, "NV12", sizeof(desc.fmt) - 1);
                self->mYuvCb((const uint8_t *)pData, len, &desc, self->mYuvUserData);
            }

            s32Ret = RK_MPI_VI_ReleaseChnFrame(self->mPipeId, self->mYuvChnId, &stFrame);
            if (s32Ret != RK_SUCCESS) {
                PRINT_ERROR(g_hCapture, "RK_MPI_VI_ReleaseChnFrame fail %x\n", s32Ret);
            }

            frame_cnt++;
            if ((frame_cnt % 30) == 0) {
                PRINT_DEBUG(g_hCapture, "yuvThread alive: cnt=%u\n", frame_cnt);
            }
        } else {
            if (self->mRunning) {
                usleep(1000);
            }
        }
    }

    PRINT_INFO(g_hCapture, "yuv thread exiting (cnt=%u)\n", frame_cnt);
    return NULL;
}

/* ======================== 清理 ======================== */
void RkCapturer::cleanup()
{
    if (mBound) {
        MPP_CHN_S stSrcChn, stDestChn;
        stSrcChn.enModId = RK_ID_VI;
        stSrcChn.s32DevId = mPipeId;
        stSrcChn.s32ChnId = mChnId;
        stDestChn.enModId = RK_ID_VENC;
        stDestChn.s32DevId = 0;
        stDestChn.s32ChnId = mVencChn;
        RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
        mBound = false;
    }

    if (mViYuvChnEnabled) {
        RK_MPI_VI_DisableChn(mPipeId, mYuvChnId);
        mViYuvChnEnabled = false;
    }

    if (mViChnEnabled) {
        RK_MPI_VI_DisableChn(mPipeId, mChnId);
        mViChnEnabled = false;
    }

    if (mVencCreated) {
        RK_MPI_VENC_StopRecvFrame(mVencChn);
        RK_MPI_VENC_DestroyChn(mVencChn);
        mVencCreated = false;
    }

    nv12BufDeinit();

    if (mMbPool != (uint32_t)-1 && mMbPool != MB_INVALID_POOLID) {
        RK_MPI_MB_DestroyPool((MB_POOL)mMbPool);
        mMbPool = (uint32_t)-1;
    }

    if (mViDevEnabled) {
        RK_MPI_VI_DisableDev(mDevId);
        mViDevEnabled = false;
    }

    if (mSysInited) {
        RK_MPI_SYS_Exit();
        mSysInited = false;
    }
}

/* ======================== start / stop ======================== */

int RkCapturer::start()
{
    if (mRunning) {
        return 0;
    }

    /* 1. RK_MPI_SYS_Init */
    int ret = RK_MPI_SYS_Init();
    if (ret != RK_SUCCESS) {
        PRINT_ERROR(g_hCapture, "RK_MPI_SYS_Init fail %x\n", ret);
        cleanup();
        return -1;
    }
    mSysInited = true;

    if (mOsdEnabled) {
        /* ===== OSD 模式 =====
         * 不绑定 VI chn0 → VENC
         * 仅使用 VI chn1 获取 YUV 供分析
         * 外部 BGR 帧通过 sendFrame() 送入 VENC 编码
         */

        /* 2a. VI 设备 + chn1 初始化 */
        if (viInit() != 0) {
            cleanup();
            return -1;
        }
        if (viYuvInit() != 0) {
            PRINT_WARN(g_hCapture, "Warning: viYuvInit failed, analysis will not work\n");
        }

        /* 2b. NV12 DMA-BUF 分配 */
        if (nv12BufInit() != 0) {
            cleanup();
            return -1;
        }

        /* 2c. MB pool 分配 */
        int blkSize = mWidth * mHeight * 3 / 2;
        MB_POOL_CONFIG_S poolCfg;
        memset(&poolCfg, 0, sizeof(poolCfg));
        poolCfg.u64MBSize = blkSize;
        poolCfg.u32MBCnt = 4;
        poolCfg.enRemapMode = MB_REMAP_MODE_CACHED;
        poolCfg.enAllocType = MB_ALLOC_TYPE_DMA;
        poolCfg.bPreAlloc = RK_TRUE;
        mMbPool = (uint32_t)RK_MPI_MB_CreatePool(&poolCfg);
        if (mMbPool == MB_INVALID_POOLID) {
            PRINT_ERROR(g_hCapture, "RK_MPI_MB_CreatePool failed\n");
            mMbPool = (uint32_t)-1;
            cleanup();
            return -1;
        }

        /* 2d. VENC 初始化 */
        if (vencInit() != 0) {
            cleanup();
            return -1;
        }

        PRINT_INFO(g_hCapture, "RkCapturer started in OSD mode: %dx%d@%d %s\n",
               mWidth, mHeight, mFramerate, mFmt.c_str());
    } else {
        /* ===== 直通模式 =====
         * VI chn0 绑定到 VENC，ISP 自动送帧编码
         */

        /* 2. VI 初始化（设备 + chn0） */
        if (viInit() != 0) {
            cleanup();
            return -1;
        }

        /* 3. VI chn1 初始化（供算法分析） */
        if (viYuvInit() != 0) {
            PRINT_WARN(g_hCapture, "Warning: viYuvInit failed, analysis will not work\n");
        }

        /* 4. VENC 初始化 */
        if (vencInit() != 0) {
            cleanup();
            return -1;
        }

        /* 5. 绑定 VI chn0 → VENC */
        if (bindViVenc() != 0) {
            cleanup();
            return -1;
        }

        PRINT_INFO(g_hCapture, "RkCapturer started in passthrough mode: %dx%d@%d %s\n",
               mWidth, mHeight, mFramerate, mFmt.c_str());
    }

    mRunning = true;

    /* 启动编码码流取流线程 */
    pthread_create(&mStreamTid, NULL, streamThread, this);

    /* 启动 YUV 原始帧取帧线程 */
    if (mYuvCb) {
        pthread_create(&mYuvTid, NULL, yuvThread, this);
    }

    return 0;
}

void RkCapturer::stop()
{
    if (!mRunning) {
        return;
    }

    mRunning = false;

    /* 中断 VENC GetStream 的阻塞等待 */
    RK_MPI_VENC_StopRecvFrame(mVencChn);

    if (mStreamTid) {
        pthread_join(mStreamTid, NULL);
        mStreamTid = 0;
    }

    if (mYuvTid) {
        pthread_join(mYuvTid, NULL);
        mYuvTid = 0;
    }

    cleanup();

    PRINT_INFO(g_hCapture, "RkCapturer stopped\n");
}
