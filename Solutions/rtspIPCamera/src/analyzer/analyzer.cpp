//=====================  C++  =====================
#include <string>
#include <list>
//=====================   C   =====================
#include "system.h"
#include <sys/ioctl.h>
//=====================  SDK  =====================
#include "system_opt.h"
#include "rga_wrapper.h"
//=====================  PRJ  =====================
#include "logHandle.h"
#include "analyzer.h"

/* DMA-BUF sync definitions (same as rga_wrapper.c) */
typedef unsigned long long __u64;
struct dma_buf_sync_local {
    __u64 flags;
};
#define DMA_BUF_BASE_LOCAL       'b'
#define DMA_BUF_IOCTL_SYNC_LOCAL _IOW(DMA_BUF_BASE_LOCAL, 0, struct dma_buf_sync_local)

using namespace cv;

static Scalar colorArray[10]={
    Scalar(0, 0, 255, 255),
    Scalar(0, 255, 0, 255),
    Scalar(139,0,0,255),
    Scalar(0,100,0,255),
    Scalar(0,139,139,255),
    Scalar(0,206,209,255),
    Scalar(255,127,0,255),
    Scalar(72,61,139,255),
    Scalar(0,255,0,255),
    Scalar(0,0,255,255),
};

static int plot_one_box(Mat src, int x1, int x2, int y1, int y2, char *label, char colour)
{
    int tl = round(0.002 * (src.rows + src.cols) / 2) + 1;
    rectangle(src, cv::Point(x1, y1), cv::Point(x2, y2), colorArray[(unsigned char)colour], 3);

    int tf = max(tl -1, 1);

    int base_line = 0;
    cv::Size t_size = getTextSize(label, FONT_HERSHEY_SIMPLEX, (float)tl/3, tf, &base_line);
    int x3 = x1 + t_size.width;
    int y3 = y1 - t_size.height - 3;

    rectangle(src, cv::Point(x1, y1), cv::Point(x3, y3), colorArray[(unsigned char)colour], -1);
    putText(src, label, cv::Point(x1, y1 - 2), FONT_HERSHEY_SIMPLEX, (float)tl/3, cv::Scalar(255, 255, 255, 255), tf, 8);
    return 0;
}

static void paint_algorithm_result(Mat image, ChnResult_t result)
{
    char text[256];
    for (int algoIndex = 0; algoIndex < ALGOMAXNUM; algoIndex++){
        for (int j = 0; j < result.algoRes[algoIndex].resNumber; j++) {
            detect_result_t *det_result = &(result.algoRes[algoIndex].detect_Group.results[j]);
            if(det_result->prop < 0.4) {
                continue;
            }

            sprintf(text, "%s %.1f%%", det_result->name, det_result->prop * 100);
            int x1 = det_result->box.left;
            int y1 = det_result->box.top;
            int x2 = det_result->box.right;
            int y2 = det_result->box.bottom;
            plot_one_box(image, x1, x2, y1, y2, text, j%10);
        }
    }
}

class Analyzer
{
public:
    Analyzer(int32_t maxChn);
    ~Analyzer();

    static Analyzer *instance() { return m_pSelf; }
    static void createAnalyzer(int32_t maxChn);

    int32_t upDateVideoChannel(int chnId, char *imgData, ImgDesc_t imgDesc);
    vChnObject *getVideoChnObject(int chnId);
    uint8_t* videoChannelData(vChnObject *pVideoObj, int &width, int &height);
    int32_t videoChannelAnalyRes(int chnId);

    /* 获取指定通道已画 OSD 的 BGR888 帧数据 */
    int getOsdFrame(int chnId, uint8_t **bgrData, int *width, int *height, int *dmaFd);

    bool mAnalyzeThreadWorking;
    bool mPaintBoxThreadWorking;
    pthread_mutex_t mVideoChnLock;
    int32_t mMaxChnNum;

protected:
    vChnObject *createVideoChnObject(int32_t chnId, int32_t imgWidth, int32_t imgHeight);
    int32_t releaseVideoChnObject(vChnObject *pObj);
    int32_t delAllVideoChannel();

private:
    static Analyzer *m_pSelf;
    std::list<vChnObject*> m_VideoChannellist;
    pthread_t mAnalyzeTid;
    pthread_t mPaintBoxTid;
};

static void *imgAnalyze_thread(void *para)
{
    Analyzer *pSelf = (Analyzer *)para;

    int chnId = 0;
    Mat image;
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
            pthread_rwlock_rdlock(&pVideoObj->imgLock);
            pVideoObj->image.copyTo(image);
            pthread_rwlock_unlock(&pVideoObj->imgLock);

            result = algorithm_process(chnId, image);
        }

        pVideoObj = pSelf->getVideoChnObject(chnId);
        if(pVideoObj){
            memcpy(&pVideoObj->chnResult, &result, sizeof(ChnResult_t));
        }

        chnId++;
        chnId %= pSelf->mMaxChnNum;
        msleep(20);
    }

    pthread_exit(NULL);
}

static void *paintBox_thread(void *para)
{
    Analyzer *pSelf = (Analyzer *)para;

    int chnId = 0;
    ChnResult_t result;
    pSelf->mPaintBoxThreadWorking = true;
    while(1){
        if(!pSelf->mPaintBoxThreadWorking){
            msleep(5);
            break;
        }

        if(NULL == pSelf){
            msleep(5);
            break;
        }

        for(chnId = 0; chnId < pSelf->mMaxChnNum; chnId++){
            vChnObject *pVideoObj = pSelf->getVideoChnObject(chnId);
            if(pVideoObj){
                pthread_rwlock_rdlock(&pVideoObj->imgLock);
                int ic = pVideoObj->image.cols;
                int ir = pVideoObj->image.rows;
                memset(&result, 0, sizeof(ChnResult_t));
                memcpy(&result, &pVideoObj->chnResult, sizeof(ChnResult_t));
                pthread_rwlock_unlock(&pVideoObj->imgLock);

                if (ic > 0 && ir > 0) {
                    Mat drawOverlay(ir, ic, CV_8UC3);
                    drawOverlay.setTo(Scalar(0, 0, 0));
                    paint_algorithm_result(drawOverlay, result);
                    /* 将 OSD 叠加到原图 */
                    pthread_rwlock_wrlock(&pVideoObj->imgLock);
                    cv::addWeighted(pVideoObj->image, 1.0, drawOverlay, 1.0, 0, pVideoObj->image);
                    pthread_rwlock_unlock(&pVideoObj->imgLock);
                }
            }
        }
        msleep(15);
    }

    pthread_exit(NULL);
}

Analyzer *Analyzer::m_pSelf = NULL;
Analyzer::Analyzer(int32_t maxChn) :
    mAnalyzeThreadWorking(false),
    mPaintBoxThreadWorking(false),
    mMaxChnNum(maxChn)
{
    pthread_mutex_init(&mVideoChnLock, NULL);

    if(0 != CreateJoinThread(imgAnalyze_thread, this, &mAnalyzeTid)){
        return ;
    }
    if(0 != CreateJoinThread(paintBox_thread, this, &mPaintBoxTid)){
        return ;
    }
}

Analyzer::~Analyzer()
{
    int timeOut_ms = 1000;
    while(1){
        if(((true == mAnalyzeThreadWorking)&&(true == mPaintBoxThreadWorking))||(timeOut_ms <= 0)){
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
        }
    }
    mPaintBoxThreadWorking = false;
    while(1) {
        usleep(20*1000);
        int32_t exitCode = pthread_join(mPaintBoxTid, NULL);
        if(0 == exitCode){
            break;
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

int32_t Analyzer::upDateVideoChannel(int chnId, char *imgData, ImgDesc_t imgDesc)
{
    if(chnId < 0)
        return -1;

    pthread_mutex_lock(&mVideoChnLock);
    vChnObject* targetObj = nullptr;
    for (auto it = m_VideoChannellist.begin(); it != m_VideoChannellist.end(); ++it) {
        if ((*it)->chnId == chnId) {
            targetObj = *it;

            if((targetObj->image.cols != imgDesc.width)||(targetObj->image.rows != imgDesc.height)){
                if(0 == releaseVideoChnObject(targetObj)){
                    it = m_VideoChannellist.erase(it);
                }else{
                    pthread_mutex_unlock(&mVideoChnLock);
                    return -2;
                }
            }

            break;
        }
    }

    if (!targetObj) {
        targetObj = createVideoChnObject(chnId, imgDesc.width, imgDesc.height);
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
    srcImage.fd = -1;
    srcImage.pBuf = imgData;

    dstImage.fmt = RK_FORMAT_BGR_888;
    dstImage.width = targetObj->image.cols;
    dstImage.height = targetObj->image.rows;
    dstImage.hor_stride = targetObj->image.cols;
    dstImage.ver_stride = targetObj->image.rows;
    dstImage.rotation = HAL_TRANSFORM_ROT_0;
    dstImage.fd = targetObj->dma.fd;
    dstImage.pBuf = (void *)targetObj->dma.pBuffer;

    pthread_rwlock_wrlock(&targetObj->imgLock);
    srcImg_ConvertTo_dstImg(&dstImage, &srcImage);
    pthread_rwlock_unlock(&targetObj->imgLock);
    return 0;
}

vChnObject *Analyzer::getVideoChnObject(int chnId)
{
    if(chnId < 0)
        return NULL;

    vChnObject* targetObj = nullptr;
    pthread_mutex_lock(&mVideoChnLock);
    for (auto it = m_VideoChannellist.begin(); it != m_VideoChannellist.end(); ++it) {
        if ((*it)->chnId == chnId) {
            targetObj = *it;
            break;
        }
    }
    pthread_mutex_unlock(&mVideoChnLock);

    return targetObj;
}

int Analyzer::getOsdFrame(int chnId, uint8_t **bgrData, int *width, int *height, int *dmaFd)
{
    vChnObject *pVideoObj = getVideoChnObject(chnId);
    if(!pVideoObj) {
        return -1;
    }

    pthread_rwlock_rdlock(&pVideoObj->imgLock);
    if(pVideoObj->image.cols <= 0 || pVideoObj->image.rows <= 0 || !pVideoObj->dma.pBuffer) {
        pthread_rwlock_unlock(&pVideoObj->imgLock);
        return -2;
    }

    /* 同步 DMA-BUF: CPU → device，使 RGA 能读取 CPU 写入的最新数据 */
    if(pVideoObj->dma.fd >= 0) {
        struct dma_buf_sync_local sync;
        memset(&sync, 0, sizeof(sync));
        sync.flags = (1 << 0) | (1 << 1); /* DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW */
        ioctl(pVideoObj->dma.fd, DMA_BUF_IOCTL_SYNC_LOCAL, &sync);
    }

    *bgrData = (uint8_t *)pVideoObj->dma.pBuffer;
    *width = pVideoObj->image.cols;
    *height = pVideoObj->image.rows;
    *dmaFd = pVideoObj->dma.fd;

    /* 不再需要 END 同步，因为后续由 RGA 硬件读取，不需要 CPU cache 同步结束 */

    pthread_rwlock_unlock(&pVideoObj->imgLock);
    return 0;
}

vChnObject *Analyzer::createVideoChnObject(int32_t chnId, int32_t imgWidth, int32_t imgHeight)
{
    vChnObject *newChnObj = new vChnObject;
    if (!newChnObj) {
        return NULL;
    }

    pthread_rwlock_init(&newChnObj->imgLock, nullptr);

    newChnObj->dma.pBuffer = NULL;
    newChnObj->dma.fd = -1;
    newChnObj->dma.size = imgWidth * imgHeight * 3;

    if (alloc_dmabuf((size_t)newChnObj->dma.size, &newChnObj->dma.fd, &newChnObj->dma.pBuffer) != 0) {
        PRINT_ERROR(g_hAnalyze, "alloc_dmabuf failed (%d bytes)\n", newChnObj->dma.size);
        pthread_rwlock_destroy(&newChnObj->imgLock);
        delete newChnObj;
        return NULL;
    }

    newChnObj->chnId = chnId;
    newChnObj->image = Mat(imgHeight, imgWidth, CV_8UC3, newChnObj->dma.pBuffer);
    newChnObj->image.setTo(Scalar(0, 255, 0));

    memset(&newChnObj->chnResult, 0, sizeof(ChnResult_t));
    return newChnObj;
}

int32_t Analyzer::releaseVideoChnObject(vChnObject *pObj)
{
    if(NULL == pObj)
        return -1;

    pthread_rwlock_wrlock(&pObj->imgLock);
    pObj->image.release();
    if (pObj->dma.pBuffer && pObj->dma.size > 0) {
        munmap(pObj->dma.pBuffer, pObj->dma.size);
        pObj->dma.pBuffer = NULL;
        pObj->dma.size = 0;
        close(pObj->dma.fd);
    }
    pthread_rwlock_unlock(&pObj->imgLock);

    pthread_rwlock_destroy(&pObj->imgLock);

    delete pObj;

    return 0;
}

int32_t Analyzer::delAllVideoChannel()
{
    pthread_mutex_lock(&mVideoChnLock);
    for (auto it = m_VideoChannellist.begin(); it != m_VideoChannellist.end(); ++it) {
        if(0 == releaseVideoChnObject(*it)){
            it = m_VideoChannellist.erase(it);
        }
    }
    pthread_mutex_unlock(&mVideoChnLock);
    return 0;
}

int analyzer_init(int32_t maxChn)
{
    Analyzer::createAnalyzer(maxChn);
    algorithm_init();
    return 0;
}

void analyzer_exit()
{
    Analyzer *pAnalyzer = Analyzer::instance();
    if(pAnalyzer){
        delete pAnalyzer;
    }
    algorithm_unInit();
}

int videoOutHandle(char *imgData, ImgDesc_t imgDesc)
{
    Analyzer *pAnalyzer = Analyzer::instance();

    if(pAnalyzer){
        pAnalyzer->upDateVideoChannel(imgDesc.chnId, imgData, imgDesc);
    }

    return 0;
}

int analyzer_getOsdFrame(int chnId, uint8_t **bgrData, int *width, int *height, int *dmaFd)
{
    Analyzer *pAnalyzer = Analyzer::instance();
    if(!pAnalyzer) {
        return -1;
    }
    return pAnalyzer->getOsdFrame(chnId, bgrData, width, height, dmaFd);
}
