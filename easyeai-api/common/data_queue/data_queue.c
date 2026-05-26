#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "data_queue.h"

typedef struct {
    int              slot;   /* 当前槽位在 items 数组中索引，push/pop 用 */
    unsigned char   *data;   /* 指向 pool 中的地址 */
    unsigned int     size;   /* 实际数据长度 */
} QueueItem_t;

struct _DataQueue {
    int max_items;
    int max_item_size;
    int count;
    QueueItem_t *items;
    int head;
    int tail;

    /* 预分配池 */
    unsigned char *pool;
    int pool_total;          /* ~= max_items，但用 free_stack 管理 */
    int pool_free_top;       /* 空闲指针栈顶 */
    int *pool_free_slots;    /* 空闲 index 栈，对应 pool 中的块 */

    pthread_mutex_t lock;
    sem_t sem_push;          /* 可用 pool 块数 */
    sem_t sem_pop;           /* 入队数据数 */
};

/*
 * 从预分配池取一块空闲 buffer
 */
static unsigned char *pool_alloc(struct _DataQueue *q)
{
    if (q->pool_free_top < 0)
        return NULL;
    int idx = q->pool_free_slots[q->pool_free_top--];
    return q->pool + (unsigned int)idx * q->max_item_size;
}

/*
 * 归还 buffer 到预分配池
 */
static void pool_free(struct _DataQueue *q, unsigned char *ptr)
{
    int idx = (int)((ptr - q->pool) / q->max_item_size);
    q->pool_free_slots[++q->pool_free_top] = idx;
}

DataQueueHandle DataQueue_Create(int max_items, int max_item_size)
{
    struct _DataQueue *q;

    if (max_items <= 0 || max_item_size <= 0)
        return NULL;

    q = (struct _DataQueue *)calloc(1, sizeof(struct _DataQueue));
    if (!q)
        return NULL;

    q->max_items = max_items;
    q->max_item_size = max_item_size;
    q->count = 0;
    q->head = 0;
    q->tail = 0;

    q->items = (QueueItem_t *)calloc(max_items, sizeof(QueueItem_t));
    if (!q->items)
        goto fail;

    q->pool = (unsigned char *)malloc((size_t)max_items * max_item_size);
    if (!q->pool)
        goto fail;

    q->pool_total = max_items;
    q->pool_free_slots = (int *)calloc(max_items, sizeof(int));
    if (!q->pool_free_slots)
        goto fail;

    for (int i = 0; i < max_items; i++)
        q->pool_free_slots[i] = max_items - 1 - i;
    q->pool_free_top = max_items - 1;

    pthread_mutex_init(&q->lock, NULL);
    sem_init(&q->sem_push, 0, max_items);
    sem_init(&q->sem_pop, 0, 0);

    return (DataQueueHandle)q;

fail:
    if (q->pool_free_slots) free(q->pool_free_slots);
    if (q->pool) free(q->pool);
    if (q->items) free(q->items);
    free(q);
    return NULL;
}

int DataQueue_Push(DataQueueHandle handle, const void *data, unsigned int size)
{
    struct _DataQueue *q = (struct _DataQueue *)handle;

    if (!q || !data || size == 0 || size > (unsigned int)q->max_item_size)
        return -1;

    sem_wait(&q->sem_push);

    pthread_mutex_lock(&q->lock);

    unsigned char *buf = pool_alloc(q);
    if (!buf) {
        pthread_mutex_unlock(&q->lock);
        sem_post(&q->sem_push);
        return -1;
    }

    memcpy(buf, data, size);
    q->items[q->tail].slot = q->tail;
    q->items[q->tail].data = buf;
    q->items[q->tail].size = size;
    q->tail = (q->tail + 1) % q->max_items;
    q->count++;

    pthread_mutex_unlock(&q->lock);
    sem_post(&q->sem_pop);
    return 0;
}

int DataQueue_PushScatter(DataQueueHandle handle, const DataQueueScatter_t *segs, int seg_cnt)
{
    struct _DataQueue *q = (struct _DataQueue *)handle;
    unsigned int total = 0;

    if (!q || !segs || seg_cnt <= 0)
        return -1;

    for (int i = 0; i < seg_cnt; i++)
        total += segs[i].len;

    if (total == 0 || total > (unsigned int)q->max_item_size)
        return -1;

    sem_wait(&q->sem_push);

    pthread_mutex_lock(&q->lock);

    unsigned char *buf = pool_alloc(q);
    if (!buf) {
        pthread_mutex_unlock(&q->lock);
        sem_post(&q->sem_push);
        return -1;
    }

    unsigned char *dst = buf;
    for (int i = 0; i < seg_cnt; i++) {
        memcpy(dst, segs[i].ptr, segs[i].len);
        dst += segs[i].len;
    }

    q->items[q->tail].data = buf;
    q->items[q->tail].size = total;
    q->tail = (q->tail + 1) % q->max_items;
    q->count++;

    pthread_mutex_unlock(&q->lock);
    sem_post(&q->sem_pop);
    return 0;
}

int DataQueue_Pop(DataQueueHandle handle, void **data, unsigned int *size, int timeout_ms)
{
    struct _DataQueue *q = (struct _DataQueue *)handle;
    struct timespec ts;
    int ret;

    if (!q || !data || !size)
        return -1;

    if (timeout_ms > 0) {
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += (timeout_ms % 1000) * 1000000;
        ts.tv_sec += timeout_ms / 1000 + ts.tv_nsec / 1000000000;
        ts.tv_nsec %= 1000000000;
        while ((ret = sem_timedwait(&q->sem_pop, &ts)) == -1 && errno == EINTR)
            continue;
    } else if (timeout_ms == 0) {
        while ((ret = sem_trywait(&q->sem_pop)) == -1 && errno == EINTR)
            continue;
    } else {
        while ((ret = sem_wait(&q->sem_pop)) == -1 && errno == EINTR)
            continue;
    }

    if (ret != 0)
        return (errno == ETIMEDOUT || errno == EAGAIN) ? 0 : -1;

    pthread_mutex_lock(&q->lock);

    *data = q->items[q->head].data;
    *size = q->items[q->head].size;
    q->items[q->head].data = NULL;
    q->items[q->head].size = 0;
    q->head = (q->head + 1) % q->max_items;
    q->count--;

    pthread_mutex_unlock(&q->lock);
    return 1;
}

int DataQueue_Release(DataQueueHandle handle, void *data)
{
    struct _DataQueue *q = (struct _DataQueue *)handle;

    if (!q || !data)
        return -1;

    pthread_mutex_lock(&q->lock);
    pool_free(q, (unsigned char *)data);
    pthread_mutex_unlock(&q->lock);

    sem_post(&q->sem_push);
    return 0;
}

int DataQueue_Count(DataQueueHandle handle)
{
    struct _DataQueue *q = (struct _DataQueue *)handle;
    if (!q)
        return 0;

    int count;
    pthread_mutex_lock(&q->lock);
    count = q->count;
    pthread_mutex_unlock(&q->lock);
    return count;
}

bool DataQueue_IsFull(DataQueueHandle handle)
{
    return DataQueue_Count(handle) >= ((struct _DataQueue *)handle)->max_items;
}

bool DataQueue_IsEmpty(DataQueueHandle handle)
{
    return DataQueue_Count(handle) == 0;
}

int DataQueue_Flush(DataQueueHandle handle)
{
    struct _DataQueue *q = (struct _DataQueue *)handle;
    void *item_data = NULL;
    unsigned int item_size = 0;

    if (!q)
        return -1;

    while (DataQueue_Pop(q, &item_data, &item_size, 0) > 0)
        DataQueue_Release(handle, item_data);

    return 0;
}

int DataQueue_Destroy(DataQueueHandle handle)
{
    struct _DataQueue *q = (struct _DataQueue *)handle;

    if (!q)
        return -1;

    sem_destroy(&q->sem_push);
    sem_destroy(&q->sem_pop);
    pthread_mutex_destroy(&q->lock);

    if (q->pool_free_slots) free(q->pool_free_slots);
    if (q->pool) free(q->pool);
    if (q->items) free(q->items);
    free(q);
    return 0;
}
