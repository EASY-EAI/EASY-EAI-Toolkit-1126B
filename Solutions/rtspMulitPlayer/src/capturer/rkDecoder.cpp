/**
 * RK_MPI VDEC 硬件解码模块 —— 实现
 *
 * 流程：
 *   1. RK_MPI_SYS_Init()
 *   2. RK_MPI_VDEC_CreateChn() — 创建 VDEC 通道
 *   3. RK_MPI_VDEC_StartRecvStream() — 开始接收码流
 *   4. pushData() → 入队（非阻塞）→ pushThread 异步 RK_MPI_MMZ_Alloc + RK_MPI_VDEC_SendStream
 *   5. getFrameThread → RK_MPI_VDEC_GetFrame — 获取解码后 NV12 帧
 *   6. 回调上层，直接传递 DMA-BUF fd（zero-copy，由 display 库做 RGA 转换）
 *
 * 关键设计：
 *   - pushData 非阻塞，仅拷贝数据入队列，避免阻塞回调线程
 *   - pushThread 独立线程送流，SendStream 使用有限超时（100ms）防止死锁
 *   - 队列满时丢旧帧，保证实时性
 */

#include "rkDecoder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "log_manager_pro.h"

#include "rk_mpi_sys.h"
#include "rk_mpi_vdec.h"
#include "rk_mpi_mmz.h"
#include "rk_mpi_mb.h"

#define VDEC_CHN_ID  0

/* ======================== 日志句柄 ======================== */
static log_mgr_t    s_mgr  = LOG_MGR_INVALID;
static log_handle_t s_hLog = LOG_HANDLE_INVALID;
static log_handle_t getLogHandle()
{
    if (s_hLog < 0) {
        if (s_mgr < 0) s_mgr = log_manager_init("/userdata/logs/rtspMulitPlayer.ini");
        s_hLog = log_register(s_mgr, "rkdec");
    }
    return s_hLog;
}

/* ======================== 构造 / 析构 ======================== */

RkDecoder::RkDecoder(const std::string &fmt, int width, int height)
    : mFmt(fmt)
    , mWidth(width)
    , mHeight(height)
    , mCb(NULL)
    , mUserData(NULL)
    , mGetTid(0)
    , mPushTid(0)
    , mRunning(false)
    , mSysInited(false)
    , mVdecCreated(false)
{
    pthread_mutex_init(&m_pushMutex, NULL);
    sem_init(&m_pushSem, 0, 0);
}

RkDecoder::~RkDecoder()
{
    stop();
    pthread_mutex_destroy(&m_pushMutex);
    sem_destroy(&m_pushSem);
}

void RkDecoder::setDecodedCallback(DecodedCallback cb, void *userData)
{
    mCb = cb;
    mUserData = userData;
}

/* ======================== VDEC 初始化 ======================== */
int RkDecoder::vdecInit()
{
    RK_CODEC_ID_E codec_id;
    if (mFmt == "h265") {
        codec_id = RK_VIDEO_ID_HEVC;
    } else {
        codec_id = RK_VIDEO_ID_AVC;
    }

    VDEC_CHN_ATTR_S vdec_chn_attr;
    memset(&vdec_chn_attr, 0, sizeof(VDEC_CHN_ATTR_S));
    vdec_chn_attr.enType = codec_id;
    vdec_chn_attr.enMode = VIDEO_MODE_FRAME;
    vdec_chn_attr.u32PicWidth = mWidth;
    vdec_chn_attr.u32PicHeight = mHeight;
    vdec_chn_attr.u32FrameBufCnt = 8;
    vdec_chn_attr.u32StreamBufCnt = 8;
    vdec_chn_attr.u32FrameBufDepth = 8;
    vdec_chn_attr.enPixelFormat = RK_FMT_YUV420SP;
    vdec_chn_attr.enCompressMode = COMPRESS_MODE_NONE;

    int ret = RK_MPI_VDEC_CreateChn(VDEC_CHN_ID, &vdec_chn_attr);
    if (ret != RK_SUCCESS) {
        fprintf(stderr, "[rkdec] RK_MPI_VDEC_CreateChn fail 0x%x\n", ret);
        PRINT_ERROR(getLogHandle(), "RK_MPI_VDEC_CreateChn fail %x\n", ret);
        return -1;
    }

    ret = RK_MPI_VDEC_StartRecvStream(VDEC_CHN_ID);
    if (ret != RK_SUCCESS) {
        fprintf(stderr, "[rkdec] RK_MPI_VDEC_StartRecvStream fail 0x%x\n", ret);
        PRINT_ERROR(getLogHandle(), "RK_MPI_VDEC_StartRecvStream fail %x\n", ret);
        RK_MPI_VDEC_DestroyChn(VDEC_CHN_ID);
        return -1;
    }

    mVdecCreated = true;
    return 0;
}

/* ======================== 清理 ======================== */
void RkDecoder::cleanup()
{
    if (mVdecCreated) {
        RK_MPI_VDEC_StopRecvStream(VDEC_CHN_ID);
        RK_MPI_VDEC_DestroyChn(VDEC_CHN_ID);
        mVdecCreated = false;
    }

    if (mSysInited) {
        RK_MPI_SYS_Exit();
        mSysInited = false;
    }
}

/* ======================== 取帧线程 ======================== */
void *RkDecoder::getFrameThread(void *para)
{
    RkDecoder *self = (RkDecoder *)para;
    int ret;
    VIDEO_FRAME_INFO_S sFrame;
    uint32_t timeout_cnt = 0;
    uint32_t getframe_cnt = 0;

    PRINT_INFO(getLogHandle(), "getFrame thread started\n");

    while (self->mRunning) {
        memset(&sFrame, 0, sizeof(VIDEO_FRAME_INFO_S));
        ret = RK_MPI_VDEC_GetFrame(VDEC_CHN_ID, &sFrame, 1000);
        if (ret == RK_SUCCESS) {
            timeout_cnt = 0;
            getframe_cnt++;
            /* 检查是否 EOS */
            if ((sFrame.stVFrame.u32FrameFlag & (RK_U32)FRAME_FLAG_SNAP_END) == (RK_U32)FRAME_FLAG_SNAP_END) {
                RK_MPI_VDEC_ReleaseFrame(VDEC_CHN_ID, &sFrame);
                PRINT_INFO(getLogHandle(), "reach eos frame\n");
                continue;
            }

            int width  = sFrame.stVFrame.u32Width;
            int height = sFrame.stVFrame.u32Height;
            int hor_stride = sFrame.stVFrame.u32VirWidth;
            int ver_stride = sFrame.stVFrame.u32VirHeight;
            int fd = -1;

            if (width > 0 && height > 0 && self->mCb) {
                /* flush cache 使 VDEC 输出数据对设备可见 */
                RK_MPI_SYS_MmzFlushCache(sFrame.stVFrame.pMbBlk, RK_TRUE);

                /* 直接传递 DMA-BUF fd 给上层（zero-copy） */
                fd = RK_MPI_MB_Handle2Fd(sFrame.stVFrame.pMbBlk);
                self->mCb(fd, width, height, hor_stride, ver_stride, self->mUserData);
            }

            RK_MPI_VDEC_ReleaseFrame(VDEC_CHN_ID, &sFrame);

            if ((getframe_cnt % 30) == 0) {
                PRINT_DEBUG(getLogHandle(), "getFrame alive: cnt=%u %dx%d fd=%d\n",
                            getframe_cnt, width, height, fd);
            }
        } else {
            /* GetFrame 超时或出错，打印日志帮助诊断 */
            timeout_cnt++;
            if (timeout_cnt == 1) {
                PRINT_WARN(getLogHandle(), "VDEC_GetFrame timeout/error ret=0x%x (first occurrence)\n", ret);
            } else if (timeout_cnt % 10 == 0) {
                PRINT_WARN(getLogHandle(), "VDEC_GetFrame timeout/error ret=0x%x (count=%u)\n", ret, timeout_cnt);
            }
        }
    }

    PRINT_DEBUG(getLogHandle(), "getFrame thread exiting (cnt=%u)\n", getframe_cnt);
    return NULL;
}

/* ======================== 送流线程（生产者-消费者） ======================== */
void *RkDecoder::pushThread(void *para)
{
    RkDecoder *self = (RkDecoder *)para;
    uint32_t push_cnt = 0;
    uint32_t send_fail_cnt = 0;

    PRINT_INFO(getLogHandle(), "push thread started\n");

    while (self->mRunning) {
        /* 等待队列有数据，超时 500ms 以便检查 mRunning */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 500 * 1000 * 1000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000;
        }
        int sret = sem_timedwait(&self->m_pushSem, &ts);
        if (sret != 0) {
            continue;  /* 超时，循环检查 mRunning */
        }

        /* 取出队首数据 */
        PushItem item;
        bool hasItem = false;
        pthread_mutex_lock(&self->m_pushMutex);
        if (!self->m_pushQueue.empty()) {
            item = self->m_pushQueue.front();
            self->m_pushQueue.pop();
            hasItem = true;
        }
        pthread_mutex_unlock(&self->m_pushMutex);

        if (!hasItem)
            continue;

        /* 分配 MB_BLK 并送入 VDEC */
        MB_BLK mb_blk;
        int ret = RK_MPI_MMZ_Alloc(&mb_blk, item.len, 0);
        if (ret != RK_SUCCESS) {
            PRINT_ERROR(getLogHandle(), "RK_MPI_MMZ_Alloc fail %x (len=%u)\n", ret, item.len);
            free(item.data);
            continue;
        }

        void *virAddr = RK_MPI_MMZ_Handle2VirAddr(mb_blk);
        if (!virAddr) {
            RK_MPI_MMZ_Free(mb_blk);
            free(item.data);
            continue;
        }

        memcpy(virAddr, item.data, item.len);
        free(item.data);

        VDEC_STREAM_S vdec_stream;
        memset(&vdec_stream, 0, sizeof(vdec_stream));
        vdec_stream.pMbBlk = mb_blk;
        vdec_stream.u32Len = item.len;
        vdec_stream.u64PTS = 0;
        vdec_stream.bEndOfStream = RK_FALSE;
        vdec_stream.bEndOfFrame = RK_TRUE;

        /* 使用有限超时（100ms），避免 VDEC 缓冲区满时无限阻塞 */
        ret = RK_MPI_VDEC_SendStream(VDEC_CHN_ID, &vdec_stream, 100);
        if (ret != RK_SUCCESS) {
            send_fail_cnt++;
            PRINT_WARN(getLogHandle(), "RK_MPI_VDEC_SendStream fail/timeout 0x%x (len=%u)\n", ret, item.len);
        }

        RK_MPI_MMZ_Free(mb_blk);

        push_cnt++;
        if ((push_cnt % 30) == 0) {
            int qsize = 0;
            pthread_mutex_lock(&self->m_pushMutex);
            qsize = (int)self->m_pushQueue.size();
            pthread_mutex_unlock(&self->m_pushMutex);
            PRINT_DEBUG(getLogHandle(), "pushThread alive: cnt=%u qsize=%d send_fail=%u\n",
                        push_cnt, qsize, send_fail_cnt);
        }
    }

    PRINT_DEBUG(getLogHandle(), "push thread exiting (cnt=%u send_fail=%u)\n", push_cnt, send_fail_cnt);
    return NULL;
}

/* ======================== start / stop ======================== */

int RkDecoder::start()
{
    if (mRunning) {
        return 0;
    }

    /* 抑制 rockit 刷屏日志（mb_create_buffer 等）。
     * 通过环境变量 rt_log_level 在 RK_MPI_SYS_Init 之前设置日志级别。
     * 日志级别: 0=关闭 1=FATAL 2=ERR 3=WARN 4=INFO 5=DEBUG 6=VERBOSE
     */
    setenv("rt_log_level", "3", 1);

    /* 1. RK_MPI_SYS_Init */
    int ret = RK_MPI_SYS_Init();
    if (ret != RK_SUCCESS) {
        fprintf(stderr, "[rkdec] RK_MPI_SYS_Init fail 0x%x\n", ret);
        PRINT_ERROR(getLogHandle(), "RK_MPI_SYS_Init fail %x\n", ret);
        return -1;
    }
    mSysInited = true;
    fprintf(stderr, "[rkdec] RK_MPI_SYS_Init OK\n");

    /* 2. VDEC 初始化 */
    if (vdecInit() != 0) {
        fprintf(stderr, "[rkdec] vdecInit failed\n");
        cleanup();
        return -1;
    }
    fprintf(stderr, "[rkdec] VDEC init OK\n");

    mRunning = true;
    pthread_create(&mGetTid, NULL, getFrameThread, this);
    pthread_create(&mPushTid, NULL, pushThread, this);

    fprintf(stderr, "[rkdec] RkDecoder started: fmt=%s %dx%d\n",
           mFmt.c_str(), mWidth, mHeight);
    PRINT_INFO(getLogHandle(), "RkDecoder started: fmt=%s %dx%d\n",
           mFmt.c_str(), mWidth, mHeight);

    return 0;
}

void RkDecoder::stop()
{
    if (!mRunning) {
        return;
    }

    mRunning = false;

    /* 唤醒 push 线程使其退出 */
    sem_post(&m_pushSem);
    if (mPushTid) {
        pthread_join(mPushTid, NULL);
        mPushTid = 0;
    }
    flushPushQueue();

    /* 发送 EOS 以唤醒 GetFrame 的阻塞等待 */
    if (mVdecCreated) {
        MB_BLK mb_blk;
        if (RK_MPI_MMZ_Alloc(&mb_blk, 1024, 0) == RK_SUCCESS) {
            VDEC_STREAM_S vdec_stream;
            memset(&vdec_stream, 0, sizeof(vdec_stream));
            vdec_stream.pMbBlk = mb_blk;
            vdec_stream.u32Len = 1024;
            vdec_stream.u64PTS = 0;
            vdec_stream.bEndOfStream = RK_TRUE;
            RK_MPI_VDEC_SendStream(VDEC_CHN_ID, &vdec_stream, 100);
            RK_MPI_MMZ_Free(mb_blk);
        }
    }

    if (mGetTid) {
        pthread_join(mGetTid, NULL);
        mGetTid = 0;
    }

    cleanup();

    PRINT_INFO(getLogHandle(), "RkDecoder stopped\n");
}

/* ======================== flushPushQueue ======================== */
void RkDecoder::flushPushQueue()
{
    pthread_mutex_lock(&m_pushMutex);
    while (!m_pushQueue.empty()) {
        PushItem &item = m_pushQueue.front();
        free(item.data);
        m_pushQueue.pop();
    }
    pthread_mutex_unlock(&m_pushMutex);
}

/* ======================== pushData ======================== */
/* 非阻塞：将数据拷贝入队列，由 pushThread 异步送入 VDEC。
 * 队列满时丢弃最旧帧，保证实时性。 */
int RkDecoder::pushData(const uint8_t *data, uint32_t len)
{
    if (!mRunning || !data || len == 0) {
        return -1;
    }

    /* 拷贝一份数据入队列 */
    uint8_t *buf = (uint8_t *)malloc(len);
    if (!buf) {
        PRINT_ERROR(getLogHandle(), "pushData malloc fail (len=%u)\n", len);
        return -1;
    }
    memcpy(buf, data, len);

    PushItem item;
    item.data = buf;
    item.len  = len;

    pthread_mutex_lock(&m_pushMutex);

    /* 队列满时丢掉最旧帧，避免积压导致延迟 */
    while ((int)m_pushQueue.size() >= PUSH_QUEUE_MAX) {
        PushItem &old = m_pushQueue.front();
        free(old.data);
        m_pushQueue.pop();
        PRINT_WARN(getLogHandle(), "push queue full, dropped old frame (qsize=%d)\n", PUSH_QUEUE_MAX);
    }

    m_pushQueue.push(item);
    pthread_mutex_unlock(&m_pushMutex);

    sem_post(&m_pushSem);
    return 0;
}
