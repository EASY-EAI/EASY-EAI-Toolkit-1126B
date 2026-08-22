//=====================  C++  =====================
#include <string>
//=====================   C   =====================
#include "system.h"
//=====================  PRJ  =====================
#include "logHandle.h"
#include "algoProcess.h"

static bool g_Algorithm_is_NotReady = true;
static rknn_context g_personCtx;

int algorithm_init()
{
    int ret = person_detect_init(&g_personCtx, "person_detect.model");
    if (0 != ret) {
        PRINT_ERROR(g_hAnalyze, "person_detect_init failed! ret = %d\n", ret);
        g_Algorithm_is_NotReady = true;
        return -1;
    }
    g_Algorithm_is_NotReady = false;
    return 0;
}

int algorithm_unInit()
{
    person_detect_release(g_personCtx);
    return 0;
}

ChnResult_t algorithm_process(int chnId, Mat image)
{
    int ret = 0;
    ChnResult_t chnResult = {0};

    int resultNum = 0;
    detect_result_group_t detect_result_group = {0};

    if (g_Algorithm_is_NotReady) {
        usleep(1000);
        return chnResult;
    }

    ret = person_detect_run(g_personCtx, image, &detect_result_group);
    if (0 != ret) {
        usleep(1000);
        return chnResult;
    }

    resultNum = detect_result_group.count;
    if (resultNum <= 0) {
        memset(&chnResult.algoRes[0], 0, sizeof(AlgoRes_t));
        usleep(1000);
        return chnResult;
    }

    chnResult.algoRes[0].resNumber = resultNum;
    memcpy(&chnResult.algoRes[0].detect_Group, &detect_result_group, sizeof(detect_result_group));

    return chnResult;
}
