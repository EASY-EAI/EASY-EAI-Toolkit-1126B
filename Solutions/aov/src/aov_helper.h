/*
 * AOV (Always On Video) 功能辅助接口头文件
 * 提供电源管理、CPU 控制、寄存器访问和 SD 卡挂载等功能
 */

#ifndef __AOV_HELPER_H__
#define __AOV_HELPER_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief AOV 事件枚举类型
 */
typedef enum {
    AOV_ENTER_SLEEP = 0,    ///< 进入睡眠事件
    AOV_EVENT_BUTT          ///< 事件类型结束标记
} AovEvent_e;

/**
 * @brief AOV 通知回调函数类型定义
 * @param enEvent 事件类型
 * @param msg 事件消息指针
 */
typedef void (*AovNotifyCallback)(AovEvent_e enEvent, void *msg);

/**
 * @brief AOV 初始化参数结构体
 */
typedef struct {
    
    AovNotifyCallback pfnNotifyCallback;    ///< 通知回调函数指针
} AovArg_t;

/**
 * @brief 获取唤醒锁，防止系统进入睡眠状态
 */
void Aov_WakeupLock(void);

/**
 * @brief 释放唤醒锁，允许系统进入睡眠状态
 */
void Aov_WakeupUnlock(void);

/**
 * @brief 初始化 AOV 模块
 * @param pstAovAttr AOV 初始化参数结构体指针
 * @return 成功返回 0，失败返回负值
 */
int  Aov_Init(AovArg_t *pstAovAttr);

/**
 * @brief 反初始化 AOV 模块
 * @return 成功返回 0，失败返回负值
 */
int  Aov_DeInit(void);

/**
 * @brief 让系统进入睡眠状态
 * @return 成功返回 0，失败返回负值
 */
int  Aov_EnterSleep(void);

/**
 * @brief 设置唤醒暂停时间
 * @param u32WakeupSuspendTime 唤醒暂停时间（单位：毫秒）
 * @return 成功返回 0，失败返回负值
 */
int  Aov_SetSuspendTime(int u32WakeupSuspendTime);

/**
 * @brief 禁用非引导 CPU 核心
 * @return 成功返回 0，失败返回负值
 */
int  Aov_DisableNonBootCPUs(void);

/**
 * @brief 启用非引导 CPU 核心
 * @return 成功返回 0，失败返回负值
 */
int  Aov_EnableNonBootCPUs(void);

/**
 * @brief 发送 AOV 通知
 * @param enEvent 事件类型
 * @param msg 事件消息指针
 */
void Aov_Notify(AovEvent_e enEvent, void *msg);

/**
 * @brief 读取指定地址的寄存器值
 * @param addr 寄存器地址
 * @param buf 用于存储读取数据的缓冲区
 * @param len 要读取的数据长度
 * @return 成功返回 0，失败返回负值
 */
int  Aov_ReadReg(int addr, unsigned char *buf, int len);

/**
 * @brief 写入指定地址的寄存器值
 * @param addr 寄存器地址
 * @param value 要写入的值
 * @return 成功返回 0，失败返回负值
 */
int  Aov_WriteReg(int addr, int value);

/**
 * @brief 休眠前卸载 USB xhci 控制器驱动，避免阻塞系统 suspend
 *        通过 /sys/bus/platform/drivers/xhci-hcd/unbind 解绑
 * @return 成功返回 0，失败返回 -1
 */
int  Aov_DisableUSB(void);

/**
 * @brief 唤醒后重新加载 USB xhci 控制器驱动
 *        通过 /sys/bus/platform/drivers/xhci-hcd/bind 绑定
 * @return 成功返回 0，失败返回 -1
 */
int  Aov_EnableUSB(void);

#ifdef __cplusplus
}
#endif

#endif
