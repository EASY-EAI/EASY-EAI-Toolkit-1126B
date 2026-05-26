/*
 * SD 卡挂载/卸载模块
 * 负责 RV1126B 平台上 SD 卡的绑定、挂载、卸载和解除绑定
 */

#ifndef __SDCARD_H__
#define __SDCARD_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 挂载 SD 卡到 /mnt/sdcard
 *        执行流程：绑定驱动 → 检查是否已挂载 → mount 分区
 * @return 成功返回 0，失败返回 -1
 */
int MountSdcard(void);

/**
 * @brief 卸载 SD 卡
 *        执行流程：umount 分区 → 解除驱动绑定
 * @return 成功返回 0，失败返回 -1
 */
int UmountSdcard(void);

#ifdef __cplusplus
}
#endif

#endif
