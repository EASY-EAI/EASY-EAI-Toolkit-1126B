/**
 * Copyright 2023 by Guangzhou Easy EAI Technologny Co.,Ltd.
 * website: www.easy-eai.com
 *
 * VCIC (Vehicle Camera Interface Consortium) — ISO 17215
 * 车载摄像头接口库公共 API
 */

#ifndef VCIC_H
#define VCIC_H

#include <stdint.h>
#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* ========================================================================
 *  常量定义
 * ======================================================================== */

/* 服务 / 方法 / 事件 ID */
#define VCIC_SERVICE_ID             0x0101
#define VCIC_INSTANCE_ID            0x0001

#define VCIC_METHOD_START_STREAM    0x0001
#define VCIC_METHOD_STOP_STREAM     0x0002
#define VCIC_METHOD_SET_PARAM       0x0003
#define VCIC_METHOD_GET_PARAM       0x0004

#define VCIC_EVENT_FRAME_LOST       0x8001
#define VCIC_EVENT_CAM_STATUS       0x8002

/* 参数 ID（SET_PARAM / GET_PARAM 使用） */
#define VCIC_PARAM_RESOLUTION       0x0001   /* uint16_t width, uint16_t height */
#define VCIC_PARAM_FRAMERATE        0x0002   /* uint8_t fps */
#define VCIC_PARAM_BITRATE          0x0003   /* uint32_t bps */
#define VCIC_PARAM_FLIP             0x0004   /* uint8_t: 0=none, 1=H, 2=V, 3=HV */
#define VCIC_PARAM_CODEC            0x0005   /* uint8_t: 1=H.264, 2=H.265 */
#define VCIC_PARAM_QUALITY          0x0006   /* uint8_t: 1~10 */

/* 编解码格式 */
#define VCIC_CODEC_H264             0x01
#define VCIC_CODEC_H265             0x02

/* 摄像头状态码 */
#define VCIC_STATUS_OK              0x00
#define VCIC_STATUS_ERROR           0x01
#define VCIC_STATUS_LOST_FRAME      0x02
#define VCIC_STATUS_RECOVERED       0x03

/* 默认应用端口 */
#define VCIC_APP_PORT               30491

/* ========================================================================
 *  配置结构体
 * ======================================================================== */

/* 摄像头侧配置（SoC 端：提供视频流） */
typedef struct {
    char     ifname[16];           /* 网络接口，如 "eth0" */
    char     local_ip[16];         /* 本机 IP，空串则自动选择 */
    uint16_t service_id;           /* 服务 ID，通常 VCIC_SERVICE_ID */
    uint16_t instance_id;          /* 实例 ID，通常 VCIC_INSTANCE_ID */
    uint16_t app_port;             /* 应用端口，通常 VCIC_APP_PORT */
    uint8_t  stream_id[8];         /* 数据流 ID */
    uint8_t  dst_mac[6];           /* 组播目标 MAC */
    uint8_t  codec;                /* VCIC_CODEC_H264 / VCIC_CODEC_H265 */
} VCIC_ServerConfig_t;

/* 域控侧配置（Host 端：接收视频流） */
typedef struct {
    char     ifname[16];           /* 网络接口 */
    char     local_ip[16];         /* 本机 IP */
    uint16_t service_id;           /* 服务 ID */
    uint16_t instance_id;          /* 实例 ID */
    uint16_t local_port;           /* 本地端口 */
    char     server_ip[16];        /* 摄像头 SoC IP */
    uint16_t server_port;          /* 摄像头 SoC 应用端口 */
    uint8_t  stream_id[8];         /* 数据流 ID（需与摄像头侧一致） */
    uint8_t  multicast_mac[6];     /* 组播 MAC（需与摄像头侧一致） */
} VCIC_ClientConfig_t;

/* ========================================================================
 *  回调函数类型
 * ======================================================================== */

/* 视频帧回调（域控侧收到视频帧时调用） */
typedef int32_t (*VCIC_Frame_CB)(void *pObj, const uint8_t *data, uint32_t len,
                                  uint32_t avtp_ts, uint8_t isLastFrag,
                                  uint8_t codec);

/* 事件回调（帧丢失 / 摄像头状态变化） */
typedef int32_t (*VCIC_Event_CB)(void *pObj, uint16_t eventId,
                                  const uint8_t *data, uint32_t len);

/* 视频帧数据源回调（摄像头侧提供视频数据） */
typedef int32_t (*VCIC_Feed_CB)(void *pObj, uint8_t **data, uint32_t *len,
                                 uint8_t *isLastFrag);

/* ========================================================================
 *  摄像头侧 API（Server — SoC 端）
 * ======================================================================== */

typedef struct VCIC_Server VCIC_Server;

/**
 * 创建摄像头侧 VCIC 服务实例
 *
 * @cfg : 配置参数
 * 返回：非 NULL 成功，NULL 失败
 */
VCIC_Server *VCIC_server_create(const VCIC_ServerConfig_t *cfg);

/**
 * 销毁服务实例，释放所有资源
 */
void VCIC_server_destroy(VCIC_Server *h);

/**
 * 注册视频帧数据源回调
 * 当域控请求取流时，VCIC 会循环调用此回调获取视频帧数据
 *
 * @pObj    : 透传给回调的用户对象
 * @feed_cb : 数据源回调
 */
int32_t VCIC_server_register_feed(VCIC_Server *h, void *pObj, VCIC_Feed_CB feed_cb);

/**
 * 向所有已订阅的域控推送事件通知
 *
 * @eventId : VCIC_EVENT_FRAME_LOST / VCIC_EVENT_CAM_STATUS
 * @data    : 事件数据
 * @len     : 数据长度
 */
int32_t VCIC_server_send_event(VCIC_Server *h, uint16_t eventId,
                                const uint8_t *data, uint32_t len);

/* ========================================================================
 *  域控侧 API（Client — Host 端）
 * ======================================================================== */

typedef struct VCIC_Client VCIC_Client;

/**
 * 创建域控侧 VCIC 客户端实例
 *
 * @cfg : 配置参数
 * 返回：非 NULL 成功，NULL 失败
 */
VCIC_Client *VCIC_client_create(const VCIC_ClientConfig_t *cfg);

/**
 * 销毁客户端实例，释放所有资源
 */
void VCIC_client_destroy(VCIC_Client *h);

/**
 * 启动视频流
 * 向摄像头发送取流请求，成功后开始接收视频帧
 *
 * @pObj     : 透传给回调的用户对象
 * @frame_cb : 视频帧回调
 * 返回：0 成功，-1 失败
 */
int32_t VCIC_client_start_stream(VCIC_Client *h, void *pObj, VCIC_Frame_CB frame_cb);

/**
 * 停止视频流
 */
int32_t VCIC_client_stop_stream(VCIC_Client *h);

/**
 * 设置摄像头参数
 *
 * @param_id : VCIC_PARAM_RESOLUTION / FRAMERATE / BITRATE / ...
 * @data     : 参数值（格式取决于 param_id）
 * @len      : 参数值长度
 */
int32_t VCIC_client_set_param(VCIC_Client *h, uint16_t param_id,
                                const uint8_t *data, uint32_t len);

/**
 * 查询摄像头参数
 *
 * @param_id : 要查询的参数 ID
 * @resp     : 输出缓冲
 * @resp_len : 输入为缓冲大小，输出为实际长度
 */
int32_t VCIC_client_get_param(VCIC_Client *h, uint16_t param_id,
                                uint8_t *resp, uint32_t *resp_len);

/**
 * 订阅事件通知
 *
 * @pObj     : 透传给回调的用户对象
 * @eventId  : VCIC_EVENT_FRAME_LOST / VCIC_EVENT_CAM_STATUS
 * @event_cb : 事件回调
 */
int32_t VCIC_client_subscribe_event(VCIC_Client *h, void *pObj, uint16_t eventId,
                                     VCIC_Event_CB event_cb);

/**
 * 取消订阅事件
 */
int32_t VCIC_client_unsubscribe_event(VCIC_Client *h, uint16_t eventId);

#if defined(__cplusplus)
}
#endif

#endif /* VCIC_H */
