//=====================  C++  =====================
#include <string>
#include <list>
#include <cstdio>
//=====================   C   =====================
#include <unistd.h>
//=====================  PRJ  =====================
#include "system_opt.h"
#include <rga/rga.h>
#include "rga_wrapper.h"
#include "display.h"
#include "display_pro.h"

#include "analyzer.h"

using namespace cv;

/* ======================== vChnObject（带 DMA-BUF） ======================== */
typedef struct {
    cv::Mat image;              /* 用于算法分析的 BGR888 Mat */
    pthread_rwlock_t imgLock;
    int chnId;
    ChnResult_t chnResult;

    /* DMA-BUF 零拷贝显示路径 */
    int dmabuf_fd;             /* NV12 DMA-BUF fd */
    int dma_width;
    int dma_height;
    int dma_horStride;
    int dma_verStride;
    pthread_rwlock_t dmaLock;  /* 保护 dmabuf_fd 字段 */
} vChnObjectEx;

/* ======================== Analyzer 类 ======================== */
class Analyzer
{
public:
	Analyzer(int32_t maxChn);
	~Analyzer();

    static Analyzer *instance() { return m_pSelf; }
    static void createAnalyzer(int32_t maxChn);

    /* DMA-BUF 零拷贝接口 */
    int32_t upDateDmaChannel(int chnId, DmaFrame_t frame);
    /* 旧 CPU 内存接口（兼容保留） */
    int32_t upDateVideoChannel(int chnId, char *imgData, ImgDesc_t imgDesc);

    vChnObject *getVideoChnObject(int chnId);
    int32_t videoChannelAnalyRes(int chnId);

    bool mAnalyzeThreadWorking;
    bool mDisplayThreadWorking;
    pthread_mutex_t mVideoChnLock;
    int32_t mMaxChnNum;

protected:
    vChnObjectEx *createVideoChnObjectEx(int32_t chnId, int32_t imgWidth, int32_t imgHeight);
    int32_t releaseVideoChnObjectEx(vChnObjectEx *pObj);
    int32_t delAllVideoChannel();

private:
    static Analyzer *m_pSelf;
	std::list<vChnObjectEx*> m_VideoChannellist;
	pthread_t mAnalyzeTid;
	pthread_t mDisplayTid;
};

/* ======================== 分析线程 ======================== */
/* 仅在需要做模型推理时，做一次 NV12 DMA-BUF → BGR888 RGA 转换。
 * 显示路径不经过此线程，直接走 window_commit_pro 零拷贝。 */
static void *imgAnalyze_thread(void *para)
{
    Analyzer *pSelf = (Analyzer *)para;

    int chnId = 0;
    ChnResult_t result;
    pSelf->mAnalyzeThreadWorking = true;
    while(1){
        if(!pSelf->mAnalyzeThreadWorking){
            msleep(5);
            break;
        }

        if(NULL == pSelf){
            msleep(5);
            break;
        }

        vChnObject *pVideoObj = pSelf->getVideoChnObject(chnId);
        if(pVideoObj){
            /* 取出待分析图像（BGR888 Mat） */
            Mat image;
            pthread_rwlock_rdlock(&pVideoObj->imgLock);
            if(!pVideoObj->image.empty())
                pVideoObj->image.copyTo(image);
            pthread_rwlock_unlock(&pVideoObj->imgLock);

            if(!image.empty()){
                result = algorithm_process(chnId, image);
            }
        }

        pVideoObj = pSelf->getVideoChnObject(chnId);
        if(pVideoObj){
            memcpy(&pVideoObj->chnResult, &result, sizeof(ChnResult_t));
        }

        chnId++;
        chnId%=pSelf->mMaxChnNum;
        msleep(20);
    }

    pthread_exit(NULL);
}

/* ======================== 显示线程（零拷贝） ======================== */
/* 直接用 window_commit_pro 把 NV12 DMA-BUF fd 交给 display 库，
 * 由 display 库内部做 RGA 硬件转换 + DRM commit。
 * OSD 画框在 UI 层叠加（如需），不阻塞显示路径。 */
static void *imgDisplay_thread(void *para)
{
    Analyzer *pSelf = (Analyzer *)para;

    /* 初始化 display pro（zero-copy 路径） */
    screen_init();
    int screenW = 0, screenH = 0, refresh = 0;
    screen_info(&screenW, &screenH, &refresh);

    display_t disp;
    memset(&disp, 0, sizeof(disp));
    disp.width = screenW;
    disp.height = screenH;
    disp_init_pro(&disp);

    /* 创建全屏窗口 */
    window_t win;
    memset(&win, 0, sizeof(win));
    win.zpos = 1;
    win.win_x = 0;
    win.win_y = 0;
    win.win_w = screenW;
    win.win_h = screenH;
    int winChn = add_window_to(DISPLAY, &win);

    bool bShowNoSig = true;
    int videoDuration = 30; //秒
    int preTimeStamp = get_timeval_ms();
    int curTimeStamp = preTimeStamp;

    int chnId = 0;
    pSelf->mDisplayThreadWorking = true;
    while(1){
        if(!pSelf->mDisplayThreadWorking){
            msleep(5);
            break;
        }

        if(NULL == pSelf){
            msleep(5);
            break;
        }

        curTimeStamp = get_timeval_ms();
        if(videoDuration*1000 <= (curTimeStamp-preTimeStamp)){
            chnId++;
            chnId%=pSelf->mMaxChnNum;
            preTimeStamp = curTimeStamp;
        }

        vChnObject *pVideoObj = pSelf->getVideoChnObject(chnId);
        if(pVideoObj){
            /* 取出 DMA-BUF fd */
            vChnObjectEx *pEx = (vChnObjectEx *)pVideoObj;
            int dmabuf_fd = -1, dmaW = 0, dmaH = 0, dmaHor = 0;

            pthread_rwlock_rdlock(&pEx->dmaLock);
            dmabuf_fd = pEx->dmabuf_fd;
            dmaW = pEx->dma_width;
            dmaH = pEx->dma_height;
            dmaHor = pEx->dma_horStride;
            pthread_rwlock_unlock(&pEx->dmaLock);

            if(dmabuf_fd >= 0 && dmaW > 0 && dmaH > 0){
                /* 零拷贝：直接把 NV12 DMA-BUF fd 交给 display */
                display_dmabuf_frame_t frame;
                memset(&frame, 0, sizeof(frame));
                frame.dmabuf_fd = dmabuf_fd;
                frame.width = dmaW;
                frame.height = dmaH;
                frame.pitch_bytes = dmaHor;
                frame.rotation = 0;
                frame.rga_format = RK_FORMAT_YCbCr_420_SP; /* NV12 */

                window_commit_pro(winChn, &frame);
                window_refresh_pro();
                bShowNoSig = true;
            }else if(bShowNoSig){
                /* 无信号时也刷新一次（黑屏） */
                window_refresh_pro();
                bShowNoSig = false;
            }
        }

        msleep(15);
    }

    if(winChn >= 0)
        remove_window_from(DISPLAY, winChn);
    disp_release_pro();
    screen_exit();
    pthread_exit(NULL);
}

/* ======================== Analyzer 实现 ======================== */
Analyzer *Analyzer::m_pSelf = NULL;
Analyzer::Analyzer(int32_t maxChn) :
    mAnalyzeThreadWorking(false),
    mDisplayThreadWorking(false),
    mMaxChnNum(maxChn)
{
    pthread_mutex_init(&mVideoChnLock, NULL);

    if(0 != CreateJoinThread(imgAnalyze_thread, this, &mAnalyzeTid)){
        return;
    }
    if(0 != CreateJoinThread(imgDisplay_thread, this, &mDisplayTid)){
        return;
    }
}
Analyzer::~Analyzer()
{
    int timeOut_ms = 1000;
    while(1){
        if(((true == mDisplayThreadWorking)&&(true == mAnalyzeThreadWorking))||(timeOut_ms <= 0)){
            break;
        }
        timeOut_ms--;
        usleep(1000);
    }
    mAnalyzeThreadWorking = false;
    while(1) {
        usleep(20*1000);
        int32_t exitCode = pthread_join(mAnalyzeTid, NULL);
        if(0 == exitCode){
            break;
        }else if(0 != exitCode){
            switch (exitCode) {
                case ESRCH:
                    fprintf(stderr, "imgAnalyze_thread exit: No thread with the given ID was found.\n");
                    break;
                case EINVAL:
                    fprintf(stderr, "imgAnalyze_thread exit: Thread is detached or already being waited on.\n");
                    break;
                case EDEADLK:
                    fprintf(stderr, "imgAnalyze_thread exit: Deadlock detected - thread is trying to join itself.\n");
                    break;
            }
            continue;
        }
    }
    mDisplayThreadWorking = false;
    while(1) {
        usleep(20*1000);
        int32_t exitCode = pthread_join(mDisplayTid, NULL);
        if(0 == exitCode){
            break;
        }else if(0 != exitCode){
            switch (exitCode) {
                case ESRCH:
                    fprintf(stderr, "imgDisplay_thread exit: No thread with the given ID was found.\n");
                    break;
                case EINVAL:
                    fprintf(stderr, "imgDisplay_thread exit: Thread is detached or already being waited on.\n");
                    break;
                case EDEADLK:
                    fprintf(stderr, "imgDisplay_thread exit: Deadlock detected - thread is trying to join itself.\n");
                    break;
            }
            continue;
        }
    }

    delAllVideoChannel();
    pthread_mutex_destroy(&mVideoChnLock);
}
void Analyzer::createAnalyzer(int32_t maxChn)
{
    if(m_pSelf == NULL) {
        m_pSelf = new Analyzer(maxChn);
   }
}

/* ======================== DMA-BUF 零拷贝更新 ======================== */
int32_t Analyzer::upDateDmaChannel(int chnId, DmaFrame_t frame)
{
    if(chnId < 0)
        return -1;

    pthread_mutex_lock(&mVideoChnLock);
    vChnObjectEx* targetObj = nullptr;
    for (auto it = m_VideoChannellist.begin(); it != m_VideoChannellist.end(); ++it) {
        if ((*it)->chnId == chnId) {
            targetObj = *it;
            /* 分辨率变化时重建 Mat */
            if((targetObj->dma_width != frame.width)||(targetObj->dma_height != frame.height)){
                if(0 == releaseVideoChnObjectEx(targetObj)){
                    it = m_VideoChannellist.erase(it);
                }else{
                    pthread_mutex_unlock(&mVideoChnLock);
                    return -2;
                }
                targetObj = nullptr;
            }
            break;
        }
    }

    if (!targetObj) {
        targetObj = createVideoChnObjectEx(chnId, frame.width, frame.height);
        if(!targetObj)
            return -3;
        m_VideoChannellist.push_back(targetObj);
    }
    pthread_mutex_unlock(&mVideoChnLock);

    /* 更新 DMA-BUF fd（显示路径零拷贝） */
    pthread_rwlock_wrlock(&targetObj->dmaLock);
    targetObj->dmabuf_fd = frame.dmabuf_fd;
    targetObj->dma_width = frame.width;
    targetObj->dma_height = frame.height;
    targetObj->dma_horStride = frame.horStride;
    targetObj->dma_verStride = frame.verStride;
    pthread_rwlock_unlock(&targetObj->dmaLock);

    /* 分析路径：做一次 NV12→BGR888 RGA 转换（仅用于模型推理） */
    if(frame.dmabuf_fd >= 0){
        size_t mapSize = (size_t)frame.horStride * frame.verStride * 3 / 2;
        void *nv12Data = mmap(NULL, mapSize, PROT_READ, MAP_SHARED,
                              frame.dmabuf_fd, 0);
        if(nv12Data != MAP_FAILED){
            Image srcImage, dstImage;
            memset(&srcImage, 0, sizeof(srcImage));
            memset(&dstImage, 0, sizeof(dstImage));
            srcImage.fmt = RK_FORMAT_YCbCr_420_SP;
            srcImage.width = frame.width;
            srcImage.height = frame.height;
            srcImage.hor_stride = frame.horStride;
            srcImage.ver_stride = frame.verStride;
            srcImage.rotation = HAL_TRANSFORM_ROT_0;
            srcImage.fd = frame.dmabuf_fd;
            srcImage.pBuf = nv12Data;

            dstImage.fmt = RK_FORMAT_BGR_888;
            dstImage.width = targetObj->image.cols;
            dstImage.height = targetObj->image.rows;
            dstImage.hor_stride = targetObj->image.cols;
            dstImage.ver_stride = targetObj->image.rows;
            dstImage.rotation = HAL_TRANSFORM_ROT_0;
            dstImage.fd = -1;
            dstImage.pBuf = (void *)targetObj->image.data;

            pthread_rwlock_wrlock(&targetObj->imgLock);
            srcImg_ConvertTo_dstImg(&dstImage, &srcImage);
            pthread_rwlock_unlock(&targetObj->imgLock);

            munmap(nv12Data, mapSize);
        }
    }

    return 0;
}

/* 旧的 CPU 内存接口（兼容保留） */
int32_t Analyzer::upDateVideoChannel(int chnId, char *imgData, ImgDesc_t imgDesc)
{
    if(chnId < 0)
        return -1;

    pthread_mutex_lock(&mVideoChnLock);
    vChnObjectEx* targetObj = nullptr;
    for (auto it = m_VideoChannellist.begin(); it != m_VideoChannellist.end(); ++it) {
        if ((*it)->chnId == chnId) {
            targetObj = *it;
            if((targetObj->image.cols != imgDesc.width)||(targetObj->image.rows != imgDesc.height)){
                if(0 == releaseVideoChnObjectEx(targetObj)){
                    it = m_VideoChannellist.erase(it);
                }else{
                    pthread_mutex_unlock(&mVideoChnLock);
                    return -2;
                }
                targetObj = nullptr;
            }
            break;
        }
    }

    if (!targetObj) {
        targetObj = createVideoChnObjectEx(chnId, imgDesc.width, imgDesc.height);
        if(!targetObj)
            return -3;
        m_VideoChannellist.push_back(targetObj);
    }
    pthread_mutex_unlock(&mVideoChnLock);

    Image srcImage, dstImage;
    memset(&srcImage, 0, sizeof(srcImage));
    memset(&dstImage, 0, sizeof(dstImage));
    srcImage.fmt = rgaFmt(imgDesc.fmt);
    srcImage.width = imgDesc.width;
    srcImage.height = imgDesc.height;
    srcImage.hor_stride = imgDesc.horStride;
    srcImage.ver_stride = imgDesc.verStride;
    srcImage.rotation = HAL_TRANSFORM_ROT_0;
    srcImage.pBuf = imgData;

    dstImage.fmt = RK_FORMAT_BGR_888;
    dstImage.width = targetObj->image.cols;
    dstImage.height = targetObj->image.rows;
    dstImage.hor_stride = targetObj->image.cols;
    dstImage.ver_stride = targetObj->image.rows;
    dstImage.rotation = HAL_TRANSFORM_ROT_0;
    dstImage.pBuf = (void *)targetObj->image.data;

    pthread_rwlock_wrlock(&targetObj->imgLock);
    srcImg_ConvertTo_dstImg(&dstImage, &srcImage);
    pthread_rwlock_unlock(&targetObj->imgLock);
    return 0;
}

vChnObject *Analyzer::getVideoChnObject(int chnId)
{
    if(chnId < 0)
        return NULL;

    vChnObjectEx* targetObj = nullptr;
    pthread_mutex_lock(&mVideoChnLock);
    for (auto it = m_VideoChannellist.begin(); it != m_VideoChannellist.end(); ++it) {
        if ((*it)->chnId == chnId) {
            targetObj = *it;
            break;
        }
    }
    pthread_mutex_unlock(&mVideoChnLock);

    /* vChnObjectEx 的前几个字段与 vChnObject 布局兼容
     * (image, imgLock, chnId, chnResult) */
    return (vChnObject *)targetObj;
}

vChnObjectEx *Analyzer::createVideoChnObjectEx(int32_t chnId, int32_t imgWidth, int32_t imgHeight)
{
    vChnObjectEx* newChnObj = new vChnObjectEx;
    if(!newChnObj)
        return NULL;

    pthread_rwlock_init(&newChnObj->imgLock, nullptr);
    pthread_rwlock_init(&newChnObj->dmaLock, nullptr);

    newChnObj->chnId = chnId;
    newChnObj->image = Mat(imgHeight, imgWidth, CV_8UC3, Scalar(0, 255, 0));
    memset(&newChnObj->chnResult, 0, sizeof(ChnResult_t));

    newChnObj->dmabuf_fd = -1;
    newChnObj->dma_width = 0;
    newChnObj->dma_height = 0;
    newChnObj->dma_horStride = 0;
    newChnObj->dma_verStride = 0;

    return newChnObj;
}

int32_t Analyzer::releaseVideoChnObjectEx(vChnObjectEx *pObj)
{
    if(NULL == pObj)
        return -1;

    pthread_rwlock_wrlock(&pObj->imgLock);
    pObj->image.release();
    pthread_rwlock_unlock(&pObj->imgLock);
    pthread_rwlock_destroy(&pObj->imgLock);

    pthread_rwlock_destroy(&pObj->dmaLock);

    delete pObj;
    return 0;
}

int32_t Analyzer::delAllVideoChannel()
{
    pthread_mutex_lock(&mVideoChnLock);
    for (auto it = m_VideoChannellist.begin(); it != m_VideoChannellist.end(); ++it) {
        if(0 == releaseVideoChnObjectEx(*it)){
            it = m_VideoChannellist.erase(it);
        }
    }
    pthread_mutex_unlock(&mVideoChnLock);
    return 0;
}

/* ======================== C 接口 ======================== */
int analyzer_init(int32_t maxChn)
{
    Analyzer::createAnalyzer(maxChn);
    algorithm_init();
    return 0;
}

int videoOutHandle(char *imgData, ImgDesc_t imgDesc)
{
    Analyzer *pAnalyzer = Analyzer::instance();
    if(pAnalyzer){
        pAnalyzer->upDateVideoChannel(imgDesc.chnId, imgData, imgDesc);
    }
    return 0;
}

int videoDmaHandle(DmaFrame_t frame)
{
    Analyzer *pAnalyzer = Analyzer::instance();
    if(pAnalyzer){
        pAnalyzer->upDateDmaChannel(frame.chnId, frame);
    }
    return 0;
}
