 /**
 *
 * Copyright 2025 by Guangzhou Easy EAI Technologny Co.,Ltd.
 * website: www.easy-eai.com
 *
 * Author: Jiehao.Zhong <zhongjiehao@easy-eai.com>
 *
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * License file for more details.
 * 
 */
#ifndef RGAWAPPER_H
#define RGAWAPPER_H

#include <stdint.h>
#include <rga/RgaApi.h>

#ifdef __cplusplus
extern "C"{
#endif

/* rotate source image 0 degrees clockwise */
#define HAL_TRANSFORM_ROT_0      0x00
#if 0 // ---此组定义已写在<rga/RgaApi.h>中
/* flip source image horizontally (around the vertical axis) */
#define HAL_TRANSFORM_FLIP_H     0x01
/* flip source image vertically (around the horizontal axis)*/
#define HAL_TRANSFORM_FLIP_V     0x02
/* rotate source image 180 degrees */
#define HAL_TRANSFORM_ROT_180    0x03
/* rotate source image 90 degrees clockwise */
#define HAL_TRANSFORM_ROT_90     0x04
/* rotate source image 270 degrees clockwise */
#define HAL_TRANSFORM_ROT_270    0x07
#endif

typedef struct {
    RgaSURF_FORMAT fmt;
    int width;
    int height;
    int hor_stride;
    int ver_stride;
    int rotation;
    void *pBuf;
}Image;

//*strFmt: "NV12"、"NV21"、"RGB"、"BGR"
extern RgaSURF_FORMAT rgaFmt(char *strFmt);

extern void rga_init();
extern void rga_unInit();
extern int  srcImg_ConvertTo_dstImg(Image *pDst, Image *pSrc);



#ifdef __cplusplus
}
#endif
#endif /* RGAWAPPER_H */
