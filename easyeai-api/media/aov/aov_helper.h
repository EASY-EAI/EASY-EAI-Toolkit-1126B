/*
 * aov_helper.h — AOV 休眠唤醒辅助模块
 *
 * 提供系统休眠/唤醒前后的设备管理功能，
 * 包括 CPU 核心控制、设备绑定/解绑、GPIO 唤醒检测。
 */

#ifndef __AOV_HELPER_H__
#define __AOV_HELPER_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ======================== CPU 热插拔 ======================== */

/* 休眠前：关闭非引导 CPU 核以降低漏电流 */
int aov_helper_disable_nonboot_cpus(void);

/* 唤醒后：重新使能非引导 CPU 核 */
int aov_helper_enable_nonboot_cpus(void);

/* ======================== 设备绑定 / 解绑 ======================== */

/* --- SD 卡 --- */
int aov_helper_bind_sdcard(void);
int aov_helper_unbind_sdcard(void);

/* --- 以太网 --- */
int aov_helper_bind_ethernet(void);
int aov_helper_unbind_ethernet(void);

/* --- 声卡 --- */
int aov_helper_bind_soundcard(void);
int aov_helper_unbind_soundcard(void);

/* --- eMMC --- */
int aov_helper_bind_emmc(void);
int aov_helper_unbind_emmc(void);

/* --- USB host 控制器 --- */
int aov_helper_disable_usb(void);
int aov_helper_enable_usb(void);

/* --- SDIO (WiFi / BT) --- */
int aov_helper_bind_sdio(void);
int aov_helper_unbind_sdio(void);

/* --- WiFi 内核模块 (必须在 SDIO 解绑前卸载) --- */
int aov_helper_unload_wifi_modules(void);
int aov_helper_load_wifi_modules(void);

/* ======================== GPIO 唤醒检测 ======================== */

/*
 * 初始化 GPIO 唤醒输入检测
 *
 * 应在 aov_init() 之后、进入 AOV 调度循环之前调用。
 * @return 0 成功，-1 失败（无 GPIO 唤醒功能不影响主流程）
 */
int aov_helper_init_gpio_irq(void);

/*
 * 反初始化 GPIO 唤醒输入检测
 *
 * @return 0 成功，-1 失败
 */
int aov_helper_deinit_gpio_irq(void);

/*
 * 获取 GPIO 唤醒状态（非阻塞）
 *
 * @return 1 检测到唤醒事件，0 无事件
 */
int aov_helper_get_gpio_irq_stat(void);

#ifdef __cplusplus
}
#endif

#endif /* __AOV_HELPER_H__ */
