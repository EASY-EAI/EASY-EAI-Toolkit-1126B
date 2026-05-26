/*
 * SD 卡挂载/卸载模块 (RV1126B)
 * 通过 netlink 监听内核 uevent 完成驱动绑定/解绑，再对分区进行 mount/umount
 */

#include "sdcard.h"
#include "aov_helper.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <linux/netlink.h>
#include <unistd.h>

/* ======================== 平台参数 (RV1126B) ======================== */
#define MAX_LINE_SIZE       256
#define MAX_NL_BUF_SIZE     (1024 * 16)
#define SDCARD_MOUNT_PATH   "/mnt/sdcard"
#define SDCARD_DRIVER       "/sys/bus/platform/drivers/dwmmc_rockchip"

#define MOUNT_DEV_1         "/dev/mmcblk1p1"
#define MOUNT_DEV_2         "/dev/mmcblk1"

#define MAX_SELECT_TIMEOUT  (5 * 1000 * 1000)
#define SDCARD_DEVICE               "21d60000.mmc"
#define SDCARD_DRIVER_PREPARED      "bind@/devices/platform/21d60000.mmc"
#define SDCARD_BIND_DONE    "bind@/devices/platform/21d60000.mmc/mmc_host/mmc1/mmc1"
#define SDCARD_UNBIND_DONE  "unbind@/devices/platform/21d60000.mmc"

/* ======================== 驱动绑定/解绑 助手函数 ======================== */

static bool device_driver_is_bound(const char *device, const char *driver)
{
    char path[256] = {'\0'};
    snprintf(path, 256, "%s/%s", driver, device);
    return (access(path, F_OK) == 0);
}

static int device_attach_driver(const char *device, const char *driver)
{
    char path[256] = {'\0'};
    int fd;
    int ret = 0;

    snprintf(path, sizeof(path), "%s/bind", driver);
    fd = open(path, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        printf("[SDCARD] can't open %s, errno = %s\n", driver, strerror(errno));
        return -1;
    }
    printf("[SDCARD] start bind %s to %s\n", device, driver);

    ret = write(fd, device, strlen(device));
    if (ret < 0) {
        printf("[SDCARD] bind %s to %s failed, errno = %s\n",
               device, driver, strerror(errno));
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

static int device_detach_driver(const char *device, const char *driver)
{
    char path[256] = {'\0'};
    int fd, ret;

    snprintf(path, sizeof(path), "%s/unbind", driver);
    fd = open(path, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        printf("[SDCARD] can't open %s, errno = %s\n", path, strerror(errno));
        return -1;
    }

    printf("[SDCARD] start unbind %s from %s\n", device, driver);
    ret = write(fd, device, strlen(device));
    if (ret < 0) {
        printf("[SDCARD] unbind %s from %s failed, errno = %s\n",
               device, driver, strerror(errno));
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

static bool detect_sdcard_is_enable(void)
{
    int value = 0;
    int addr = 0x21D60050;

    if (Aov_ReadReg(addr, (unsigned char *)&value, sizeof(value)) != 0)
        return false;
    return (value == 0);
}

/* ======================== SD 卡绑定/解绑 ======================== */

static int bind_sdcard(void)
{
    int ret, fd;
    char buf[MAX_NL_BUF_SIZE];
    fd_set read_set;
    struct timeval timeout;
    struct sockaddr_nl addr;

    memset(&buf, 0, sizeof(buf));

    if (device_driver_is_bound(SDCARD_DEVICE, SDCARD_DRIVER)) {
        printf("[SDCARD] device already bound\n");
        return 0;
    }

    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = NETLINK_KOBJECT_UEVENT;
    addr.nl_pid = 0;

    fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_KOBJECT_UEVENT);
    if (fd < 0) {
        printf("[SDCARD] Failed to open netlink, errno = %s\n", strerror(errno));
        return -1;
    }
    if (bind(fd, (struct sockaddr *)(&addr), sizeof(addr)) != 0) {
        printf("[SDCARD] bind netlink addr failed, errno = %s\n", strerror(errno));
        goto fail;
    }

    FD_ZERO(&read_set);
    FD_SET(fd, &read_set);
    timeout.tv_sec = 0;
    timeout.tv_usec = MAX_SELECT_TIMEOUT;

    if (device_attach_driver(SDCARD_DEVICE, SDCARD_DRIVER) != 0)
        goto fail;

retry:
    ret = select(fd + 1, &read_set, NULL, NULL, &timeout);
    if (ret > 0) {
        memset(&buf, 0, sizeof(buf));
        read(fd, buf, sizeof(buf));
        buf[MAX_NL_BUF_SIZE - 1] = '\0';
        if (strncmp(buf, SDCARD_DRIVER_PREPARED, strlen(SDCARD_DRIVER_PREPARED)) == 0) {
            detect_sdcard_is_enable();
        }
        if (strncmp(buf, SDCARD_BIND_DONE, strlen(SDCARD_BIND_DONE)) == 0) {
            printf("[SDCARD] Bind success: %s\n", buf);
            goto success;
        }
        goto retry;
    }
    printf("[SDCARD] select error = %s\n", strerror(errno));
    goto fail;

success:
    close(fd);
    return 0;
fail:
    close(fd);
    return -1;
}

static int unbind_sdcard(void)
{
    int ret, fd;
    char buf[MAX_NL_BUF_SIZE];
    fd_set read_set;
    struct timeval timeout;
    struct sockaddr_nl addr;

    memset(&buf, 0, sizeof(buf));

    if (!device_driver_is_bound(SDCARD_DEVICE, SDCARD_DRIVER)) {
        printf("[SDCARD] device already unbind!\n");
        return 0;
    }

    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = NETLINK_KOBJECT_UEVENT;
    addr.nl_pid = 0;

    fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_KOBJECT_UEVENT);
    if (fd < 0) {
        printf("[SDCARD] Failed to open netlink, errno = %s\n", strerror(errno));
        return -1;
    }
    if (bind(fd, (struct sockaddr *)(&addr), sizeof(addr)) != 0) {
        printf("[SDCARD] bind netlink addr failed, errno = %s\n", strerror(errno));
        goto fail;
    }

    FD_ZERO(&read_set);
    FD_SET(fd, &read_set);
    timeout.tv_sec = 0;
    timeout.tv_usec = MAX_SELECT_TIMEOUT;

    if (device_detach_driver(SDCARD_DEVICE, SDCARD_DRIVER) != 0)
        goto fail;

retry:
    ret = select(fd + 1, &read_set, NULL, NULL, &timeout);
    if (ret > 0) {
        memset(&buf, 0, sizeof(buf));
        read(fd, buf, sizeof(buf));
        buf[MAX_NL_BUF_SIZE - 1] = '\0';
        if (strcmp(buf, SDCARD_UNBIND_DONE) == 0) {
            printf("[SDCARD] Unbind success: %s\n", buf);
            goto success;
        }
        goto retry;
    }
    printf("[SDCARD] select error %s\n", strerror(errno));
    goto fail;

success:
    close(fd);
    return 0;
fail:
    close(fd);
    return -1;
}

/* ======================== 挂载检查 ======================== */

static int check_sdcard_mount(void)
{
    int fd, pos = 0;
    char line[MAX_LINE_SIZE];
    ssize_t bytesRead;
    int ret = -1;

    fd = open("/proc/mounts", O_RDONLY);
    if (fd == -1) {
        printf("[SDCARD] Error opening /proc/mounts\n");
        return ret;
    }

    while ((bytesRead = read(fd, &line[pos], 1)) > 0) {
        if (line[pos] == '\n') {
            line[pos] = '\0';
            if (strstr(line, SDCARD_MOUNT_PATH)) {
                printf("[SDCARD] Found '%s' in line: %s\n", SDCARD_MOUNT_PATH, line);
                ret = 0;
                break;
            }
            pos = 0;
        } else {
            pos++;
            if (pos >= MAX_LINE_SIZE - 1)
                pos = 0;
        }
    }

    close(fd);
    return ret;
}

/* ======================== 公开接口 ======================== */

int MountSdcard(void)
{
    int ret = 0;

    printf("[SDCARD] Enter mount\n");

    bind_sdcard();

    if (check_sdcard_mount() == 0) {
        printf("[SDCARD] already mount\n");
        return 0;
    }

    if (access(MOUNT_DEV_1, F_OK) == 0) {
        ret = mount(MOUNT_DEV_1, SDCARD_MOUNT_PATH, "vfat", 0, NULL);
        if (ret != 0)
            printf("[SDCARD] mount failed, errno = %s\n", strerror(errno));
        else
            printf("[SDCARD] mount success\n");
    } else if (access(MOUNT_DEV_2, F_OK) == 0) {
        ret = mount(MOUNT_DEV_2, SDCARD_MOUNT_PATH, "vfat", 0, NULL);
        if (ret != 0)
            printf("[SDCARD] mount failed, errno = %s\n", strerror(errno));
        else
            printf("[SDCARD] mount success\n");
    } else {
        printf("[SDCARD] bad mount path!\n");
    }

    if (check_sdcard_mount() != 0) {
        printf("[SDCARD] Not found mount sdcard on %s\n", SDCARD_MOUNT_PATH);
        unbind_sdcard();
        ret = -1;
    }

    printf("[SDCARD] Exit mount\n");
    return ret;
}

int UmountSdcard(void)
{
    int ret = 0;

    printf("[SDCARD] Enter umount\n");

    if (check_sdcard_mount() != 0) {
        printf("[SDCARD] already umount\n");
        return 0;
    }

    ret = umount2(SDCARD_MOUNT_PATH, MNT_DETACH);
    if (ret == 0)
        printf("[SDCARD] unmount success\n");
    else
        printf("[SDCARD] unmount failed because %s\n", strerror(errno));

    ret = unbind_sdcard();
    if (ret == 0)
        printf("[SDCARD] UnbindSdcard success\n");
    else
        printf("[SDCARD] UnbindSdcard failed because %s\n", strerror(errno));

    printf("[SDCARD] Exit umount\n");
    return ret;
}
