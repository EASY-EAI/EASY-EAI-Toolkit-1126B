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

#ifndef DISPLAY_H
#define DISPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


// 屏幕操作接口:
int  screen_init(void);
int  screen_info(int *width, int *height, int *refresh);
void screen_exit(void);
void set_uiLayer_on_top(bool onTop);
void set_alpha_blend_mode(int mode); // [0:premultiplied, 1:Non-premultiplied]

// 显示区域操作接口:
typedef struct {
	int x_off;
	int y_off;
    int width;
	int height;
} display_t;
int disp_init(display_t *pDisplay);
int uiLayer_init(display_t *pDisplay);
void disp_release();
void uiLayer_release();

// windows操作接口:
void window_commit(void *ptr/*default BGR888*/, int imgWidth, int imgHeight);
void uiLayer_commit(void *ptr/*default BGR888*/, int imgWidth, int imgHeight);

#ifdef __cplusplus
}
#endif
#endif

