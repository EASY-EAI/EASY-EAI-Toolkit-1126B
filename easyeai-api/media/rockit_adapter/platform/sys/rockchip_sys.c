#include "rockchip_sys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "rk_mpi_sys.h"

static pthread_mutex_t g_sys_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_sys_ref_count = 0;

int rockchip_sys_init(void)
{
    int ret = 0;
    pthread_mutex_lock(&g_sys_lock);
    if (g_sys_ref_count == 0) {
        ret = RK_MPI_SYS_Init();
        if (ret != RK_SUCCESS) {
            printf("[CAM_ADAPTER] RK_MPI_SYS_Init failed: %x\n", ret);
            pthread_mutex_unlock(&g_sys_lock);
            return -1;
        }
    }
    g_sys_ref_count++;
    pthread_mutex_unlock(&g_sys_lock);
    return 0;
}

void rockchip_sys_exit(void)
{
    pthread_mutex_lock(&g_sys_lock);
    if (g_sys_ref_count > 0) {
        g_sys_ref_count--;
        if (g_sys_ref_count == 0)
            RK_MPI_SYS_Exit();
    }
    pthread_mutex_unlock(&g_sys_lock);
}
