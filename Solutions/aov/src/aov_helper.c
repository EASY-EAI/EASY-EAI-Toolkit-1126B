/*
 * AOV 休眠唤醒 辅助模块
 * 提供电源管理、CPU 控制、寄存器访问等功能
 *
 * 注：SD 卡挂载功能已迁移到 sdcard.c
 */

#include "aov_helper.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/* ======================== 平台寄存器地址 (RV1126B) ======================== */
#define SUSPEND_TIME_REG 0x20834310

#define SOC_SLEEP_STR "mem"
#define SOC_SLEEP_PATH "/sys/power/state"

/* ======================== AOV 内部状态 ======================== */
static pthread_mutex_t gWakeupRunMutex;
static AovNotifyCallback gNotifyCallback = NULL;

/* ======================== AOV 休眠/唤醒接口 ======================== */
void Aov_WakeupLock(void)
{
    pthread_mutex_lock(&gWakeupRunMutex);
}

void Aov_WakeupUnlock(void)
{
    pthread_mutex_unlock(&gWakeupRunMutex);
}

int Aov_Init(AovArg_t *pstAovAttr)
{
    int ret;

    ret = pthread_mutex_init(&gWakeupRunMutex, NULL);
    if (ret) {
        printf("[AOV] mutex init failed[%d]\n", ret);
        return -1;
    }

    gNotifyCallback = pstAovAttr->pfnNotifyCallback;
    return 0;
}

int Aov_DeInit(void)
{
    pthread_mutex_destroy(&gWakeupRunMutex);
    gNotifyCallback = NULL;
    return 0;
}

int Aov_EnterSleep(void)
{
    int fd = -1;
    ssize_t ret = -1;

    fd = open(SOC_SLEEP_PATH, O_WRONLY | O_TRUNC);
    if (fd == -1) {
        printf("[AOV] Failed to open %s, errno=%d, %s\n",
               SOC_SLEEP_PATH, errno, strerror(errno));
        return -1;
    }

    ret = write(fd, SOC_SLEEP_STR, strlen(SOC_SLEEP_STR));
    if (ret == -1) {
        printf("[AOV] Failed to write %s, errno=%d, %s\n",
               SOC_SLEEP_STR, errno, strerror(errno));
        close(fd);
        return -1;
    }

    printf("[AOV] echo \"%s\" > %s successfully, system entered suspend\n",
           SOC_SLEEP_STR, SOC_SLEEP_PATH);
    close(fd);
    return 0;
}

void Aov_Notify(AovEvent_e enEvent, void *msg)
{
    if (gNotifyCallback)
        gNotifyCallback(enEvent, msg);
    else
        printf("[AOV] Unregistered notify callback\n");
}

int Aov_SetSuspendTime(int u32WakeupSuspendTime)
{
    int ret;

    ret = Aov_WriteReg(SUSPEND_TIME_REG, u32WakeupSuspendTime * 32.768);
    if (ret != 0) {
        printf("[AOV] Failed to set suspend time!\n");
        return -1;
    }

    printf("[AOV] wakeup suspend time = %d\n", u32WakeupSuspendTime);
    return 0;
}

int Aov_ReadReg(int addr, unsigned char *buf, int len)
{
    int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0) {
        printf("[AOV] Error opening /dev/mem\n");
        return -1;
    }

    size_t page_size = getpagesize();
    if ((size_t)len > page_size) {
        printf("[AOV] Dump length is too long!\n");
        close(mem_fd);
        return -1;
    }

    off_t page_base = (addr & ~(page_size - 1));
    off_t page_offset = addr - page_base;

    void *mem_map = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
                         MAP_SHARED, mem_fd, page_base);
    if (mem_map == MAP_FAILED) {
        printf("[AOV] Error mapping memory\n");
        close(mem_fd);
        return -1;
    }

    const unsigned char *target_reg =
        (unsigned char *)((char *)mem_map + page_offset);
    memcpy(buf, target_reg, len);

    munmap(mem_map, page_size);
    close(mem_fd);
    return 0;
}

int Aov_WriteReg(int addr, int value)
{
    int memFd = open("/dev/mem", O_RDWR | O_SYNC);
    if (memFd < 0) {
        printf("[AOV] Error opening /dev/mem\n");
        return -1;
    }

    size_t pageSize = getpagesize();
    off_t pageBase = (addr & ~(pageSize - 1));
    off_t pageOffset = addr - pageBase;

    void *memMap = mmap(NULL, pageSize, PROT_READ | PROT_WRITE,
                        MAP_SHARED, memFd, pageBase);
    if (memMap == MAP_FAILED) {
        printf("[AOV] Error mapping memory\n");
        close(memFd);
        return -1;
    }

    int *targetReg = (int *)((char *)memMap + pageOffset);
    *targetReg = value;

    munmap(memMap, pageSize);
    close(memFd);
    return 0;
}

int Aov_DisableNonBootCPUs(void)
{
    int fd;
    const char *off = "0";

    fd = open("/sys/devices/system/cpu/cpu1/online", O_WRONLY);
    if (fd >= 0) {
        write(fd, off, strlen(off));
        close(fd);
    }
    fd = open("/sys/devices/system/cpu/cpu2/online", O_WRONLY);
    if (fd >= 0) {
        write(fd, off, strlen(off));
        close(fd);
    }
    fd = open("/sys/devices/system/cpu/cpu3/online", O_WRONLY);
    if (fd >= 0) {
        write(fd, off, strlen(off));
        close(fd);
    }
    printf("[AOV] non-boot CPUs disabled\n");
    return 0;
}

int Aov_EnableNonBootCPUs(void)
{
    int fd;
    const char *on = "1";

    fd = open("/sys/devices/system/cpu/cpu1/online", O_WRONLY);
    if (fd >= 0) {
        write(fd, on, strlen(on));
        close(fd);
    }
    fd = open("/sys/devices/system/cpu/cpu2/online", O_WRONLY);
    if (fd >= 0) {
        write(fd, on, strlen(on));
        close(fd);
    }
    fd = open("/sys/devices/system/cpu/cpu3/online", O_WRONLY);
    if (fd >= 0) {
        write(fd, on, strlen(on));
        close(fd);
    }
    printf("[AOV] non-boot CPUs enabled\n");
    return 0;
}

/* ======================== USB xhci 控制器（休眠前解绑/唤醒后重绑） ======================== */

/*
 * RV1126B USB xhci 控制器设备名和驱动路径。
 *
 * 内核日志显示：
 *   xhci-hcd xhci-hcd.0.auto: PM: failed to suspend async: error -22
 *
 * 休眠前需将其从驱动解绑，避免阻塞系统 suspend；唤醒后重新绑定。
 */
#define XHCI_DEVICE_NAME    "xhci-hcd.0.auto"
#define XHCI_DRIVER_PATH    "/sys/bus/platform/drivers/xhci-hcd"

int Aov_DisableUSB(void)
{
    char path[128];
    int fd;
    ssize_t ret;

    snprintf(path, sizeof(path), "%s/unbind", XHCI_DRIVER_PATH);
    fd = open(path, O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        printf("[AOV] Failed to open %s for unbind, errno=%d, %s\n",
               path, errno, strerror(errno));
        return -1;
    }

    ret = write(fd, XHCI_DEVICE_NAME, strlen(XHCI_DEVICE_NAME));
    if (ret < 0) {
        printf("[AOV] Failed to unbind %s, errno=%d, %s\n",
               XHCI_DEVICE_NAME, errno, strerror(errno));
        close(fd);
        return -1;
    }

    close(fd);
    printf("[AOV] USB xhci unbound (%s from %s)\n", XHCI_DEVICE_NAME, XHCI_DRIVER_PATH);

    /* 给内核一点时间完成解绑 */
    usleep(10000);
    return 0;
}

int Aov_EnableUSB(void)
{
    char path[128];
    int fd;
    ssize_t ret;

    snprintf(path, sizeof(path), "%s/bind", XHCI_DRIVER_PATH);
    fd = open(path, O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        printf("[AOV] Failed to open %s for bind, errno=%d, %s\n",
               path, errno, strerror(errno));
        return -1;
    }

    ret = write(fd, XHCI_DEVICE_NAME, strlen(XHCI_DEVICE_NAME));
    if (ret < 0) {
        printf("[AOV] Failed to bind %s, errno=%d, %s\n",
               XHCI_DEVICE_NAME, errno, strerror(errno));
        close(fd);
        return -1;
    }

    close(fd);
    printf("[AOV] USB xhci bound (%s to %s)\n", XHCI_DEVICE_NAME, XHCI_DRIVER_PATH);

    /* 给内核一点时间完成绑定 */
    usleep(10000);
    return 0;
}

