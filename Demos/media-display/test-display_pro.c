#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <rga/RgaApi.h>
#include "rga_wrapper.h" //要实现显示提速，需要用到dma内存，以及rga的0拷贝功能

#include "display_pro.h"

#define IMAGE_PATH "720X1280.bgr"
#define IMG_WIDTH  720
#define IMG_HEIGHT 1280
#define IMG_BPP    3
#define IMAGE_SIZE ((size_t)IMG_WIDTH * IMG_HEIGHT * IMG_BPP)

#define NUM_WINDOWS 3

/**
 * 分配 DMA-BUF，将 720X1280 BGR888 测试图读入（mmap 地址），供 window_commit_pro 零拷贝使用。
 * 成功返回 0，并设置 *dmabuf_fd、*pBuf_Map、*out_size；失败返回 -1。
 */
static int load_test_image(int *dmabuf_fd, void **pBuf_Map, size_t *out_size)
{
	int fd = -1;
	void *pBuffer = NULL;
	if (alloc_dmabuf(IMAGE_SIZE, &fd, &pBuffer) != 0) {
		fprintf(stderr, "alloc_dmabuf failed (%zu bytes)\n", IMAGE_SIZE);
		return -1;
	}

	FILE *fp = fopen(IMAGE_PATH, "rb");
	if (!fp) {
		perror(IMAGE_PATH);
		munmap(pBuffer, IMAGE_SIZE);
		close(fd);
		return -1;
	}

	if (fread(pBuffer, 1, IMAGE_SIZE, fp) != IMAGE_SIZE) {
		fprintf(stderr, "fread %s: expected %zu bytes\n", IMAGE_PATH, IMAGE_SIZE);
		fclose(fp);
		munmap(pBuffer, IMAGE_SIZE);
		close(fd);
		return -1;
	}
	fclose(fp);

	if (dma_sync_cpu_to_device(fd) != 0)
		perror("dma_sync_cpu_to_device");

	*dmabuf_fd = fd;
	*pBuf_Map = pBuffer;
	if (out_size)
		*out_size = IMAGE_SIZE;
	return 0;
}

int main(int argc, char *argv[])
{
	int sw = 0, sh = 0, refresh = 0;
	display_dmabuf_frame_t frame;

	(void)argc;
	(void)argv;
	// 1.分配好dma内存，方便后续显示时零拷贝使用，并提前加载好测试图像
	int dmabuf_fd = -1;
	void *img_va = NULL;
	size_t img_size = 0;
	if (load_test_image(&dmabuf_fd, &img_va, &img_size)) {
		return -1;
	}
	
	// 2.初始化屏幕硬件，并创建display区域
	if (0 != screen_init()){
		goto exit;
	}
	if (0 == screen_info(&sw, &sh, &refresh)){
		display_t display_dev = {0, 0, sw, sh};
		if (0 != disp_init_pro(&display_dev)) {
			goto exit2;
		}
	}

	for (int i = 0; i < NUM_WINDOWS; i++) {
		window_t w;
		memset(&w, 0, sizeof(w));
		w.zpos = i;
		if(i == 0){
			w.win_x = 300;
			w.win_y = 400;
		}else if(i == 1){
			w.win_x = 200;
			w.win_y = 150;
		}else if(i == 2){
			w.win_x = 50;
			w.win_y = 300;
		}
		w.win_w = 360;
		w.win_h = 640;

		if (add_window_to(DISPLAY, &w) < 0) {
			fprintf(stderr, "add_window_to DISPLAY chn %d failed\n", i);
		}
	}

	memset(&frame, 0, sizeof(frame));
	frame.dmabuf_fd = dmabuf_fd;
	frame.width = IMG_WIDTH;
	frame.height = IMG_HEIGHT;
	frame.pitch_bytes = IMG_WIDTH * IMG_BPP;
	frame.rotation = 0;//HAL_TRANSFORM_ROT_270;
	frame.rga_format = RK_FORMAT_BGR_888;
	for (int i = 0; i < NUM_WINDOWS; i++) {
		if (window_commit_pro(i, &frame) != 0)
			fprintf(stderr, "window_commit_pro(%d) failed\n", i);
	}
    window_commit_pro(0, &frame); //--用于确认显示顺序是否按预期进行

	if (window_refresh_pro() != 0)
		perror("window_refresh_pro");

	printf("按 Ctrl+C 退出\n");
    
	while (1){
		sleep(1);
	}

	for (int i = 0; i < NUM_WINDOWS; i++) {
		remove_window_from(DISPLAY, i);
	}
	disp_release_pro();
exit2:
	screen_exit();
exit:
	munmap(img_va, img_size);
	close(dmabuf_fd);

	return 0;
}
