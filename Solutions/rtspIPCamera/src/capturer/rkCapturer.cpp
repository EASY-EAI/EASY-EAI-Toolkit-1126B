/**
 * RK_MPI VI + VENC 采集编码模块 —— 实现
 *
 * 3A server 模式：ISP 初始化由 rkaiq_3A.service（rkaiq_3A_server）负责，
 * 本模块只做 RK_MPI 层面的 VI + VENC。
 *
 * 数据流：
 *   VI chn0 ──bind──> VENC ──> GetStream 回调
 *
 * 前置条件：rkaiq_3A.service 必须已启动（systemctl start rkaiq_3A.service）
 */

#include "rkCapturer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/prctl.h>

#include "rk_mpi_sys.h"
#include "rk_mpi_vi.h"
#include "rk_mpi_venc.h"
#include "rk_mpi_mb.h"
#include "logHandle.h"

#define RK_ALIGN(x, a) (((x) + (a)-1) & ~((a)-1))
#define RK_ALIGN_2(x) RK_ALIGN(x, 2)

/* ======================== 构造 / 析构 ======================== */

RkCapturer::RkCapturer(const std::string &device, int width, int height,
                       int framerate, const std::string &fmt)
    : mDevice(device)
    , mWidth(width)
    , mHeight(height)
    , mFramerate(framerate > 0 ? framerate : 30)
    , mFmt(fmt)
    , mCb(NULL)
    , mUserData(NULL)
    , mStreamTid(0)
    , mRunning(false)
    , mDevId(0)
    , mPipeId(0)
    , mChnId(0)
    , mVencChn(0)
    , mSysInited(false)
    , mViDevEnabled(false)
    , mViChnEnabled(false)
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

    /* ---- chn0 配置（绑定到 VENC，u32Depth=0） ---- */
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

    if (mViChnEnabled) {
        RK_MPI_VI_DisableChn(mPipeId, mChnId);
        mViChnEnabled = false;
    }

    if (mVencCreated) {
        RK_MPI_VENC_StopRecvFrame(mVencChn);
        RK_MPI_VENC_DestroyChn(mVencChn);
        mVencCreated = false;
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

    /* 2. VI 初始化（设备 + chn0） */
    if (viInit() != 0) {
        cleanup();
        return -1;
    }

    /* 3. VENC 初始化 */
    if (vencInit() != 0) {
        cleanup();
        return -1;
    }

    /* 4. 绑定 VI chn0 → VENC，ISP 自动送帧编码 */
    if (bindViVenc() != 0) {
        cleanup();
        return -1;
    }

    PRINT_INFO(g_hCapture, "RkCapturer started: %dx%d@%d %s\n",
           mWidth, mHeight, mFramerate, mFmt.c_str());

    mRunning = true;

    /* 启动编码码流取流线程 */
    pthread_create(&mStreamTid, NULL, streamThread, this);

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

    cleanup();

    PRINT_INFO(g_hCapture, "RkCapturer stopped\n");
}
