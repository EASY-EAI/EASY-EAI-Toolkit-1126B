#ifndef __ROCKCHIP_SYS_H__
#define __ROCKCHIP_SYS_H__

#ifdef __cplusplus
extern "C" {
#endif

/*
 * MPP 系统全局初始化/退出（带引用计数，可多次调用）
 *   内部使用 pthread_mutex 保证线程安全，
 *   只有首次调用 init 时真正初始化，末次 exit 时真正退出。
 */
int rockchip_sys_init(void);    /* 初始化 MPP 系统，成功返回 0 */
void rockchip_sys_exit(void);   /* 退出 MPP 系统 */

#ifdef __cplusplus
}
#endif

#endif
