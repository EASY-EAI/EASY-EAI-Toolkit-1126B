/*
 * aov_helper.c — AOV 休眠唤醒辅助模块
 *
 * 提供电源管理、CPU 控制、以及外设驱动绑定/解绑功能。
 * 休眠前解绑外设降漏电，唤醒后重新绑定恢复功能。
 *
 * 支持设备：SD卡、Ethernet、声卡、eMMC、USB、SDIO
 * 平台：RV1126B
 */

#define _GNU_SOURCE
#include "aov_helper.h"
#include "aov_error.h"

/* ======================== 编译开关 ======================== */

/*
 * OHCI/EHCI USB Host 控制器解绑开关
 *
 *   0 = 只解绑 dwc3 (默认, 与原厂 SDK 对齐)
 *   1 = 同时解绑/绑定 OHCI/EHCI 控制器
 *
 * usb0(4G/r8152) 挂在 EHCI 上。不解绑时 AOV 每个休眠周期都要带着该 USB
 * 设备一起挂起/恢复, 实测唤醒恢复时间多约 1.2s (周期 ≈2.7s; 解绑后 ≈1.0s)。
 * 代价: AOV 期间 usb0 不可用, 恢复时由 module_4g_power_reset() 重新枚举。
 */
#define AOV_USB_UNBIND_HOST_CTRL    1

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <net/if.h>
#include <linux/if.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/input.h>              /* struct input_event, EV_KEY, KEY_POWER */

/* ======================== 通用辅助 ======================== */

#define MAX_NL_BUF_SIZE     (1024 * 16)
#define MAX_SELECT_TIMEOUT  (5 * 1000 * 1000)

/*
 * 检查设备驱动是否已绑定。
 * 通过检测 /sys/bus/platform/drivers/<driver>/<device> 是否存在来判断。
 */
static bool device_driver_is_bound(const char *device, const char *driver)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", driver, device);
    return (access(path, F_OK) == 0);
}

/*
 * 将设备绑定到驱动：写设备名到 <driver>/bind。
 */
static int device_attach_driver(const char *device, const char *driver)
{
    char path[256];
    int fd;
    int ret;

    snprintf(path, sizeof(path), "%s/bind", driver);
    fd = open(path, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        printf("can't open %s for bind: %s\n", path, strerror(errno));
        return AOV_ERR_IO;
    }

    ret = write(fd, device, strlen(device));
    if (ret < 0)
        printf("bind %s to %s failed: %s\n", device, driver, strerror(errno));

    close(fd);
    return (ret < 0) ? AOV_ERR_IO : AOV_ERR_SUCCESS;
}

/*
 * 将设备从驱动解绑：写设备名到 <driver>/unbind。
 */
static int device_detach_driver(const char *device, const char *driver)
{
    char path[256];
    int fd;
    int ret;

    snprintf(path, sizeof(path), "%s/unbind", driver);
    fd = open(path, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        printf("can't open %s for unbind: %s\n", path, strerror(errno));
        return AOV_ERR_IO;
    }

    ret = write(fd, device, strlen(device));
    if (ret < 0)
        printf("unbind %s from %s failed: %s\n", device, driver, strerror(errno));

    close(fd);
    return (ret < 0) ? AOV_ERR_IO : AOV_ERR_SUCCESS;
}

/*
 * 通过 /dev/mem 写寄存器值。
 */
static int write_register(unsigned int addr, unsigned int value)
{
    int fd;
    void *map_base;
    unsigned int page_size;
    unsigned int page_addr;

    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        printf("can't open /dev/mem: %s\n", strerror(errno));
        return AOV_ERR_IO;
    }

    page_size = (unsigned int)sysconf(_SC_PAGESIZE);
    page_addr = addr & ~(page_size - 1);

    map_base = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
                    MAP_SHARED, fd, page_addr);
    if (map_base == MAP_FAILED) {
        printf("mmap failed for addr 0x%x: %s\n", addr, strerror(errno));
        close(fd);
        return AOV_ERR_IO;
    }

    *((volatile unsigned int *)((unsigned char *)map_base + (addr - page_addr))) = value;

    munmap(map_base, page_size);
    close(fd);
    return AOV_ERR_SUCCESS;
}

/* ======================== CPU 热插拔控制 ======================== */

int aov_helper_disable_nonboot_cpus(void)
{
    int fd;
    const char *off = "0";

    if (0) {
        fd = open("/sys/devices/system/cpu/cpu1/online", O_WRONLY);
        if (fd >= 0) { write(fd, off, strlen(off)); close(fd); }
    }
    fd = open("/sys/devices/system/cpu/cpu2/online", O_WRONLY);
    if (fd >= 0) { write(fd, off, strlen(off)); close(fd); }
    fd = open("/sys/devices/system/cpu/cpu3/online", O_WRONLY);
    if (fd >= 0) { write(fd, off, strlen(off)); close(fd); }

    printf("non-boot CPUs disabled\n");
    return AOV_ERR_SUCCESS;
}

int aov_helper_enable_nonboot_cpus(void)
{
    int fd;
    const char *on = "1";

    if (0) {
        fd = open("/sys/devices/system/cpu/cpu1/online", O_WRONLY);
        if (fd >= 0) { write(fd, on, strlen(on)); close(fd); }
    }
    fd = open("/sys/devices/system/cpu/cpu2/online", O_WRONLY);
    if (fd >= 0) { write(fd, on, strlen(on)); close(fd); }
    fd = open("/sys/devices/system/cpu/cpu3/online", O_WRONLY);
    if (fd >= 0) { write(fd, on, strlen(on)); close(fd); }

    printf("non-boot CPUs enabled\n");
    return AOV_ERR_SUCCESS;
}

/* ======================== Ethernet (gmac) ======================== */

#define ETHERNET_DEVICE   "21c70000.ethernet"
#define ETHERNET_DRIVER   "/sys/bus/platform/drivers/rk_gmac-dwmac/"

int aov_helper_bind_ethernet(void)
{
    printf("Binding ethernet...\n");

    if (device_driver_is_bound(ETHERNET_DEVICE, ETHERNET_DRIVER)) {
        printf("ethernet already bound\n");
        return AOV_ERR_SUCCESS;
    }

    if (device_attach_driver(ETHERNET_DEVICE, ETHERNET_DRIVER) != 0)
        return AOV_ERR_FAILED;

    system("ifconfig eth0 up");

    if (!device_driver_is_bound(ETHERNET_DEVICE, ETHERNET_DRIVER)) {
        printf("ethernet bind failed\n");
        return AOV_ERR_FAILED;
    }

    printf("ethernet bound successfully\n");
    return AOV_ERR_SUCCESS;
}

int aov_helper_unbind_ethernet(void)
{
    printf("Unbinding ethernet...\n");

    if (!device_driver_is_bound(ETHERNET_DEVICE, ETHERNET_DRIVER)) {
        printf("ethernet already unbound\n");
        return AOV_ERR_SUCCESS;
    }

    system("ifconfig eth0 0.0.0.0");
    system("ifconfig eth0 down");

    if (device_detach_driver(ETHERNET_DEVICE, ETHERNET_DRIVER) != 0)
        return AOV_ERR_FAILED;

    if (device_driver_is_bound(ETHERNET_DEVICE, ETHERNET_DRIVER)) {
        printf("ethernet unbind failed\n");
        return AOV_ERR_FAILED;
    }

    printf("ethernet unbound successfully\n");
    return AOV_ERR_SUCCESS;
}

/* ======================== SD 卡 ======================== */

#define SDCARD_DEVICE             "21d60000.mmc"
#define SDCARD_DRIVER             "/sys/bus/platform/drivers/dwmmc_rockchip"
#define SDCARD_BIND_EVENT_PREFIX  "bind@/devices/platform/21d60000.mmc"
#define SDCARD_BIND_DONE_PREFIX   "bind@/devices/platform/21d60000.mmc/mmc_host/mmc1/mmc1"
#define SDCARD_UNBIND_DONE        "unbind@/devices/platform/21d60000.mmc"

/*
 * 检测 SD 卡是否在位 (读 SDMMC_CD 寄存器)。
 */
static bool detect_sdcard_is_enable(void)
{
    int fd;
    unsigned int value = 0;

    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0)
        return false;

    unsigned int page_addr = 0x21D60050 & ~((unsigned int)sysconf(_SC_PAGESIZE) - 1);
    void *map_base = mmap(NULL, (unsigned int)sysconf(_SC_PAGESIZE),
                          PROT_READ, MAP_SHARED, fd, page_addr);
    if (map_base != MAP_FAILED) {
        value = *((volatile unsigned int *)((unsigned char *)map_base +
                   (0x21D60050 - page_addr)));
        munmap(map_base, (unsigned int)sysconf(_SC_PAGESIZE));
    }
    close(fd);
    return (value == 0);
}

int aov_helper_bind_sdcard(void)
{
    int fd, ret;
    char buf[MAX_NL_BUF_SIZE];
    fd_set read_set;
    struct timeval timeout;
    struct sockaddr_nl addr;

    printf("Binding SD card...\n");

    if (device_driver_is_bound(SDCARD_DEVICE, SDCARD_DRIVER)) {
        printf("SD card already bound\n");
        return AOV_ERR_SUCCESS;
    }

    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = NETLINK_KOBJECT_UEVENT;
    addr.nl_pid = 0;

    fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_KOBJECT_UEVENT);
    if (fd < 0 || bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        printf("SD card netlink setup failed\n");
        if (fd >= 0) close(fd);
        return AOV_ERR_FAILED;
    }

    FD_ZERO(&read_set);
    FD_SET(fd, &read_set);
    timeout.tv_sec = 0;
    timeout.tv_usec = MAX_SELECT_TIMEOUT;

    if (device_attach_driver(SDCARD_DEVICE, SDCARD_DRIVER) != 0)
        goto fail;

__RETRY:
    ret = select(fd + 1, &read_set, NULL, NULL, &timeout);
    if (ret > 0) {
        memset(buf, 0, sizeof(buf));
        read(fd, buf, sizeof(buf));
        buf[MAX_NL_BUF_SIZE - 1] = '\0';

        if (strncmp(buf, SDCARD_BIND_EVENT_PREFIX,
                    strlen(SDCARD_BIND_EVENT_PREFIX)) == 0) {
            detect_sdcard_is_enable(); /* 加速绑定时序 */
        }
        if (strncmp(buf, SDCARD_BIND_DONE_PREFIX,
                    strlen(SDCARD_BIND_DONE_PREFIX)) == 0) {
            printf("SD card bound successfully\n");
            close(fd);
            return AOV_ERR_SUCCESS;
        }
        goto __RETRY;
    }

fail:
    printf("SD card bind failed\n");
    close(fd);
    return AOV_ERR_FAILED;
}

int aov_helper_unbind_sdcard(void)
{
    int fd, ret;
    char buf[MAX_NL_BUF_SIZE];
    fd_set read_set;
    struct timeval timeout;
    struct sockaddr_nl addr;

    printf("Unbinding SD card...\n");

    if (!device_driver_is_bound(SDCARD_DEVICE, SDCARD_DRIVER)) {
        printf("SD card already unbound\n");
        return AOV_ERR_SUCCESS;
    }

    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = NETLINK_KOBJECT_UEVENT;
    addr.nl_pid = 0;

    fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_KOBJECT_UEVENT);
    if (fd < 0 || bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        printf("SD card netlink setup failed\n");
        if (fd >= 0) close(fd);
        return AOV_ERR_FAILED;
    }

    FD_ZERO(&read_set);
    FD_SET(fd, &read_set);
    timeout.tv_sec = 0;
    timeout.tv_usec = MAX_SELECT_TIMEOUT;

    if (device_detach_driver(SDCARD_DEVICE, SDCARD_DRIVER) != 0)
        goto fail;

__RETRY:
    ret = select(fd + 1, &read_set, NULL, NULL, &timeout);
    if (ret > 0) {
        memset(buf, 0, sizeof(buf));
        read(fd, buf, sizeof(buf));
        buf[MAX_NL_BUF_SIZE - 1] = '\0';

        if (strcmp(buf, SDCARD_UNBIND_DONE) == 0) {
            printf("SD card unbound successfully\n");
            close(fd);
            return AOV_ERR_SUCCESS;
        }
        goto __RETRY;
    }

fail:
    printf("SD card unbind failed\n");
    close(fd);
    return AOV_ERR_FAILED;
}

/* ======================== 声卡 (I2S + acodec + dsm + multicodecs) ======================== */

#define I2S_DRIVER    "/sys/bus/platform/drivers/rockchip-sai/"
#define ACODEC_DRIVER "/sys/bus/platform/drivers/rk3506-acodec/"
#define RKDSM_DRIVER  "/sys/bus/platform/drivers/rk_dsm/"
#define ASOC_DRIVER   "/sys/bus/platform/drivers/rk-multicodecs/"

#define I2S_DEVICE    "20980000.sai"
#define ACODEC_DEVICE "20bf0000.audio-codec"
#define RKDSM_DEVICE  "209a0000.acdcdig-dsm"
#define ASOC_DEVICE   "acodec-sound"

#define SOUND_BIND_DONE     "bind@/devices/platform/acodec-sound"
#define SOUND_UNBIND_DONE   "unbind@/devices/platform/20980000.sai"

int aov_helper_bind_soundcard(void)
{
    int fd, ret, cnt = 0;
    char buf[MAX_NL_BUF_SIZE];
    fd_set read_set;
    struct timeval timeout;
    struct sockaddr_nl addr;

    printf("Binding sound card...\n");

    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = NETLINK_KOBJECT_UEVENT;
    addr.nl_pid = 0;

    fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_KOBJECT_UEVENT);
    if (fd < 0 || bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        printf("sound card netlink setup failed\n");
        if (fd >= 0) close(fd);
        return AOV_ERR_FAILED;
    }

    FD_ZERO(&read_set);
    FD_SET(fd, &read_set);
    timeout.tv_sec = 0;
    timeout.tv_usec = MAX_SELECT_TIMEOUT;

    ret = device_attach_driver(I2S_DEVICE, I2S_DRIVER);
    ret |= device_attach_driver(ACODEC_DEVICE, ACODEC_DRIVER);
    ret |= device_attach_driver(RKDSM_DEVICE, RKDSM_DRIVER);
    ret |= device_attach_driver(ASOC_DEVICE, ASOC_DRIVER);
    if (ret != 0)
        goto fail;

__RETRY:
    ret = select(fd + 1, &read_set, NULL, NULL, &timeout);
    if (ret > 0) {
        memset(buf, 0, sizeof(buf));
        read(fd, buf, sizeof(buf));
        buf[MAX_NL_BUF_SIZE - 1] = '\0';

        if (strncmp(buf, SOUND_BIND_DONE, strlen(SOUND_BIND_DONE)) == 0) {
            /* 等待 PCM 设备节点出现 (最多重试 3 次) */
            while (access("/dev/snd/pcmC0D0c", F_OK) ||
                   access("/dev/snd/pcmC0D0p", F_OK)) {
                if (++cnt > 3) {
                    printf("sound card pcm device not ready\n");
                    goto fail;
                }
                usleep(200000);
            }
            printf("sound card bound successfully\n");
            close(fd);
            return AOV_ERR_SUCCESS;
        }
        goto __RETRY;
    }

fail:
    printf("sound card bind failed\n");
    close(fd);
    return AOV_ERR_FAILED;
}

int aov_helper_unbind_soundcard(void)
{
    int fd, ret;
    char buf[MAX_NL_BUF_SIZE];
    fd_set read_set;
    struct timeval timeout;
    struct sockaddr_nl addr;

    printf("Unbinding sound card...\n");

    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = NETLINK_KOBJECT_UEVENT;
    addr.nl_pid = 0;

    fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_KOBJECT_UEVENT);
    if (fd < 0 || bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        printf("sound card netlink setup failed\n");
        if (fd >= 0) close(fd);
        return AOV_ERR_FAILED;
    }

    FD_ZERO(&read_set);
    FD_SET(fd, &read_set);
    timeout.tv_sec = 0;
    timeout.tv_usec = MAX_SELECT_TIMEOUT;

    ret = device_detach_driver(I2S_DEVICE, I2S_DRIVER);
    ret |= device_detach_driver(ACODEC_DEVICE, ACODEC_DRIVER);
    ret |= device_detach_driver(RKDSM_DEVICE, RKDSM_DRIVER);
    ret |= device_detach_driver(ASOC_DEVICE, ASOC_DRIVER);
    if (ret != 0)
        goto fail;

__RETRY:
    ret = select(fd + 1, &read_set, NULL, NULL, &timeout);
    if (ret > 0) {
        memset(buf, 0, sizeof(buf));
        read(fd, buf, sizeof(buf));
        buf[MAX_NL_BUF_SIZE - 1] = '\0';

        if (strncmp(buf, SOUND_UNBIND_DONE, strlen(SOUND_UNBIND_DONE)) == 0) {
            printf("sound card unbound successfully\n");
            close(fd);
            return AOV_ERR_SUCCESS;
        }
        goto __RETRY;
    }

fail:
    printf("sound card unbind failed\n");
    close(fd);
    return AOV_ERR_FAILED;
}

/* ======================== eMMC ======================== */

#define EMMC_DEVICE       "21470000.mmc"
#define EMMC_DRIVER       "/sys/bus/platform/drivers/dwmmc_rockchip"
#define EMMC_BIND_DONE    "bind@/devices/platform/21470000.mmc/mmc_host/mmc0/mmc0:0001"
#define EMMC_UNBIND_DONE  "unbind@/devices/platform/21470000.mmc"

int aov_helper_bind_emmc(void)
{
    int fd, ret;
    char buf[MAX_NL_BUF_SIZE];
    fd_set read_set;
    struct timeval timeout;
    struct sockaddr_nl addr;

    printf("Binding eMMC...\n");

    if (device_driver_is_bound(EMMC_DEVICE, EMMC_DRIVER)) {
        printf("eMMC already bound\n");
        return AOV_ERR_SUCCESS;
    }

    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = NETLINK_KOBJECT_UEVENT;
    addr.nl_pid = 0;

    fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_KOBJECT_UEVENT);
    if (fd < 0 || bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        printf("eMMC netlink setup failed\n");
        if (fd >= 0) close(fd);
        return AOV_ERR_FAILED;
    }

    FD_ZERO(&read_set);
    FD_SET(fd, &read_set);
    timeout.tv_sec = 0;
    timeout.tv_usec = MAX_SELECT_TIMEOUT;

    if (device_attach_driver(EMMC_DEVICE, EMMC_DRIVER) != 0)
        goto fail;

__RETRY:
    ret = select(fd + 1, &read_set, NULL, NULL, &timeout);
    if (ret > 0) {
        memset(buf, 0, sizeof(buf));
        read(fd, buf, sizeof(buf));
        buf[MAX_NL_BUF_SIZE - 1] = '\0';

        if (strcmp(buf, EMMC_BIND_DONE) == 0) {
            printf("eMMC bound successfully\n");
            close(fd);
            return AOV_ERR_SUCCESS;
        }
        goto __RETRY;
    }

fail:
    printf("eMMC bind failed\n");
    close(fd);
    return AOV_ERR_FAILED;
}

int aov_helper_unbind_emmc(void)
{
    int fd, ret;
    char buf[MAX_NL_BUF_SIZE];
    fd_set read_set;
    struct timeval timeout;
    struct sockaddr_nl addr;

    printf("Unbinding eMMC...\n");

    if (!device_driver_is_bound(EMMC_DEVICE, EMMC_DRIVER)) {
        printf("eMMC already unbound\n");
        return AOV_ERR_SUCCESS;
    }

    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = NETLINK_KOBJECT_UEVENT;
    addr.nl_pid = 0;

    fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_KOBJECT_UEVENT);
    if (fd < 0 || bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        printf("eMMC netlink setup failed\n");
        if (fd >= 0) close(fd);
        return AOV_ERR_FAILED;
    }

    FD_ZERO(&read_set);
    FD_SET(fd, &read_set);
    timeout.tv_sec = 0;
    timeout.tv_usec = MAX_SELECT_TIMEOUT;

    if (device_detach_driver(EMMC_DEVICE, EMMC_DRIVER) != 0)
        goto fail;

__RETRY:
    ret = select(fd + 1, &read_set, NULL, NULL, &timeout);
    if (ret > 0) {
        memset(buf, 0, sizeof(buf));
        read(fd, buf, sizeof(buf));
        buf[MAX_NL_BUF_SIZE - 1] = '\0';

        if (strcmp(buf, EMMC_UNBIND_DONE) == 0) {
            printf("eMMC unbound successfully\n");
            close(fd);
            return AOV_ERR_SUCCESS;
        }
        goto __RETRY;
    }

fail:
    printf("eMMC unbind failed\n");
    close(fd);
    return AOV_ERR_FAILED;
}

/* ======================== USB host 控制器 ======================== */

/*
 * RV1126B Nano 板子 USB 拓扑 (通过实测确认):
 *
 *   21500000.usb  → dwc3 驱动     (USB OTG, dr_mode=otg, 当前工作在 gadget 模式)
 *   21480000.usb  → ehci-platform  (USB 2.0 Host, usb0 网卡挂在这里)
 *   214c0000.usb  → ohci-platform  (USB 1.1 Host, 当前无外接设备)
 *
 * 三个控制器是独立的硬件模块，共享同一个 USB2 PHY (21400000.usb2-phy)。
 *
 * 默认只解绑/绑定 dwc3 (与原厂 SDK 对齐):
 *
 *   1. dwc3 是 OTG 控制器，解绑/绑定不会影响 EHCI/OHCI 下的 USB 设备
 *      (实测确认: 解绑 dwc3 后 usb0 网卡仍然存在)
 *
 *   2. 解绑 EHCI 会导致 usb0 网卡立即消失，且重新绑定后 USB 设备不会
 *      自动重新枚举 (只有 root hub 回来，外接设备不恢复)，因此
 *      EHCI/OHCI 不能解绑
 *
 *   3. dwc3 在 gadget 模式下是 USB Device (用于 ADB 等)，解绑后
 *      gadget 功能暂停，重新绑定后恢复。对 AOV 休眠降功耗足够。
 *
 * 可选: 同时解绑 OHCI/EHCI Host 控制器 (开关: AOV_USB_UNBIND_HOST_CTRL)
 *
 *   usb0(4G/r8152) 挂在 EHCI 上。若 EHCI 不解绑，AOV 每个休眠周期都要
 *   带着该 USB 设备一起挂起/恢复，实测唤醒恢复时间会多出约 1.2s
 *   (对比参考: 解绑 EHCI 时周期 ≈1.0s, 不解绑时周期 ≈2.7s)。
 *   代价是 AOV 期间 usb0 不可用，恢复时依赖 module_4g_power_reset()
 *   重新枚举 (约 2~20s)。默认关闭 (宏置 0)，需要时把宏改为 1。
 */
#define DWC3_DEVICE       "21500000.usb"
#define DWC3_DRIVER       "/sys/bus/platform/drivers/dwc3/"
#define USB_BIND_DONE     "bind@/devices/platform/21500000.usb"
#define USB_UNBIND_DONE   "unbind@/devices/platform/21500000.usb"

/* USB Host 控制器 (仅在解绑/绑定开关打开时使用) */
#define EHCI_DEVICE       "21480000.usb"
#define EHCI_DRIVER       "/sys/bus/platform/drivers/ehci-platform"
#define OHCI_DEVICE       "214c0000.usb"
#define OHCI_DRIVER       "/sys/bus/platform/drivers/ohci-platform"

/*
 * 4G 模块电源控制 (GPIO5_C6 = sysfs gpio182, 低电平有效)
 *
 * 系统休眠 (/sys/power/state "mem") 时内核 PM 框架会挂起 EHCI
 * 控制器，导致 USB 总线断开、4G 模块掉线。唤醒后 EHCI 虽然恢复，
 * 但 USB 外接设备不会自动重新枚举。
 *
 * 解决方案：唤醒后如果 usb0 不存在，通过 GPIO182 对 4G 模块做
 * 电源复位（断电 → 上电），强制重新枚举。
 *
 * GPIO182 = GPIO5_C6, 低电平=上电/使能, 高电平=断电/禁用
 */
#define GPIO_4G_POWER_NUM    182
#define GPIO_4G_POWER_PATH   "/sys/class/gpio/gpio182/value"
#define GPIO_4G_POWER_DIR    "/sys/class/gpio/gpio182/direction"

/*
 * 通过 GPIO182 对 4G 模块进行电源复位。
 * 断电 (echo 1) → 等待 2 秒 → 上电 (echo 0) → 等待重新枚举。
 */
static void module_4g_power_reset(void)
{
    int fd;
    const char *off = "1";  /* 高电平断电 */
    const char *on  = "0";  /* 低电平上电 */

    /* 确保 GPIO 已导出 */
    if (access(GPIO_4G_POWER_PATH, F_OK) != 0) {
        fd = open("/sys/class/gpio/export", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0) {
            char num[8];
            snprintf(num, sizeof(num), "%d", GPIO_4G_POWER_NUM);
            write(fd, num, strlen(num));
            close(fd);
        }
    }

    /* 设置为输出方向 */
    fd = open(GPIO_4G_POWER_DIR, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd >= 0) {
        write(fd, "out", 3);
        close(fd);
    }

    printf("Resetting 4G module via GPIO%d...\n", GPIO_4G_POWER_NUM);

    /* 断电 */
    fd = open(GPIO_4G_POWER_PATH, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        printf("can't open %s: %s\n", GPIO_4G_POWER_PATH, strerror(errno));
        return;
    }
    write(fd, off, strlen(off));
    close(fd);

    usleep(2000000);  /* 断电 2 秒 */

    /* 上电 */
    fd = open(GPIO_4G_POWER_PATH, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        printf("can't open %s for power-on: %s\n", GPIO_4G_POWER_PATH, strerror(errno));
        return;
    }
    write(fd, on, strlen(on));
    close(fd);

    printf("4G module powered on, waiting for enumeration...\n");
}

int aov_helper_enable_usb(void)
{
    int fd, ret;
    char buf[MAX_NL_BUF_SIZE];
    fd_set read_set;
    struct timeval timeout;
    struct sockaddr_nl addr;

    /*
     * 可选: 重新绑定 OHCI/EHCI Host 控制器
     *
     * 由文件开头的 AOV_USB_UNBIND_HOST_CTRL 统一控制, 与
     * aov_helper_disable_usb() 里的解绑成对生效。
     * 注意: 重新绑定后外接 USB 设备不会自动重新枚举, usb0 需要靠下面的
     *       module_4g_power_reset() 复位 4G 模块来恢复。
     */
#if AOV_USB_UNBIND_HOST_CTRL
    if (!device_driver_is_bound(EHCI_DEVICE, EHCI_DRIVER)) {
        if (device_attach_driver(EHCI_DEVICE, EHCI_DRIVER) == AOV_ERR_SUCCESS)
            printf("USB host (ehci) bound successfully\n");
    }
    if (!device_driver_is_bound(OHCI_DEVICE, OHCI_DRIVER)) {
        if (device_attach_driver(OHCI_DEVICE, OHCI_DRIVER) == AOV_ERR_SUCCESS)
            printf("USB host (ohci) bound successfully\n");
    }
#endif

    printf("Binding USB (dwc3)...\n");

    /* Step 1: 绑定 dwc3 (gadget 控制器，用于 ADB 等) */
    if (!device_driver_is_bound(DWC3_DEVICE, DWC3_DRIVER)) {
        memset(&addr, 0, sizeof(addr));
        addr.nl_family = AF_NETLINK;
        addr.nl_groups = NETLINK_KOBJECT_UEVENT;
        addr.nl_pid = 0;

        fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_KOBJECT_UEVENT);
        if (fd < 0 || bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
            printf("USB netlink setup failed\n");
            if (fd >= 0) close(fd);
            return AOV_ERR_FAILED;
        }

        FD_ZERO(&read_set);
        FD_SET(fd, &read_set);
        timeout.tv_sec = 0;
        timeout.tv_usec = MAX_SELECT_TIMEOUT;

        if (device_attach_driver(DWC3_DEVICE, DWC3_DRIVER) != 0) {
            printf("USB (dwc3) bind failed\n");
            close(fd);
            return AOV_ERR_FAILED;
        }

    __DWC3_RETRY:
        ret = select(fd + 1, &read_set, NULL, NULL, &timeout);
        if (ret > 0) {
            memset(buf, 0, sizeof(buf));
            read(fd, buf, sizeof(buf));
            buf[MAX_NL_BUF_SIZE - 1] = '\0';

            if (strcmp(buf, USB_BIND_DONE) == 0) {
                printf("USB (dwc3) bound successfully\n");
                close(fd);
                fd = -1;
            } else {
                goto __DWC3_RETRY;
            }
        } else {
            printf("USB (dwc3) bind timeout\n");
            close(fd);
            return AOV_ERR_FAILED;
        }
    } else {
        printf("USB (dwc3) already bound\n");
    }

    /*
     * Step 2: 检查 usb0 是否存在，不存在则通过 GPIO 复位 4G 模块
     *
     * 系统休眠期间内核 PM 框架挂起 EHCI，导致 4G 模块断开。
     * 唤醒后 EHCI 自动恢复，但 4G 模块不会自动重新枚举。
     * 通过 GPIO182 对 4G 模块做电源复位（断电→上电），
     * 强制重新枚举。
     */
    if (access("/sys/class/net/usb0", F_OK) != 0) {
        printf("usb0 missing, resetting 4G module...\n");
        module_4g_power_reset();
    }

    /*
     * Step 3: 等待 usb0 网卡就绪
     *
     * 如果做了 4G 模块复位，需要等待 ~15 秒枚举完成。
     * 如果 usb0 一直存在（无需复位），此处立即通过。
     */
    {
        int wait_ms = 0;
        while (access("/sys/class/net/usb0", F_OK) != 0 && wait_ms < 20000) {
            usleep(100000);
            wait_ms += 100;
        }
        if (access("/sys/class/net/usb0", F_OK) == 0) {
            printf("usb0 ready after %dms\n", wait_ms);
            system("ifconfig usb0 up 2>/dev/null");
        } else {
            printf("usb0 not ready after 20s"
                       " (4G module reset may have failed)\n");
        }
    }

    return AOV_ERR_SUCCESS;
}

int aov_helper_disable_usb(void)
{
    int fd, ret;
    char buf[MAX_NL_BUF_SIZE];
    fd_set read_set;
    struct timeval timeout;
    struct sockaddr_nl addr;

    /*
     * 可选: 同时解绑 OHCI/EHCI Host 控制器
     *
     * 解绑后 EHCI 上不再挂着 usb0(4G/r8152) 设备, AOV 休眠周期里就不会
     * 每轮都跟着 USB 子系统挂起/恢复, 唤醒恢复时间可少约 1.2s。
     * 代价: AOV 期间 usb0 不可用, 恢复时由 module_4g_power_reset() 重新枚举。
     * 开关见文件开头的 AOV_USB_UNBIND_HOST_CTRL。
     */
#if AOV_USB_UNBIND_HOST_CTRL
    if (device_driver_is_bound(EHCI_DEVICE, EHCI_DRIVER)) {
        if (device_detach_driver(EHCI_DEVICE, EHCI_DRIVER) == AOV_ERR_SUCCESS)
            printf("USB host (ehci) unbound successfully\n");
    }
    if (device_driver_is_bound(OHCI_DEVICE, OHCI_DRIVER)) {
        if (device_detach_driver(OHCI_DEVICE, OHCI_DRIVER) == AOV_ERR_SUCCESS)
            printf("USB host (ohci) unbound successfully\n");
    }
#endif

    printf("Unbinding USB (dwc3)...\n");

    if (!device_driver_is_bound(DWC3_DEVICE, DWC3_DRIVER)) {
        printf("USB (dwc3) already unbound\n");
        return AOV_ERR_SUCCESS;
    }

    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = NETLINK_KOBJECT_UEVENT;
    addr.nl_pid = 0;

    fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_KOBJECT_UEVENT);
    if (fd < 0 || bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        printf("USB netlink setup failed\n");
        if (fd >= 0) close(fd);
        return AOV_ERR_FAILED;
    }

    FD_ZERO(&read_set);
    FD_SET(fd, &read_set);
    timeout.tv_sec = 0;
    timeout.tv_usec = MAX_SELECT_TIMEOUT;

    if (device_detach_driver(DWC3_DEVICE, DWC3_DRIVER) != 0)
        goto fail;

__RETRY:
    ret = select(fd + 1, &read_set, NULL, NULL, &timeout);
    if (ret > 0) {
        memset(buf, 0, sizeof(buf));
        read(fd, buf, sizeof(buf));
        buf[MAX_NL_BUF_SIZE - 1] = '\0';

        if (strcmp(buf, USB_UNBIND_DONE) == 0) {
            printf("USB (dwc3) unbound successfully\n");
            close(fd);
            return AOV_ERR_SUCCESS;
        }
        goto __RETRY;
    }

fail:
    printf("USB (dwc3) unbind failed\n");
    close(fd);
    return AOV_ERR_FAILED;
}

/* ======================== SDIO (WiFi / BT) ======================== */

#define SDIO_DEVICE       "21f60000.mmc"
#define SDIO_DRIVER       "/sys/bus/platform/drivers/dwmmc_rockchip"
#define SDIO_BIND_DONE    "bind@/devices/platform/21f60000.mmc"
#define SDIO_UNBIND_DONE  "unbind@/devices/platform/21f60000.mmc"

int aov_helper_bind_sdio(void)
{
    int fd, ret;
    char buf[MAX_NL_BUF_SIZE];
    fd_set read_set;
    struct timeval timeout;
    struct sockaddr_nl addr;

    printf("Binding SDIO...\n");

    if (device_driver_is_bound(SDIO_DEVICE, SDIO_DRIVER)) {
        printf("SDIO already bound\n");
        return AOV_ERR_SUCCESS;
    }

    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = NETLINK_KOBJECT_UEVENT;
    addr.nl_pid = 0;

    fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_KOBJECT_UEVENT);
    if (fd < 0 || bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        printf("SDIO netlink setup failed\n");
        if (fd >= 0) close(fd);
        return AOV_ERR_FAILED;
    }

    FD_ZERO(&read_set);
    FD_SET(fd, &read_set);
    timeout.tv_sec = 0;
    timeout.tv_usec = MAX_SELECT_TIMEOUT;

    if (device_attach_driver(SDIO_DEVICE, SDIO_DRIVER) != 0)
        goto fail;

__RETRY:
    ret = select(fd + 1, &read_set, NULL, NULL, &timeout);
    if (ret > 0) {
        memset(buf, 0, sizeof(buf));
        read(fd, buf, sizeof(buf));
        buf[MAX_NL_BUF_SIZE - 1] = '\0';

        if (strncmp(buf, SDIO_BIND_DONE, strlen(SDIO_BIND_DONE)) == 0) {
            printf("SDIO bound successfully\n");
            close(fd);
            return AOV_ERR_SUCCESS;
        }
        goto __RETRY;
    }

fail:
    printf("SDIO bind failed\n");
    close(fd);
    return AOV_ERR_FAILED;
}

int aov_helper_unbind_sdio(void)
{
    int fd, ret;
    char buf[MAX_NL_BUF_SIZE];
    fd_set read_set;
    struct timeval timeout;
    struct sockaddr_nl addr;

    printf("Unbinding SDIO...\n");

    if (!device_driver_is_bound(SDIO_DEVICE, SDIO_DRIVER)) {
        printf("SDIO already unbound\n");
        return AOV_ERR_SUCCESS;
    }

    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = NETLINK_KOBJECT_UEVENT;
    addr.nl_pid = 0;

    fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_KOBJECT_UEVENT);
    if (fd < 0 || bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        printf("SDIO netlink setup failed\n");
        if (fd >= 0) close(fd);
        return AOV_ERR_FAILED;
    }

    FD_ZERO(&read_set);
    FD_SET(fd, &read_set);
    timeout.tv_sec = 0;
    timeout.tv_usec = MAX_SELECT_TIMEOUT;

    if (device_detach_driver(SDIO_DEVICE, SDIO_DRIVER) != 0)
        goto fail;

__RETRY:
    ret = select(fd + 1, &read_set, NULL, NULL, &timeout);
    if (ret > 0) {
        memset(buf, 0, sizeof(buf));
        read(fd, buf, sizeof(buf));
        buf[MAX_NL_BUF_SIZE - 1] = '\0';

        if (strcmp(buf, SDIO_UNBIND_DONE) == 0) {
            printf("SDIO unbound successfully\n");
            close(fd);
            return AOV_ERR_SUCCESS;
        }
        goto __RETRY;
    }

fail:
    printf("SDIO unbind failed\n");
    close(fd);
    return AOV_ERR_FAILED;
}

/* ======================== WiFi 模块管理 ======================== */

/*
 * 卸载 WiFi 内核模块。
 *
 * 必须在 SDIO 解绑之前调用。SDIO 解绑后 WiFi 固件线程 (wifi_frw_msg)
 * 仍持有 SDIO host 引用，此时访问 SDIO 寄存器将触发内核 Oops/NULL
 * pointer dereference panic。
 *
 * 卸载顺序: wifi_soc → ble_soc → sle_soc → plat_soc (逆依赖序)
 *
 * 注意: sle_soc 必须在 plat_soc 之前卸载，否则 sle_soc 持有的 plat_soc
 * 引用计数不为零，rmmod plat_soc 会因 -EBUSY 失败，导致 plat_soc
 * 残留在内核中。系统唤醒时 plat_soc 的 PM resume 回调会访问已被
 * 解绑的 SDIO 寄存器，触发 level 3 translation fault → kernel panic。
 * 唤醒后通过 aov_helper_load_wifi_modules() 恢复。
 */
int aov_helper_unload_wifi_modules(void)
{
    printf("Unloading WiFi modules...\n");

    /* 按依赖逆序卸载: wifi_soc → ble_soc → sle_soc → plat_soc */
    if (system("rmmod wifi_soc 2>/dev/null") != 0) {
        printf("rmmod wifi_soc failed (may already be unloaded)\n");
    }
    if (system("rmmod ble_soc 2>/dev/null") != 0) {
        /* ble_soc 可选，不存在或已卸载均忽略 */
    }
    if (system("rmmod sle_soc 2>/dev/null") != 0) {
        /* sle_soc 可选，不存在或已卸载均忽略 */
    }
    if (system("rmmod plat_soc 2>/dev/null") != 0) {
        printf("rmmod plat_soc failed (may already be unloaded)\n");
    }

    printf("WiFi modules unloaded\n");
    return AOV_ERR_SUCCESS;
}

/*
 * 重新加载 WiFi 内核模块。
 *
 * 必须在 SDIO 重新绑定之后调用，此时 SDIO host 硬件已就绪。
 *
 * 加载顺序: plat_soc → ble_soc → sle_soc → wifi_soc (依赖序)
 */
int aov_helper_load_wifi_modules(void)
{
    int ret;

    printf("Loading WiFi modules...\n");

    /*
     * 检查模块是否已加载（休眠前卸载失败或模块未被卸载时可能残留）。
     * 如果 plat_soc 已在内核中，modprobe 会返回成功（幂等），无需重试。
     */
    ret = system("lsmod 2>/dev/null | grep -q plat_soc");
    if (ret == 0) {
        printf("WiFi modules already loaded\n");
        return AOV_ERR_SUCCESS;
    }

    /*
     * 加载顺序: plat_soc → ble_soc → sle_soc → wifi_soc (依赖序)
     *
     * 板子上 .ko 文件位于 /etc/ws73/ 目录，modprobe 可能找不到
     * 模块索引，因此 modprobe 失败后用 insmod 从 /etc/ws73/ 加载。
     */

    /* plat_soc: 基础平台驱动，必须先加载 */
    if (system("modprobe plat_soc 2>/dev/null") != 0) {
        if (system("insmod /etc/ws73/plat_soc.ko 2>/dev/null") != 0) {
            printf("load plat_soc failed "
                       "(tried modprobe + insmod /etc/ws73/)\n");
            return AOV_ERR_FAILED;
        }
    }
    printf("plat_soc loaded\n");

    if (system("modprobe ble_soc 2>/dev/null") != 0) {
        system("insmod /etc/ws73/ble_soc.ko 2>/dev/null");
        /* ble_soc 可选，失败忽略 */
    }
    if (system("modprobe sle_soc 2>/dev/null") != 0) {
        system("insmod /etc/ws73/sle_soc.ko 2>/dev/null");
        /* sle_soc 可选，失败忽略 */
    }
    if (system("modprobe wifi_soc 2>/dev/null") != 0) {
        system("insmod /etc/ws73/wifi_soc.ko 2>/dev/null");
        /* wifi_soc 失败不阻断，仅记日志 */
        if (system("lsmod 2>/dev/null | grep -q wifi_soc") != 0)
            printf("load wifi_soc failed\n");
    }

    printf("WiFi modules loaded\n");
    return AOV_ERR_SUCCESS;
}

/* ======================== GPIO IRQ 唤醒检测 ======================== */

/*
 * GPIO IRQ 输入设备文件描述符（静态，模块内部使用）
 *
 * 通过 SAMPLE_COMM_AOV_Init/Deinit 管理生命周期。
 * 参考: sample_comm_aov.c g_input_device_fd
 */
static int g_input_device_fd = -1;

int aov_helper_init_gpio_irq(void)
{
    /*
     * INFO: 更好的做法是从 /proc/bus/input/devices 中查找事件设备路径。
     * 这里默认使用 /dev/input/event0。
     */
    g_input_device_fd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
    if (g_input_device_fd < 0) {
        printf("Open /dev/input/event0 failed: %s, you can ignore this"
               " msg if you don't need gpio wakeup...\n",
               strerror(errno));
        return AOV_ERR_IO;
    }

    printf("GPIO IRQ init done (fd=%d)\n", g_input_device_fd);
    return AOV_ERR_SUCCESS;
}

int aov_helper_deinit_gpio_irq(void)
{
    if (g_input_device_fd >= 0) {
        close(g_input_device_fd);
        g_input_device_fd = -1;
        printf("GPIO IRQ deinit done\n");
    }
    return AOV_ERR_SUCCESS;
}

/*
 * 获取 GPIO IRQ 状态 — 非阻塞检测是否有 GPIO 按键唤醒事件
 *
 * 读取 /dev/input/event0 中的 input_event 记录，
 * 检测 EV_KEY + KEY_POWER + value=1（按键按下）事件。
 *
 * 返回:
 *   1: 检测到 GPIO IRQ 唤醒事件
 *   0: 无事件或设备未初始化
 *
 * SDK 参考: SAMPLE_COMM_AOV_GetGpioIrqStat() — sample_comm_aov.c:1283-1307
 */
int aov_helper_get_gpio_irq_stat(void)
{
    int input_event_happened = 0;
    struct input_event event;

    if (g_input_device_fd < 0) {
        /* 设备未打开，不支持 GPIO 唤醒检测 */
        return 0;
    }

    /*
     * read() 使用 O_NONBLOCK 模式，无新事件时返回 EAGAIN。
     * 循环读取直到缓冲区清空，只要有一次 EV_KEY+KEY_POWER 按下就返回 true。
     */
    while (read(g_input_device_fd, &event, sizeof(event)) > 0) {
        if (event.type == EV_KEY && event.code == KEY_POWER && event.value == 1) {
            input_event_happened = 1;
        }
    }

    return input_event_happened;
}
