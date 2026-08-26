# VCIC Stream — 车载摄像头视频流传输

基于 VCIC（ISO 17215）协议的双板协同实时视频传输方案。

## 1. 使用方法

### 1.1 Server 端（摄像头 / 推流端）

支持两种数据源，二选一：

**RK_MPI 模式（默认）** — 从 MIPI sensor 实时采集 + 硬件编码：

```bash
sudo ./vcic_stream_server [ifname] [local_ip] [width] [height] [framerate] [fmt]
# 示例：
sudo ./vcic_stream_server eth0 192.168.1.20 1920 1080 30 h264
```

**文件模式** — 直接读取 H.264/H.265 AnnexB 码流文件（无需 camera）：

```bash
sudo ./vcic_stream_server --file <h264_file> [ifname] [local_ip] [framerate] [fmt]
# 示例：
sudo ./vcic_stream_server --file miaobiao.h264 eth0 192.168.1.20 30 h264
```

启动后终端命令：

| 命令 | 说明 |
|------|------|
| `start` | 开始推流 |
| `stop` | 停止推流 |
| `status` | 查看当前状态 |
| `quit` | 退出程序 |

### 1.2 Client 端（接收 / 显示端）

```bash
sudo ./vcic_stream_client <server_ip> [ifname] [local_ip] [fmt]
# 示例：
sudo ./vcic_stream_client 192.168.1.20 eth0 192.168.1.30 h264
```

Client 启动后自动等待 Server 推流，收到画面后自动解码显示。

### 1.3 编译

```bash
cd Solutions/vcic_stream
./build.sh
```

编译产物输出到 `Release/`，设置 `SYSROOT` 环境变量后自动拷贝到板端 `$SYSROOT/userdata/Solu/vcic_stream/`。

## 2. 前置条件

### 2.1 网络配置

两块板子通过网线连接到同一局域网（同一网段），或直接网线对接。

### 2.2 权限

AVTP 使用裸以太网 socket（Layer 2），需要 root 权限或赋予 capability：

```bash
sudo setcap cap_net_raw+ep ./vcic_stream_server
sudo setcap cap_net_raw+ep ./vcic_stream_client
```

### 2.3 RK_MPI (Rockit) 库

两块板子都需要安装 `librockit.so`，提供 `RK_MPI_VI`、`RK_MPI_VENC`、`RK_MPI_VDEC` 等硬件编解码接口：

```bash
ls /usr/lib/librockit.so   # 确认是否已安装
```

> 文件模式下 Server 使用 `--file` 参数时无需 RK_MPI 采集管线和 MIPI sensor。Client 端仍需 RK_MPI 解码库。

### 2.4 ISP 3A 服务（仅 RK_MPI 模式）

RK_MPI 模式依赖 `rkaiq_3A.service` 负责 ISP 3A 算法初始化，运行前需确保服务已启动：

```bash
systemctl start rkaiq_3A.service
```

文件模式跳过此步骤。

### 2.5 MIPI Sensor 设备节点（仅 RK_MPI 模式）

```bash
ls /dev/video*                    # 确认设备节点
v4l2-ctl -d /dev/video23 --list-formats-ext   # 查看支持的分辨率和格式
```

## 3. VCIC 闭源库接口介绍

VCIC（Vehicle Camera Interface Consortium）是基于 ISO 17215 标准的车载摄像头接口通信库，以闭源静态库形式提供（`libvcic.a`），头文件为 `vcic.h`。

### 3.1 Server 端 API（摄像头侧）

| 接口 | 说明 |
|------|------|
| `VCIC_server_create(cfg)` | 创建服务实例，配置网络接口、服务 ID、组播 MAC、编解码格式等 |
| `VCIC_server_destroy(h)` | 销毁服务实例，释放资源 |
| `VCIC_server_register_feed(h, pObj, feed_cb)` | 注册视频帧数据源回调，域控请求取流时循环调用获取视频帧 |
| `VCIC_server_send_event(h, eventId, data, len)` | 向已订阅的域控推送事件通知（帧丢失 / 摄像头状态） |

### 3.2 Client 端 API（域控侧）

| 接口 | 说明 |
|------|------|
| `VCIC_client_create(cfg)` | 创建客户端实例，配置网络接口、Server IP、组播 MAC 等 |
| `VCIC_client_destroy(h)` | 销毁客户端实例，释放资源 |
| `VCIC_client_start_stream(h, pObj, frame_cb)` | 向摄像头发送取流请求，成功后通过回调接收视频帧 |
| `VCIC_client_stop_stream(h)` | 停止取流 |
| `VCIC_client_set_param(h, param_id, data, len)` | 设置摄像头参数（分辨率 / 帧率 / 码率 / 翻转 / 编码格式 / 画质） |
| `VCIC_client_get_param(h, param_id, resp, resp_len)` | 查询摄像头参数 |
| `VCIC_client_subscribe_event(h, pObj, eventId, event_cb)` | 订阅事件通知（帧丢失 / 摄像头状态变化） |
| `VCIC_client_unsubscribe_event(h, eventId)` | 取消订阅事件 |

### 3.3 回调函数类型

| 回调类型 | 侧 | 说明 |
|----------|------|------|
| `VCIC_Feed_CB` | Server | 数据源回调，提供视频帧数据（指针 + 长度 + 是否末帧） |
| `VCIC_Frame_CB` | Client | 视频帧回调，接收视频帧（数据 + 长度 + AVTP 时间戳 + 编码格式） |
| `VCIC_Event_CB` | Client | 事件回调，接收事件通知（事件 ID + 数据） |

### 3.4 配置结构体

**VCIC_ServerConfig_t**（摄像头侧）：

| 字段 | 说明 |
|------|------|
| `ifname` | 网络接口，如 `"eth0"` |
| `local_ip` | 本机 IP，空串则自动选择 |
| `service_id` | 服务 ID，通常 `VCIC_SERVICE_ID`（0x0101） |
| `instance_id` | 实例 ID，通常 `VCIC_INSTANCE_ID`（0x0001） |
| `app_port` | 应用端口，通常 `VCIC_APP_PORT`（30491） |
| `stream_id[8]` | AVTP 数据流 ID |
| `dst_mac[6]` | 组播目标 MAC 地址 |
| `codec` | 编解码格式：`VCIC_CODEC_H264` / `VCIC_CODEC_H265` |

**VCIC_ClientConfig_t**（域控侧）：

| 字段 | 说明 |
|------|------|
| `ifname` | 网络接口 |
| `local_ip` | 本机 IP |
| `service_id` | 服务 ID |
| `instance_id` | 实例 ID |
| `local_port` | 本地端口 |
| `server_ip` | 摄像头 SoC 的 IP 地址 |
| `server_port` | 摄像头 SoC 应用端口 |
| `stream_id[8]` | 数据流 ID（需与摄像头侧一致） |
| `multicast_mac[6]` | 组播 MAC（需与摄像头侧一致） |

### 3.5 事件与参数 ID

**事件 ID**：

| 宏 | 值 | 说明 |
|------|------|------|
| `VCIC_EVENT_FRAME_LOST` | 0x8001 | 帧丢失事件 |
| `VCIC_EVENT_CAM_STATUS` | 0x8002 | 摄像头状态变化事件 |

**参数 ID**（`VCIC_client_set_param` / `VCIC_client_get_param` 使用）：

| 宏 | 值 | 数据格式 |
|------|------|------|
| `VCIC_PARAM_RESOLUTION` | 0x0001 | `uint16_t width, uint16_t height` |
| `VCIC_PARAM_FRAMERATE` | 0x0002 | `uint8_t fps` |
| `VCIC_PARAM_BITRATE` | 0x0003 | `uint32_t bps` |
| `VCIC_PARAM_FLIP` | 0x0004 | `uint8_t`: 0=none, 1=H, 2=V, 3=HV |
| `VCIC_PARAM_CODEC` | 0x0005 | `uint8_t`: 1=H.264, 2=H.265 |
| `VCIC_PARAM_QUALITY` | 0x0006 | `uint8_t`: 1~10 |
