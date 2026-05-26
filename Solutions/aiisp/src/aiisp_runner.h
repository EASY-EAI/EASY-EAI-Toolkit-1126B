#ifndef __AIISP_RUNNER_H__
#define __AIISP_RUNNER_H__

#include <stdbool.h>

#include "pipeline.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AIISP_RUNNER_MAX_RTSPS CAM_PIPE_MAX_OUTPUTS

typedef struct {
    int route_id;
    int rtsp_port;
    const char *rtsp_path;
} AiispRunnerRtspCfg_t;

typedef struct {
    CamPipeCfg_t pipe_cfg;
    int fps;
    int rtsp_count;
    AiispRunnerRtspCfg_t rtsps[AIISP_RUNNER_MAX_RTSPS];
} AiispRunnerCfg_t;

int AiispRunner_Run(const AiispRunnerCfg_t *cfg, volatile bool *running);

#ifdef __cplusplus
}
#endif

#endif
