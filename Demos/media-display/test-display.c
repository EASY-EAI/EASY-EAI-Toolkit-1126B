#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "display.h"

/* 读取固定的BGR测试图像，成功返回已分配缓冲区，失败返回NULL */
static char *load_test_image(size_t *out_size)
{
#define IMAGE_PATH "720X1280.bgr"
#define IMGRATIO   3
#define IMAGE_SIZE (720 * 1280 * IMGRATIO)
    char *pbuf = (char *)malloc(IMAGE_SIZE);
    if (!pbuf) {
        printf("malloc error: %s, %d\n", __func__, __LINE__);
        return NULL;
    }

    FILE *fp = fopen(IMAGE_PATH, "r");
    if (!fp) {
        printf("fopen error: %s, %d\n", __func__, __LINE__);
        free(pbuf);
        return NULL;
    }

    int ret = fread(pbuf, 1, IMAGE_SIZE, fp);
    fclose(fp);
    if (ret != IMAGE_SIZE) {
        printf("fread error: %s, %d\n", __func__, __LINE__);
        free(pbuf);
        return NULL;
    }

    if (out_size) {
        *out_size = IMAGE_SIZE;
    }
    return pbuf;
}

int main(int argc, char *argv[])
{
    if(disp_init()){
        fprintf(stderr, "display区域始化失败\n");
        return -2;        
    }
    printf("成功初始化display区域，正在显示BGR测试图像...\n");
    
    // 加载图像数据
    size_t image_size = 0;
    char *pbuf = load_test_image(&image_size);
    if (!pbuf) {
        return -1;
    }

    // 把图像显示在窗口里[采用拉伸缩放]
    window_commit(pbuf, 720, 1280, 0);
    printf("按Ctrl+C退出\n");
    
    // 保持显示
    while (1) {
        // 可以考虑在这里更新图像内容
        sleep(1);
    }

    free(pbuf);
    // disp_release(); //disp_exit();里已经包含了disp_release();动作

    disp_exit();
    
    return 0;
}
