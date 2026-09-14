/*
 * user_splice.c — AOV 应用层 Demo（拼接 / 非拼接运行时选择）
 *
 * 默认使用 AOV_VIDEO_BACKEND_SPLICE_AVS 后端：双 sensor 经 AVS 硬件水平拼接后
 * 送入 VENC。传入 multi 参数时使用 AOV_VIDEO_BACKEND_MULTI 后端：
 * 双 sensor 独立 VI 通道直连 VENC，不做拼接。
 *
 * 演示 AOV 接口的完整测试流程：
 *   - 注册 enter/exit/write 回调
 *   - 初始化视频管线（VI + VENC + 回调）
 *   - 命令行参数自由组合测试序列
 *   - 序列完成后保持运行，等待信号退出
 *   - Ctrl+C 自动恢复外设并清理
 *
 * 用法: ./aov [action[:param]] ...
 * 不带参数时打印用法说明并运行默认序列。
 */

#include "aov.h"
#include "aov_video.h"
#include "aov_record.h"
#include "aov_record_queue.h"
#include "aov_isp.h"
#include "aov_helper.h"
#include "lmo_common.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <unistd.h>
#include <signal.h>
#include <sys/prctl.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/sendfile.h>

#include "rk_mpi_sys.h"
#include "rk_common.h"
#include <time.h>

/* ==================================================================
 * 帧率测试开关
 *
 * TEST_FPS > 0 时，将 VI/ISP/VENC/录像帧率统一设为 TEST_FPS，
 * 并在 VENC 回调中统计实际帧率，每隔 5 秒打印一次统计信息。
 * 编译时可通过 -DTEST_FPS=30 范围覆盖，也可直接修改此处宏值。
 * ================================================================== */
#define TEST_FPS 30

#if TEST_FPS
#define TST_VIDEO_FPS      TEST_FPS
#define TST_VIDEO_GOP      TEST_FPS
#define TST_VI_FRAME_RATE  TEST_FPS
#define TST_ISP_FPS        TEST_FPS
#else
#define TST_VIDEO_FPS      REC_VIDEO_FPS
#define TST_VIDEO_GOP      REC_VIDEO_GOP
#define TST_VI_FRAME_RATE  USER_VI_FRAME_RATE
#define TST_ISP_FPS        15
#endif

/* ==================================================================
 * 全局变量
 * ================================================================== */

/* 录像上下文最大容量: 非拼接为 2 sensor × 主/子，拼接只使用 [0][主/子] */
#define USER_SENSOR_NUM 2
#define USER_INPUT_MAIN_WIDTH  1920
#define USER_INPUT_MAIN_HEIGHT 1080
#define USER_INPUT_SUB_WIDTH   640
#define USER_INPUT_SUB_HEIGHT  360
#define USER_SPLICE_MAIN_WIDTH  (USER_INPUT_MAIN_WIDTH * USER_SENSOR_NUM)
#define USER_SPLICE_MAIN_HEIGHT USER_INPUT_MAIN_HEIGHT
#define USER_SPLICE_SUB_WIDTH   (USER_SPLICE_MAIN_WIDTH / 2)
#define USER_SPLICE_SUB_HEIGHT  (USER_SPLICE_MAIN_HEIGHT / 2)
#define REC_SENSOR_MAX USER_SENSOR_NUM
#define REC_CHN_MAX    2
#define REC_MODE_SINGLE 0   /* AOV 休眠单帧模式, 1fps */
#define REC_MODE_MULTI  1   /* 连续帧模式, 15fps */
#define REC_MODE_MAX    2
#define REC_VIDEO_FPS   15
#define REC_VIDEO_GOP   15
#if TEST_FPS
#undef REC_VIDEO_FPS
#undef REC_VIDEO_GOP
#define REC_VIDEO_FPS   TST_VIDEO_FPS
#define REC_VIDEO_GOP   TST_VIDEO_GOP
#endif
#define REC_MAIN_KBPS   2048
#define REC_SUB_KBPS    512
#define REC_MAIN_MAX_KBPS 4096
#define REC_SUB_MAX_KBPS  1024
#define REC_AUDIO_ENABLE 0  /* 1: MP4 混入 AAC 音频, 0: 纯视频 MP4 */
#define REC_AUDIO_SAMPLE_RATE 48000
#define REC_AUDIO_CHANNELS 1
#define REC_AUDIO_SAMPLES_PER_FRAME 1024
#define REC_OUTPUT_MOUNT "/mnt/aov_sdcard"
#define REC_OUTPUT_DIR   "/mnt/aov_sdcard/aov"
#define SHOT_TMP_DIR     "/tmp/aov_shot"   /* AOV 期间 SD 不可用时的临时截图目录 */
#define REC_SD_DEVICE    "/dev/mmcblk1p1"
#define SYS_SDCARD_MOUNT "/mnt/sdcard"
#define SYS_SDCARD_AUTOMOUNT_UNIT "mnt-sdcard.automount"
#define REC_QUEUE_CACHE_SECONDS 180              /* 目标缓存窗口：希望队列可承载约 180 秒积压 */
#define REC_QUEUE_POOL_MIN_BYTES (32U * 1024U * 1024U) /* 内存池下限，避免低码率时算得过小 */
#define REC_QUEUE_POOL_MAX_BYTES (160U * 1024U * 1024U) /* 内存池上限，限制 RAM 占用 */
#define REC_QUEUE_FRAME_MIN  512                /* 帧槽下限，避免先撞 metadata 容量 */
#define REC_QUEUE_POOL_ALIGN_BYTES (64U * 1024U) /* 内存池按 64KB 对齐，便于容量规整 */
#define REC_QUEUE_BURST_PERCENT 110U            /* 额外预留 10% 突发余量给码率波动 */
#define REC_QUEUE_WAKE_WATERMARK 80
#define REC_QUEUE_LOG_DROP_STEP 30
#define USER_AOV_FRAMES_PER_SECOND_MAX 3
#if TEST_FPS
#define USER_VI_FRAME_RATE   TST_VI_FRAME_RATE  /* TEST_FPS: VI/ISP 采集帧率 */
#else
#define USER_VI_FRAME_RATE   15    /* VI/ISP 采集帧率 (fps)，0 表示使用默认值 15 */
#endif
static AOV_RECORD_CTX_T* g_record_ctx[REC_SENSOR_MAX][REC_CHN_MAX];

/*
 * 录像帧计数器 — 解决 IDLE 态无调度线程触发落盘的问题。
 *
 * AOV 单帧模式由调度线程周期性调用 write_event 落盘，
 * 但退出 AOV 进入 IDLE 态（连续帧模式）后 write_event 不再触发，
 * 帧仅缓冲在 fmp4_muxer sample_pool 中，一旦溢出就会丢帧。
 *
 * 这里在 VENC 回调中对多帧模式做帧计数，达到阈值立即落盘，
 * 保证连续帧模式下也具备自动落盘能力。
 */
#define REC_MULTI_FLUSH_THRESHOLD  15*30 /*大约30秒落盘一次*/
static int g_record_frame_cnt[REC_SENSOR_MAX][REC_CHN_MAX];

static AOV_RECORD_QUEUE_T *g_record_queue = NULL;
static pthread_t g_record_thread;
static pthread_mutex_t g_record_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_record_cond = PTHREAD_COND_INITIALIZER;
static int g_record_thread_running;
static int g_record_thread_stop;
static int g_record_sd_ready;
static int g_record_drain_pause;
static int g_record_drain_active;
static unsigned long g_record_drop_log_cnt;
static AOV_VENC_ATTR_T g_record_venc_attr[REC_CHN_MAX];
static int g_record_venc_attr_valid[REC_CHN_MAX];

#define USER_OSD_BMP_FILE "./osd_aov.bmp"
static LMO_RGN_S *g_osd_rgn[REC_SENSOR_MAX][REC_CHN_MAX];
static int g_osd_ready;

/* 运行标志 */
static volatile int g_running = 1;

/* 外设解绑标记：AOV 休眠前解绑，唤醒后重新绑定。
 * Ctrl+C 退出时若外设仍处于解绑状态，则自动恢复。 */
static int g_peripherals_unbound = 0;

typedef enum {
    USER_BACKEND_SPLICE = 0,
    USER_BACKEND_MULTI  = 1,
} USER_BACKEND_MODE_E;

static USER_BACKEND_MODE_E g_user_backend = USER_BACKEND_SPLICE;

static int user_record_sensor_count(void)
{
    return (g_user_backend == USER_BACKEND_SPLICE) ? 1 : USER_SENSOR_NUM;
}

static int user_record_target_kbps(int vchn)
{
    return (vchn == AOV_VIDEO_MAIN_CHN) ? REC_MAIN_KBPS : REC_SUB_KBPS;
}

static int user_record_max_kbps(int vchn)
{
    return (vchn == AOV_VIDEO_MAIN_CHN) ? REC_MAIN_MAX_KBPS : REC_SUB_MAX_KBPS;
}

static const char *user_backend_name(void)
{
    return (g_user_backend == USER_BACKEND_SPLICE) ? "splice" : "multi";
}

static int user_record_width(int vchn)
{
    if (g_user_backend == USER_BACKEND_SPLICE)
        return (vchn == AOV_VIDEO_MAIN_CHN) ? USER_SPLICE_MAIN_WIDTH
                                            : USER_SPLICE_SUB_WIDTH;

    return (vchn == AOV_VIDEO_MAIN_CHN) ? USER_INPUT_MAIN_WIDTH
                                        : USER_INPUT_SUB_WIDTH;
}

static int user_record_height(int vchn)
{
    if (g_user_backend == USER_BACKEND_SPLICE)
        return (vchn == AOV_VIDEO_MAIN_CHN) ? USER_SPLICE_MAIN_HEIGHT
                                            : USER_SPLICE_SUB_HEIGHT;

    return (vchn == AOV_VIDEO_MAIN_CHN) ? USER_INPUT_MAIN_HEIGHT
                                        : USER_INPUT_SUB_HEIGHT;
}

static unsigned int user_aov_frame_duration_us(const aov_action_t *action)
{
    int fps;

    if (!action)
        return 1000000U;

    fps = action->frames_per_second;
    if (fps > 0) {
        if (fps > USER_AOV_FRAMES_PER_SECOND_MAX)
            fps = USER_AOV_FRAMES_PER_SECOND_MAX;
        return (unsigned int)(1000000U / (unsigned int)fps);
    }

    if (action->seconds_per_frame > 0)
        return (unsigned int)action->seconds_per_frame * 1000000U;

    return 1000000U;
}

static void user_record_file_name(char *buf, size_t size, int schn, int vchn)
{
    const char *chn_label = (vchn == AOV_VIDEO_MAIN_CHN) ? "main" : "sub";

    if (g_user_backend == USER_BACKEND_SPLICE)
        snprintf(buf, size, "splice_%s", chn_label);
    else
        snprintf(buf, size, "d%d_%s", schn, chn_label);
}

static void user_record_on_need_idr(int vchn, void *userdata)
{
    (void)userdata;
    printf("[USER] record requests IDR on vchn=%d\n", vchn);
    aov_video_request_idr_frame((AOV_VIDEO_CHN_E)vchn);
}

static int user_is_backend_arg(const char *arg)
{
    if (!arg)
        return 0;

    return strcmp(arg, "splice") == 0 ||
           strcmp(arg, "--splice") == 0 ||
           strcmp(arg, "backend:splice") == 0 ||
           strcmp(arg, "--backend=splice") == 0 ||
           strcmp(arg, "multi") == 0 ||
           strcmp(arg, "--multi") == 0 ||
           strcmp(arg, "backend:multi") == 0 ||
           strcmp(arg, "--backend=multi") == 0;
}

static void user_parse_backend_args(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (!arg)
            continue;

        if (strcmp(arg, "splice") == 0 ||
            strcmp(arg, "--splice") == 0 ||
            strcmp(arg, "backend:splice") == 0 ||
            strcmp(arg, "--backend=splice") == 0) {
            g_user_backend = USER_BACKEND_SPLICE;
        } else if (strcmp(arg, "multi") == 0 ||
                   strcmp(arg, "--multi") == 0 ||
                   strcmp(arg, "backend:multi") == 0 ||
                   strcmp(arg, "--backend=multi") == 0) {
            g_user_backend = USER_BACKEND_MULTI;
        }
    }
}

/* ==================================================================
 * 信号处理
 * ================================================================== */

static void sig_handler(int sig)
{
    (void)sig;
    printf("\n[USER] signal %d caught, shutting down...\n", sig);
    g_running = 0;
}

/* ==================================================================
 * 录像缓存 / SD 状态 / drain 线程
 * ================================================================== */

static void record_signal_drain(void)
{
    pthread_mutex_lock(&g_record_lock);
    pthread_cond_broadcast(&g_record_cond);
    pthread_mutex_unlock(&g_record_lock);
}

static size_t record_align_up(size_t value, size_t align)
{
    if (align == 0)
        return value;
    return ((value + align - 1) / align) * align;
}

static size_t record_max_size(size_t a, size_t b)
{
    return (a > b) ? a : b;
}

static size_t record_min_size(size_t a, size_t b)
{
    return (a < b) ? a : b;
}

static int record_max_int(int a, int b)
{
    return (a > b) ? a : b;
}

static int record_queue_create(void)
{
    if (g_record_queue)
        return 0;

    /* 队列缓存的是编码后码流，容量按所有有效录像通道的聚合码率/fps 估算。 */
    int sensor_count = user_record_sensor_count();
    int total_kbps = 0;
    int total_fps = 0;

    for (int c = 0; c < REC_CHN_MAX; c++) {
        if (!g_record_venc_attr_valid[c]) {
            printf("[USER] record venc attr not ready: chn=%d\n", c);
            return AOV_ERR_NOT_READY;
        }

        int fps = g_record_venc_attr[c].bit_rate_attr.fps > 0
                      ? g_record_venc_attr[c].bit_rate_attr.fps
                      : REC_VIDEO_FPS;
        int kbps = g_record_venc_attr[c].bit_rate_attr.target_kbps > 0
                       ? g_record_venc_attr[c].bit_rate_attr.target_kbps
                       : user_record_target_kbps(c);
        total_kbps += kbps * sensor_count;
        total_fps += fps * sensor_count;
    }

    /* pool 按目标缓存时长折算字节数，frame_capacity 按同一时长折算帧数。 */
    uint64_t bytes_per_second = ((uint64_t)total_kbps * 1000ULL) / 8ULL;
    uint64_t raw_pool_size =
        (bytes_per_second * (uint64_t)REC_QUEUE_CACHE_SECONDS *
         (uint64_t)REC_QUEUE_BURST_PERCENT) / 100ULL;
    size_t pool_size = record_align_up((size_t)raw_pool_size,
                                       REC_QUEUE_POOL_ALIGN_BYTES);
    pool_size = record_max_size(pool_size, REC_QUEUE_POOL_MIN_BYTES);
    pool_size = record_min_size(pool_size, REC_QUEUE_POOL_MAX_BYTES);

    int frame_capacity =
        (total_fps * REC_QUEUE_CACHE_SECONDS * (int)REC_QUEUE_BURST_PERCENT) / 100;
    frame_capacity = record_max_int(frame_capacity, REC_QUEUE_FRAME_MIN);

    printf("[USER] queue size: kbps=%d fps=%d cache=%ds => pool=%zu frames=%d\n",
           total_kbps, total_fps, REC_QUEUE_CACHE_SECONDS,
           pool_size, frame_capacity);

    return aov_record_queue_init(&g_record_queue, pool_size, frame_capacity);
}

static int record_mount_exists(const char *mount_point)
{
    FILE *fp = fopen("/proc/mounts", "r");
    if (!fp)
        return 0;

    char dev[128];
    char mnt[256];
    char type[64];
    int found = 0;

    while (fscanf(fp, "%127s %255s %63s %*s %*d %*d\n", dev, mnt, type) == 3) {
        if (strcmp(mnt, mount_point) == 0) {
            found = 1;
            break;
        }
    }

    fclose(fp);
    return found;
}

static int record_release_system_sdcard(void)
{
    int ret;

    ret = system("systemctl is-active --quiet " SYS_SDCARD_AUTOMOUNT_UNIT);
    if (ret == 0) {
        printf("[USER] stopping %s before AOV SD control\n",
               SYS_SDCARD_AUTOMOUNT_UNIT);
        ret = system("systemctl stop " SYS_SDCARD_AUTOMOUNT_UNIT);
        if (ret != 0) {
            printf("[USER] stop %s failed: ret=%d\n",
                   SYS_SDCARD_AUTOMOUNT_UNIT, ret);
            return -1;
        }
    }

    if (record_mount_exists(SYS_SDCARD_MOUNT)) {
        sync();
        if (umount(SYS_SDCARD_MOUNT) != 0) {
            printf("[USER] umount %s failed before AOV SD control: %d\n",
                   SYS_SDCARD_MOUNT, errno);
            return -1;
        }
        printf("[USER] system SD unmounted: %s\n", SYS_SDCARD_MOUNT);
    }

    return 0;
}

static int record_sd_mount(void)
{
    struct stat st;

    /* 设备节点不存在则跳过 */
    if (stat(REC_SD_DEVICE, &st) != 0)
        return -1;

    /* 已挂载则直接返回 */
    if (record_mount_exists(REC_OUTPUT_MOUNT))
        return 0;

    if (mkdir(REC_OUTPUT_MOUNT, 0755) != 0 && errno != EEXIST) {
        printf("[USER] mkdir %s failed: %d\n", REC_OUTPUT_MOUNT, errno);
        return -1;
    }

    if (mount(REC_SD_DEVICE, REC_OUTPUT_MOUNT, "vfat",
              MS_NOATIME | MS_NODIRATIME, "") != 0) {
        /* vfat 失败尝试 exfat */
        if (mount(REC_SD_DEVICE, REC_OUTPUT_MOUNT, "exfat",
                  MS_NOATIME | MS_NODIRATIME, "") != 0) {
            printf("[USER] mount %s -> %s failed: %d\n",
                   REC_SD_DEVICE, REC_OUTPUT_MOUNT, errno);
            return -1;
        }
    }

    printf("[USER] SD mounted: %s -> %s\n", REC_SD_DEVICE, REC_OUTPUT_MOUNT);
    return 0;
}

static void record_sd_umount(void)
{
    if (!record_mount_exists(REC_OUTPUT_MOUNT))
        return;

    sync();
    if (umount(REC_OUTPUT_MOUNT) != 0)
        printf("[USER] umount %s failed: %d\n", REC_OUTPUT_MOUNT, errno);
    else
        printf("[USER] SD unmounted: %s\n", REC_OUTPUT_MOUNT);
}

static void record_recover_sd_storage(void)
{
    printf("[USER] SD storage I/O error, remount %s\n", REC_OUTPUT_MOUNT);
    record_sd_umount();
    aov_helper_bind_sdcard();
}

static int record_prepare_sd_storage(int verbose)
{
    struct stat st;

    if (!record_mount_exists(REC_OUTPUT_MOUNT)) {
        /* 尝试自动挂载 SD 卡 */
        if (record_sd_mount() != 0) {
            if (verbose)
                printf("[USER] SD mount not ready: %s\n", REC_OUTPUT_MOUNT);
            return -1;
        }
    }

    if (stat(REC_OUTPUT_DIR, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            if (verbose)
                printf("[USER] record output exists but is not dir: %s\n", REC_OUTPUT_DIR);
            return -1;
        }
    } else if (mkdir(REC_OUTPUT_DIR, 0755) != 0) {
        int err = errno;
        if (verbose)
            printf("[USER] mkdir %s failed: errno=%d\n", REC_OUTPUT_DIR, err);
        return -err;
    }

    if (access(REC_OUTPUT_DIR, W_OK) != 0) {
        int err = errno;
        if (verbose)
            printf("[USER] record output not writable: %s errno=%d\n",
                   REC_OUTPUT_DIR, err);
        return -err;
    }

    return 0;
}

static int record_wait_sd_storage(int timeout_ms)
{
    int waited = 0;
    int recovered = 0;

    while (g_running && (timeout_ms < 0 || waited <= timeout_ms)) {
        int verbose = (waited == 0 || (waited % 1000) == 0);
        int ret = record_prepare_sd_storage(verbose);
        if (ret == 0)
            return 0;
        if (ret == -EIO) {
            if (!recovered) {
                recovered = 1;
                record_recover_sd_storage();
                usleep(300 * 1000);
                waited += 300;
                continue;
            }
            printf("[USER] SD storage I/O error after remount, stop retrying: %s\n",
                   REC_OUTPUT_DIR);
            return -1;
        }
        usleep(100 * 1000);
        waited += 100;
    }
    return -1;
}

static void record_set_sd_ready(int ready)
{
    pthread_mutex_lock(&g_record_lock);
    g_record_sd_ready = ready;
    pthread_cond_broadcast(&g_record_cond);
    pthread_mutex_unlock(&g_record_lock);
}

static void record_set_drain_pause(int pause)
{
    pthread_mutex_lock(&g_record_lock);
    g_record_drain_pause = pause;
    pthread_cond_broadcast(&g_record_cond);
    pthread_mutex_unlock(&g_record_lock);
}

static int record_wait_drain_idle(int timeout_ms)
{
    int waited = 0;
    while (timeout_ms < 0 || waited <= timeout_ms) {
        pthread_mutex_lock(&g_record_lock);
        int active = g_record_drain_active;
        pthread_mutex_unlock(&g_record_lock);
        if (!active)
            return 0;
        usleep(10 * 1000);
        waited += 10;
    }
    return -1;
}

static int record_wait_queue_empty(int timeout_ms)
{
    if (!g_record_queue)
        return 0;

    int waited = 0;
    while (timeout_ms < 0 || waited <= timeout_ms) {
        int count = aov_record_queue_count(g_record_queue);
        pthread_mutex_lock(&g_record_lock);
        int active = g_record_drain_active;
        pthread_mutex_unlock(&g_record_lock);
        if (count == 0 && !active)
            return 0;
        record_signal_drain();
        usleep(20 * 1000);
        waited += 20;
    }

    printf("[USER] wait queue empty timeout: count=%d used=%zu/%zu\n",
           aov_record_queue_count(g_record_queue),
           aov_record_queue_used(g_record_queue),
           aov_record_queue_size(g_record_queue));
    return -1;
}

static void record_flush_all(void)
{
    int sensor_count = user_record_sensor_count();

    for (int s = 0; s < sensor_count; s++) {
        for (int c = 0; c < REC_CHN_MAX; c++) {
            if (g_record_ctx[s][c]) {
                int ret = aov_record_write_event(g_record_ctx[s][c]);
                if (ret != 0)
                    printf("[USER] record flush d%d c%d failed: %d\n",
                           s, c, ret);
                g_record_frame_cnt[s][c] = 0;
            }
        }
    }
}

static void record_suspend_all_io(void)
{
    int sensor_count = user_record_sensor_count();

    for (int s = 0; s < sensor_count; s++) {
        for (int c = 0; c < REC_CHN_MAX; c++) {
            if (g_record_ctx[s][c]) {
                int ret = aov_record_suspend_io(g_record_ctx[s][c]);
                if (ret != 0)
                    printf("[USER] record suspend d%d c%d failed: %d\n",
                           s, c, ret);
            }
        }
    }
}

static void record_resume_all_io(void)
{
    int sensor_count = user_record_sensor_count();

    for (int s = 0; s < sensor_count; s++) {
        for (int c = 0; c < REC_CHN_MAX; c++) {
            if (g_record_ctx[s][c]) {
                int ret = aov_record_resume_io(g_record_ctx[s][c]);
                if (ret != 0)
                    printf("[USER] record resume d%d c%d failed: %d\n",
                           s, c, ret);
            }
        }
    }
}

static int record_drain_one(void)
{
    AOV_RECORD_QUEUE_FRAME_T qf;
    int ret = aov_record_queue_peek(g_record_queue, &qf);
    if (ret != AOV_ERR_SUCCESS)
        return ret;

    if (qf.schn >= user_record_sensor_count() || qf.vchn >= REC_CHN_MAX ||
        qf.mode >= REC_MODE_MAX || !g_record_ctx[qf.schn][qf.vchn]) {
        printf("[USER] drop invalid queued frame: s=%u c=%u m=%u len=%u\n",
               qf.schn, qf.vchn, qf.mode, qf.data_size);
        aov_record_queue_pop(g_record_queue);
        return AOV_ERR_SUCCESS;
    }

    AOV_RECORD_FRAME_T frame;
    memset(&frame, 0, sizeof(frame));
    frame.data_vaddr = qf.data;
    frame.data_size = qf.data_size;
    frame.pts = qf.pts;
    frame.frame_type = qf.frame_type;

    AOV_RECORD_CTX_T *ctx = g_record_ctx[qf.schn][qf.vchn];
    ret = aov_record_input_frame(ctx, &frame);
    if (ret != AOV_ERR_SUCCESS) {
        printf("[USER] queued record input failed: s=%u c=%u m=%u ret=%d\n",
               qf.schn, qf.vchn, qf.mode, ret);
        if (ret == AOV_ERR_IO || ret == AOV_ERR_NOT_READY) {
            record_set_sd_ready(0);
            return ret;
        }
        aov_record_queue_pop(g_record_queue);
        return ret;
    }

    g_record_frame_cnt[qf.schn][qf.vchn]++;
    int threshold = (qf.mode == REC_MODE_SINGLE) ? 60 : REC_MULTI_FLUSH_THRESHOLD;
    if (g_record_frame_cnt[qf.schn][qf.vchn] >= threshold) {
        ret = aov_record_write_event(ctx);
        if (ret == AOV_ERR_SUCCESS) {
            g_record_frame_cnt[qf.schn][qf.vchn] = 0;
        } else {
            printf("[USER] queued record flush failed: s=%u c=%u m=%u ret=%d\n",
                   qf.schn, qf.vchn, qf.mode, ret);
            if (ret == AOV_ERR_IO || ret == AOV_ERR_NOT_READY) {
                record_set_sd_ready(0);
                return ret;
            }
        }
    }

    aov_record_queue_pop(g_record_queue);
    return AOV_ERR_SUCCESS;
}

static void *record_drain_thread(void *arg)
{
    (void)arg;
    printf("[USER] record drain thread started\n");

    while (1) {
        pthread_mutex_lock(&g_record_lock);
        while (!g_record_thread_stop &&
               (!g_record_sd_ready || g_record_drain_pause ||
                aov_record_queue_count(g_record_queue) <= 0)) {
            pthread_cond_wait(&g_record_cond, &g_record_lock);
        }
        if (g_record_thread_stop) {
            pthread_mutex_unlock(&g_record_lock);
            break;
        }
        g_record_drain_active = 1;
        pthread_mutex_unlock(&g_record_lock);

        int ret = record_drain_one();

        pthread_mutex_lock(&g_record_lock);
        g_record_drain_active = 0;
        pthread_cond_broadcast(&g_record_cond);
        pthread_mutex_unlock(&g_record_lock);

        if (ret == AOV_ERR_IO || ret == AOV_ERR_NOT_READY)
            usleep(100 * 1000);
    }

    printf("[USER] record drain thread stopped\n");
    return NULL;
}

static int record_drain_start(void)
{
    if (!g_record_queue) {
        int ret = record_queue_create();
        if (ret != AOV_ERR_SUCCESS) {
            printf("[USER] record queue init failed: %d\n", ret);
            return -1;
        }
    }

    pthread_mutex_lock(&g_record_lock);
    g_record_thread_stop = 0;
    g_record_thread_running = 1;
    pthread_mutex_unlock(&g_record_lock);

    if (pthread_create(&g_record_thread, NULL, record_drain_thread, NULL) != 0) {
        pthread_mutex_lock(&g_record_lock);
        g_record_thread_running = 0;
        pthread_mutex_unlock(&g_record_lock);
        printf("[USER] record drain thread create failed\n");
        return -1;
    }

    return 0;
}

static void record_drain_stop(void)
{
    pthread_mutex_lock(&g_record_lock);
    int running = g_record_thread_running;
    g_record_thread_stop = 1;
    pthread_cond_broadcast(&g_record_cond);
    pthread_mutex_unlock(&g_record_lock);

    if (running)
        pthread_join(g_record_thread, NULL);

    pthread_mutex_lock(&g_record_lock);
    g_record_thread_running = 0;
    pthread_mutex_unlock(&g_record_lock);
}

/* ==================================================================
 * OSD：AOV 单帧模式右下角标记
 * ================================================================== */

static int user_venc_chn_id(int schn, int vchn)
{
    if (g_user_backend == USER_BACKEND_MULTI)
        return schn * REC_CHN_MAX + vchn;

    /* AVS 拼接后端当前 VENC 映射: main=0, sub=1 */
    return vchn;
}

static void user_osd_show_all(int show)
{
    if (!g_osd_ready)
        return;

    int sensor_count = user_record_sensor_count();

    for (int s = 0; s < sensor_count; s++) {
        for (int c = 0; c < REC_CHN_MAX; c++) {
            if (g_osd_rgn[s][c]) {
                int ret = LMO_COMM_RGN_Show(g_osd_rgn[s][c], show);
                if (ret != LMO_SUCCESS) {
                    printf("[USER] osd %s d%d c%d failed: %d\n",
                           show ? "show" : "hide", s, c, ret);
                }
            }
        }
    }
}

static int user_osd_init_all(void)
{
    int width[REC_CHN_MAX] = {
        user_record_width(AOV_VIDEO_MAIN_CHN),
        user_record_width(AOV_VIDEO_SUB_CHN),
    };
    int height[REC_CHN_MAX] = {
        user_record_height(AOV_VIDEO_MAIN_CHN),
        user_record_height(AOV_VIDEO_SUB_CHN),
    };
    int osd_w[REC_CHN_MAX] = { 128, 80 };
    int osd_h[REC_CHN_MAX] = { 48, 32 };
    int margin_x[REC_CHN_MAX] = { 32, 16 };
    int margin_y[REC_CHN_MAX] = { 32, 16 };

    int sensor_count = user_record_sensor_count();

    for (int s = 0; s < sensor_count; s++) {
        for (int c = 0; c < REC_CHN_MAX; c++) {
            int vchn = user_venc_chn_id(s, c);

            LMO_RGN_Params params;
            memset(&params, 0, sizeof(params));
            params.rgnHandle      = (LMO_U32)vchn;
            params.rgnType        = LMO_RGN_TYPE_OVERLAY;
            params.modId          = LMO_ID_VENC;
            params.devId          = 0;
            params.chnId          = vchn;
            params.regionX        = (width[c] - osd_w[c] - margin_x[c]) & ~15;
            params.regionY        = (height[c] - osd_h[c] - margin_y[c]) & ~15;
            params.regionW        = (LMO_U32)osd_w[c];
            params.regionH        = (LMO_U32)osd_h[c];
            params.layer          = 7;
            params.fgAlpha        = 255;
            params.bgAlpha        = 0;
            params.bmpFormat      = LMO_FMT_BGRA8888;
            params.srcFileBmpName = USER_OSD_BMP_FILE;

            g_osd_rgn[s][c] = LMO_COMM_RGN_Create(&params);
            if (!g_osd_rgn[s][c]) {
                printf("[USER] osd create d%d c%d failed\n", s, c);
                continue;
            }

            LMO_COMM_RGN_Show(g_osd_rgn[s][c], 0);
        }
    }

    g_osd_ready = 1;
    user_osd_show_all(aov_status_get() == AOV_STATUS_ACTIVE);
    printf("[USER] osd init done, aov mark %s\n",
           (aov_status_get() == AOV_STATUS_ACTIVE) ? "shown" : "hidden");
    return AOV_ERR_SUCCESS;
}

static void user_osd_deinit_all(void)
{
    if (!g_osd_ready)
        return;

    int sensor_count = user_record_sensor_count();

    for (int s = 0; s < sensor_count; s++) {
        for (int c = 0; c < REC_CHN_MAX; c++) {
            if (g_osd_rgn[s][c]) {
                LMO_COMM_RGN_Destroy(g_osd_rgn[s][c]);
                g_osd_rgn[s][c] = NULL;
            }
        }
    }
    g_osd_ready = 0;
    printf("[USER] osd deinit done\n");
}

/* ==================================================================
 * AOV 行为回调 — enter / exit / write
 * ================================================================== */

/*
 * 进入 AOV 模式 — 休眠前准备工作
 *
 * 执行顺序：
 *   1. 卸载 WiFi 内核模块（必须在 SDIO 解绑前，否则 wifi 线程访问
 *      已解绑的 SDIO 会触发 kernel panic）
 *   2. 解绑 SD 卡
 *   3. 解绑 SDIO
 *   4. 解绑声卡
 *   5. 解绑 USB host 控制器
 *   6. 解绑 Ethernet (gmac)
 *   7. 关闭非引导 CPU 核降漏电
 *
 * 注：系统实际休眠由 aov_state_enter_sleep() (aov_state.c)
 *      在视频线程中触发，本回调负责休眠前的外围设备准备。
 */

/* 前向声明: shot_migrate_tmp_to_sd 定义在 user_jpeg_mjpeg_save 之后 */
static void shot_migrate_tmp_to_sd(void);

/* 外设恢复：AOV 休眠期间 Ctrl+C 退出时，重新绑定被解绑的外设。
 * user_aov_exit_action 和清理路径共用此函数。 */
static void restore_peripherals(void)
{
    if (!g_peripherals_unbound)
        return;

    printf("[USER] restoring peripherals...\n");

    aov_helper_enable_nonboot_cpus();
    aov_helper_enable_usb();
    system("ifconfig usb0 up");
    system("dhclient usb0");
    aov_helper_bind_ethernet();
    aov_helper_bind_soundcard();
    aov_helper_bind_sdio();
    aov_helper_load_wifi_modules();
    aov_helper_bind_sdcard();
    if (record_wait_sd_storage(5000) == 0) {
        record_resume_all_io();
        record_set_sd_ready(1);
        record_set_drain_pause(0);
        record_signal_drain();
        /* 把 AOV 期间暂存在 /tmp/aov_shot 的截图迁移到 SD 卡 */
        shot_migrate_tmp_to_sd();
        if (g_record_queue) {
            printf("[USER] SD ready, record drain resumed: queue=%d used=%zu/%zu\n",
                   aov_record_queue_count(g_record_queue),
                   aov_record_queue_used(g_record_queue),
                   aov_record_queue_size(g_record_queue));
        } else {
            printf("[USER] SD ready, record drain resumed\n");
        }
    } else {
        record_set_sd_ready(0);
        record_set_drain_pause(1);
        printf("[USER] SD not ready after restore, queued frames stay in RAM\n");
    }

    g_peripherals_unbound = 0;
    printf("[USER] peripherals restored\n");
}

int user_aov_enter_action(void)
{
    user_osd_show_all(1);

    /*
     * SD 卡即将解绑：先让 drain 线程尽量写空队列，再暂停所有 record IO。
     * suspend_io 保留 muxer 状态和文件路径，退出 AOV 后可继续 append 同一 MP4。
     */
    record_wait_queue_empty(2000);
    record_set_drain_pause(1);
    record_wait_drain_idle(1000);
    record_flush_all();
    record_suspend_all_io();
    record_set_sd_ready(0);

    /*
     * 休眠前准备 —— 按 SDK sample_aov_vi_venc.c unbindAllDevice() 对齐:
     *   1. 先卸载 WiFi 内核模块 (必须在 SDIO 解绑前完成！否则 wifi 线程
     *      访问已解绑的 SDIO 会导致 kernel panic)
     *   2. 解绑 SD 卡 (mmc 驱动)
     *   3. 解绑 SDIO (WiFi/BT)
     *   4. 解绑声卡 (I2S + acodec + dsm + multicodecs)
     *   5. 解绑 USB host 控制器 (避免 PM resume 重新枚举 USB 设备耗时)
     *   6. 解绑 Ethernet/gmac (避免 PM resume 重初始化 PHY/MAC)
     *   7. 关闭非引导 CPU 核 (cpu2/cpu3) 降漏电
     */
    aov_helper_unload_wifi_modules();
    record_sd_umount();
    aov_helper_unbind_sdcard();
    aov_helper_unbind_sdio();
    aov_helper_unbind_soundcard();
    aov_helper_disable_usb();
    aov_helper_unbind_ethernet();
    aov_helper_disable_nonboot_cpus();

    g_peripherals_unbound = 1;

    /*
     * 进入 AOV 休眠：单帧/连续帧共用同一个 MP4，SD 解绑期间帧先进内存队列，
     * 退出 AOV 恢复 SD 后由 drain 线程继续写回原文件。
     */
    printf("[USER] entering AOV sleep\n");

    return 0;
}

/*
 * 退出 AOV 模式 — 唤醒后恢复工作
 *
 * 执行顺序：
 *   1. 重新使能非引导 CPU 核
 *   2. 重新绑定 USB host 控制器驱动
 *   3. 重新绑定 Ethernet (gmac)
 *   4. 重新绑定声卡
 *   5. 重新绑定 SDIO (WiFi/BT)
 *   6. 重新加载 WiFi 内核模块（必须在 SDIO 绑定后）
 *   7. 重新绑定 SD 卡
 *
 * 注：网络恢复由其他模块负责。
 */
int user_aov_exit_action(void)
{
    printf("[USER] exiting AOV sleep\n");
    fflush(stdout);
    user_osd_show_all(0);

    restore_peripherals();

    return 0;
}

/*
 * 写入事件回调 — AOV 休眠周期结束时调用。
 *
 *   - MP4 模式：fmp4_muxer_build_fragment 将缓冲帧打包为 moof+mdat 片段，
 *     fwrite + fsync 落盘。文件保持打开，后续帧追加到同一文件。
 *     kill -9 安全：fsync 后数据已持久化。
 */
int user_aov_write_event(void)
{
    if (!g_record_queue) {
        aov_video_aov_info_clear();
        printf("[USER] write event skipped: record queue not initialized\n");
        return 0;
    }

    int usage = aov_record_queue_usage_percent(g_record_queue);
    if (usage >= REC_QUEUE_WAKE_WATERMARK) {
        printf("[USER] queue usage %d%%, request AOV exit for SD drain\n", usage);
        aov_exit_set(AOV_EXIT_TIMING, 30);
    }

    record_signal_drain();
    aov_video_aov_info_clear();
    printf("[USER] write event done: queue=%d used=%zu/%zu dropped=%llu\n",
           aov_record_queue_count(g_record_queue),
           aov_record_queue_used(g_record_queue),
           aov_record_queue_size(g_record_queue),
           (unsigned long long)aov_record_queue_dropped(g_record_queue));
    return 0;
}

/* ==================================================================
 * 截图工作线程 — 把 JPEG/MJPEG 截图移出 VENC 回调路径
 *
 * aov_video_jpeg_save / aov_video_mjpeg_save 为异步触发接口: 只投递截图
 * 请求并立即返回, 编码结果由 SDK 通过 user_venc_data_cb 上报
 * (frame_type 'J'=单帧 JPEG, 'M'=多帧拼接 MJPEG, 数据在 frame->jpeg 成员)。
 * 因此分工为:
 *   1. 触发时登记目标文件路径 (SHOT_REQ_T.path);
 *   2. user_venc_data_cb 收到 'J'/'M' 帧时只做一次 memcpy 把数据取走
 *      (SDK 回调返回后 frame->jpeg.data_vaddr 即失效), 不碰慢速 IO;
 *   3. shot_worker_thread 串行 "触发请求 → 等回调送数据 → 自己写盘",
 *      即文件写入完全由工作线程执行, VENC 回调线程只付出 memcpy 代价,
 *      截图与写盘耗时不会被 aov_state adjust_suspend_time() 算进 CPU 活动
 *      时间, 避免下一轮 enter_sleep 时长被补偿性缩短。
 * ================================================================== */

/* 截图请求上下文: 触发时登记目标路径, 回调取走数据, 工作线程写盘 */
#define SHOT_PATH_MAX         512
#define SHOT_MJPEG_FRAMES     3      /* MJPEG 动态截图拼接帧数 */
/* 等待回调送数据的超时:
 * AOV 单帧模式下 VI 是"唤醒-暂停"节流采帧, 实测帧间隔可达 ~3s, MJPEG 需要
 * 拼 3 帧 (跨 3 次唤醒) 约 9s, 所以超时必须覆盖 3 帧采集时间, 不能按连续帧
 * 模式 (~33ms/帧) 估。 */
#define SHOT_DATA_TIMEOUT_MS  20000

typedef struct {
    int  pending;                    /* 1 = 已触发, 等待回调送数据 */
    int  recved;                     /* 1 = 回调已 memcpy 出数据, 等待写盘 */
    int  frames;                     /* 请求帧数 (1=JPEG, >1=MJPEG) */
    char path[SHOT_PATH_MAX];        /* 触发时计算好的目标文件路径 */
    unsigned char *buf;              /* 回调 memcpy 出来的 JPEG/MJPEG 数据 */
    int  size;                       /* buf 字节数 */
    unsigned char type;              /* 'J' = JPEG, 'M' = MJPEG */
    unsigned int width;              /* 图像宽 */
    unsigned int height;             /* 图像高 */
    unsigned int frame_num;          /* 拼接帧数 */
} SHOT_REQ_T;

/* 前向声明：实际由工作线程串行调用 */
static int user_jpeg_mjpeg_save(unsigned char schn, unsigned char vchn);

static pthread_t       g_shot_thread;
static pthread_mutex_t g_shot_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_shot_cond  = PTHREAD_COND_INITIALIZER;
static volatile int    g_shot_thread_running = 0;
static volatile int    g_shot_pending_schn  = -1;  /* -1 = 无待处理请求 */
static SHOT_REQ_T      g_shot_req[REC_SENSOR_MAX];

/* 截图工作线程主循环: 等待请求 -> 调用 user_jpeg_mjpeg_save -> 回到等待 */
static void* shot_worker_thread(void *arg)
{
    (void)arg;
    prctl(PR_SET_NAME, "shot_worker");
    pthread_mutex_lock(&g_shot_mutex);
    while (g_shot_thread_running) {
        while (g_shot_pending_schn < 0 && g_shot_thread_running) {
            pthread_cond_wait(&g_shot_cond, &g_shot_mutex);
        }
        if (!g_shot_thread_running)
            break;

        int schn = g_shot_pending_schn;
        g_shot_pending_schn = -1;
        pthread_mutex_unlock(&g_shot_mutex);

        /* 串行执行 JPEG + MJPEG 截图, 失败不影响主流程 */
        (void)user_jpeg_mjpeg_save((unsigned char)schn, AOV_VIDEO_MAIN_CHN);

        pthread_mutex_lock(&g_shot_mutex);
    }
    pthread_mutex_unlock(&g_shot_mutex);
    return NULL;
}

/* 启动截图工作线程 */
static int shot_worker_init(void)
{
    g_shot_pending_schn = -1;
    g_shot_thread_running = 1;
    if (pthread_create(&g_shot_thread, NULL, shot_worker_thread, NULL) != 0) {
        g_shot_thread_running = 0;
        printf("[USER] shot worker thread create failed\n");
        return -1;
    }
    printf("[USER] shot worker thread started\n");
    return 0;
}

/* 停止截图工作线程 (等待当前截图完成) */
static void shot_worker_deinit(void)
{
    if (!g_shot_thread_running)
        return;
    pthread_mutex_lock(&g_shot_mutex);
    g_shot_thread_running = 0;
    pthread_cond_signal(&g_shot_cond);
    pthread_mutex_unlock(&g_shot_mutex);

    pthread_join(g_shot_thread, NULL);
    g_shot_thread_running = 0;
    printf("[USER] shot worker thread stopped\n");
}

/* VENC 回调中调用: 投递一个截图请求 (非阻塞)
 * 若上一请求未处理, 直接覆盖 (AOV 单帧模式 1fps, 丢失可接受) */
static void shot_request(unsigned char schn)
{
    pthread_mutex_lock(&g_shot_mutex);
    g_shot_pending_schn = schn;
    pthread_cond_signal(&g_shot_cond);
    pthread_mutex_unlock(&g_shot_mutex);
}

/* 接收截图结果: 取走 SDK 上报的 JPEG/MJPEG 数据 (运行在后端 VENC 回调线程)
 *
 * frame->jpeg.data_vaddr 仅在回调期间有效（SDK 回调返回后即释放），所以这里
 * 必须把数据 memcpy 到自己的 buf。为不阻塞 VENC 回调线程, 本函数只做拷贝,
 * 落盘交给 shot_worker_thread; 数据备好后唤醒它。 */
static int user_screenshot_frame_recv(unsigned char schn, AOV_VIDEO_FRAME_T *frame)
{
    const unsigned char *data = (const unsigned char *)frame->jpeg.data_vaddr;
    unsigned char *buf;
    int total = (int)frame->data_size;
    int ret = -1;

    if (schn >= REC_SENSOR_MAX)
        return -1;

    if (!data || total <= 0) {
        printf("[USER] shot %c frame empty: size=%d\n", frame->frame_type, total);
        return -1;
    }

    buf = (unsigned char *)malloc((size_t)total);
    if (!buf) {
        printf("[USER] shot %c frame malloc %d failed\n", frame->frame_type, total);
        return -1;
    }
    memcpy(buf, data, (size_t)total);

    pthread_mutex_lock(&g_shot_mutex);
    if (g_shot_req[schn].pending && !g_shot_req[schn].recved) {
        g_shot_req[schn].buf       = buf;
        g_shot_req[schn].size      = total;
        g_shot_req[schn].type      = frame->frame_type;
        g_shot_req[schn].width     = frame->jpeg.width;
        g_shot_req[schn].height    = frame->jpeg.height;
        g_shot_req[schn].frame_num = frame->jpeg.frame_num;
        g_shot_req[schn].recved    = 1;
        ret = 0;
    }
    pthread_cond_signal(&g_shot_cond);
    pthread_mutex_unlock(&g_shot_mutex);

    if (ret != 0) {
        printf("[USER] shot %c frame dropped: no pending request schn=%d\n",
               frame->frame_type, schn);
        free(buf);
    }
    return ret;
}

/* 把截图数据写入文件 (由 shot_worker_thread 执行, 不在 VENC 回调线程里) */
static int user_screenshot_write_file(const char *path, const unsigned char *data,
                                      int size, unsigned char type,
                                      unsigned int width, unsigned int height,
                                      unsigned int frame_num)
{
    int left = size;
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (fd < 0) {
        printf("[USER] shot open %s failed: errno=%d\n", path, errno);
        return -1;
    }

    while (left > 0) {
        ssize_t n = write(fd, data, (size_t)left);
        if (n <= 0) {
            printf("[USER] shot write %s failed: errno=%d\n", path, errno);
            close(fd);
            return -1;
        }
        data += n;
        left -= (int)n;
    }
    close(fd);

    printf("[USER] %s saved: %s (%d bytes, %ux%u, %u frame(s))\n",
           (type == 'M') ? "mjpeg" : "jpeg", path, size, width, height, frame_num);
    return 0;
}

/* ==================================================================
 * 视频帧回调 — VENC 编码完成后送入 muxer 封装 MP4
 * ================================================================== */

static int user_venc_data_cb(unsigned char schn, unsigned char vchn, AOV_VIDEO_FRAME_T* frame)
{
    if (!frame) {
        printf("[USER] frame or data is NULL\n");
        return -1;
    }

    /* ── 三通道截图结果 (frame_type 'J'/'M') ──
     * 数据在 frame->jpeg 成员, vchn == AOV_VIDEO_THIRD_CHN, 不进入录像队列,
     * 这里只 memcpy 取走数据(回调返回后即失效)并交给 shot worker 落盘,
     * 避免慢速写文件阻塞本回调。注意必须在下面 vchn >= REC_CHN_MAX 的
     * 过滤之前处理 (THIRD_CHN 不占用录像通道号)。 */
    if (frame->frame_type == 'J' || frame->frame_type == 'M')
        return user_screenshot_frame_recv(schn, frame);

    if (!frame->enc.data_vaddr) {
        printf("[USER] frame or data is NULL\n");
        return -1;
    }

    if (schn >= user_record_sensor_count() || vchn >= REC_CHN_MAX)
        return 0;

    /* 区分单帧/连续帧模式：AOV 模式=单帧，非 AOV=连续帧 */
    aov_status_e status = aov_status_get();
    int midx = (status == AOV_STATUS_ACTIVE) ? REC_MODE_SINGLE : REC_MODE_MULTI;

#if TEST_FPS
    /* ── 帧率统计（仅连续帧模式，即退出 AOV 后） ──
     * AOV 模式下为单帧唤醒，帧率无意义，跳过统计。
     * 退出 AOV 进入连续帧模式后，每 5 秒打印一次实际帧率。
     * 若重新进入 AOV 模式则重置标记，下次退出 AOV 时从头统计。 */
    {
        static int    fps_frame_cnt[REC_SENSOR_MAX][REC_CHN_MAX];
        static struct timespec fps_last_ts[REC_SENSOR_MAX][REC_CHN_MAX];
        static int    fps_active[REC_SENSOR_MAX][REC_CHN_MAX]; /* 该通道是否正在统计 */
        static int    fps_init = 0;

        if (!fps_init) {
            memset(fps_frame_cnt, 0, sizeof(fps_frame_cnt));
            memset(fps_active, 0, sizeof(fps_active));
            fps_init = 1;
        }

        if (midx == REC_MODE_MULTI) {
            /* 首次进入连续帧模式（或从 AOV 切回后）重置该通道的统计基准 */
            if (!fps_active[schn][vchn]) {
                fps_active[schn][vchn] = 1;
                fps_frame_cnt[schn][vchn] = 0;
                clock_gettime(CLOCK_MONOTONIC, &fps_last_ts[schn][vchn]);
            }

            fps_frame_cnt[schn][vchn]++;

            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);

            double elapsed = (now.tv_sec  - fps_last_ts[schn][vchn].tv_sec)
                           + (now.tv_nsec - fps_last_ts[schn][vchn].tv_nsec) / 1e9;

            if (elapsed >= 5.0) {
                double actual_fps = fps_frame_cnt[schn][vchn] / elapsed;
                char rec_name[32];
                user_record_file_name(rec_name, sizeof(rec_name), schn, vchn);
                printf("[TEST_FPS] %s: %d frames in %.2fs = %.2f fps (target=%d)\n",
                       rec_name, fps_frame_cnt[schn][vchn], elapsed,
                       actual_fps, TEST_FPS);
                fps_frame_cnt[schn][vchn] = 0;
                fps_last_ts[schn][vchn] = now;
            }
        } else {
            /* AOV 单帧模式：不统计，标记未激活以便下次连续帧时重新开始 */
            fps_active[schn][vchn] = 0;
        }
    }
#endif /* TEST_FPS */

    /* ── AOV 单帧模式：投递截图请求到工作线程 ──
     * 仅在 AOV 模式（单帧唤醒）时触发，退出 AOV 后（连续帧）不再截图。
     * 只对主通道 (vchn=0) 触发，避免子通道重复截图。
     * 工作线程异步执行 user_jpeg_mjpeg_save, 不阻塞本回调路径,
     * 避免截图耗时被 aov_state adjust_suspend_time 算进 CPU 活动时间。 */
    if (midx == REC_MODE_SINGLE && vchn == AOV_VIDEO_MAIN_CHN) {
        shot_request(schn);
    }

    int ret = aov_record_queue_push(g_record_queue,
                                    schn, vchn, (unsigned char)midx,
                                    frame->frame_type, frame->pts,
                                    (const unsigned char*)frame->enc.data_vaddr,
                                    frame->data_size);
    if (ret != 0) {
        g_record_drop_log_cnt++;
        if (g_record_drop_log_cnt == 1 ||
            (g_record_drop_log_cnt % REC_QUEUE_LOG_DROP_STEP) == 0) {
            char rec_name[32];
            user_record_file_name(rec_name, sizeof(rec_name), schn, vchn);
            printf("[USER] record queue push failed %s %s ret=%d "
                   "queue=%d used=%zu/%zu dropped=%llu\n",
                   rec_name,
                   (midx == REC_MODE_SINGLE) ? "single" : "multi", ret,
                   aov_record_queue_count(g_record_queue),
                   aov_record_queue_used(g_record_queue),
                   aov_record_queue_size(g_record_queue),
                   (unsigned long long)aov_record_queue_dropped(g_record_queue));
        }
        if (midx == REC_MODE_SINGLE &&
            aov_record_queue_usage_percent(g_record_queue) >= REC_QUEUE_WAKE_WATERMARK) {
            aov_exit_set(AOV_EXIT_TIMING, 30);
        }
        return -1;
    }

    record_signal_drain();

    return 0;
}

/*
 * RAW 数据回调（暂不使用）
 */
static int user_raw_data_cb(unsigned char schn, unsigned char vchn, AOV_VIDEO_FRAME_T* frame)
{
    (void)schn;
    (void)vchn;
    (void)frame;
    return 0;
}

/* ==================================================================
 * 视频管线构建
 * ================================================================== */

static int build_video_pipeline(void)
{
    int ret;
    memset(g_record_venc_attr, 0, sizeof(g_record_venc_attr));
    memset(g_record_venc_attr_valid, 0, sizeof(g_record_venc_attr_valid));

    /* Step 1: 选择后端 */
    if (g_user_backend == USER_BACKEND_SPLICE)
        aov_video_func_init(AOV_VIDEO_BACKEND_SPLICE_AVS);
    else
        aov_video_func_init(AOV_VIDEO_BACKEND_MULTI);

    /* Step 2: VI 初始化 */
    AOV_SPLICE_MULTI_ATTR_T vi_attr;
    memset(&vi_attr, 0, sizeof(vi_attr));

    vi_attr.vi_info[AOV_VIDEO_MAIN_CHN].width   = USER_INPUT_MAIN_WIDTH;
    vi_attr.vi_info[AOV_VIDEO_MAIN_CHN].height  = USER_INPUT_MAIN_HEIGHT;
    vi_attr.vi_info[AOV_VIDEO_SUB_CHN].width    = USER_INPUT_SUB_WIDTH;
    vi_attr.vi_info[AOV_VIDEO_SUB_CHN].height   = USER_INPUT_SUB_HEIGHT;
    vi_attr.vi_info[AOV_VIDEO_THIRD_CHN].width  = USER_INPUT_SUB_WIDTH;
    vi_attr.vi_info[AOV_VIDEO_THIRD_CHN].height = USER_INPUT_SUB_HEIGHT;
    vi_attr.comm_info.sensor_num = USER_SENSOR_NUM;
    vi_attr.avs_mode = AVS_SPLICE_HORIZONTAL;
    vi_attr.vi_frame_rate = USER_VI_FRAME_RATE;

    ret = aov_video_vi_init(&vi_attr);
    if (ret != 0) {
        printf("[USER] aov_video_vi_init failed: %d\n", ret);
        return -1;
    }
    printf("[USER] VI init done, backend=%s, vi_frame_rate=%d\n",
           user_backend_name(),
           vi_attr.vi_frame_rate ? vi_attr.vi_frame_rate : 15);

    /* Step 3: VENC 主码流 */
    AOV_VENC_ATTR_T venc_attr;
    memset(&venc_attr, 0, sizeof(venc_attr));
    venc_attr.encode_type              = AOV_VIDEO_ENCODE_H264;
    venc_attr.br_mode                  = 0;
    venc_attr.venc_res.width           = user_record_width(AOV_VIDEO_MAIN_CHN);
    venc_attr.venc_res.height          = user_record_height(AOV_VIDEO_MAIN_CHN);
    venc_attr.bit_rate_attr.fps         = REC_VIDEO_FPS;
    venc_attr.bit_rate_attr.gop_len      = REC_VIDEO_GOP;
    venc_attr.bit_rate_attr.target_kbps = user_record_target_kbps(AOV_VIDEO_MAIN_CHN);
    venc_attr.bit_rate_attr.max_kbps    = user_record_max_kbps(AOV_VIDEO_MAIN_CHN);

    ret = aov_video_venc_init(AOV_VIDEO_MAIN_CHN, &venc_attr);
    if (ret != 0) {
        printf("[USER] venc init MAIN failed: %d\n", ret);
        aov_video_vi_uninit();
        return -1;
    }
    memcpy(&g_record_venc_attr[AOV_VIDEO_MAIN_CHN], &venc_attr, sizeof(venc_attr));
    g_record_venc_attr_valid[AOV_VIDEO_MAIN_CHN] = 1;
    printf("[USER] VENC MAIN init done\n");

    /* Step 4: VENC 子码流 */
    venc_attr.venc_res.width           = user_record_width(AOV_VIDEO_SUB_CHN);
    venc_attr.venc_res.height          = user_record_height(AOV_VIDEO_SUB_CHN);
    venc_attr.bit_rate_attr.target_kbps = user_record_target_kbps(AOV_VIDEO_SUB_CHN);
    venc_attr.bit_rate_attr.max_kbps    = user_record_max_kbps(AOV_VIDEO_SUB_CHN);

    ret = aov_video_venc_init(AOV_VIDEO_SUB_CHN, &venc_attr);
    if (ret != 0) {
        printf("[USER] venc init SUB failed: %d\n", ret);
        memset(&g_record_venc_attr[AOV_VIDEO_MAIN_CHN], 0,
               sizeof(g_record_venc_attr[AOV_VIDEO_MAIN_CHN]));
        g_record_venc_attr_valid[AOV_VIDEO_MAIN_CHN] = 0;
        aov_video_venc_uninit(AOV_VIDEO_MAIN_CHN);
        aov_video_vi_uninit();
        return -1;
    }
    memcpy(&g_record_venc_attr[AOV_VIDEO_SUB_CHN], &venc_attr, sizeof(venc_attr));
    g_record_venc_attr_valid[AOV_VIDEO_SUB_CHN] = 1;
    printf("[USER] VENC SUB init done\n");

    /* Step 5: VENC 第三通道（JPEG/MJPEG 共用编码器）
     *
     * 第三通道由 screenshot 模块独立管理（不使用 SYS_Bind），
     * 内部创建 JPEG 编码器 + 注册 VI/AVS 源信息。
     * aov_video_jpeg_save / aov_video_mjpeg_save 触发截图请求, 编码结果
     * 经 user_venc_data_cb (frame_type 'J'/'M') 回调上报, 由上层落盘。
     */
    AOV_VENC_ATTR_T third_attr;
    memset(&third_attr, 0, sizeof(third_attr));
    third_attr.encode_type              = AOV_VIDEO_ENCODE_JPEG;
    third_attr.jpeg_enc_type            = 0;
    third_attr.venc_res.width           = user_record_width(AOV_VIDEO_MAIN_CHN);
    third_attr.venc_res.height          = user_record_height(AOV_VIDEO_MAIN_CHN);
    third_attr.bit_rate_attr.fps        = REC_VIDEO_FPS;
    third_attr.bit_rate_attr.gop_len    = REC_VIDEO_GOP;
    third_attr.bit_rate_attr.target_kbps = user_record_target_kbps(AOV_VIDEO_MAIN_CHN);
    third_attr.bit_rate_attr.max_kbps   = user_record_max_kbps(AOV_VIDEO_MAIN_CHN);

    ret = aov_video_venc_init(AOV_VIDEO_THIRD_CHN, &third_attr);
    if (ret != 0) {
        printf("[USER] venc init THIRD failed: %d\n", ret);
        aov_video_venc_uninit(AOV_VIDEO_SUB_CHN);
        aov_video_venc_uninit(AOV_VIDEO_MAIN_CHN);
        aov_video_vi_uninit();
        return -1;
    }
    printf("[USER] VENC THIRD (JPEG/MJPEG) init done\n");

    /* 启动截图工作线程 (异步处理 JPEG/MJPEG 截图请求, 不阻塞 VENC 回调) */
    if (shot_worker_init() != 0) {
        printf("[USER] shot_worker_init failed, screenshot disabled\n");
    }

    return 0;
}

static void destroy_video_pipeline(void)
{
    /* 先停止截图工作线程 (等待当前截图完成, 避免与 VENC 销毁竞态) */
    shot_worker_deinit();

    aov_video_venc_stop();
    aov_video_venc_uninit(AOV_VIDEO_THIRD_CHN);
    aov_video_venc_uninit(AOV_VIDEO_SUB_CHN);
    aov_video_venc_uninit(AOV_VIDEO_MAIN_CHN);
    aov_video_vi_uninit();
    memset(g_record_venc_attr, 0, sizeof(g_record_venc_attr));
    memset(g_record_venc_attr_valid, 0, sizeof(g_record_venc_attr_valid));
    printf("[USER] video pipeline destroyed\n");
}

/* ==================================================================
 * JPEG / MJPEG 截图 — 保存到与 .mp4 同一目录
 * ================================================================== */

/*
 * 在 AOV 单帧模式下，每次 user_venc_data_cb 触发时调用此函数，
 * 依次触发一帧 JPEG 静态截图和一帧 MJPEG 动态截图。
 *
 * 截图接口为异步触发: 本函数登记目标路径 + 投递请求, 等 user_venc_data_cb
 * 收到 'J'/'M' 帧并把数据 memcpy 出来, 再由本线程 (shot worker) 写盘。
 * SDK 同一时刻只允许一个截图请求, 所以两次请求必须串行: 等上一张写完再
 * 触发下一张。
 *
 * 文件命名规则 (每次调用序号自增, 避免覆盖)：
 *   splice 模式: splice_jpeg_001.jpg, splice_mjpeg_001.mjpeg, ...
 *   multi  模式: d0_jpeg_001.jpg, d0_mjpeg_001.mjpeg, ...
 *
 * @param schn 传感器通道号
 * @param vchn 视频通道号 (0=主通道全分辨率, 1=子通道半分辨率)
 * @return 0 成功, 负值失败
 */
static int user_jpeg_mjpeg_save(unsigned char schn, unsigned char vchn)
{
    static int s_shot_seq = 0;   /* 每调用一次自增, 用于文件命名 */

    char prefix[32];
    const char *out_dir;        /* 实际保存目录: SD 卡 or /tmp 临时区 */
    int seq = ++s_shot_seq;
    int i;

    if (g_user_backend == USER_BACKEND_SPLICE)
        snprintf(prefix, sizeof(prefix), "splice");
    else
        snprintf(prefix, sizeof(prefix), "d%d", schn);

    /* 根据 SD 卡状态选择保存目录:
     *   SD ready (连续帧模式 / 退出 AOV 后): 直接写 SD 卡
     *   SD not ready (AOV 单帧模式期间): 写 /tmp/aov_shot 临时区,
     *   等退出 AOV 恢复 SD 后由 shot_migrate_tmp_to_sd() 批量迁移 */
    out_dir = g_record_sd_ready ? REC_OUTPUT_DIR : SHOT_TMP_DIR;
    if (!g_record_sd_ready) {
        /* 确保临时目录存在 (tmpfs, 不持久) */
        if (mkdir(SHOT_TMP_DIR, 0755) != 0 && errno != EEXIST) {
            printf("[USER] mkdir %s failed: %d\n", SHOT_TMP_DIR, errno);
        }
    }

    /* 1. JPEG 静态截图 (frame_num=1), 2. MJPEG 动态截图 (多帧拼接) */
    for (i = 0; i < 2; i++) {
        int         frames = (i == 0) ? 1 : SHOT_MJPEG_FRAMES;
        const char *suffix = (i == 0) ? "jpeg" : "mjpeg";
        const char *ext    = (i == 0) ? "jpg"  : "mjpeg";
        struct timespec ts;
        int ret;

        /* 登记目标路径与请求状态, 供回调取数据 / 本线程写盘时取用 */
        pthread_mutex_lock(&g_shot_mutex);
        snprintf(g_shot_req[schn].path, sizeof(g_shot_req[schn].path),
                 "%s/%s_%s_%03d.%s", out_dir, prefix, suffix, seq, ext);
        g_shot_req[schn].buf     = NULL;
        g_shot_req[schn].recved  = 0;
        g_shot_req[schn].frames  = frames;
        g_shot_req[schn].pending = 1;
        pthread_mutex_unlock(&g_shot_mutex);

        /* 触发截图 (异步): 结果经 user_venc_data_cb 上报 */
        if (frames > 1)
            ret = aov_video_mjpeg_save(schn, vchn, (unsigned char)frames);
        else
            ret = aov_video_jpeg_save(schn, vchn);

        if (ret != 0) {
            printf("[USER] %s shot request %s failed: %d\n",
                   suffix, g_shot_req[schn].path, ret);
            pthread_mutex_lock(&g_shot_mutex);
            g_shot_req[schn].pending = 0;
            pthread_mutex_unlock(&g_shot_mutex);
            continue;
        }

        /* 等回调把数据 memcpy 出来 (回调在后端 VENC 线程只做拷贝, 不写盘)
         * 线程退出 (g_shot_thread_running=0) 时立即结束等待, 不必等超时。 */
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec  += SHOT_DATA_TIMEOUT_MS / 1000;
        ts.tv_nsec += (long)(SHOT_DATA_TIMEOUT_MS % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }

        pthread_mutex_lock(&g_shot_mutex);
        while (g_shot_req[schn].pending && !g_shot_req[schn].recved
               && g_shot_thread_running) {
            if (pthread_cond_timedwait(&g_shot_cond, &g_shot_mutex, &ts) != 0) {
                printf("[USER] %s shot timeout: %s\n", suffix, g_shot_req[schn].path);
                break;
            }
        }

        /* 取出数据并复位请求 (写盘不在锁内进行) */
        unsigned char *buf      = g_shot_req[schn].buf;
        int            size     = g_shot_req[schn].size;
        unsigned char  type     = g_shot_req[schn].type;
        unsigned int   w        = g_shot_req[schn].width;
        unsigned int   h        = g_shot_req[schn].height;
        unsigned int   fnum     = g_shot_req[schn].frame_num;
        char           path[SHOT_PATH_MAX];
        snprintf(path, sizeof(path), "%s", g_shot_req[schn].path);
        g_shot_req[schn].buf     = NULL;
        g_shot_req[schn].recved  = 0;
        g_shot_req[schn].pending = 0;
        pthread_mutex_unlock(&g_shot_mutex);

        /* 本线程写盘: VENC 回调线程不承担慢速 IO */
        if (buf) {
            user_screenshot_write_file(path, buf, size, type, w, h, fnum);
            free(buf);
        }

        /* 线程正在退出, 不再触发后续请求 */
        if (!g_shot_thread_running)
            break;
    }

    return 0;
}

/* 把 /tmp/aov_shot 下的所有截图迁移到 SD 卡 REC_OUTPUT_DIR
 * 在退出 AOV 恢复 SD 后调用, 一次性拷贝 */
static void shot_migrate_tmp_to_sd(void)
{
    DIR *dir = opendir(SHOT_TMP_DIR);
    if (!dir) {
        /* 没有临时截图, 直接返回 */
        return;
    }

    /* 确保 SD 卡目标目录存在 */
    if (access(REC_OUTPUT_DIR, W_OK) != 0) {
        if (mkdir(REC_OUTPUT_DIR, 0755) != 0 && errno != EEXIST) {
            printf("[USER] migrate: mkdir %s failed: %d\n", REC_OUTPUT_DIR, errno);
            closedir(dir);
            return;
        }
    }

    struct dirent *ent;
    int migrated = 0;
    int failed = 0;

    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.')
            continue;

        char src[512];
        char dst[512];
        snprintf(src, sizeof(src), "%s/%s", SHOT_TMP_DIR, ent->d_name);
        snprintf(dst, sizeof(dst), "%s/%s", REC_OUTPUT_DIR, ent->d_name);

        /* /tmp 是 tmpfs, SD 卡是另一个 fs -> rename 跨 fs 会失败, 用 read/write 拷贝 */
        int in_fd = open(src, O_RDONLY);
        if (in_fd < 0) {
            printf("[USER] migrate: open %s failed: %d\n", src, errno);
            failed++;
            continue;
        }
        int out_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out_fd < 0) {
            printf("[USER] migrate: open %s failed: %d\n", dst, errno);
            close(in_fd);
            failed++;
            continue;
        }

        /* 用 sendfile 在内核态拷贝 (零拷贝) */
        struct stat st;
        if (fstat(in_fd, &st) == 0 && st.st_size > 0) {
            off_t offset = 0;
            ssize_t sent = sendfile(out_fd, in_fd, &offset, st.st_size);
            if (sent < 0) {
                /* sendfile 失败回退到 read/write */
                char buf[8192];
                ssize_t n;
                while ((n = read(in_fd, buf, sizeof(buf))) > 0) {
                    if (write(out_fd, buf, n) != n) {
                        failed++;
                        break;
                    }
                }
                if (n < 0) failed++;
            }
        }

        close(in_fd);
        close(out_fd);

        /* 拷贝成功后删除临时文件 */
        if (unlink(src) == 0)
            migrated++;
        else
            failed++;
    }
    closedir(dir);

    /* 目录空了就删掉 */
    rmdir(SHOT_TMP_DIR);

    if (migrated > 0 || failed > 0) {
        printf("[USER] shot migrate tmp -> sd: migrated=%d failed=%d\n",
               migrated, failed);
    }
}

/* ====== 命令行参数自由组合测试序列 ======
 *
 * 用法: ./aov [action[:param]] ...
 *
 * 支持的动作:
 *   wait:N 或 w:N        等待 N 秒
 *   enter 或 e           立即进入 AOV (aov_exit_set(AOV_EXIT_ENTER_NOW, 0))
 *   exit_forever 或 ef   永久退出 AOV (aov_exit_set(AOV_EXIT_FOREVER, 0))
 *   exit_timed:N 或 et:N 退出 AOV N 秒后自动重入 (aov_exit_set(AOV_EXIT_TIMING, N))
 *
 * 示例:
 *   ./aov wait:60 exit_forever
 *   ./aov wait:120 exit_timed:10 wait:60 exit_forever
 *   ./aov enter           (进入 AOV 并保持)
 *
 * 不带参数时打印用法说明并运行默认序列。
 */

static void run_test_sequence(int argc, char *argv[])
{
    int action_total = 0;

    for (int i = 1; i < argc; i++) {
        if (!user_is_backend_arg(argv[i]))
            action_total++;
    }

    if (action_total <= 0) {
        printf("\n[USER] ═══════════════════════════════════════\n");
        printf("[USER]  用法: %s [splice|multi] <动作1> [动作2] ...\n", argv[0]);
        printf("[USER] ═══════════════════════════════════════\n");
        printf("[USER] 后端选择:\n");
        printf("[USER]   splice              AVS 水平拼接，输出 splice_main/sub 两个文件\n");
        printf("[USER]   multi              双 sensor 独立录像，输出 d0/d1 main/sub 四个文件\n");
        printf("[USER] 当前后端: %s\n", user_backend_name());
        printf("[USER] 支持的动作:\n");
        printf("[USER]   wait:N / w:N       等待 N 秒\n");
        printf("[USER]   enter / e          立即进入 AOV 模式\n");
        printf("[USER]   exit_forever / ef  永久退出 AOV 模式\n");
        printf("[USER]   exit_timed:N / et:N  退出 AOV，N 秒后自动重入\n");
        printf("[USER] ───────────────────────────────────────\n");
        printf("[USER] 示例:\n");
        printf("[USER]   ① %s splice wait:60 exit_forever\n", argv[0]);
        printf("[USER]      → 拼接录像运行 AOV 1 分钟后永久退出，程序保持后台\n");
        printf("[USER]   ② %s multi wait:120 exit_timed:10 wait:60 exit_forever\n", argv[0]);
        printf("[USER]      → 非拼接录像运行 AOV 2 分钟 → 退出 10 秒 → 再运行 1 分钟 → 永久退出\n");
        printf("[USER]   ③ %s splice enter\n", argv[0]);
        printf("[USER]      → 立即进入 AOV 并一直保持，直到 Ctrl+C\n");
        printf("[USER]   ④ %s multi wait:30 ef wait:10 e wait:60 ef\n", argv[0]);
        printf("[USER]      → 等 30 秒 → 永久退出 → 等 10 秒 → 进入 AOV → 等 60 秒 → 永久退出\n");
        printf("[USER] ═══════════════════════════════════════\n\n");

        printf("[USER] 无参数，运行默认序列: wait:60 exit_forever\n");

        for (int t = 0; t < 60 && g_running; t++) sleep(1);
        if (g_running) aov_exit_set(AOV_EXIT_FOREVER, 0);
        printf("[USER] 测试序列完成，程序保持后台运行 (Ctrl+C 退出)\n");
        return;
    }

    int total = action_total;
    int step = 0;
    for (int i = 1; i < argc && g_running; i++) {
        char *arg = argv[i];

        if (user_is_backend_arg(arg))
            continue;

        step++;

        if (strncmp(arg, "wait:", 5) == 0 || strncmp(arg, "w:", 2) == 0) {
            const char *val = (arg[0] == 'w' && arg[1] == ':') ? arg + 2 : arg + 5;
            int sec = atoi(val);
            if (sec > 0) {
                printf("[USER] [step %d/%d] waiting %d seconds\n", step, total, sec);
                for (int t = 0; t < sec && g_running; t++) sleep(1);
            }
        } else if (strcmp(arg, "enter") == 0 || strcmp(arg, "e") == 0) {
            printf("[USER] [step %d/%d] enter AOV\n", step, total);
            aov_exit_set(AOV_EXIT_ENTER_NOW, 0);
        } else if (strcmp(arg, "exit_forever") == 0 || strcmp(arg, "ef") == 0) {
            printf("[USER] [step %d/%d] exit AOV forever\n", step, total);
            aov_exit_set(AOV_EXIT_FOREVER, 0);
        } else if (strncmp(arg, "exit_timed:", 11) == 0 ||
                   strncmp(arg, "et:", 3) == 0) {
            const char *val = (arg[0] == 'e' && arg[1] == 't' && arg[2] == ':')
                                  ? arg + 3 : arg + 11;
            int sec = atoi(val);
            printf("[USER] [step %d/%d] exit AOV for %d seconds\n",
                   step, total, sec);
            aov_exit_set(AOV_EXIT_TIMING, sec);
        } else {
            printf("[USER] [step %d/%d] unknown action: %s (skip)\n",
                   step, total, arg);
        }
    }

    printf("[USER] 测试序列完成，程序保持后台运行 (Ctrl+C 退出)\n");
}

/* ==================================================================
 * demo main()
 *
 * 流程:
 *   1. 初始化 AOV (自动进入 AOV 模式)
 *   2. 构建视频管线
 *   3. 启动视频采集
 *   4. 执行硬编码的测试序列 (g_test_steps[])
 *   5. 序列完成后程序保持运行，等待信号退出
 *   6. 清理
 *
 * 改测试场景：编辑 g_test_steps[] 数组后重新编译即可。
 * ================================================================== */

int main(int argc, char *argv[])
{
    int ret;

    user_parse_backend_args(argc, argv);

    printf("╔══════════════════════════════════════════╗\n");
    printf("║         AOV Refactor Demo v1.0          ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");
    printf("[USER] selected backend: %s\n", user_backend_name());

    /* 注册信号处理 */
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    /* ────────────────────────────────────────────
     * Step 1: ISP 初始化
     *
     * 必须在 RK_MPI_SYS_Init() 之前调用。
     * 内部通过 rk_aiq_uapi2_* 初始化 AIQ 引擎、启动 3A 算法。
     * ──────────────────────────────────────────── */
    {
        const char *iq_dir = getenv("IQ_FILE_DIR");
        if (!iq_dir) iq_dir = "/etc/iqfiles/";

        AOV_ISP_ATTR_T isp_attr;
        memset(&isp_attr, 0, sizeof(isp_attr));
        isp_attr.sensor_num  = 2;
        isp_attr.hdr_mode    = AOV_ISP_HDR_MODE_NORMAL;
        isp_attr.bMultictx   = 0;
        isp_attr.fps         = TST_ISP_FPS;
        isp_attr.iq_file_dir = iq_dir;

        printf("[USER] ISP init: sensor=%d fps=%d\n",
               isp_attr.sensor_num, isp_attr.fps);

        ret = aov_isp_init(&isp_attr);
        if (ret != 0) {
            printf("[USER] ISP init failed\n");
            return -1;
        }
        printf("[USER] ISP init done, SOF=%d\n", aov_isp_get_sof_cnt());
    }

    /* ────────────────────────────────────────────
     * Step 2: SYS 初始化
     * ──────────────────────────────────────────── */
    ret = RK_MPI_SYS_Init();
    if (ret != RK_SUCCESS) {
        printf("[USER] RK_MPI_SYS_Init failed: %#x\n", ret);
        goto fail_isp;
    }
    printf("[USER] SYS init done\n");

    if (record_release_system_sdcard() != 0) {
        printf("[USER] system SD mount is still busy, abort record init\n");
        goto fail_record_all;
    }
    aov_helper_bind_sdcard();
    if (record_wait_sd_storage(5000) != 0) {
        printf("[USER] SD storage is not ready after bind, abort record init "
               "(check mount point %s)\n", REC_OUTPUT_MOUNT);
        goto fail_record_all;
    }

    /* ────────────────────────────────────────────
     * Step 3: 构建视频管线 (VI + VENC)
     *
     * 先创建 VENC，再从 aov_video 的属性缓存读取实际编码参数，
     * 供 record queue sizing 和 record ctx 初始化复用，避免应用层维护第二套值。
     * ──────────────────────────────────────────── */
    ret = build_video_pipeline();
    if (ret != 0) {
        printf("[USER] build video pipeline failed\n");
        goto fail_record_all;
    }

    if (record_drain_start() != 0) {
        printf("[USER] record drain start failed\n");
        goto fail_pipeline;
    }
    record_set_sd_ready(1);
    record_set_drain_pause(0);

    aov_action_t callbacks = {
        .enter_func  = user_aov_enter_action,
        .exit_func   = user_aov_exit_action,
        .write_event = user_aov_write_event,
        /* 唤醒配置：二选一。示例：seconds_per_frame=3 表示 3 秒 1 帧；
         * frames_per_second=3 表示 1 秒 3 帧（上限 3fps）。 */
        .seconds_per_frame = 1,
    };

    /* ────────────────────────────────────────────
     * Step 4: 初始化录像模块
     *
     *   splice:
     *     [0][0] splice main 3840x1080
     *     [0][1] splice sub  1920x540
     *
     *   multi:
     *     [0][0] dev0 main 1920x1080
     *     [0][1] dev0 sub  640x360
     *     [1][0] dev1 main 1920x1080
     *     [1][1] dev1 sub  640x360
     *
     * 帧回调中按 (schn, vchn) 路由，frame_mode 只用于日志和 flush 策略。
     * ──────────────────────────────────────────── */
    {
        /*
         * aov_record_init 参数说明：
         *
         * ┌─────────────────────┬──────────────────────────────────────────┐
         * │ 参数                │ 说明                                     │
         * ├─────────────────────┼──────────────────────────────────────────┤
         * │ output_dir          │ MP4 输出目录                             │
         * │ file_name           │ 文件名前缀，自动拼接时间戳 + .mp4 后缀   │
         * │ video_width/height  │ 编码分辨率按后端选择：拼接或独立 sensor   │
         * │ fps                 │ 编码器帧率 (VI → VENC 抓帧速率, 15fps)  │
         * │ gop_size            │ GOP 长度，影响 I 帧间隔和寻址粒度         │
         * │ video_kbps          │ 编码码率：main=2048kbps, sub=512kbps     │
         * │ audio_enable        │ 0=纯视频 MP4, 1=混入 AAC 音频            │
         * │ audio_sample_rate   │ 音频采样率 (Hz)                          │
         * │ audio_channels      │ 音频通道数                               │
         * │ audio_samples/frame │ 每帧音频采样数                           │
         * │ encode_type         │ 编码类型：AOV_VIDEO_ENCODE_H264/H265      │
         * │ record_fps          │ 名义帧率；muxer 使用 PTS 计算实际间隔     │
         * │ skip_initial_frames │ 建文件前跳过的预热帧数                    │
         * │ max_frames          │ fmp4 muxer sample_pool 容量 (见下)       │
         * └─────────────────────┴──────────────────────────────────────────┘
         */

        int sensor_count = user_record_sensor_count();

        for (int s = 0; s < sensor_count; s++) {
            for (int c = 0; c < REC_CHN_MAX; c++) {
                AOV_RECORD_CFG_T cfg;
                char rec_name[32];
                memset(&cfg, 0, sizeof(cfg));

                if (!g_record_venc_attr_valid[c]) {
                    printf("[USER] record venc attr not ready: s=%d c=%d\n", s, c);
                    goto fail_pipeline;
                }

                user_record_file_name(rec_name, sizeof(rec_name), s, c);

                snprintf(cfg.output_dir, sizeof(cfg.output_dir), "%s", REC_OUTPUT_DIR);
                snprintf(cfg.file_name, sizeof(cfg.file_name), "%s", rec_name);

                /* 编码参数 */
                cfg.video_width    = g_record_venc_attr[c].venc_res.width;
                cfg.video_height   = g_record_venc_attr[c].venc_res.height;
                cfg.fps            = g_record_venc_attr[c].bit_rate_attr.fps > 0
                                         ? g_record_venc_attr[c].bit_rate_attr.fps
                                         : REC_VIDEO_FPS;
                cfg.gop_size       = g_record_venc_attr[c].bit_rate_attr.gop_len > 0
                                         ? g_record_venc_attr[c].bit_rate_attr.gop_len
                                         : cfg.fps;
                cfg.video_kbps     = g_record_venc_attr[c].bit_rate_attr.target_kbps > 0
                                         ? g_record_venc_attr[c].bit_rate_attr.target_kbps
                                         : user_record_target_kbps(c);
                cfg.encode_type    = g_record_venc_attr[c].encode_type;

                /* 音频 (当前关闭) */
                cfg.audio_enable   = REC_AUDIO_ENABLE;
                cfg.audio_sample_rate = REC_AUDIO_SAMPLE_RATE;
                cfg.audio_channels = REC_AUDIO_CHANNELS;
                cfg.audio_samples_per_frame = REC_AUDIO_SAMPLES_PER_FRAME;

                /*
                 * record_fps 是连续录像的 MP4 播放帧率。
                 * AOV 单帧录像按 callbacks 的唤醒间隔播放：
                 *   seconds_per_frame=N -> N 秒 1 帧
                 *   frames_per_second=N -> 1 秒 N 帧
                 */
                cfg.record_fps = cfg.fps;
                cfg.aov_frame_duration_us = user_aov_frame_duration_us(&callbacks);
                cfg.skip_initial_frames = 0;
                cfg.max_frames = REC_MULTI_FLUSH_THRESHOLD;
                cfg.venc_chn      = c;
                cfg.on_need_idr   = user_record_on_need_idr;
                cfg.idr_userdata  = NULL;

                g_record_ctx[s][c] = aov_record_init(&cfg);
                if (!g_record_ctx[s][c]) {
                    printf("[USER] record init %s failed\n", rec_name);
                    goto fail_record_all;
                }
                printf("[USER] record %s init: %dx%d %ukbps mixed %dfps\n",
                       rec_name, cfg.video_width, cfg.video_height,
                       cfg.video_kbps, cfg.fps);
            }
        }
    }

    /* ────────────────────────────────────────────
     * Step 5: 初始化 OSD
     *
     * 必须在 VENC 启动前完成，确保首帧就有 OSD 覆盖就绪。
     * ──────────────────────────────────────────── */
    ret = user_osd_init_all();
    if (ret != AOV_ERR_SUCCESS)
        printf("[USER] osd disabled, continue without AOV mark\n");

    /* ────────────────────────────────────────────
     * Step 6: 启动视频采集（注册 VENC 回调）
     * ──────────────────────────────────────────── */
    ret = aov_video_venc_start(user_raw_data_cb, user_venc_data_cb);
    if (ret != 0) {
        printf("[USER] venc start failed: %d\n", ret);
        goto fail_pipeline;
    }
    printf("[USER] video pipeline started\n");

    /* ────────────────────────────────────────────
     * Step 6.5: JPEG/MJPEG 截图 — 在 AOV 单帧模式回调中动态触发
     *
     * 不再在此处一次性截图，而是在 user_venc_data_cb 中检测到
     * AOV 单帧模式（REC_MODE_SINGLE）时，投递截图请求给 shot worker
     * （触发 aov_video_jpeg_save + aov_video_mjpeg_save），编码结果
     * 经 user_venc_data_cb 的 'J'/'M' 帧回调按登记路径写盘。
     * 退出 AOV 模式（连续帧）后不再调用截图接口。
     * ──────────────────────────────────────────── */

    /* ────────────────────────────────────────────
     * Step 7: 初始化 AOV，注册回调
     * ──────────────────────────────────────────── */
    aov_init(&callbacks);
    printf("[USER] AOV init done, scheduler thread running\n");

    /* ────────────────────────────────────────────
     * Step 8: 执行测试序列（命令行参数自由组合）
     *
     * 不带参数时运行默认序列。
     * ──────────────────────────────────────────── */
    printf("[USER] running test sequence (%d args)...\n", argc - 1);

    run_test_sequence(argc, argv);

    /* ────────────────────────────────────────────
     * Step 9: 测试序列跑完，程序保持运行直到收到信号
     *
     * 后台运行时视频管线继续工作（录像/回调不中断）。
     * 用 kill <pid> 或 Ctrl-C 触发 g_running=0 进入清理。
     * ──────────────────────────────────────────── */
    if (g_running) {
        printf("[USER] idle, waiting for SIGINT/SIGTERM...\n");
        while (g_running) {
            sleep(1);
        }
    }

    /* ────────────────────────────────────────────
     * Step 10: 清理
     *
     * 先恢复可能被解绑的外设（Ctrl+C 在 AOV 休眠期间退出时）。
     * ──────────────────────────────────────────── */
    printf("[USER] shutting down...\n");

    restore_peripherals();
    user_osd_deinit_all();
    destroy_video_pipeline();
    goto fail_record_all;

fail_pipeline:
    user_osd_deinit_all();
    destroy_video_pipeline();
fail_record_all:
    if (g_record_queue && record_wait_sd_storage(1000) == 0) {
        record_resume_all_io();
        record_set_sd_ready(1);
        record_set_drain_pause(0);
        record_signal_drain();
        if (g_record_queue)
            record_wait_queue_empty(3000);
        record_flush_all();
    }
    record_drain_stop();

    for (int s = 0; s < REC_SENSOR_MAX; s++)
        for (int c = 0; c < REC_CHN_MAX; c++)
            if (g_record_ctx[s][c]) {
                aov_record_deinit(g_record_ctx[s][c]);
                g_record_ctx[s][c] = NULL;
            }

    if (g_record_queue) {
        aov_record_queue_deinit(g_record_queue);
        g_record_queue = NULL;
    }

    aov_deinit();

    record_sd_umount();

    RK_MPI_SYS_Exit();

fail_isp:
    aov_isp_deinit();

    printf("[USER] ═════ AOV Demo Exit ═════\n");

    return 0;
}
