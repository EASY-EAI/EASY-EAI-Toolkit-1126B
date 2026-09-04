#ifndef __AOV_H__
#define __AOV_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ==================================================================
 * AOV 公开状态
 * ================================================================== */

/*
 * AOV 运行状态（对外简化视图）
 *
 * AOV_STATUS_IDLE:   已退出 AOV，可安全执行截图等非 AOV 操作
 * AOV_STATUS_ACTIVE: AOV 运行中
 */
typedef enum {
    AOV_STATUS_IDLE   = 0,  /* 已退出 AOV */
    AOV_STATUS_ACTIVE = 1,  /* AOV 运行中 */
} aov_status_e;

/* ==================================================================
 * 常量
 * ================================================================== */

/*
 * AOV 模式下满足预写入的最小帧数
 *
 * 首次进入 AOV 时，帧数累积达到此阈值即触发 write_event 回调写入 SD 卡，
 * 无需等待达到最大帧数。
 */
#define MV_AOV_PRE_WRITE    10

/* ==================================================================
 * 回调类型
 * ================================================================== */

/*
 * 进入 AOV 模式时的行为回调
 *
 * AOV 进入时被调用，典型用途：挂起网络、关闭外设等省电操作。
 *
 * @return 0 成功，非 0 失败
 */
typedef int (*aov_enter_action)(void);

/*
 * 退出 AOV 模式时的行为回调
 *
 * AOV 退出时被调用，典型用途：恢复网络、重新使能外设。
 *
 * @return 0 成功，非 0 失败
 */
typedef int (*aov_exit_action)(void);

/*
 * 写入事件回调 — 触发写入 SD 卡
 *
 * 帧数达到阈值（首次 MV_AOV_PRE_WRITE 帧、后续达到最大帧数）时被调用。
 * 实现方应在此回调中将已缓存的编码帧写入 SD 卡。
 *
 * @return 0 成功，非 0 失败
 */
typedef int (*aov_write_event)(void);

/*
 * 回调注册集
 *
 * 通过 aov_init() 一次性注册，支持部分为 NULL（未设置的回调不触发）。
 */
typedef struct {
    aov_enter_action enter_func;       /* 进入 AOV 的行为回调    */
    aov_exit_action  exit_func;        /* 退出 AOV 的行为回调    */
    aov_write_event  write_event;      /* 写入 SD 卡回调        */
    int              alive_sec;        /* 默认保活时长（秒）     */

    /* --- 唤醒配置（二选一，不能同时使用；同时设置时 frames_per_second 优先） --- */
    int              seconds_per_frame;  /* N 秒拍 1 帧（如 2 表示 2 秒 1 帧） */
    int              frames_per_second;  /* 1 秒拍 N 帧（最大 3） */
} aov_action_t;

/* ==================================================================
 * 退出 / 唤醒控制枚举
 * ================================================================== */

/*
 * AOV 退出策略
 *
 * 用于 aov_exit_set() 的 action 参数，替代旧的 int 三态语义。
 */
typedef enum {
    AOV_EXIT_ENTER_NOW    = 0,  /* 立即进入 AOV 模式 */
    AOV_EXIT_TIMING       = 1,  /* 退出 AOV，保持 sec 秒后自动重新进入 */
    AOV_EXIT_FOREVER      = 2,  /* 永久退出 AOV，直到下一次调用 aov_exit_set() */
} aov_exit_action_e;

/* ==================================================================
 * 公开接口
 * ================================================================== */

/*
 * AOV 初始化
 *
 * 注册回调与唤醒配置，初始化完成后自动进入 AOV 模式。
 *
 * @note  非线程安全，应在主线程调用且仅调用一次。
 * @param action  回调函数集及保活 / 唤醒配置（指针传递，内部拷贝）
 * @return 0 成功，非 0 失败（AOV_ERR_NOT_READY 状态机初始化失败，
 *         AOV_ERR_FAILED 线程创建失败）
 */
int aov_init(const aov_action_t *action);

/*
 * AOV 反初始化
 *
 * 停止调度、释放资源。调用后不可再使用其他 AOV 接口。
 */
void aov_deinit(void);

/*
 * 阻塞等待 AOV 退出
 *
 * 阻塞当前线程直至 AOV 完全退出（AOV_STATUS_IDLE）。
 * 等待期间主线程可执行截图等非 AOV 操作。
 */
void aov_wait_exit(void);

/*
 * 获取当前 AOV 运行状态（非阻塞，立即返回）
 *
 * 调用方可据此选择录像模式（AOV_STATUS_ACTIVE → 单帧，IDLE → 连续帧）
 * 或判断是否可以安全执行截图等非 AOV 操作。
 *
 * @return 当前 AOV 状态
 */
aov_status_e aov_status_get(void);

/*
 * 等待 AOV 退出到 IDLE 状态（阻塞，带超时）
 *
 * 调用方可据此等待 AOV 完全退出后再执行非 AOV 操作。
 *
 * @param timeout_ms  超时等待（毫秒）：
 *                    >0  — 等待最多 timeout_ms 毫秒直至 AOV_STATUS_IDLE
 *                    <0  — 无限阻塞直至 AOV_STATUS_IDLE
 * @return AOV_STATUS_IDLE 已退出，AOV_STATUS_ACTIVE 超时仍处于 AOV 模式
 */
aov_status_e aov_status_wait_idle(int timeout_ms);

/*
 * 退出 / 唤醒控制
 *
 * @param action  退出策略（见 aov_exit_action_e 枚举）
 * @param sec     当 action == AOV_EXIT_TIMING 时，保持 sec 秒后重新进入；
 *               其他策略下忽略此参数
 */
void aov_exit_set(aov_exit_action_e action, int sec);

/* ==================================================================
 * 初始化调用顺序
 * ==================================================================
 *
 * 使用 AOV 模块需按以下顺序调用各接口（跨多个头文件）：
 *
 *   1. aov_isp_init()          — ISP 初始化（必须在 RK_MPI_SYS_Init 之前）
 *                                见 aov_isp.h
 *   2. RK_MPI_SYS_Init()       — RK 系统初始化
 *   3. aov_video_func_init()   — 选择视频后端 (SPLICE / SPLICE_AVS / MULTI)
 *                                见 aov_video.h
 *   4. aov_video_vi_init()     — VI + AVS 管线初始化
 *   5. aov_video_venc_init()   — VENC 编码通道初始化（可多次调用，每通道一次）
 *   6. aov_video_venc_start() — 启动编码线程，注册回调
 *   7. aov_init()              — AOV 调度启动（本文件）
 *   8. ... 运行期间通过 aov_exit_set / aov_status_get 控制状态 ...
 *   9. aov_deinit()             — AOV 反初始化
 *  10. aov_video_venc_stop()   — 停止编码
 *  11. aov_video_venc_uninit() — VENC 反初始化
 *  12. aov_video_vi_uninit()   — VI 管线反初始化
 *  13. aov_isp_deinit()        — ISP 反初始化
 *
 * 录像相关接口 (aov_record.h) 不依赖上述顺序，可在步骤 6 之后
 * 任意时机创建录像上下文。
 *
 * 休眠唤醒辅助接口 (aov_helper.h) 应在 aov_init() 之后、
 * 进入 AOV 调度循环之前调用。
 * ================================================================== */

#ifdef __cplusplus
}
#endif

#endif /* __AOV_H__ */
