//=====================  C++  =====================
#include <string>
#include <list>
//=====================   C   =====================
#include "system.h"
//=====================  PRJ  =====================
#include "system_opt.h"
#include "log_manager.h"
#include "rga_wrapper.h"
#include "analyzer.h"

using namespace cv;

class Analyzer
{
public:
	Analyzer();
	~Analyzer();

    static Analyzer *instance() { return m_pSelf; }
    static void createAnalyzer();
    
    // --视频资源处理
    // 1.更新某路[视频]通道图像数据
    int32_t upDateVideoChannel(int chnId, char *imgData, ImgDesc_t imgDesc);
    // 2.取某路[视频]通道图像数据地址
    vChnObject *getVideoChnObject(int chnId);
    uint8_t* videoChannelData(vChnObject *pVideoObj, int &width, int &height);
    // 3.取某路[视频]通道的分析结果
    int32_t videoChannelAnalyRes(int chnId);

    // --音频资源处理
    // 1.更新某路[音频]通道数据
    // 2.取某路[音频]通道数据地址
    // 3.取某路[音频]通道的分析结果
    

    bool mThreadWorking;
    pthread_mutex_t mVideoChnLock;
    //pthread_mutex_t mAudioChnLock;

protected:
    vChnObject *createVideoChnObject(int32_t chnId, int32_t imgWidth, int32_t imgHeight);
    int32_t releaseVideoChnObject(vChnObject *pObj);
    int32_t delAllVideoChannel();

    //aChnObject *searchAudioChnObject(int chnId);
    //aChnObject *createAudioChnObject();
    //int32_t releaseAudioChnObject(aChnObject *pObj);
    //int32_t delAllAudioChannel();

    
private:
    static Analyzer *m_pSelf;
    
    // 解码器输出数据 - RGB格式
	std::list<vChnObject*> m_VideoChannellist;
	//std::list<aChnObject*> m_MediaAudioChannellist;

	pthread_t mTid;
};

static void *imgAnalyze_thread(void *para)
{
    Analyzer *pSelf = (Analyzer *)para;

    int chnId = 0;
    Mat image;
    while(1){
        if(!pSelf->mThreadWorking){
            msleep(5);
            break;
        }
        
        if(NULL == pSelf){
            msleep(5);
            break;
        }
        
        vChnObject *pVideoObj = pSelf->getVideoChnObject(chnId);
        if(pVideoObj){
            // 取出待分析图像
            pthread_rwlock_rdlock(&pVideoObj->imgLock);
            pVideoObj->image.copyTo(image);
            pthread_rwlock_unlock(&pVideoObj->imgLock);

            algorithm_process(chnId, image);
        }
        chnId++;
        chnId%=10;

        msleep(20);
    }
    
    pthread_exit(NULL);
}

Analyzer *Analyzer::m_pSelf = NULL;
Analyzer::Analyzer() :
    mThreadWorking(false)
{
    /*初始化通道锁*/
    pthread_mutex_init(&mVideoChnLock, NULL);
    //pthread_mutex_init(&mAudioChnLock, NULL);

    /*创建线程*/
    if(0 != CreateJoinThread(imgAnalyze_thread, this, &mTid)){
        return ;
    }
    mThreadWorking = true;
}
Analyzer::~Analyzer()
{
    /*回收线程*/
    // 1，等待取流线程跑起来
    int timeOut_ms = 1000; //设置n(ms)超时，超时就不等了
    while(1){
        if((true == mThreadWorking)||(timeOut_ms <= 0)){
            break;
        }
        timeOut_ms--;
        usleep(1000);
    }
    // 2，退出线程并等待其结束
    mThreadWorking = false;
    // --[等待取流线程结束]--
    while(1) {
        usleep(20*1000);
        int32_t exitCode = pthread_join(mTid, NULL);
        if(0 == exitCode){
            break;
        }else if(0 != exitCode){
            switch (exitCode) {
                case ESRCH:  // 没有找到线程ID
                    PRINT_ERROR("imgAnalyze_thread exit: No thread with the given ID was found.");
                    break;
                case EINVAL: // 线程不可连接或已经有其他线程在等待它
                    PRINT_ERROR("imgAnalyze_thread exit: Thread is detached or already being waited on.");
                    break;
                case EDEADLK: // 死锁 - 线程尝试join自己
                    PRINT_ERROR("imgAnalyze_thread exit: Deadlock detected - thread is trying to join itself.");
                    break;
            }
            continue;
        }
    }


    /*回收视频资源*/
    delAllVideoChannel();
    pthread_mutex_destroy(&mVideoChnLock);

    /*回收音频资源*/
    //delAllAudioChannel();
    //pthread_mutex_destroy(&mAudioChnLock);
}
void Analyzer::createAnalyzer()
{
    if(m_pSelf == NULL) {
        m_pSelf = new Analyzer();
   }
}

int32_t Analyzer::upDateVideoChannel(int chnId, char *imgData, ImgDesc_t imgDesc)
{
    if(chnId < 0)
        return -1;

    pthread_mutex_lock(&mVideoChnLock);
    vChnObject* targetObj = nullptr;
    for (auto it = m_VideoChannellist.begin(); it != m_VideoChannellist.end(); ++it) {
        // 找到目标对象
        if ((*it)->chnId == chnId) {
            targetObj = *it;  
            
            // 图像信息改变，销毁原来图像缓存
            if((targetObj->image.cols != imgDesc.width)||(targetObj->image.rows != imgDesc.height)){
                if(0 == releaseVideoChnObject(targetObj)){
                    // 从链表中移除chnObj
                    it = m_VideoChannellist.erase(it);
                }else{
                    pthread_mutex_unlock(&mVideoChnLock);
                    return -2;
                }
            }
            
            break;
        }
    }
    
    // 需要创建一个[视频]通道对象
    if (!targetObj) {
        targetObj = createVideoChnObject(chnId, imgDesc.width, imgDesc.height);
        if(!targetObj)
            return -3;
        
        m_VideoChannellist.push_back(targetObj);
    }
    pthread_mutex_unlock(&mVideoChnLock);

    // 更新[视频]通道图像数据
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

    vChnObject* targetObj = nullptr;
    pthread_mutex_lock(&mVideoChnLock);
    for (auto it = m_VideoChannellist.begin(); it != m_VideoChannellist.end(); ++it) {
        // 找到目标对象
        if ((*it)->chnId == chnId) {
            targetObj = *it;
            break;
        }
    }
    pthread_mutex_unlock(&mVideoChnLock);

    return targetObj;
}


vChnObject *Analyzer::createVideoChnObject(int32_t chnId, int32_t imgWidth, int32_t imgHeight)
{
    // 1. 创建通道对象
    vChnObject* newChnObj = new vChnObject;
    if(!newChnObj)
        return NULL;
    
    // 2. 初始化图像数据读写锁
    pthread_rwlock_init(&newChnObj->imgLock, nullptr);

    // 3. 创建图像缓存
    newChnObj->chnId = chnId;
    newChnObj->image = Mat(imgHeight, imgWidth, CV_8UC3);
    newChnObj->result = 0;

    return newChnObj;
}


int32_t Analyzer::releaseVideoChnObject(vChnObject *pObj)
{
    if(NULL == pObj)
        return -1;

    // 1. 销毁Mat资源（OpenCV会自动管理）
    pthread_rwlock_wrlock(&pObj->imgLock);
    pObj->image.release();
    pthread_rwlock_unlock(&pObj->imgLock);
    
    // 2. 销毁读写锁
    pthread_rwlock_destroy(&pObj->imgLock);

    // 3. 销毁通道对象
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


int analyzer_init()
{
    // 创建图像分析器
    Analyzer::createAnalyzer();
    
    // 模型初始化
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

