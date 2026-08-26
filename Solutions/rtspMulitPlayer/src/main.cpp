//=====================  C++  =====================
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
//======================  C  ======================
#include <unistd.h>
//=====================  PRJ  =====================
#include "capturer/capturer.h"
#include "analyzer/analyzer.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
static const SrcCfg_t SrcCfg_tab[] = {
	{
		.srcType   = "rtsp",
		.loaction  = "rtsp://192.168.3.141:8554/live/0",
		.videoEncType = "h264",
		.audioEncType = "null",
	},
	/* ── 以下为扩展配置示例，取消注释即可启用 ── */
	/* 注意：当前仅支持视频画面显示，音频播放暂未支持 */
#if 0
	{	/* RTSP H264（带账号密码） */
		.srcType   = "rtsp",
		.loaction  = "rtsp://admin:a12345678@192.168.1.69",
		.videoEncType = "h264",
		.audioEncType = "null",          /* 音频暂未支持 */
	},
	{	/* RTSP H265 */
		.srcType   = "rtsp",
		.loaction  = "rtsp://192.168.3.101:554/aabb",
		.videoEncType = "h265",
		.audioEncType = "pcma",           /* 音频暂未支持 */
	},
	{	/* 本地文件（暂未支持） */
		.srcType   = "file",
		.loaction  = "/userdata/mydata/car.mp4",
		.videoEncType = "h264",
		.audioEncType = "aac",            /* 音频暂未支持 */
	},
#endif
};

int main(int argc, char **argv)
{
    int ret = -1;
    int chnNums = ARRAY_SIZE(SrcCfg_tab);
    if(chnNums <= 0){
        return -1;
    }
    
    /* Initialize algotithm model */
    ret = analyzer_init(chnNums);
    if(0 != ret){
        printf("Initialize algotithm model faild ! ret = %d\n", ret);
        return ret;
    }

    /* Create Display */
#if 0
    Display_t dispDesc = {"rtspMulitPlayer",0,0,1920,1080};
    char **pDispBuffer = dispBufferMap(&dispDesc);
    if(!(*pDispBuffer)){
        return -2;
    }
#endif

    /* RTSP client + RK_MPI VDEC (no gstreamer needed) */
    Capturer *pCapturer[32] = {NULL};
    for(int i = 0; i <chnNums; i++) {
        pCapturer[i] = new Capturer(i, SrcCfg_tab[i]);
        if(pCapturer[i]){
            if(0 != pCapturer[i]->init()){
                printf("playChn[%d] init faild\n", i);
                delete pCapturer[i];
                pCapturer[i] = NULL;
            }
        }
    }

#if 0
    //进入显示事件循环
    display(&dispDesc);
#else
    while(1){
        sleep(2);
    }
#endif

    for(int i = 0; i <chnNums; i++) {
        if(pCapturer[i]){
            delete pCapturer[i];
            pCapturer[i] = NULL;
        }
    }
    
    return 0;
}

