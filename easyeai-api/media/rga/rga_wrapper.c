#include <stdio.h>
#include <pthread.h>
#include "rga_wrapper.h"

static pthread_mutex_t gmutex;


RgaSURF_FORMAT rgaFmt(char *strFmt)
{
    if(0 == strcmp(strFmt, "NV12")){
        return RK_FORMAT_YCbCr_420_SP;
    }else if(0 == strcmp(strFmt, "NV21")){
        return RK_FORMAT_YCrCb_420_SP;
    }else if(0 == strcmp(strFmt, "BGR")){
        return RK_FORMAT_BGR_888;
    }else if(0 == strcmp(strFmt, "RGB")){
        return RK_FORMAT_RGB_888;
    }else{
        return RK_FORMAT_UNKNOWN;
    }
}

void rga_init()
{
    pthread_mutex_init(&gmutex, NULL);

}

void rga_unInit()
{
    pthread_mutex_destroy(&gmutex);
}

int srcImg_ConvertTo_dstImg(Image *pDst, Image *pSrc)
{
	rga_info_t src, dst;
	int ret = -1;

	if (!pSrc || !pDst) {
		printf("%s: NULL PTR!\n", __func__);
		return -1;
	}

    pthread_mutex_lock(&gmutex);
	//图像参数转换
	memset(&src, 0, sizeof(rga_info_t));
	src.fd = -1;
	src.virAddr = pSrc->pBuf;
	src.mmuFlag = 1;
	src.rotation =  pSrc->rotation;
	rga_set_rect(&src.rect, 0, 0, pSrc->width, pSrc->height, pSrc->hor_stride, pSrc->ver_stride, pSrc->fmt);

	memset(&dst, 0, sizeof(rga_info_t));
	dst.fd = -1;
	dst.virAddr = pDst->pBuf;
	dst.mmuFlag = 1;
	dst.rotation =  pDst->rotation;
	rga_set_rect(&dst.rect, 0, 0, pDst->width, pDst->height, pDst->hor_stride, pDst->ver_stride, pDst->fmt);
	if (c_RkRgaBlit(&src, &dst, NULL)) {
		printf("%s: rga fail\n", __func__);
		ret = -1;
	}
	else {
		ret = 0;
	}
    pthread_mutex_unlock(&gmutex);

	return ret;
}

