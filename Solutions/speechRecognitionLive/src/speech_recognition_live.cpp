#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <alsa/asoundlib.h>
#include "speech_recognition.h"
#include "audio_utils.h"

/*逻辑总结：分配 30 秒的推理缓冲区，每个采样帧清零，只把最新的 3 秒 PCM 拷贝到开头，然后每秒触发一次推理。*/


#define WINDOW_SECONDS  5    // 环形缓冲区长度(保存最近5秒PCM)
#define STEP_SECONDS    1    // 滑动步长

// CAPTURE_DEBUG：由 cmake -DCAPTURE_DEBUG=ON 控制，定义则打印采集峰值排查


// ── 环形缓冲区 ────────────────────────────────────────────────────
// write_pos/filled/step_count 只有采集回调写，不加锁
// step_ready/snap_mutex/snap_cond 用于通知主线程
typedef struct {
    float  *ring;
    int     window_samples;
    int     step_samples;
    int     write_pos;      // 采集回调写，无锁
    int     filled;         // 采集回调写，无锁
    int     step_count;     // 采集回调写，无锁（绝对累计量，不清零）
    int     step_ready;     // 受 snap_mutex 保护
    pthread_mutex_t snap_mutex;
    pthread_cond_t  snap_cond;
} RingCtx;

// ── 推理队列 ──────────────────────────────────────────────────────
#define QUEUE_DEPTH 2

typedef struct {
    float  *slots[QUEUE_DEPTH];
    int     head;
    int     tail;
    int     count;
    pthread_mutex_t mutex;
    pthread_cond_t  cond_data;
} InferQueue;

typedef struct {
    InferQueue     *queue;
    rknn_whisper_t *whisper;
    int             task_code;
    int             full_samples;
} InferArg;

static volatile sig_atomic_t g_running = 1;

// ── 信号处理函数 ──────────────────────────────────────────────────
// 必须使用 sigaction + 去掉 SA_RESTART，这样阻塞的 snd_pcm_readi()
// 会被 EINTR 中断返回， capture_thread 才能检测到 g_running == 0 并退出。
// 如果用 signal() 默认带 SA_RESTART，read 会自动重启，采集线程永远卡死。
static void sig_handler(int sig)
{
    (void)sig;
    g_running = 0;
}


// ── 通过 ALSA 控制接口设置硬件 mixer 控件（替代 RK_MPI_AMIX_SetControl）──
// 先查询控件类型，再按 BOOLEAN/INTEGER/ENUMERATED 写入对应值，
// 否则类型不匹配会写入失败（且驱动可能静默忽略，破坏已正常的路由状态）。
static void set_alsa_ctl(const char *ctl_name, int val)
{
    snd_ctl_t *ctl;
    snd_ctl_elem_id_t *id;
    snd_ctl_elem_info_t *info;
    snd_ctl_elem_value_t *value;

    if (snd_ctl_open(&ctl, "hw:0", 0) < 0)
        return;  // 非致命，静默失败

    snd_ctl_elem_id_alloca(&id);
    snd_ctl_elem_info_alloca(&info);
    snd_ctl_elem_value_alloca(&value);

    snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
    snd_ctl_elem_id_set_name(id, ctl_name);

    // 查询控件类型
    snd_ctl_elem_info_set_id(info, id);
    if (snd_ctl_elem_info(ctl, info) < 0) {
        snd_ctl_close(ctl);
        return;
    }

    snd_ctl_elem_value_set_id(value, id);
    switch (snd_ctl_elem_info_get_type(info)) {
    case SND_CTL_ELEM_TYPE_BOOLEAN:
        snd_ctl_elem_value_set_boolean(value, 0, val);
        break;
    case SND_CTL_ELEM_TYPE_INTEGER:
        snd_ctl_elem_value_set_integer(value, 0, val);
        break;
    case SND_CTL_ELEM_TYPE_ENUMERATED:
        snd_ctl_elem_value_set_enumerated(value, 0, val);
        break;
    default:
        snd_ctl_close(ctl);
        return;
    }

    snd_ctl_elem_write(ctl, value);
    snd_ctl_close(ctl);
}

// ── 在启动采集前清理残留的 ALSA PCM 状态 ──────────────────────────
// 用 kill -9 杀死进程后，snd_pcm_close() 不会被调用，硬件仍处于打开状态。
// 下次启动时先 open/drop/close 一次，让 ALSA 驱动恢复空闲状态。
static void alsa_pre_cleanup(void)
{
    snd_pcm_t *h;
    if (snd_pcm_open(&h, "hw:0,0", SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK) == 0) {
        snd_pcm_drop(h);
        snd_pcm_close(h);
    }
}

// ── PCM 采集线程：先初始化硬件 mixer，再通过 ALSA 读取 ───────────
static void *capture_thread(void *arg)
{
    RingCtx *ctx = (RingCtx *)arg;
    snd_pcm_t *handle;
    snd_pcm_uframes_t period_size = 1024;
    // 硬件声卡 hw:0,0 物理上是 2 声道，只能以立体声打开（单声道会在第一次
    // snd_pcm_readi 时返回 EIO）。每帧含 L/R 两个采样点，故 buf 取 2 倍大小。
    int16_t buf[1024 * 2];
    int ret;

    // 0. 清理上次残留的 ALSA 状态（kill -9 后 snd_pcm_close 没被调用）
    alsa_pre_cleanup();

    // 1. 硬件初始化：配置 SAI Loopback 路由 + 开启 ADC（替代 MPP RK_MPI_AMIX_SetControl）
    set_alsa_ctl("ACodec_LP ADC Switch", 1);              // 开启 ADC
    set_alsa_ctl("ACodec_LP PGA Gain Volume", 16);        // ADC 增益 48dB（默认值）
    set_alsa_ctl("Headset Mic Switch", 1);                // 耳机麦克风
    set_alsa_ctl("Main Mic Switch", 1);                   // 板载麦克风
    set_alsa_ctl("SAI2 SDI0 Loopback Switch", 1);         // Enable Loopback
    set_alsa_ctl("SAI2 SDI0 Loopback I2S LR Switch", 1);  // Enable LR 交换
    // Src Select 默认就是 From SDO0（val=0），无需设置

    // 2. 打开 ALSA 设备（捕获、阻塞模式）
    ret = snd_pcm_open(&handle, "hw:0,0", SND_PCM_STREAM_CAPTURE, 0);
    if (ret < 0) {
        printf("ALSA open hw:0,0 failed: %s\n", snd_strerror(ret));
        g_running = 0;  // 通知主线程退出
        return NULL;
    }

    // 3. 配置硬件参数
    snd_pcm_hw_params_t *params;
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(handle, params);
    snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(handle, params, 2);  // 硬件仅支持 2 声道，软件取左声道
    unsigned int rate = SAMPLE_RATE;
    snd_pcm_hw_params_set_rate_near(handle, params, &rate, 0);
    snd_pcm_hw_params_set_period_size_near(handle, params, &period_size, 0);

    ret = snd_pcm_hw_params(handle, params);
    if (ret < 0) {
        printf("ALSA hw_params failed: %s\n", snd_strerror(ret));
        snd_pcm_close(handle);
        g_running = 0;
        return NULL;
    }

    printf("ALSA capture started: dev=hw:0,0 %uHz 16-bit stereo (use left channel)\n", rate);

    // 4. 采集循环
    while (g_running) {
        ret = snd_pcm_readi(handle, buf, period_size);
        if (ret < 0) {
            // -EINTR: 信号中断（SIGTERM/SIGINT），检查 g_running 后退出
            if (ret == -EINTR) {
                // EINTR 时 snd_pcm_recover 会重试，此处直接检查退出条件
                continue;
            }
            // 缓冲区欠载/溢出，恢复
            ret = snd_pcm_recover(handle, ret, 0);
            if (ret < 0) {
                printf("ALSA read error (unrecoverable): %s\n", snd_strerror(ret));
                break;
            }
            continue;
        }

        int frames = ret;  // 实际读到的帧数（每帧含 L/R 两个采样点）

        // ── 调试：确认 readi 真有数据出来（每读约 1 秒打印一次左右声道峰值）──
#ifdef CAPTURE_DEBUG
        {
            static long dbg_acc = 0;
            static long dbg_calls = 0;
            int16_t peakL = 0, peakR = 0;
            for (int i = 0; i < frames; i++) {
                int16_t l = buf[i * 2];
                int16_t r = buf[i * 2 + 1];
                int16_t al = l < 0 ? -l : l;
                int16_t ar = r < 0 ? -r : r;
                if (al > peakL) peakL = al;
                if (ar > peakR) peakR = ar;
            }
            dbg_acc += frames;
            dbg_calls++;
            if (dbg_acc >= SAMPLE_RATE) {  // 累计约 1 秒打印一次
                printf("[capture] %ld calls, frames=%d, peakL=%d peakR=%d\n",
                       dbg_calls, frames, peakL, peakR);
                dbg_acc = 0;
                dbg_calls = 0;
            }
        }
#endif

        int prev_step = ctx->step_count / ctx->step_samples;

        for (int i = 0; i < frames; i++) {
            // 立体声交错布局：取左声道 buf[i*2]，丢弃右声道 buf[i*2+1]
            ctx->ring[ctx->write_pos] = buf[i * 2] / 32768.0f;
            ctx->write_pos = (ctx->write_pos + 1) % ctx->window_samples;
            if (ctx->filled < ctx->window_samples)
                ctx->filled++;
            ctx->step_count++;
        }

        int curr_step = ctx->step_count / ctx->step_samples;

        // 当 curr_step > prev_step，说明 step_count 至少增长了 step_samples 个采样点
        // 即从上次检查到现在，至少又过去了 1 秒，满足一次滑动步长
        // 再加上 ring 已填满，即可通知主线程做一次推理
        if (ctx->filled >= ctx->window_samples && curr_step > prev_step) {
            pthread_mutex_lock(&ctx->snap_mutex);
            ctx->step_ready = 1;
            pthread_cond_signal(&ctx->snap_cond);
            pthread_mutex_unlock(&ctx->snap_mutex);
        }
    }

    // 退出前先 drop，让 ALSA 驱动恢复空闲（否则下次 open 可能 EIO）
    snd_pcm_drop(handle);
    snd_pcm_close(handle);
    printf("ALSA capture stopped.\n");
    g_running = 0;  // 通知主线程退出
    return NULL;
}


// ── 推理线程 ──────────────────────────────────────────────────────
static void *infer_thread(void *arg)
{
    InferArg *a = (InferArg *)arg;
    InferQueue *q = a->queue;
    std::vector<std::string> recognized_text;
    int iter = 0;

    while (g_running) {
        pthread_mutex_lock(&q->mutex);
        while (g_running && q->count == 0) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 1;
            pthread_cond_timedwait(&q->cond_data, &q->mutex, &ts);
        }
        if (!g_running && q->count == 0) {
            pthread_mutex_unlock(&q->mutex);
            break;
        }
        float *buf = q->slots[q->head];
        q->head    = (q->head + 1) % QUEUE_DEPTH;
        q->count--;
        pthread_mutex_unlock(&q->mutex);

        clock_t start = clock();

        audio_buffer_t audio;
        audio.data         = buf;
        audio.num_channels = 1;
        audio.sample_rate  = SAMPLE_RATE;
        audio.num_frames   = a->full_samples;

        recognized_text.clear();

        {
            struct timespec start, end;
            clock_gettime(CLOCK_MONOTONIC, &start);
            speech_recognition_run(a->whisper, audio, a->task_code, recognized_text);
            clock_gettime(CLOCK_MONOTONIC, &end);
            double sec = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
            double ms  = sec * 1000.0;
            printf("[infer] speech_recognition_run took %.0f ms (%.3f s)\n", ms, sec);
        }

        clock_t end = clock();
        double infer_time = ((double)(end - start)) / CLOCKS_PER_SEC;

        std::cout << "\nspeech recognition output: ";
        for (const auto &s : recognized_text) std::cout << s;
        std::cout << std::endl;

        printf("%d, RTF: %.3f / %ds = %.3f\n\n",
               iter++, infer_time, WINDOW_SECONDS, (float)infer_time / WINDOW_SECONDS);
    }
    return NULL;
}


int main(int argc, char **argv)
{
    if (argc != 6) {
        printf("%s <encoder_path> <decoder_path> <filter_path> <vocab_path> <task(en/cn)>\n", argv[0]);
        printf("Example: %s speech_encoder.model speech_decoder.model filters.txt CN.txt cn\n", argv[0]);
        return -1;
    }

    const char *p_encoder_path = argv[1];
    const char *p_decoder_path = argv[2];
    const char *p_filter_path  = argv[3];
    const char *p_vocab_path   = argv[4];
    const char *p_task         = argv[5];
    int task_code = 0;

    if (strcmp(p_task, "en") == 0)       task_code = 50259;
    else if (strcmp(p_task, "cn") == 0)  task_code = 50260;
    else {
        printf("\n\033[1;33mOnly en/cn supported\033[0m\n");
        return -1;
    }

    int full_samples   = CHUNK_LENGTH * SAMPLE_RATE;   // Whisper 模型一次推理的输入采样点数（30s × 16000 = 480000）
    int window_samples = WINDOW_SECONDS * SAMPLE_RATE;  // 环形缓冲区大小，保存最近 3 秒的 PCM 采样点数（3s × 16000 = 48000）
    int step_samples   = STEP_SECONDS   * SAMPLE_RATE;  // 每次推理向前滑动的步长采样点数（1s × 16000 = 16000）

    // 环形缓冲区
    RingCtx ring;
    ring.ring = (float *)calloc(window_samples, sizeof(float));
    if (!ring.ring) { printf("calloc ring failed\n"); return -1; }
    ring.window_samples = window_samples;
    ring.step_samples   = step_samples;
    ring.write_pos      = 0;
    ring.filled         = 0;
    ring.step_count     = 0;
    ring.step_ready     = 0;
    pthread_mutex_init(&ring.snap_mutex, NULL);
    pthread_cond_init(&ring.snap_cond,   NULL);

    // 推理队列
    InferQueue queue;
    for (int i = 0; i < QUEUE_DEPTH; i++) {
        queue.slots[i] = (float *)calloc(full_samples, sizeof(float));
        if (!queue.slots[i]) { printf("calloc queue slot failed\n"); return -1; }
    }
    queue.head  = 0;
    queue.tail  = 0;
    queue.count = 0;
    pthread_mutex_init(&queue.mutex,     NULL);
    pthread_cond_init(&queue.cond_data,  NULL);

    // speech recognition 初始化
    rknn_whisper_t whisper;
    int ret = speech_recognition_init(p_encoder_path, p_decoder_path,
                                      p_filter_path, p_vocab_path, &whisper);
    if (ret != 0) { printf("speech_recognition_init failed: %d\n", ret); return -1; }

    // 推理线程
    InferArg infer_arg = { &queue, &whisper, task_code, full_samples };
    pthread_t tid_infer;
    pthread_create(&tid_infer, NULL, infer_thread, &infer_arg);

    // 启动 ALSA 采集线程
    pthread_t tid_capture;
    pthread_create(&tid_capture, NULL, capture_thread, &ring);

    {
        struct sigaction sa;
        sa.sa_handler = sig_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;                          // 不设 SA_RESTART，让阻塞 syscall 返回 EINTR
        sigaction(SIGINT,  &sa, NULL);
        sigaction(SIGTERM, &sa, NULL);
    }

    printf("\n=== Live Speech Recognition (ALSA + ring + async infer) ===\n");
    printf("Window: %ds  Step: %ds  Press Ctrl+C to stop.\n\n",
           WINDOW_SECONDS, STEP_SECONDS);

    // 主线程：等步长信号 → 快照环形缓冲 → 入推理队列
    while (g_running) {
        // 等采集回调通知步长就绪
        pthread_mutex_lock(&ring.snap_mutex);
        while (g_running && !ring.step_ready) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 1;
            pthread_cond_timedwait(&ring.snap_cond, &ring.snap_mutex, &ts);
        }
        if (!g_running) { pthread_mutex_unlock(&ring.snap_mutex); break; }
        ring.step_ready = 0;
        pthread_mutex_unlock(&ring.snap_mutex);

        // 队列满则丢弃本帧（推理跟不上时不积压）
        pthread_mutex_lock(&queue.mutex);
        if (queue.count >= QUEUE_DEPTH) {
            pthread_mutex_unlock(&queue.mutex);
            continue;
        }

        // 展开环形缓冲到推理 slot（read_pos = write_pos = 最旧位置）
        float *slot = queue.slots[queue.tail];
        memset(slot, 0, full_samples * sizeof(float));
        int oldest = ring.write_pos;
        for (int i = 0; i < window_samples; i++)
            slot[i] = ring.ring[(oldest + i) % window_samples];

        queue.tail  = (queue.tail + 1) % QUEUE_DEPTH;
        queue.count++;
        pthread_cond_signal(&queue.cond_data);
        pthread_mutex_unlock(&queue.mutex);
    }

    printf("\nStopping...\n");

    // 等待线程退出
    pthread_join(tid_capture, NULL);

    pthread_cond_broadcast(&queue.cond_data);
    pthread_join(tid_infer, NULL);

    speech_recognition_release(&whisper);

    free(ring.ring);
    for (int i = 0; i < QUEUE_DEPTH; i++) free(queue.slots[i]);
    pthread_mutex_destroy(&ring.snap_mutex);
    pthread_cond_destroy(&ring.snap_cond);
    pthread_mutex_destroy(&queue.mutex);
    pthread_cond_destroy(&queue.cond_data);

    return 0;
}
