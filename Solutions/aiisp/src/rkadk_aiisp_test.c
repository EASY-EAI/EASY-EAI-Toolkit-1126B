/*
 * Copyright (c) 2021 Rockchip, Inc. All Rights Reserved.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rk_comm_video.h"
#include "aiisp_runner.h"

static volatile bool g_running = true;

static void sig_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        printf("\n#Get SIGINT/SIGTERM, quitting...\n");
        g_running = false;
    }
}

#define IQ_FILE_PATH "/etc/iqfiles"

typedef enum {
    APP_BIND_MODE_VI_VPSS_VENC = 0,
    APP_BIND_MODE_VI_VENC,
} AppBindMode_e;

typedef struct {
    const char *iq_file_dir;
    int cam_num;
    int cam0_id;
    int cam1_id;
    int cam0_main_width;
    int cam0_main_height;
    int cam0_sub_width;
    int cam0_sub_height;
    int cam1_main_width;
    int cam1_main_height;
    int cam1_sub_width;
    int cam1_sub_height;
    int fps;
    int rtsp_base_port;
    bool debug;
    bool enable_sub_stream;
    AppBindMode_e bind_mode;
    char aiisp_model[256];
    char cam0_main_path[64];
    char cam0_sub_path[64];
    char cam1_main_path[64];
    char cam1_sub_path[64];
} AppArgs_t;

enum {
    OPT_CAM0_MAIN_WIDTH = 1000,
    OPT_CAM0_MAIN_HEIGHT,
    OPT_CAM0_SUB_WIDTH,
    OPT_CAM0_SUB_HEIGHT,
    OPT_CAM1_MAIN_WIDTH,
    OPT_CAM1_MAIN_HEIGHT,
    OPT_CAM1_SUB_WIDTH,
    OPT_CAM1_SUB_HEIGHT,
    OPT_CAM0_MAIN_PATH,
    OPT_CAM0_SUB_PATH,
    OPT_CAM1_MAIN_PATH,
    OPT_CAM1_SUB_PATH,
    OPT_BIND_MODE,
    OPT_ENABLE_SUB_STREAM,
};

static const struct option g_long_options[] = {
    {"aiq", required_argument, NULL, 'a'},
    {"cam0_id", required_argument, NULL, 'I'},
    {"cam1_id", required_argument, NULL, 'J'},
    {"camera_num", required_argument, NULL, 'n'},
    {"fps", required_argument, NULL, 'f'},
    {"aiisp_model_path", required_argument, NULL, 'm'},
    {"rtsp_port", required_argument, NULL, 'p'},
    {"cam0_width", required_argument, NULL, OPT_CAM0_MAIN_WIDTH},
    {"cam0_height", required_argument, NULL, OPT_CAM0_MAIN_HEIGHT},
    {"cam0_sub_width", required_argument, NULL, OPT_CAM0_SUB_WIDTH},
    {"cam0_sub_height", required_argument, NULL, OPT_CAM0_SUB_HEIGHT},
    {"cam1_width", required_argument, NULL, OPT_CAM1_MAIN_WIDTH},
    {"cam1_height", required_argument, NULL, OPT_CAM1_MAIN_HEIGHT},
    {"cam1_sub_width", required_argument, NULL, OPT_CAM1_SUB_WIDTH},
    {"cam1_sub_height", required_argument, NULL, OPT_CAM1_SUB_HEIGHT},
    {"cam0_main_path", required_argument, NULL, OPT_CAM0_MAIN_PATH},
    {"cam0_sub_path", required_argument, NULL, OPT_CAM0_SUB_PATH},
    {"cam1_main_path", required_argument, NULL, OPT_CAM1_MAIN_PATH},
    {"cam1_sub_path", required_argument, NULL, OPT_CAM1_SUB_PATH},
    {"bind_mode", required_argument, NULL, OPT_BIND_MODE},
    {"enable_sub_stream", required_argument, NULL, OPT_ENABLE_SUB_STREAM},
    {"debug", no_argument, NULL, 'd'},
    {"help", no_argument, NULL, 'h'},
    {0, 0, 0, 0},
};

static void print_usage(const char *name) {
  printf("usage example:\n");
  printf("\t%s -n 2 -I 0 -J 1 -a /etc/iqfiles -m /oem/usr/lib/ --bind_mode vpss\n", name);
  printf("\t%s -n 2 -I 0 -J 1 -a /etc/iqfiles --bind_mode direct\n", name);
  printf("\t-a: AIQ iqfiles 路径, 默认 /etc/iqfiles\n");
  printf("\t-I: cam0 sensor id, 默认 0\n");
  printf("\t-J: cam1 sensor id, 默认 1\n");
  printf("\t-n: camera num, 1/2, 默认 1\n");
  printf("\t-f: fps, 默认 25\n");
  printf("\t-p: RTSP 起始端口, 默认 554\n");
  printf("\t-m: AIISP 模型目录或文件路径\n");
  printf("\t--bind_mode: vpss 或 direct, 默认 vpss\n");
  printf("\t--enable_sub_stream: 0/1, 默认 1\n");
  printf("\t--cam0_width/--cam0_height: cam0 主码流尺寸\n");
  printf("\t--cam0_sub_width/--cam0_sub_height: cam0 子码流尺寸\n");
  printf("\t--cam1_width/--cam1_height: cam1 主码流尺寸\n");
  printf("\t--cam1_sub_width/--cam1_sub_height: cam1 子码流尺寸\n");
}

static void set_default_args(AppArgs_t *args)
{
    memset(args, 0, sizeof(*args));
    args->iq_file_dir = IQ_FILE_PATH;
    args->cam_num = 1;
    args->cam0_id = 0;
    args->cam1_id = 1;
    args->fps = 25;
    args->rtsp_base_port = 554;
    args->enable_sub_stream = true;
    args->bind_mode = APP_BIND_MODE_VI_VPSS_VENC;
    args->cam0_main_width = 2688;
    args->cam0_main_height = 1520;
    args->cam1_main_width = 2688;
    args->cam1_main_height = 1520;
    args->cam0_sub_width = 720;
    args->cam0_sub_height = 480;
    args->cam1_sub_width = 720;
    args->cam1_sub_height = 480;
    snprintf(args->cam0_main_path, sizeof(args->cam0_main_path), "%s", "/live/cam0_main");
    snprintf(args->cam0_sub_path, sizeof(args->cam0_sub_path), "%s", "/live/cam0_sub");
    snprintf(args->cam1_main_path, sizeof(args->cam1_main_path), "%s", "/live/cam1_main");
    snprintf(args->cam1_sub_path, sizeof(args->cam1_sub_path), "%s", "/live/cam1_sub");
}

static int parse_bind_mode(const char *mode, AppBindMode_e *bind_mode)
{
    if (!mode || !bind_mode)
        return -1;

    if (strcmp(mode, "vpss") == 0) {
        *bind_mode = APP_BIND_MODE_VI_VPSS_VENC;
        return 0;
    }

    if (strcmp(mode, "direct") == 0) {
        *bind_mode = APP_BIND_MODE_VI_VENC;
        return 0;
    }

    return -1;
}

static int parse_args(int argc, char *argv[], AppArgs_t *args)
{
    int c;

    while ((c = getopt_long(argc, argv, "a:I:J:n:f:m:p:dh", g_long_options, NULL)) != -1) {
        switch (c) {
        case 'a':
            args->iq_file_dir = optarg;
            break;
        case 'I':
            args->cam0_id = atoi(optarg);
            break;
        case 'J':
            args->cam1_id = atoi(optarg);
            break;
        case 'n':
            args->cam_num = atoi(optarg);
            break;
        case 'f':
            args->fps = atoi(optarg);
            break;
        case 'm':
            strncpy(args->aiisp_model, optarg, sizeof(args->aiisp_model) - 1);
            args->aiisp_model[sizeof(args->aiisp_model) - 1] = '\0';
            break;
        case 'p':
            args->rtsp_base_port = atoi(optarg);
            break;
        case 'd':
            args->debug = true;
            printf("[DEBUG] debug mode enabled\n");
            break;
        case OPT_CAM0_MAIN_WIDTH:
            args->cam0_main_width = atoi(optarg);
            break;
        case OPT_CAM0_MAIN_HEIGHT:
            args->cam0_main_height = atoi(optarg);
            break;
        case OPT_CAM0_SUB_WIDTH:
            args->cam0_sub_width = atoi(optarg);
            break;
        case OPT_CAM0_SUB_HEIGHT:
            args->cam0_sub_height = atoi(optarg);
            break;
        case OPT_CAM1_MAIN_WIDTH:
            args->cam1_main_width = atoi(optarg);
            break;
        case OPT_CAM1_MAIN_HEIGHT:
            args->cam1_main_height = atoi(optarg);
            break;
        case OPT_CAM1_SUB_WIDTH:
            args->cam1_sub_width = atoi(optarg);
            break;
        case OPT_CAM1_SUB_HEIGHT:
            args->cam1_sub_height = atoi(optarg);
            break;
        case OPT_CAM0_MAIN_PATH:
            strncpy(args->cam0_main_path, optarg, sizeof(args->cam0_main_path) - 1);
            args->cam0_main_path[sizeof(args->cam0_main_path) - 1] = '\0';
            break;
        case OPT_CAM0_SUB_PATH:
            strncpy(args->cam0_sub_path, optarg, sizeof(args->cam0_sub_path) - 1);
            args->cam0_sub_path[sizeof(args->cam0_sub_path) - 1] = '\0';
            break;
        case OPT_CAM1_MAIN_PATH:
            strncpy(args->cam1_main_path, optarg, sizeof(args->cam1_main_path) - 1);
            args->cam1_main_path[sizeof(args->cam1_main_path) - 1] = '\0';
            break;
        case OPT_CAM1_SUB_PATH:
            strncpy(args->cam1_sub_path, optarg, sizeof(args->cam1_sub_path) - 1);
            args->cam1_sub_path[sizeof(args->cam1_sub_path) - 1] = '\0';
            break;
        case OPT_BIND_MODE:
            if (parse_bind_mode(optarg, &args->bind_mode) != 0) {
                printf("invalid bind mode: %s\n", optarg);
                return -1;
            }
            break;
        case OPT_ENABLE_SUB_STREAM:
            args->enable_sub_stream = atoi(optarg) ? true : false;
            break;
        case 'h':
        default:
            print_usage(argv[0]);
            return -1;
        }
    }

    if (args->cam_num != 1 && args->cam_num != 2) {
        printf("camera num must be 1 or 2\n");
        return -1;
    }
    if (args->rtsp_base_port <= 0 || args->fps <= 0) {
        printf("rtsp port and fps must be positive\n");
        return -1;
    }
    if (args->cam0_main_width <= 0 || args->cam0_main_height <= 0 ||
        args->cam0_sub_width <= 0 || args->cam0_sub_height <= 0 ||
        (args->cam_num == 2 &&
         (args->cam1_main_width <= 0 || args->cam1_main_height <= 0 ||
          args->cam1_sub_width <= 0 || args->cam1_sub_height <= 0))) {
        printf("width/height must be positive\n");
        return -1;
    }

    return 0;
}

static void fill_camera_cfg(CamPipeCameraCfg_t *camera_cfg,
                            int cam_id,
                            const char *iq_dir,
                            int fps,
                            int main_width,
                            int main_height,
                            int sub_width,
                            int sub_height,
                            int vpss_grp_id,
                            bool enable_vpss,
                            bool enable_sub_stream,
                            const char *aiisp_model,
                            int aiisp_buf_cnt,
                            bool is_multi_cam)
{
    memset(camera_cfg, 0, sizeof(*camera_cfg));

    camera_cfg->isp_cfg.cam_id = cam_id;
    camera_cfg->isp_cfg.iq_file_dir = iq_dir;
    camera_cfg->isp_cfg.hdr_mode = CAM_ADAPTER_HDR_NORMAL;
    camera_cfg->isp_cfg.fps = fps;
    camera_cfg->isp_cfg.is_multi_cam = is_multi_cam;

    camera_cfg->vi_cfg.pipe_id = cam_id;
    camera_cfg->vi_cfg.chn_id = 0;
    camera_cfg->vi_cfg.width = main_width;
    camera_cfg->vi_cfg.height = main_height;
    camera_cfg->vi_cfg.buf_cnt = 2;
    camera_cfg->vi_cfg.pix_fmt = RK_FMT_YUV420SP;
    camera_cfg->vi_cfg.buf_wrap_enable = false;
    camera_cfg->vi_cfg.buf_line = 0;

    camera_cfg->enable_vpss = enable_vpss;
    camera_cfg->vpss_cfg.grp_id = vpss_grp_id;
    camera_cfg->vpss_cfg.channel_count = enable_sub_stream ? 2 : 1;
    camera_cfg->vpss_cfg.enable_aiisp = (enable_vpss && aiisp_model && aiisp_model[0]);
    camera_cfg->vpss_cfg.aiisp_model_path = aiisp_model;
    camera_cfg->vpss_cfg.aiisp_buf_cnt = aiisp_buf_cnt;

    camera_cfg->vpss_cfg.channels[0].chn_id = 0;
    camera_cfg->vpss_cfg.channels[0].width = main_width;
    camera_cfg->vpss_cfg.channels[0].height = main_height;
    camera_cfg->vpss_cfg.channels[0].pix_fmt = RK_FMT_YUV420SP;
    camera_cfg->vpss_cfg.channels[0].chn_mode = CAM_VPSS_CHN_MODE_PASSTHROUGH;
    camera_cfg->vpss_cfg.channels[0].enabled = true;

    if (enable_sub_stream) {
        camera_cfg->vpss_cfg.channels[1].chn_id = 1;
        camera_cfg->vpss_cfg.channels[1].width = sub_width;
        camera_cfg->vpss_cfg.channels[1].height = sub_height;
        camera_cfg->vpss_cfg.channels[1].pix_fmt = RK_FMT_YUV420SP;
        camera_cfg->vpss_cfg.channels[1].chn_mode = CAM_VPSS_CHN_MODE_AUTO;
        camera_cfg->vpss_cfg.channels[1].enabled = true;
    }
}

static void add_output(AiispRunnerCfg_t *cfg,
                       int *output_count,
                       int *bind_count,
                       int *route_id,
                       int *venc_chn_id,
                       int *rtsp_port,
                       int camera_index,
                       int width,
                       int height,
                       int bitrate,
                       int bind_src_mod,
                       int bind_src_dev,
                       int bind_src_chn,
                       const char *rtsp_path)
{
    CamPipeOutputCfg_t *output_cfg = &cfg->pipe_cfg.outputs[*output_count];
    CamPipeBindCfg_t *bind_cfg = &cfg->pipe_cfg.binds[*bind_count];
    AiispRunnerRtspCfg_t *rtsp_cfg = &cfg->rtsps[*output_count];

    memset(output_cfg, 0, sizeof(*output_cfg));
    memset(bind_cfg, 0, sizeof(*bind_cfg));
    memset(rtsp_cfg, 0, sizeof(*rtsp_cfg));

    output_cfg->route_id = *route_id;
    output_cfg->venc_cfg.chn_id = *venc_chn_id;
    output_cfg->venc_cfg.width = width;
    output_cfg->venc_cfg.height = height;
    output_cfg->venc_cfg.fps = cfg->fps;
    output_cfg->venc_cfg.gop = cfg->fps;
    output_cfg->venc_cfg.bitrate = bitrate;
    output_cfg->venc_cfg.codec_type = CAM_ADAPTER_CODEC_H264;
    output_cfg->venc_cfg.rc_mode = CAM_ADAPTER_RC_CBR;
    output_cfg->venc_cfg.buf_wrap_enable = false;
    output_cfg->venc_cfg.buf_line = 0;
    output_cfg->venc_cfg.svc_enable = false;
    output_cfg->venc_cfg.motion_deblur_enable = false;
    output_cfg->venc_cfg.ref_buf_share = true;

    bind_cfg->camera_index = camera_index;
    bind_cfg->bind_cfg.src.mod_type = bind_src_mod;
    bind_cfg->bind_cfg.src.dev_id = bind_src_dev;
    bind_cfg->bind_cfg.src.chn_id = bind_src_chn;
    bind_cfg->bind_cfg.dst.mod_type = CAM_MODULE_VENC;
    bind_cfg->bind_cfg.dst.dev_id = 0;
    bind_cfg->bind_cfg.dst.chn_id = *venc_chn_id;

    rtsp_cfg->route_id = *route_id;
    rtsp_cfg->rtsp_port = *rtsp_port;
    rtsp_cfg->rtsp_path = rtsp_path;

    (*output_count)++;
    (*bind_count)++;
    (*route_id)++;
    (*venc_chn_id)++;
    (*rtsp_port)++;
}

static void fill_runner_cfg(const AppArgs_t *args, AiispRunnerCfg_t *cfg)
{
    int output_count = 0;
    int bind_count = 0;
    int route_id = 0;
    int venc_chn_id = 0;
    int rtsp_port = args->rtsp_base_port;
    bool enable_vpss = (args->bind_mode == APP_BIND_MODE_VI_VPSS_VENC);
    bool enable_sub_stream = (enable_vpss && args->enable_sub_stream);
    int aiisp_buf_cnt = 2;

    memset(cfg, 0, sizeof(*cfg));
    cfg->fps = args->fps;
    cfg->pipe_cfg.camera_count = args->cam_num;

    aiisp_buf_cnt = 1;

    fill_camera_cfg(&cfg->pipe_cfg.cameras[0], args->cam0_id, args->iq_file_dir, args->fps,
                    args->cam0_main_width, args->cam0_main_height,
                    args->cam0_sub_width, args->cam0_sub_height,
                    0, enable_vpss, enable_sub_stream,
                    args->aiisp_model[0] ? args->aiisp_model : NULL,
                    aiisp_buf_cnt, args->cam_num > 1);

    if (args->cam_num == 2) {
        fill_camera_cfg(&cfg->pipe_cfg.cameras[1], args->cam1_id, args->iq_file_dir, args->fps,
                        args->cam1_main_width, args->cam1_main_height,
                        args->cam1_sub_width, args->cam1_sub_height,
                        1, enable_vpss, enable_sub_stream,
                        args->aiisp_model[0] ? args->aiisp_model : NULL,
                        aiisp_buf_cnt, true);
    }

    if (enable_vpss) {
        // 每个 camera 先绑定 VI -> VPSS，后面主/子码流分别从不同 VPSS chn 接到各自 VENC。
        cfg->pipe_cfg.binds[bind_count].camera_index = 0;
        cfg->pipe_cfg.binds[bind_count].bind_cfg.src.mod_type = CAM_MODULE_VI;
        cfg->pipe_cfg.binds[bind_count].bind_cfg.src.dev_id = args->cam0_id;
        cfg->pipe_cfg.binds[bind_count].bind_cfg.src.chn_id = 0;
        cfg->pipe_cfg.binds[bind_count].bind_cfg.dst.mod_type = CAM_MODULE_VPSS;
        cfg->pipe_cfg.binds[bind_count].bind_cfg.dst.dev_id = 0;
        cfg->pipe_cfg.binds[bind_count].bind_cfg.dst.chn_id = 0;
        bind_count++;

        add_output(cfg, &output_count, &bind_count, &route_id, &venc_chn_id, &rtsp_port,
                   0, args->cam0_main_width, args->cam0_main_height, 2 * 1024,
                   CAM_MODULE_VPSS, 0, 0, args->cam0_main_path);

        if (enable_sub_stream) {
            add_output(cfg, &output_count, &bind_count, &route_id, &venc_chn_id, &rtsp_port,
                       0, args->cam0_sub_width, args->cam0_sub_height, 1024,
                       CAM_MODULE_VPSS, 0, 1, args->cam0_sub_path);
        }

        if (args->cam_num == 2) {
            cfg->pipe_cfg.binds[bind_count].camera_index = 1;
            cfg->pipe_cfg.binds[bind_count].bind_cfg.src.mod_type = CAM_MODULE_VI;
            cfg->pipe_cfg.binds[bind_count].bind_cfg.src.dev_id = args->cam1_id;
            cfg->pipe_cfg.binds[bind_count].bind_cfg.src.chn_id = 0;
            cfg->pipe_cfg.binds[bind_count].bind_cfg.dst.mod_type = CAM_MODULE_VPSS;
            cfg->pipe_cfg.binds[bind_count].bind_cfg.dst.dev_id = 1;
            cfg->pipe_cfg.binds[bind_count].bind_cfg.dst.chn_id = 0;
            bind_count++;

            add_output(cfg, &output_count, &bind_count, &route_id, &venc_chn_id, &rtsp_port,
                       1, args->cam1_main_width, args->cam1_main_height, 2 * 1024,
                       CAM_MODULE_VPSS, 1, 0, args->cam1_main_path);

            if (enable_sub_stream) {
                add_output(cfg, &output_count, &bind_count, &route_id, &venc_chn_id, &rtsp_port,
                           1, args->cam1_sub_width, args->cam1_sub_height, 1024,
                           CAM_MODULE_VPSS, 1, 1, args->cam1_sub_path);
            }
        }
    } else {
        // 直绑模式更轻量，但不经过 VPSS 时无法自然扩出主/子/第三码流。
        add_output(cfg, &output_count, &bind_count, &route_id, &venc_chn_id, &rtsp_port,
                   0, args->cam0_main_width, args->cam0_main_height, 2 * 1024,
                   CAM_MODULE_VI, args->cam0_id, 0, args->cam0_main_path);

        if (args->cam_num == 2) {
            add_output(cfg, &output_count, &bind_count, &route_id, &venc_chn_id, &rtsp_port,
                       1, args->cam1_main_width, args->cam1_main_height, 2 * 1024,
                       CAM_MODULE_VI, args->cam1_id, 0, args->cam1_main_path);
        }
    }

    cfg->pipe_cfg.output_count = output_count;
    cfg->pipe_cfg.bind_count = bind_count;
    cfg->rtsp_count = output_count;
}

static void print_config(const AppArgs_t *args, const AiispRunnerCfg_t *cfg)
{
    printf("camera_num: %d, iqDir: %s, fps: %d, aiispModel: %s\n",
           args->cam_num, args->iq_file_dir, args->fps,
           args->aiisp_model[0] ? args->aiisp_model : "(disabled)");
    printf("bind_mode: %s, enable_sub_stream: %d\n",
           args->bind_mode == APP_BIND_MODE_VI_VPSS_VENC ? "vi->vpss->venc" : "vi->venc",
           args->enable_sub_stream ? 1 : 0);
    for (int i = 0; i < cfg->rtsp_count; ++i) {
        printf("route[%d]: rtsp://<board-ip>:%d%s\n",
               cfg->rtsps[i].route_id, cfg->rtsps[i].rtsp_port, cfg->rtsps[i].rtsp_path);
    }
}

int main(int argc, char *argv[])
{
    AppArgs_t args;
    AiispRunnerCfg_t runner_cfg;

    set_default_args(&args);
    if (parse_args(argc, argv, &args) != 0)
        return -1;

    fill_runner_cfg(&args, &runner_cfg);
    print_config(&args, &runner_cfg);

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    return AiispRunner_Run(&runner_cfg, &g_running);
}
