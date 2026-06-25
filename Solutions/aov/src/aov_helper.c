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

/* ======================== USB 设备级 unbind 辅助 ======================== */

/*
 * 在 xhci 控制器解绑前，先移除已知有问题的 USB 设备。
 *
 * 某些 USB 设备（如 WiFi/BT 模块 ws73）在 xhci 被 unbound/re-bound 后，
 * 其设备状态会变得不一致。当 USB core 在后续 resume 中尝试重新枚举该设备时，
 * 会因设备描述符读取失败而触发 3 次超时重试，每次 ~5-10s，总计延迟约 27s。
 *
 * 内核日志表现：
 *   usb 3-1.2: device descriptor read/64, error -71      (EPROTO)
 *   usb 3-1.2: device descriptor read/64, error -110     (ETIMEDOUT × 2)
 *
 * 通过在 xhci 解绑前先将设备从 USB core 移除（unbind），
 * 令 resume 时 USB core 不持有该设备的陈旧引用，从而跳过耗时重枚举。
 * xhci re-bind 后，USB core 会以 clean state 重新发现设备。
 */
static void unbind_usb_devices_on_bus(void)
{
    /*
     * 需要从 USB core 驱动移除的设备 ID。
     * 可通过 lsusb -t 或 readlink /sys/bus/usb/devices/{设备ID}/driver 来确认。
     * 如果挂载了 USB 存储(U盘)，也需要在此列出。
     */
    static const char *devices[] = {
        "3-1",     /* USB 总线 3, 端口 1 (根 hub 端口) — 先移除以阻止子设备枚举 */
        "3-1.2",   /* USB 总线 3, 端口 1, 子端口 2 (WiFi/BT ws73 模块) */
        NULL
    };

    for (int i = 0; devices[i]; i++) {
        char devpath[64];
        char linkpath[256];
        ssize_t len;
        int fd;

        /* 检查设备是否存在 */
        snprintf(devpath, sizeof(devpath), "/sys/bus/usb/devices/%s", devices[i]);
        if (access(devpath, F_OK) != 0)
            continue;

        /* 检查 driver 链接是否存在（已绑定驱动） */
        snprintf(linkpath, sizeof(linkpath), "%s/driver", devpath);
        len = readlink(linkpath, devpath, sizeof(devpath) - 1);
        if (len < 0 || len >= (ssize_t)sizeof(devpath) - 1)
            continue;
        devpath[len] = '\0';

        /* 从驱动 unbind */
        snprintf(linkpath, sizeof(linkpath), "/sys/bus/usb/drivers/%s/unbind",
                 strrchr(devpath, '/') ? strrchr(devpath, '/') + 1 : devpath);

        fd = open(linkpath, O_WRONLY | O_NONBLOCK);
        if (fd < 0) {
            printf("[AOV] USB: can't open %s for unbind, errno=%d\n",
                   linkpath, errno);
            continue;
        }
        write(fd, devices[i], strlen(devices[i]));
        close(fd);

        printf("[AOV] USB: unbound %s from %s\n", devices[i],
               strrchr(devpath, '/') ? strrchr(devpath, '/') + 1 : devpath);
    }
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

    /*
     * 第1步：在 xhci 解绑前，先移除已知有问题的 USB 设备
     * （如 WiFi/BT ws73 模块），避免 resume 时 USB core
     * 因陈旧引用而耗时重枚举该设备。
     */
    unbind_usb_devices_on_bus();

    /*
     * 第2步：解绑 xhci-hcd 平台驱动
     */
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

