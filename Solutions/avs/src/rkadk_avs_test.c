#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rk_comm_video.h"
#include "avs_runner.h"

#define IQ_FILE_PATH "/etc/iqfiles"
#define AVS_CALIB_PATH "/oem/usr/share/avs_calib/calib_file.xml"

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
    const char *calib_file;
    int cam_num;
    int cam0_id;
    int cam1_id;
    int vi_width;
    int vi_height;
    int avs_main_width;
    int avs_main_height;
    int avs_sub_width;
    int avs_sub_height;
    int fps;
    int bitrate;
    int rtsp_base_port;
    float distance;
    int blend_mode;
    bool enable_sub_stream;
    bool use_h265;
    char main_rtsp_path[64];
    char sub_rtsp_path[64];
} AppArgs_t;

enum {
    OPT_VI_SIZE = 1000,
    OPT_AVS_MAIN_SIZE,
    OPT_AVS_SUB_SIZE,
    OPT_CALIB_FILE,
    OPT_DISTANCE,
    OPT_ENABLE_SUB_STREAM,
    OPT_CODEC,
    OPT_BLEND_MODE,
};

static const struct option g_long_options[] = {
    {"aiq", required_argument, NULL, 'a'},
    {"cam0_id", required_argument, NULL, 'I'},
    {"cam1_id", required_argument, NULL, 'J'},
    {"camera_num", required_argument, NULL, 'n'},
    {"fps", required_argument, NULL, 'f'},
    {"bitrate", required_argument, NULL, 'b'},
    {"rtsp_port", required_argument, NULL, 'p'},
    {"vi_size", required_argument, NULL, OPT_VI_SIZE},
    {"avs_main_size", required_argument, NULL, OPT_AVS_MAIN_SIZE},
    {"avs_sub_size", required_argument, NULL, OPT_AVS_SUB_SIZE},
    {"calib_file", required_argument, NULL, OPT_CALIB_FILE},
    {"distance", required_argument, NULL, OPT_DISTANCE},
    {"enable_sub_stream", required_argument, NULL, OPT_ENABLE_SUB_STREAM},
    {"codec", required_argument, NULL, OPT_CODEC},
    {"blend_mode", required_argument, NULL, OPT_BLEND_MODE},
    {"help", no_argument, NULL, 'h'},
    {0, 0, 0, 0},
};

static int parse_size(const char *text, int *width, int *height)
{
    if (!text || !width || !height)
        return -1;
    return sscanf(text, "%dx%d", width, height) == 2 ? 0 : -1;
}

static void print_usage(const char *name)
{
    printf("usage example:\n");
    printf("\t%s -n 2 -I 0 -J 1 -a /etc/iqfiles --vi_size 1920x1080 --avs_main_size 3840x1080 \\\n",
           name);
    printf("\t   --avs_sub_size 1920x544 --calib_file %s --enable_sub_stream 1\n",
           AVS_CALIB_PATH);
    printf("\t-a: AIQ iqfiles 路径, 默认 /etc/iqfiles\n");
    printf("\t-I/-J: cam0/cam1 sensor id, 默认 0/1\n");
    printf("\t-n: camera num, 当前建议 2\n");
    printf("\t-f: fps, 默认 25\n");
    printf("\t-b: 编码码率 kbps, 默认 4096\n");
    printf("\t-p: RTSP 起始端口, 默认 554\n");
    printf("\t--vi_size: 输入 VI 尺寸, 默认 1920x1080\n");
    printf("\t--avs_main_size: AVS 主码流尺寸, 默认 3840x1080\n");
    printf("\t--avs_sub_size: AVS 子码流尺寸, 默认 1920x544\n");
    printf("\t--calib_file: AVS 标定文件路径, 默认 %s\n", AVS_CALIB_PATH);
    printf("\t--distance: stitch distance, 默认 5.0\n");
    printf("\t--enable_sub_stream: 0/1, 默认 1\n");
    printf("\t--codec: h264 或 h265, 默认 h265\n");
    printf("\t--blend_mode: AVS 拼接模式 0=融合 1=无融合垂直 2=无融合水平, 默认 0\n");
}

static void set_default_args(AppArgs_t *args)
{
    memset(args, 0, sizeof(*args));
    args->iq_file_dir = IQ_FILE_PATH;
    args->calib_file = AVS_CALIB_PATH;
    args->cam_num = 2;
    args->cam0_id = 0;
    args->cam1_id = 1;
    args->vi_width = 1920;
    args->vi_height = 1080;
    args->avs_main_width = 3840;
    args->avs_main_height = 1080;
    args->avs_sub_width = 1920;
    args->avs_sub_height = 544;
    args->fps = 25;
    args->bitrate = 4096;
    args->rtsp_base_port = 554;
    args->distance = 5.0f;
    args->enable_sub_stream = true;
    args->use_h265 = true;
    args->blend_mode = 0;
    snprintf(args->main_rtsp_path, sizeof(args->main_rtsp_path), "%s", "/live/avs_main");
    snprintf(args->sub_rtsp_path, sizeof(args->sub_rtsp_path), "%s", "/live/avs_sub");
}

static int parse_args(int argc, char *argv[], AppArgs_t *args)
{
    int c;

    while ((c = getopt_long(argc, argv, "a:I:J:n:f:b:p:h", g_long_options, NULL)) != -1) {
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
        case 'b':
            args->bitrate = atoi(optarg);
            break;
        case 'p':
            args->rtsp_base_port = atoi(optarg);
            break;
        case OPT_VI_SIZE:
            if (parse_size(optarg, &args->vi_width, &args->vi_height) != 0)
                return -1;
            break;
        case OPT_AVS_MAIN_SIZE:
            if (parse_size(optarg, &args->avs_main_width, &args->avs_main_height) != 0)
                return -1;
            break;
        case OPT_AVS_SUB_SIZE:
            if (parse_size(optarg, &args->avs_sub_width, &args->avs_sub_height) != 0)
                return -1;
            break;
        case OPT_CALIB_FILE:
            args->calib_file = optarg;
            break;
        case OPT_DISTANCE:
            args->distance = (float)atof(optarg);
            break;
        case OPT_ENABLE_SUB_STREAM:
            args->enable_sub_stream = atoi(optarg) ? true : false;
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
        case OPT_BLEND_MODE:
            args->blend_mode = atoi(optarg);
            if (args->blend_mode < 0 || args->blend_mode > 2) {
                printf("[AVS] blend_mode 需要为 0/1/2\n");
                return -1;
            }
            break;
        case 'h':
        default:
            print_usage(argv[0]);
            return -1;
        }
    }

    if (args->cam_num != 2) {
        printf("[AVS] 当前图式 AVS 入口默认按双目拼接实现，请传 -n 2\n");
        return -1;
    }
    if (args->fps <= 0 || args->bitrate <= 0 || args->rtsp_base_port <= 0)
        return -1;
    if (args->vi_width <= 0 || args->vi_height <= 0 ||
        args->avs_main_width <= 0 || args->avs_main_height <= 0)
        return -1;
    if (args->enable_sub_stream && (args->avs_sub_width <= 0 || args->avs_sub_height <= 0))
        return -1;

    return 0;
}

static void fill_camera_cfg(CamPipeCameraCfg_t *camera_cfg,
                            int cam_id,
                            const char *iq_dir,
                            int fps,
                            int vi_width,
                            int vi_height)
{
    memset(camera_cfg, 0, sizeof(*camera_cfg));
    camera_cfg->isp_cfg.cam_id = cam_id;
    camera_cfg->isp_cfg.iq_file_dir = iq_dir;
    camera_cfg->isp_cfg.hdr_mode = CAM_ADAPTER_HDR_NORMAL;
    camera_cfg->isp_cfg.fps = fps;
    camera_cfg->isp_cfg.is_multi_cam = true;

    camera_cfg->vi_cfg.pipe_id = cam_id;
    camera_cfg->vi_cfg.chn_id = 1;
    camera_cfg->vi_cfg.width = vi_width;
    camera_cfg->vi_cfg.height = vi_height;
    camera_cfg->vi_cfg.buf_cnt = 2;
    camera_cfg->vi_cfg.pix_fmt = RK_FMT_YUV420SP;
    camera_cfg->vi_cfg.buf_wrap_enable = false;
    camera_cfg->vi_cfg.buf_line = 0;
}

static void fill_runner_cfg(const AppArgs_t *args, AvsRunnerCfg_t *cfg)
{
    int bind_idx = 0;
    int output_idx = 0;
    int codec_type = args->use_h265 ? CAM_ADAPTER_CODEC_H265 : CAM_ADAPTER_CODEC_H264;
    RtspServerCodec rtsp_codec = args->use_h265 ? RTSP_SERVER_CODEC_H265 : RTSP_SERVER_CODEC_H264;

    memset(cfg, 0, sizeof(*cfg));
    cfg->fps = args->fps;
    cfg->pipe_cfg.camera_count = 2;
    cfg->pipe_cfg.avs_count = 1;

    fill_camera_cfg(&cfg->pipe_cfg.cameras[0], args->cam0_id, args->iq_file_dir,
                    args->fps, args->vi_width, args->vi_height);
    fill_camera_cfg(&cfg->pipe_cfg.cameras[1], args->cam1_id, args->iq_file_dir,
                    args->fps, args->vi_width, args->vi_height);

    cfg->pipe_cfg.avss[0].avs_cfg.grp_id = 0;
    cfg->pipe_cfg.avss[0].avs_cfg.chn_id = 0;
    cfg->pipe_cfg.avss[0].avs_cfg.cam_num = 2;
    cfg->pipe_cfg.avss[0].avs_cfg.src_width = args->vi_width;
    cfg->pipe_cfg.avss[0].avs_cfg.src_height = args->vi_height;
    cfg->pipe_cfg.avss[0].avs_cfg.dst_width = args->avs_main_width;
    cfg->pipe_cfg.avss[0].avs_cfg.dst_height = args->avs_main_height;
    cfg->pipe_cfg.avss[0].avs_cfg.distance = args->distance;
    cfg->pipe_cfg.avss[0].avs_cfg.calib_file = args->calib_file;
    /* ============================================================
     * AVS 拼接模式 (blend_mode) 说明 —— 无标定直拼 vs 融合拼接
     * ============================================================
     *
     * 底层对应 rk_comm_avs.h 中 AVS_MODE_E 枚举：
     *   0 = AVS_MODE_BLEND       : 融合模式，需要标定文件/LUT，在拼接缝做像素融合
     *   1 = AVS_MODE_NOBLEND_VER : 无融合-垂直拼接（上下排布）
     *   2 = AVS_MODE_NOBLEND_HOR : 无融合-水平拼接（左右排布）
     *
     * --- 无标定模式 (1/2) 核心逻辑 ---
     * 不需要标定文件，也不加载 librkAVS_genLutAndStitch.so 或
     * librkALG_avsCore.so。AVS 只做画面简单排布，不计算拼接缝融合。
     * 适用于快速验证 VI->AVS->VENC->RTSP 基础链路是否通畅。
     *
     *   blend_mode=1 (上下拼接 / NOBLEND_VER)：
     *     多路输入垂直堆叠。输出高度 = 输入高度 × 相机数。
     *     例如两路 1920x1080 → 输出 1920x2160，
     *         三路 1920x1080 → 输出 1920x3240。
     *     双目              三目
     *     +-----------+    +-----------+
     *     |  camera0  |    |  camera0  |
     *     +-----------+    +-----------+
     *     |  camera1  |    |  camera1  |
     *     +-----------+    +-----------+
     *                      |  camera2  |
     *                      +-----------+
     *
     *   blend_mode=2 (左右拼接 / NOBLEND_HOR)：
     *     多路输入水平并排。输出宽度 = 输入宽度 × 相机数。
     *     例如两路 1920x1080 → 输出 3840x1080，
     *         三路 1920x1080 → 输出 5760x1080。
     *     双目:
     *     +-----------+-----------+
     *     |  camera0  |  camera1  |
     *     +-----------+-----------+
     *     三目:
     *     +-----------+-----------+-----------+
     *     |  camera0  |  camera1  |  camera2  |
     *     +-----------+-----------+-----------+
     *
     * --- 决定拼接位置的参数 ---
     * 在无标定直拼模式下，每路画面在最终画布上的位置由以下参数共同决定：
     *
     *   [1] blend_mode = avs_cfg.blend_mode
     *       决定排布方向：1=垂直堆叠，2=水平并排。
     *
     *   [2] 输入尺寸 = src_width × src_height
     *       决定每路画面的原始尺寸，即 avs_cfg.src_width / src_height。
     *
     *   [3] 输出尺寸 = avs_main_width × avs_main_height
     *       决定最终画布大小，应能容纳所有相机画面。
     *       垂直拼接：main_height ≥ src_height × cam_num
     *       水平拼接：main_width  ≥ src_width  × cam_num
     *
     *   [4] VI→AVS 绑定时的 dst.chn_id (见下方 binds 配置)
     *       决定"第几个相机"对应"画面中的第几行/第几列"。
     *       dst.chn_id = 0 → 第一行(垂直模式)/第一列(水平模式)
     *       dst.chn_id = 1 → 第二行/第二列，以此类推。
     *       如果要调换两个相机的位置，交换它们的 dst.chn_id 即可。
     *
     *   [5] 相机数量 = cam_num
     *       决定排布的总行数(垂直模式)或总列数(水平模式)。
     *
     *   [6] 可选微调：RK_MPI_AVS_SetPipeAttr(grp, pipe, &attr)
     *       attr.stDstRect 可精确指定某个 pipe 在输出画布上的矩形区域，
     *       用于实现画中画、任意位置叠加等自定义排布。
     *       当不显式调用 SetPipeAttr 时，底层按 mode 自动计算默认排布。
     *
     * --- 融合模式 (0) ---
     * 需要 calib_file 或 jsonPath 提供标定参数，底层会加载对应的
     * AVS 算法库做拼接缝像素融合，输出无缝全景图。
     *
     * 当前入口默认按融合模式 (0) 配置。
     * 如果暂时没有标定文件，可临时切到 1 或 2 先验证链路。 */
    cfg->pipe_cfg.avss[0].avs_cfg.blend_mode = args->blend_mode;
    cfg->pipe_cfg.avss[0].avs_cfg.channel_count = args->enable_sub_stream ? 2 : 1;
    cfg->pipe_cfg.avss[0].avs_cfg.channels[0].chn_id = 0;
    cfg->pipe_cfg.avss[0].avs_cfg.channels[0].width = args->avs_main_width;
    cfg->pipe_cfg.avss[0].avs_cfg.channels[0].height = args->avs_main_height;
    cfg->pipe_cfg.avss[0].avs_cfg.channels[0].enabled = true;
    if (args->enable_sub_stream) {
        cfg->pipe_cfg.avss[0].avs_cfg.channels[1].chn_id = 1;
        cfg->pipe_cfg.avss[0].avs_cfg.channels[1].width = args->avs_sub_width;
        cfg->pipe_cfg.avss[0].avs_cfg.channels[1].height = args->avs_sub_height;
        cfg->pipe_cfg.avss[0].avs_cfg.channels[1].enabled = true;
    }

    /*
     * VI→AVS 绑定：将各路 VI 通道分别绑定到 AVS 的不同 pipe。
     *
     *   dst.chn_id  = pipe 编号，决定该相机在最终画面中的位置：
     *     垂直拼接 (blend_mode=1): pipe 0=最上方, 1=第二行, 2=第三行...
     *     水平拼接 (blend_mode=2): pipe 0=最左列, 1=第二列, 2=第三列...
     *
     *   换位技巧：要交换两个相机在画面中的位置，
     *   只需交换它们各自的 dst.chn_id 即可。
     */
    cfg->pipe_cfg.binds[bind_idx].camera_index = 0;
    cfg->pipe_cfg.binds[bind_idx].bind_cfg.src.mod_type = CAM_MODULE_VI;
    cfg->pipe_cfg.binds[bind_idx].bind_cfg.src.dev_id = args->cam0_id;
    cfg->pipe_cfg.binds[bind_idx].bind_cfg.src.chn_id = 1;
    cfg->pipe_cfg.binds[bind_idx].bind_cfg.dst.mod_type = CAM_MODULE_AVS;
    cfg->pipe_cfg.binds[bind_idx].bind_cfg.dst.dev_id = 0;
    cfg->pipe_cfg.binds[bind_idx].bind_cfg.dst.chn_id = 0;
    bind_idx++;

    cfg->pipe_cfg.binds[bind_idx].camera_index = 1;
    cfg->pipe_cfg.binds[bind_idx].bind_cfg.src.mod_type = CAM_MODULE_VI;
    cfg->pipe_cfg.binds[bind_idx].bind_cfg.src.dev_id = args->cam1_id;
    cfg->pipe_cfg.binds[bind_idx].bind_cfg.src.chn_id = 1;
    cfg->pipe_cfg.binds[bind_idx].bind_cfg.dst.mod_type = CAM_MODULE_AVS;
    cfg->pipe_cfg.binds[bind_idx].bind_cfg.dst.dev_id = 0;
    cfg->pipe_cfg.binds[bind_idx].bind_cfg.dst.chn_id = 1;
    bind_idx++;

    cfg->pipe_cfg.outputs[output_idx].route_id = 0;
    cfg->pipe_cfg.outputs[output_idx].venc_cfg.chn_id = 0;
    cfg->pipe_cfg.outputs[output_idx].venc_cfg.width = args->avs_main_width;
    cfg->pipe_cfg.outputs[output_idx].venc_cfg.height = args->avs_main_height;
    cfg->pipe_cfg.outputs[output_idx].venc_cfg.fps = args->fps;
    cfg->pipe_cfg.outputs[output_idx].venc_cfg.gop = args->fps;
    cfg->pipe_cfg.outputs[output_idx].venc_cfg.bitrate = args->bitrate;
    cfg->pipe_cfg.outputs[output_idx].venc_cfg.codec_type = codec_type;
    cfg->pipe_cfg.outputs[output_idx].venc_cfg.rc_mode = CAM_ADAPTER_RC_CBR;
    cfg->pipe_cfg.outputs[output_idx].venc_cfg.buf_wrap_enable = false;
    cfg->pipe_cfg.outputs[output_idx].venc_cfg.buf_line = 0;
    cfg->pipe_cfg.outputs[output_idx].venc_cfg.svc_enable = false;
    cfg->pipe_cfg.outputs[output_idx].venc_cfg.motion_deblur_enable = false;
    cfg->pipe_cfg.outputs[output_idx].venc_cfg.ref_buf_share = true;

    cfg->pipe_cfg.binds[bind_idx].camera_index = 0;
    cfg->pipe_cfg.binds[bind_idx].bind_cfg.src.mod_type = CAM_MODULE_AVS;
    cfg->pipe_cfg.binds[bind_idx].bind_cfg.src.dev_id = 0;
    cfg->pipe_cfg.binds[bind_idx].bind_cfg.src.chn_id = 0;
    cfg->pipe_cfg.binds[bind_idx].bind_cfg.dst.mod_type = CAM_MODULE_VENC;
    cfg->pipe_cfg.binds[bind_idx].bind_cfg.dst.dev_id = 0;
    cfg->pipe_cfg.binds[bind_idx].bind_cfg.dst.chn_id = 0;
    bind_idx++;

    cfg->rtsps[output_idx].route_id = 0;
    cfg->rtsps[output_idx].rtsp_port = args->rtsp_base_port;
    cfg->rtsps[output_idx].rtsp_path = args->main_rtsp_path;
    cfg->rtsps[output_idx].codec = rtsp_codec;
    output_idx++;

    if (args->enable_sub_stream) {
        cfg->pipe_cfg.outputs[output_idx].route_id = 1;
        cfg->pipe_cfg.outputs[output_idx].venc_cfg.chn_id = 1;
        cfg->pipe_cfg.outputs[output_idx].venc_cfg.width = args->avs_sub_width;
        cfg->pipe_cfg.outputs[output_idx].venc_cfg.height = args->avs_sub_height;
        cfg->pipe_cfg.outputs[output_idx].venc_cfg.fps = args->fps;
        cfg->pipe_cfg.outputs[output_idx].venc_cfg.gop = args->fps;
        cfg->pipe_cfg.outputs[output_idx].venc_cfg.bitrate = args->bitrate / 2;
        cfg->pipe_cfg.outputs[output_idx].venc_cfg.codec_type = codec_type;
        cfg->pipe_cfg.outputs[output_idx].venc_cfg.rc_mode = CAM_ADAPTER_RC_CBR;
        cfg->pipe_cfg.outputs[output_idx].venc_cfg.buf_wrap_enable = false;
        cfg->pipe_cfg.outputs[output_idx].venc_cfg.buf_line = 0;
        cfg->pipe_cfg.outputs[output_idx].venc_cfg.svc_enable = false;
        cfg->pipe_cfg.outputs[output_idx].venc_cfg.motion_deblur_enable = false;
        cfg->pipe_cfg.outputs[output_idx].venc_cfg.ref_buf_share = true;

        cfg->pipe_cfg.binds[bind_idx].camera_index = 0;
        cfg->pipe_cfg.binds[bind_idx].bind_cfg.src.mod_type = CAM_MODULE_AVS;
        cfg->pipe_cfg.binds[bind_idx].bind_cfg.src.dev_id = 0;
        cfg->pipe_cfg.binds[bind_idx].bind_cfg.src.chn_id = 1;
        cfg->pipe_cfg.binds[bind_idx].bind_cfg.dst.mod_type = CAM_MODULE_VENC;
        cfg->pipe_cfg.binds[bind_idx].bind_cfg.dst.dev_id = 0;
        cfg->pipe_cfg.binds[bind_idx].bind_cfg.dst.chn_id = 1;
        bind_idx++;

        cfg->rtsps[output_idx].route_id = 1;
        cfg->rtsps[output_idx].rtsp_port = args->rtsp_base_port + 1;
        cfg->rtsps[output_idx].rtsp_path = args->sub_rtsp_path;
        cfg->rtsps[output_idx].codec = rtsp_codec;
        output_idx++;
    }

    cfg->pipe_cfg.output_count = output_idx;
    cfg->pipe_cfg.bind_count = bind_idx;
    cfg->rtsp_count = output_idx;
}

static void print_config(const AppArgs_t *args, const AvsRunnerCfg_t *cfg)
{
    const char *blend_names[] = {"融合(BLEND)", "无融合垂直(NOBLEND_VER)", "无融合水平(NOBLEND_HOR)"};
    printf("AVS graph: vi=%dx%d, main=%dx%d, sub=%s, blend_mode=%d(%s), calib=%s\n",
           args->vi_width, args->vi_height,
           args->avs_main_width, args->avs_main_height,
           args->enable_sub_stream ? "enabled" : "disabled",
           args->blend_mode,
           (args->blend_mode >= 0 && args->blend_mode <= 2) ? blend_names[args->blend_mode] : "unknown",
           args->calib_file ? args->calib_file : "(null)");
    for (int i = 0; i < cfg->rtsp_count; ++i) {
        printf("route[%d]: rtsp://<board-ip>:%d%s\n",
               cfg->rtsps[i].route_id, cfg->rtsps[i].rtsp_port, cfg->rtsps[i].rtsp_path);
    }
}

int main(int argc, char *argv[])
{
    AppArgs_t args;
    AvsRunnerCfg_t runner_cfg;

    set_default_args(&args);
    if (parse_args(argc, argv, &args) != 0) {
        print_usage(argv[0]);
        return -1;
    }

    fill_runner_cfg(&args, &runner_cfg);
    print_config(&args, &runner_cfg);

    signal(SIGINT, sigterm_handler);
    signal(SIGTERM, sigterm_handler);

    return AvsRunner_Run(&runner_cfg, &g_running);
}
