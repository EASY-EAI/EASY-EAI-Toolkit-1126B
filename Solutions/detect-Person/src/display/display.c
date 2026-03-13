/**
 *
 * Copyright 2025 by Guangzhou Easy EAI Technologny Co.,Ltd.
 * website: www.easy-eai.com
 *
 * Author: ZJH <zhongjiehao@easy-eai.com>
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * License file for more details.
 * 
 */

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* End of #ifdef __cplusplus */



#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/mman.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <rga/RgaApi.h>

#include "display.h"


// 定义BGR888格式（蓝-绿-红，每像素3字节）
#define BGR888_FORMAT 0x101  // 注意：这不是标准FourCC，可能需要适配

#define OVERLAY_WIDTH 480
#define OVERLAY_HEIGHT 640

typedef struct {
    RgaSURF_FORMAT fmt;
    int width;
    int height;
    int hor_stride;
    int ver_stride;
    int rotation;
    void *pBuf;
}RGA_Image;

typedef struct {
    uint32_t fbId;
    size_t fbSize;
    int fbStride;
    uint8_t *pFramebuffer;
}Plane_t;

typedef struct {
    int drmFd;
    int screenWidth;
    int screenHeight;
    int screenRefresh;
    Plane_t primary;
    Plane_t overlay;
    
}Display_dev_t;

static Display_dev_t g_Display ={0};
static pthread_mutex_t gmutex;

static int srcImg2dstImg(RGA_Image *pDst, RGA_Image *pSrc)
{
	rga_info_t src, dst;
	int ret = -1;

	if (!pSrc || !pDst) {
		printf("%s: NULL PTR!\n", __func__);
		return -1;
	}

    pthread_mutex_lock(&gmutex);
	//图像参数转换
	memset(&src, 0, sizeof(rga_info_t));
	src.fd = -1;
	src.virAddr = pSrc->pBuf;
	src.mmuFlag = 1;
	src.rotation =  pSrc->rotation;
	rga_set_rect(&src.rect, 0, 0, pSrc->width, pSrc->height, pSrc->hor_stride, pSrc->ver_stride, pSrc->fmt);

	memset(&dst, 0, sizeof(rga_info_t));
	dst.fd = -1;
	dst.virAddr = pDst->pBuf;
	dst.mmuFlag = 1;
	dst.rotation =  pDst->rotation;
	rga_set_rect(&dst.rect, 0, 0, pDst->width, pDst->height, pDst->hor_stride, pDst->ver_stride, pDst->fmt);
	if (c_RkRgaBlit(&src, &dst, NULL)) {
		printf("%s: rga fail\n", __func__);
		ret = -1;
	}
	else {
		ret = 0;
	}
    pthread_mutex_unlock(&gmutex);

	return ret;
}

// 查找第一个可用的连接器
static drmModeConnector *find_connector(int fd, drmModeRes *resources)
{
    drmModeConnector *connector = NULL;
    for (int i = 0; i < resources->count_connectors; i++) {
        connector = drmModeGetConnector(fd, resources->connectors[i]);
        if (!connector) {
            continue;
        }
        
        if (connector->connection == DRM_MODE_CONNECTED && connector->count_modes > 0) {
            break;
        }
        
        drmModeFreeConnector(connector);
        connector = NULL;
    }
    
    return connector;
}

// 查找编码器
static drmModeEncoder *find_encoder(int fd, drmModeRes *resources, drmModeConnector *connector)
{
    if (connector->encoder_id) {
        return drmModeGetEncoder(fd, connector->encoder_id);
    }
    return NULL;
}

// 创建primary帧缓冲器
static int create_primary_framebuffer(Display_dev_t *pDisplay)
{
    // 创建dumb buffer
    struct drm_mode_create_dumb create = {0};
    create.width = pDisplay->screenWidth;
    create.height = pDisplay->screenHeight;
    create.bpp = 24;  // BGR888: 24位每像素
    if (drmIoctl(pDisplay->drmFd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) {
        perror("创建dumb buffer失败");
        return -1;
    }
    
    // 映射buffer
    struct drm_mode_map_dumb map = {0};
    map.handle = create.handle;
    if (drmIoctl(pDisplay->drmFd, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0) {
        perror("映射dumb buffer失败");
        drmModeRmFB(pDisplay->drmFd, pDisplay->primary.fbId);
        return -1;
    }
    
    // 内存映射
    pDisplay->primary.pFramebuffer = mmap(0, create.size, PROT_READ | PROT_WRITE, MAP_SHARED, pDisplay->drmFd, map.offset);
    if (pDisplay->primary.pFramebuffer == MAP_FAILED) {
        perror("mmap失败");
        return -1;
    }
    
    // 创建帧缓冲器对象
    struct drm_mode_fb_cmd cmd = {0};
    cmd.width = create.width;
    cmd.height = create.height;
    //cmd.pixel_format = BGR888_FORMAT;
    cmd.bpp = 24;
    cmd.pitch = create.pitch;
    cmd.depth = 24;
    cmd.handle = create.handle;
    
    // 注意：这里使用XRGB格式，但我们会写入BGR数据
    // 对于真正的BGR支持，可能需要使用DRM_FORMAT_BGR888（如果驱动支持）
    if (drmModeAddFB(pDisplay->drmFd, cmd.width, cmd.height, 24, 24, cmd.pitch, cmd.handle, &pDisplay->primary.fbId) < 0) {
        perror("添加帧缓冲器失败");
        return -1;
    }
    
    pDisplay->primary.fbSize = create.size;
    pDisplay->primary.fbStride = create.pitch;
        
    return 0;
}

#if 0
// 创建叠加画面帧缓冲器
static int create_overlay_framebuffer(Display_dev_t *pDisplay)
{
    // 创建dumb buffer
    struct drm_mode_create_dumb create = {0};
    create.width = OVERLAY_WIDTH;
    create.height = OVERLAY_HEIGHT;
    create.bpp = 24;  // BGR888: 24位每像素
    if (drmIoctl(pDisplay->drmFd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) {
        perror("创建dumb buffer失败");
        return -1;
    }
    
    // 映射buffer
    struct drm_mode_map_dumb map = {0};
    map.handle = create.handle;
    if (drmIoctl(pDisplay->drmFd, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0) {
        perror("映射dumb buffer失败");
        drmModeRmFB(pDisplay->drmFd, pDisplay->overlay.fbId);
        return -1;
    }
    
    // 内存映射
    pDisplay->overlay.pFramebuffer = mmap(0, create.size, PROT_READ | PROT_WRITE, MAP_SHARED, pDisplay->drmFd, map.offset);
    if (pDisplay->overlay.pFramebuffer == MAP_FAILED) {
        perror("mmap失败");
        return -1;
    }
    
    // 创建帧缓冲器对象
    struct drm_mode_fb_cmd cmd = {0};
    cmd.width = create.width;
    cmd.height = create.height;
    //cmd.pixel_format = BGR888_FORMAT;
    cmd.bpp = 24;
    cmd.pitch = create.pitch;
    cmd.depth = 24;
    cmd.handle = create.handle;
    
    // 注意：这里使用XRGB格式，但我们会写入BGR数据
    // 对于真正的BGR支持，可能需要使用DRM_FORMAT_BGR888（如果驱动支持）
    if (drmModeAddFB(pDisplay->drmFd, cmd.width, cmd.height, 24, 24, cmd.pitch, cmd.handle, &pDisplay->overlay.fbId) < 0) {
        perror("添加帧缓冲器失败");
        return -1;
    }
    
    pDisplay->overlay.fbSize = create.size;
    pDisplay->overlay.fbStride = create.pitch;
        
    return 0;
}

// 查找合适的叠加平面
static uint32_t find_overlay_plane(int fd, uint32_t crtc_id)
{
    drmModePlaneRes *plane_res = drmModeGetPlaneResources(fd);
    if (!plane_res) return 0;
    
    for (int i = 0; i < plane_res->count_planes; i++) {
        drmModePlane *plane = drmModeGetPlane(fd, plane_res->planes[i]);
        if (!plane) continue;
        
        // 查找支持叠加的平面
        uint32_t possible_crtcs = plane->possible_crtcs;
        
        // 检查是否支持当前CRTC
        if ((possible_crtcs & (1 << crtc_id)) &&
            plane->crtc_id == 0) {  // 当前未使用
            uint32_t plane_id = plane->plane_id;
            drmModeFreePlane(plane);
            drmModeFreePlaneResources(plane_res);
            return plane_id;
        }
        
        drmModeFreePlane(plane);
    }
    
    drmModeFreePlaneResources(plane_res);
    return 0;
}

// 设置平面属性
static int set_plane_properties(int fd, uint32_t plane_id, uint32_t crtc_id,
                                uint32_t fb_id, int x, int y,
                                int src_w, int src_h, int crtc_w, int crtc_h)
{
    drmModePlane *plane = drmModeGetPlane(fd, plane_id);
    if (!plane) return -1;
    
    // 设置平面显示参数
    int ret = drmModeSetPlane(fd, plane_id, crtc_id, fb_id, 0,
                              x, y, crtc_w, crtc_h,
                              0, 0, src_w << 16, src_h << 16);
    
    drmModeFreePlane(plane);
    return ret;
}
#endif
// 初始化DRM
static int init_drm(Display_dev_t *pDisplay)
{
    // 打开第一个DRM设备
    pDisplay->drmFd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (pDisplay->drmFd < 0) {
        fprintf(stderr, "无法打开DRM设备 /dev/dri/card0\n");
        return -1;
    }
    
    // 获取资源
    drmModeRes *resources = drmModeGetResources(pDisplay->drmFd);
    if (!resources) {
        fprintf(stderr, "无法获取DRM资源\n");
        close(pDisplay->drmFd);
        return -1;
    }
    
    // 查找连接器
    drmModeConnector *connector = find_connector(pDisplay->drmFd, resources);
    if (!connector) {
        fprintf(stderr, "未找到可用的连接器\n");
        drmModeFreeResources(resources);
        close(pDisplay->drmFd);
        return -1;
    }
    
    // 使用第一个可用模式
    drmModeModeInfo *mode = &connector->modes[0];
    pDisplay->screenWidth = mode->hdisplay;
    pDisplay->screenHeight = mode->vdisplay;
    pDisplay->screenRefresh = mode->vrefresh;
    
    printf("显示模式: %dx%d@%dHz\n", 
           pDisplay->screenWidth, pDisplay->screenHeight, pDisplay->screenRefresh);
    
    // 查找编码器
    drmModeEncoder *encoder = find_encoder(pDisplay->drmFd, resources, connector);
    if (!encoder) {
        fprintf(stderr, "未找到编码器\n");
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(pDisplay->drmFd);
        return -1;
    }
    
    // 创建帧缓冲器
    if (create_primary_framebuffer(pDisplay) < 0) {
        fprintf(stderr, "创建帧缓冲器失败\n");
        drmModeFreeEncoder(encoder);
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(pDisplay->drmFd);
        return -1;
    }
    
    // 设置CRTC
    uint32_t crtc_id = encoder->crtc_id;
    uint32_t connector_id = connector->connector_id;
    int ret = drmModeSetCrtc(pDisplay->drmFd, crtc_id, pDisplay->primary.fbId, 0, 0, &connector_id, 1, mode);
    if (ret < 0) {
        fprintf(stderr, "设置CRTC失败: %s\n", strerror(-ret));
    }
#if 0
    // 创建叠加画面帧缓冲
    if (create_overlay_framebuffer(pDisplay) < 0) {
        fprintf(stderr, "创建叠加屏幕帧缓冲器失败\n");
        drmModeFreeEncoder(encoder);
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(pDisplay->drmFd);
        return -1;
    }
    // 查找并设置叠加平面
    uint32_t overlay_plane_id = find_overlay_plane(pDisplay->drmFd, crtc_id);
    if (overlay_plane_id) {
        printf("找到叠加平面: %u\n", overlay_plane_id);
        if (set_plane_properties(pDisplay->drmFd, overlay_plane_id, crtc_id, pDisplay->overlay.fbId,
                                20/*X_POS*/, 20/*Y_POS*/,
                                OVERLAY_WIDTH, OVERLAY_HEIGHT,
                                OVERLAY_WIDTH, OVERLAY_HEIGHT) < 0) {
            printf("警告：叠加平面设置失败，回退到软件合成\n");
        } else {
            printf("硬件叠加已启用\n");
        }
    } else {
        printf("未找到可用叠加平面，使用软件合成\n");
    }
#endif
    // 清理临时资源
    drmModeFreeEncoder(encoder);
    drmModeFreeConnector(connector);
    drmModeFreeResources(resources);
    
    return 0;
}

// 清理资源
static void unInit_drm(Display_dev_t *pDisplay)
{
    printf("\n清理资源...\n");
    if(NULL==pDisplay)
        return ;
    
    if (pDisplay->primary.pFramebuffer && pDisplay->primary.pFramebuffer != MAP_FAILED) {
        munmap(pDisplay->primary.pFramebuffer, pDisplay->primary.fbSize);
    }
    if (pDisplay->primary.fbId && pDisplay->drmFd >= 0) {
        drmModeRmFB(pDisplay->drmFd, pDisplay->primary.fbId);
    }

#if 0
    if (pDisplay->overlay.pFramebuffer && pDisplay->overlay.pFramebuffer != MAP_FAILED) {
        munmap(pDisplay->overlay.pFramebuffer, pDisplay->overlay.fbSize);
    }    
    if (pDisplay->overlay.fbId && pDisplay->drmFd >= 0) {
        drmModeRmFB(pDisplay->drmFd, pDisplay->overlay.fbId);
    }

#endif
    if (pDisplay->drmFd >= 0) {
        close(pDisplay->drmFd);
    }
}

int  disp_init(int *width, int *height)
{
    *width = 0;
    *height = 0;

    if (init_drm(&g_Display) < 0) {
        fprintf(stderr, "DRM初始化失败\n");
        return -1;
    }
    *width = g_Display.screenWidth;
    *height = g_Display.screenHeight;
    
    pthread_mutex_init(&gmutex, NULL);

    return 0;
}

void disp_exit(void)
{
    pthread_mutex_destroy(&gmutex);
    
    unInit_drm(&g_Display);
}

void disp_commit(void *ptr, int width, int height, int rot)
{
    // 更新[视频]通道图像数据
    RGA_Image srcImage, dstImage;
    memset(&srcImage, 0, sizeof(srcImage));
    memset(&dstImage, 0, sizeof(dstImage));

    srcImage.fmt = RK_FORMAT_BGR_888;
    srcImage.width = width;
    srcImage.height = height;
    srcImage.hor_stride = width;
    srcImage.ver_stride = height;
    srcImage.rotation = rot;
    srcImage.pBuf = ptr;

    dstImage.fmt = RK_FORMAT_BGR_888;
    dstImage.width = g_Display.screenWidth;
    dstImage.height = g_Display.screenHeight;
    dstImage.hor_stride = g_Display.primary.fbStride/3;
    dstImage.ver_stride = g_Display.screenHeight;
    dstImage.rotation = HAL_TRANSFORM_ROT_0;
    dstImage.pBuf = (void *)g_Display.primary.pFramebuffer;

    srcImg2dstImg(&dstImage, &srcImage);
}



#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */
