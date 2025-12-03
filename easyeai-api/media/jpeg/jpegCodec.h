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
#ifndef JPEGCODEC_H
#define JPEGCODEC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"{
#endif

typedef struct {
    char fmt[16];
    int width;
    int height;
    int horStride;
    int verStride;
    int dataSize;
}SrcDataDesc_t;


//extern int JpegEnc_Init(const char *file, OutDataCB pOutFunc, int *isInited);
//extern int JpegEnc_unInit();



typedef	int (*OutDataCB)(void *, SrcDataDesc_t);
extern int  JpegDec_init(const char *file, OutDataCB pOutFunc);
extern void JpegDec_start();
extern int  JpegDec_pushData(char *pJpegData, int dataSize, int isEOS);
extern int  JpegDec_unInit();



#ifdef __cplusplus
}
#endif
#endif /* JPEGCODEC_H */
