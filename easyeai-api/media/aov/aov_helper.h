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
int Aov_DisableNonBootCPUs(void);

/* 唤醒后：重新使能非引导 CPU 核 */
int Aov_EnableNonBootCPUs(void);

/* ======================== 设备绑定 / 解绑 ======================== */

/* --- SD 卡 --- */
int Aov_BindSdcard(void);
int Aov_UnbindSdcard(void);

/* --- 以太网 --- */
int Aov_BindEthernet(void);
int Aov_UnbindEthernet(void);

/* --- 声卡 --- */
int Aov_BindSoundcard(void);
int Aov_UnbindSoundcard(void);

/* --- eMMC --- */
int Aov_BindEmmc(void);
int Aov_UnbindEmmc(void);

/* --- USB host 控制器 --- */
int Aov_DisableUSB(void);
int Aov_EnableUSB(void);

/* --- SDIO (WiFi / BT) --- */
int Aov_BindSDIO(void);
int Aov_UnbindSDIO(void);

/* --- WiFi 内核模块 (必须在 SDIO 解绑前卸载) --- */
int Aov_UnloadWifiModules(void);
int Aov_LoadWifiModules(void);

/* ======================== GPIO 唤醒检测 ======================== */

/*
 * 初始化 GPIO 唤醒输入检测
 *
 * 应在 aov_init() 之后、进入 AOV 调度循环之前调用。
 * @return 0 成功，-1 失败（无 GPIO 唤醒功能不影响主流程）
 */
int Aov_InitGpioIrq(void);

/*
 * 反初始化 GPIO 唤醒输入检测
 *
 * @return 0 成功，-1 失败
 */
int Aov_DeinitGpioIrq(void);

/*
 * 获取 GPIO 唤醒状态（非阻塞）
 *
 * @return 1 检测到唤醒事件，0 无事件
 */
int Aov_GetGpioIrqStat(void);

#ifdef __cplusplus
}
#endif

#endif /* __AOV_HELPER_H__ */
