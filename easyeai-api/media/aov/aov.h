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
 * 公开接口
 * ================================================================== */

/*
 * AOV 初始化
 *
 * 注册回调与唤醒配置，初始化完成后自动进入 AOV 模式。
 *
 * @note  非线程安全，应在主线程调用且仅调用一次。
 * @param action  回调函数集及保活 / 唤醒配置
 */
void aov_init(aov_action_t action);

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
 * 获取当前 AOV 运行状态（可选超时等待退出）
 *
 * 调用方可据此选择录像模式（AOV_STATUS_ACTIVE → 单帧，IDLE → 连续帧）
 * 或判断是否可以安全执行截图等非 AOV 操作。
 *
 * @param timeout_ms  超时等待（毫秒）：
 *                     0  — 立即返回当前状态
 *                    >0  — 等待最多 timeout_ms 毫秒直至 AOV_STATUS_IDLE
 *                    <0  — 无限阻塞直至 AOV_STATUS_IDLE
 * @return 当前 AOV 状态
 */
aov_status_e aov_status_get(int timeout_ms);

/*
 * 退出 / 唤醒控制
 *
 *   sec > 0:  退出 AOV，保持 sec 秒后自动重新进入
 *   sec == 0: 立即进入 AOV 模式
 *   sec < 0:  永久退出 AOV，直到下一次调用 aov_exit_set() 为止
 */
void aov_exit_set(int sec);

#ifdef __cplusplus
}
#endif

#endif /* __AOV_H__ */
