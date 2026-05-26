#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rk_comm_video.h"
#include "aov_runner.h"

#define IQ_FILE_PATH "/etc/iqfiles"
#define DEFAULT_REC_DIR "/mnt/sdcard/video"
#define DEFAULT_REC_PREFIX "cam0"

static volatile bool g_running = true;

static void sigterm_handler(int sig)
{
    if (sig == SIGINT || sig == SIGTERM) {
        printf("Signal %d caught, exiting...\n", sig);
        g_running = false;
    }
}

typedef struct {
    const char *iq_file_dir;
    int cam_num;
    int cam0_id;
    int fps;
    int width;
    int height;
    int gop;
    int bitrate;
    const char *bind_mode;
    const char *rec_dir;
    const char *rec_prefix;
    int rec_split_sec;
    int max_folder_size_mb;
    int loop_count;
    int loop_duration_sec;
    int suspend_time_ms;
    bool enable_aov;
    bool use_h265;
} AppArgs_t;

enum {
    OPT_WIDTH = 1000,
    OPT_HEIGHT,
    OPT_BITRATE,
    OPT_GOP,
    OPT_BIND_MODE,
    OPT_REC_DIR,
    OPT_REC_PREFIX,
    OPT_REC_SPLIT,
    OPT_MAX_FOLDER_SIZE,
    OPT_ENABLE_AOV,
    OPT_LOOP_COUNT,
    OPT_LOOP_DURATION,
    OPT_CODEC,
};

static const struct option g_long_options[] = {
    {"aiq", required_argument, NULL, 'a'},
    {"cam0_id", required_argument, NULL, 'I'},
    {"camera_num", required_argument, NULL, 'n'},
    {"fps", required_argument, NULL, 'f'},
    {"width", required_argument, NULL, OPT_WIDTH},
    {"height", required_argument, NULL, OPT_HEIGHT},
    {"bitrate", required_argument, NULL, OPT_BITRATE},
    {"gop", required_argument, NULL, OPT_GOP},
    {"bind_mode", required_argument, NULL, OPT_BIND_MODE},
    {"rec_dir", required_argument, NULL, OPT_REC_DIR},
    {"rec_prefix", required_argument, NULL, OPT_REC_PREFIX},
    {"rec_split", required_argument, NULL, OPT_REC_SPLIT},
    {"max_folder_size", required_argument, NULL, OPT_MAX_FOLDER_SIZE},
    {"enable_aov", required_argument, NULL, OPT_ENABLE_AOV},
    {"loop_count", required_argument, NULL, OPT_LOOP_COUNT},
    {"loop_duration", required_argument, NULL, OPT_LOOP_DURATION},
    {"codec", required_argument, NULL, OPT_CODEC},
    {"suspend", required_argument, NULL, 'S'},
    {"help", no_argument, NULL, 'h'},
    {0, 0, 0, 0},
};

static void print_usage(const char *name)
{
    printf("usage example:\n");
    printf("\t%s -I 0 -a /etc/iqfiles --width 1920 --height 1080 --codec h265\n", name);
    printf("\t-a: AIQ iqfiles 路径, 默认 /etc/iqfiles\n");
    printf("\t-I: cam0 sensor id, 默认 0\n");
    printf("\t-n: camera 数量 (1 或 2), 默认 1\n");
    printf("\t-f: 帧率 fps, 默认 25\n");
    printf("\t--width: 主码流宽度, 默认 1920\n");
    printf("\t--height: 主码流高度, 默认 1080\n");
    printf("\t--bitrate: 编码码率 kbps, 默认 4096\n");
    printf("\t--gop: GOP (关键帧间隔), 默认等于 fps\n");
    printf("\t--bind_mode: 绑定模式 vpss 或 direct, 默认 vpss\n");
    printf("\t--rec_dir: 录像存储路径, 默认 /mnt/sdcard/video\n");
    printf("\t--rec_prefix: 录像文件名前缀, 默认 cam0\n");
    printf("\t--rec_split: 单文件切片时长秒, 默认 60\n");
    printf("\t--max_folder_size: 录像文件夹最大容量MB, 0不限制, 默认 0\n");
    printf("\t--enable_aov: 是否启用 AOV 低功耗休眠 (0/1), 默认 1\n");
    printf("\t-S: AOV 休眠时间 ms, 默认 1000\n");
    printf("\t--codec: h264 或 h265, 默认 h265\n");
    printf("\t--loop_count: 循环切换次数, 默认 -1 (不切换)\n");
    printf("\t--loop_duration: 循环切换间隔秒, 默认 30\n");
}

static void set_default_args(AppArgs_t *args)
{
    memset(args, 0, sizeof(*args));
    args->iq_file_dir = IQ_FILE_PATH;
    args->cam_num = 1;
    args->cam0_id = 0;
    args->fps = 25;
    args->width = 1920;
    args->height = 1080;
    args->gop = -1;
    args->bitrate = 4096;
    args->bind_mode = "vpss";
    args->rec_dir = DEFAULT_REC_DIR;
    args->rec_prefix = DEFAULT_REC_PREFIX;
    args->rec_split_sec = 60;
    args->max_folder_size_mb = 0;
    args->loop_count = -1;
    args->loop_duration_sec = 30;
    args->suspend_time_ms = 1000;
    args->enable_aov = true;
    args->use_h265 = false;
}

static int parse_args(int argc, char *argv[], AppArgs_t *args)
{
    int c;

    while ((c = getopt_long(argc, argv, "a:I:n:f:S:h", g_long_options, NULL)) != -1) {
        switch (c) {
        case 'a':
            args->iq_file_dir = optarg;
            break;
        case 'I':
            args->cam0_id = atoi(optarg);
            break;
        case 'n':
            args->cam_num = atoi(optarg);
            break;
        case 'f':
            args->fps = atoi(optarg);
            break;
        case 'S':
            args->suspend_time_ms = atoi(optarg);
            break;
        case OPT_WIDTH:
            args->width = atoi(optarg);
            break;
        case OPT_HEIGHT:
            args->height = atoi(optarg);
            break;
        case OPT_BITRATE:
            args->bitrate = atoi(optarg);
            break;
        case OPT_GOP:
            args->gop = atoi(optarg);
            break;
        case OPT_BIND_MODE:
            args->bind_mode = optarg;
            break;
        case OPT_REC_DIR:
            args->rec_dir = optarg;
            break;
        case OPT_REC_PREFIX:
            args->rec_prefix = optarg;
            break;
        case OPT_REC_SPLIT:
            args->rec_split_sec = atoi(optarg);
            break;
        case OPT_MAX_FOLDER_SIZE:
            args->max_folder_size_mb = atoi(optarg);
            break;
        case OPT_ENABLE_AOV:
            args->enable_aov = atoi(optarg) ? true : false;
            break;
        case OPT_LOOP_COUNT:
            args->loop_count = atoi(optarg);
            break;
        case OPT_LOOP_DURATION:
            args->loop_duration_sec = atoi(optarg);
            break;
        case OPT_CODEC:
            if (strcmp(optarg, "h264") == 0) {
                args->use_h265 = false;
            } else if (strcmp(optarg, "h265") == 0) {
                args->use_h265 = true;
            } else {
                return -1;
            }
            break;
        case 'h':
        default:
            print_usage(argv[0]);
            return -1;
        }
    }

    if (args->cam_num < 1 || args->cam_num > 2) {
        printf("[AOV] camera_num 需要为 1 或 2\n");
        return -1;
    }
    if (args->fps <= 0)
        return -1;
    if (args->width <= 0 || args->height <= 0)
        return -1;
    if (args->rec_split_sec <= 0)
        return -1;

    return 0;
}

static void fill_camera_cfg(CamPipeCameraCfg_t *camera_cfg,
                            int cam_id,
                            const char *iq_dir,
                            int fps,
                            int width,
                            int height)
{
    memset(camera_cfg, 0, sizeof(*camera_cfg));
    camera_cfg->isp_cfg.cam_id = cam_id;
    camera_cfg->isp_cfg.iq_file_dir = iq_dir;
    camera_cfg->isp_cfg.hdr_mode = CAM_ADAPTER_HDR_NORMAL;
    camera_cfg->isp_cfg.fps = fps;
    camera_cfg->isp_cfg.is_multi_cam = false;

    camera_cfg->vi_cfg.pipe_id = cam_id;
    camera_cfg->vi_cfg.chn_id = 1;
    camera_cfg->vi_cfg.width = width;
    camera_cfg->vi_cfg.height = height;
    camera_cfg->vi_cfg.buf_cnt = 2;
    camera_cfg->vi_cfg.pix_fmt = RK_FMT_YUV420SP;
    camera_cfg->vi_cfg.buf_wrap_enable = false;
    camera_cfg->vi_cfg.buf_line = 0;
}

static void fill_runner_cfg(const AppArgs_t *args, AovRunnerCfg_t *cfg)
{
    int bind_idx = 0;
    int output_idx = 0;
    int codec_type = args->use_h265 ? CAM_ADAPTER_CODEC_H265 : CAM_ADAPTER_CODEC_H264;
    int gop = (args->gop > 0) ? args->gop : args->fps;

    memset(cfg, 0, sizeof(*cfg));
    cfg->fps = args->fps;
    cfg->enable_aov = args->enable_aov;
    cfg->use_h265 = args->use_h265;
    cfg->suspend_time_ms = args->suspend_time_ms;
    cfg->loop_count = args->loop_count;
    cfg->loop_duration_sec = args->loop_duration_sec;

    cfg->pipe_cfg.camera_count = args->cam_num;
    cfg->pipe_cfg.avs_count = 0;

    fill_camera_cfg(&cfg->pipe_cfg.cameras[0], args->cam0_id, args->iq_file_dir,
                    args->fps, args->width, args->height);

    if (args->cam_num > 1) {
        fill_camera_cfg(&cfg->pipe_cfg.cameras[1], 1, args->iq_file_dir,
                        args->fps, args->width, args->height);
    }

    for (int i = 0; i < args->cam_num; ++i) {
        int cam_id = (i == 0) ? args->cam0_id : 1;

        if (strcmp(args->bind_mode, "direct") == 0) {
            cfg->pipe_cfg.cameras[i].enable_vpss = false;

            cfg->pipe_cfg.binds[bind_idx].camera_index = i;
            cfg->pipe_cfg.binds[bind_idx].bind_cfg.src.mod_type = CAM_MODULE_VI;
            cfg->pipe_cfg.binds[bind_idx].bind_cfg.src.dev_id = cam_id;
            cfg->pipe_cfg.binds[bind_idx].bind_cfg.src.chn_id = 1;
            cfg->pipe_cfg.binds[bind_idx].bind_cfg.dst.mod_type = CAM_MODULE_VENC;
            cfg->pipe_cfg.binds[bind_idx].bind_cfg.dst.dev_id = 0;
            cfg->pipe_cfg.binds[bind_idx].bind_cfg.dst.chn_id = i;
            bind_idx++;
        } else {
            cfg->pipe_cfg.cameras[i].enable_vpss = true;
            cfg->pipe_cfg.cameras[i].vpss_cfg.grp_id = cam_id;
            cfg->pipe_cfg.cameras[i].vpss_cfg.channel_count = 1;
            cfg->pipe_cfg.cameras[i].vpss_cfg.channels[0].chn_id = 0;
            cfg->pipe_cfg.cameras[i].vpss_cfg.channels[0].width = args->width;
            cfg->pipe_cfg.cameras[i].vpss_cfg.channels[0].height = args->height;
            cfg->pipe_cfg.cameras[i].vpss_cfg.channels[0].pix_fmt = RK_FMT_YUV420SP;
            cfg->pipe_cfg.cameras[i].vpss_cfg.channels[0].chn_mode = CAM_VPSS_CHN_MODE_AUTO;
            cfg->pipe_cfg.cameras[i].vpss_cfg.channels[0].enabled = true;

            cfg->pipe_cfg.binds[bind_idx].camera_index = i;
            cfg->pipe_cfg.binds[bind_idx].bind_cfg.src.mod_type = CAM_MODULE_VI;
            cfg->pipe_cfg.binds[bind_idx].bind_cfg.src.dev_id = cam_id;
            cfg->pipe_cfg.binds[bind_idx].bind_cfg.src.chn_id = 1;
            cfg->pipe_cfg.binds[bind_idx].bind_cfg.dst.mod_type = CAM_MODULE_VPSS;
            cfg->pipe_cfg.binds[bind_idx].bind_cfg.dst.dev_id = cam_id;
            cfg->pipe_cfg.binds[bind_idx].bind_cfg.dst.chn_id = 0;
            bind_idx++;

            cfg->pipe_cfg.binds[bind_idx].camera_index = i;
            cfg->pipe_cfg.binds[bind_idx].bind_cfg.src.mod_type = CAM_MODULE_VPSS;
            cfg->pipe_cfg.binds[bind_idx].bind_cfg.src.dev_id = cam_id;
            cfg->pipe_cfg.binds[bind_idx].bind_cfg.src.chn_id = 0;
            cfg->pipe_cfg.binds[bind_idx].bind_cfg.dst.mod_type = CAM_MODULE_VENC;
            cfg->pipe_cfg.binds[bind_idx].bind_cfg.dst.dev_id = 0;
            cfg->pipe_cfg.binds[bind_idx].bind_cfg.dst.chn_id = i;
            bind_idx++;
        }

        cfg->pipe_cfg.outputs[output_idx].route_id = i;
        cfg->pipe_cfg.outputs[output_idx].venc_cfg.chn_id = i;
        cfg->pipe_cfg.outputs[output_idx].venc_cfg.width = args->width;
        cfg->pipe_cfg.outputs[output_idx].venc_cfg.height = args->height;
        cfg->pipe_cfg.outputs[output_idx].venc_cfg.fps = args->fps;
        cfg->pipe_cfg.outputs[output_idx].venc_cfg.gop = gop;
        cfg->pipe_cfg.outputs[output_idx].venc_cfg.bitrate = args->bitrate;
        cfg->pipe_cfg.outputs[output_idx].venc_cfg.codec_type = codec_type;
        cfg->pipe_cfg.outputs[output_idx].venc_cfg.rc_mode = CAM_ADAPTER_RC_CBR;
        cfg->pipe_cfg.outputs[output_idx].venc_cfg.buf_wrap_enable = false;
        cfg->pipe_cfg.outputs[output_idx].venc_cfg.buf_line = 0;
        cfg->pipe_cfg.outputs[output_idx].venc_cfg.svc_enable = false;
        cfg->pipe_cfg.outputs[output_idx].venc_cfg.motion_deblur_enable = false;
        cfg->pipe_cfg.outputs[output_idx].venc_cfg.ref_buf_share = true;
        output_idx++;
    }

    cfg->pipe_cfg.output_count = output_idx;
    cfg->pipe_cfg.bind_count = bind_idx;

    strncpy(cfg->rec_dir, args->rec_dir, sizeof(cfg->rec_dir) - 1);
    strncpy(cfg->rec_prefix, args->rec_prefix, sizeof(cfg->rec_prefix) - 1);
}

static void print_config(const AppArgs_t *args, const AovRunnerCfg_t *cfg)
{
    printf("AOV config: cam=%d, %dx%d@%d, codec=%s, bind=%s\n",
           args->cam_num, args->width, args->height, args->fps,
           args->use_h265 ? "h265" : "h264", args->bind_mode);
    printf("  rec: %s/%s_*.%s (raw stream)\n",
           args->rec_dir, args->rec_prefix,
           args->use_h265 ? "h265" : "h264");
    printf("  AOV: %s, suspend=%dms, loop=%d/%ds\n",
           args->enable_aov ? "enabled" : "disabled",
           args->suspend_time_ms, args->loop_count, args->loop_duration_sec);
}

int main(int argc, char *argv[])
{
    AppArgs_t args;
    AovRunnerCfg_t runner_cfg;

    set_default_args(&args);
    if (parse_args(argc, argv, &args) != 0) {
        print_usage(argv[0]);
        return -1;
    }

    fill_runner_cfg(&args, &runner_cfg);
    print_config(&args, &runner_cfg);

    signal(SIGINT, sigterm_handler);
    signal(SIGTERM, sigterm_handler);

    return AovRunner_Run(&runner_cfg, &g_running);
}
