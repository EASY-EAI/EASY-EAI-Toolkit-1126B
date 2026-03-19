//=====================  C++  =====================
#include <string>
//=====================   C   =====================
#include "system.h"
//=====================  PRJ  =====================
#include "algoProcess.h"

static bool g_Algorithm_is_NotReady = true;

//static rknn_context gPersonCtx;

int algorithm_init()
{
	//person_detect_init(&gPersonCtx, "person_detect.model");


    g_Algorithm_is_NotReady = false;
    return 0;
}

ChnResult_t algorithm_process(int chnId, Mat image)
{
    //int ret = 0;
    ChnResult_t chnResult={0};
    
    int resultNum = 0;
    detect_result_group_t detect_result_group = {0};

    // 模型未加载完成，不进行目标检测等操作
    if(g_Algorithm_is_NotReady){
		usleep(1000);
		return chnResult;
    }
    // 目标检测正式开始:
    // ==========================================================================================
	//ret = person_detect_run(gPersonCtx, image, &detect_result_group);
    //if(0 != ret){
	//	usleep(1000);
	//	return chnResult;
    //}
    
	resultNum = detect_result_group.count;
	if(resultNum <= 0){
        //memset(&chnResult.algoRes[0], 0, sizeof(AlgoRes_t));
		usleep(1000);
		return chnResult;
	}
	
	printf("\n>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
    printf("===[channel]--[%d]:\n", chnId);
	printf("============person number : %d\n", resultNum);
    chnResult.algoRes[0].resNumber = resultNum;
    memcpy(&chnResult.algoRes[0].detect_Group, &detect_result_group, sizeof(detect_result_group));

    return chnResult;
}

