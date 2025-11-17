//=====================  C++  =====================
#include <string>
//=====================   C   =====================
#include "system.h"
//=====================  PRJ  =====================
#include "algoProcess.h"


int algorithm_init()
{

    return 0;
}

int algorithm_process(int chnId, Mat image)
{
    static int skipImg = 10; //跳过前10张图片
    static int skipImg2 = 10; //跳过前10张图片

    //测试用：如果有[通道0]的图像过来，就存下一帧，看看是否正常
    if(0==chnId){
        printf("--[chn%d]-------------------- %d ----------------------\n",chnId,skipImg);
        if(skipImg == 0){
            imwrite("test_img_0.jpg", image);
        }
    
        skipImg--;
    }

    //测试用：如果有[通道1]的图像过来，就存下一帧，看看是否正常
    if(1==chnId){
        printf("--[chn%d]-------------------- %d ----------------------\n",chnId,skipImg);
        if(skipImg2 == 0){
            imwrite("test_img_1.jpg", image);
        }
    
        skipImg2--;
    }

    return 0;
}

