#ifndef __DATA_QUEUE_H__
#define __DATA_QUEUE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

typedef void *DataQueueHandle;

/*
 * scatter-gather 数据段描述符
 */
typedef struct {
    const void *ptr;
    unsigned int len;
} DataQueueScatter_t;

/*
 * 创建队列 (预分配池，零碎片)
 *   max_items:     最大容量
 *   max_item_size: 单条数据最大长度 (字节)
 *   return:        成功返回句柄，失败返回 NULL
 */
DataQueueHandle DataQueue_Create(int max_items, int max_item_size);

/*
 * 存入数据 (阻塞，队列满时等待)
 *   handle: 队列句柄
 *   data:   数据指针 (内部 memcpy 到预分配池)
 *   size:   数据长度 (字节)，必须 <= max_item_size
 *   return: 0 成功，-1 失败
 */
int DataQueue_Push(DataQueueHandle handle, const void *data, unsigned int size);

/*
 * 分片存入 (将多段不连续数据拼成一段连续 buffer 入队，阻塞)
 *
 *   典型用法 — 帧元数据头 + 编码帧 payload 零拷贝入队：
 *     DataQueueScatter_t segs[2] = {
 *         { &meta, sizeof(meta) },          // 栈上 FrameMeta_t
 *         { venc_data, venc_data_len },     // VENC 输出指针
 *     };
 *     DataQueue_PushScatter(q, segs, 2);
 *
 *   handle:  队列句柄
 *   segs:    数据段数组 (指针 + 长度)
 *   seg_cnt: 段数
 *   return:  0 成功，-1 失败 (总长度超出 max_item_size 等)
 */
int DataQueue_PushScatter(DataQueueHandle handle, const DataQueueScatter_t *segs, int seg_cnt);

/*
 * 取出数据 (指针来自预分配池，不支持跨线程同时使用同一个 data)
 *   handle:     队列句柄
 *   data:       [出] 数据指针 (预分配池中的地址)
 *   size:       [出] 数据长度 (字节)
 *   timeout_ms: -1 阻塞等待，0 非阻塞，>0 超时等待 (ms)
 *   return:     >0 成功，0 超时/无数据，<0 错误
 */
int DataQueue_Pop(DataQueueHandle handle, void **data, unsigned int *size, int timeout_ms);

/*
 * 归还 Pop 取出的 data 到预分配池 (替代 free)
 *   handle: 队列句柄
 *   data:   Pop 返回的 data 指针
 *   return: 0 成功，-1 失败
 */
int DataQueue_Release(DataQueueHandle handle, void *data);

/*
 * 查询当前队列元素数
 */
int DataQueue_Count(DataQueueHandle handle);

bool DataQueue_IsFull(DataQueueHandle handle);
bool DataQueue_IsEmpty(DataQueueHandle handle);

/*
 * 清空队列 (归还所有数据到预分配池)
 */
int DataQueue_Flush(DataQueueHandle handle);

/*
 * 销毁队列 (释放预分配池)
 */
int DataQueue_Destroy(DataQueueHandle handle);

#ifdef __cplusplus
}
#endif
#endif
