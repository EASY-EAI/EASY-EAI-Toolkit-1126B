#ifndef __AVS_RUNNER_H__
#define __AVS_RUNNER_H__

#include <stdbool.h>

#include "pipeline.h"
#include "rtsp_server.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AVS_RUNNER_MAX_RTSPS CAM_PIPE_MAX_OUTPUTS

typedef struct {
    int route_id;
    int rtsp_port;
    const char *rtsp_path;
    RtspServerCodec codec;
} AvsRunnerRtspCfg_t;

typedef struct {
    CamPipeCfg_t pipe_cfg;
    int fps;
    int rtsp_count;
    AvsRunnerRtspCfg_t rtsps[AVS_RUNNER_MAX_RTSPS];
} AvsRunnerCfg_t;

int AvsRunner_Run(const AvsRunnerCfg_t *cfg, volatile bool *running);

#ifdef __cplusplus
}
#endif

#endif
