# rtspIPCamera — RTSP IP 摄像头方案

基于 RK_MPI（Rockchip Media Process Interface）的 RTSP IP 摄像头方案。

## 1. 功能

- MIPI sensor 采集 → VI → VENC 硬件编码 → RTSP 推流
- VI chn0 直接 bind 到 VENC，由 ISP 自动送帧编码，低延迟
- 拉流端通过 RTSP 协议预览，支持 H264 / H265

## 2. 使用方法

### 2.1 编译

```bash
./build.sh          # 编译
./build.sh clear    # 清理
./build.sh cpres    # 编译并拷贝到板端
```

### 2.2 运行

```bash
cd /userdata/Solu/rtspIPCamera
./rtspIPCamera
```

### 2.3 拉流预览

```bash
ffplay rtsp://<板子IP>:8554/live/0
# 或
vlc rtsp://<板子IP>:8554/live/0
```

## 3. 前置条件

### 3.1 ISP 3A 服务

```bash
systemctl start rkaiq_3A.service
```

### 3.2 板端依赖库

| 依赖 | 说明 |
|------|------|
| `librockit.so` | RK_MPI 编解码库 |

## 4. 配置说明

在 `src/main.cpp` 中修改配置数组：

```c
static SrcCfg_t SrcCfg_tab[] = {
    {
        .srcType      = "MIPI",
        .loaction     = "/dev/video23",
        .width        = 1920,
        .height       = 1080,
        .framerate    = 30,
        .videoEncType = "h264",
        .audioEncType = "null",
    },
};

static StreamCfg_t StreamCfg_tab[] = {
    {
        .rtspPort  = 8554,
        .rtspPath  = "/live/0",
    },
};
```

## 5. 日志系统

使用 `log_manager_pro` 模块化日志，配置文件位于 `/userdata/logs/rtspIPCamera.ini`。
板端可通过 `log` CLI 工具实时调整日志级别：

```bash
./log                          # 列出所有模块
./log rtspIPCamera capture=debug   # 设置 capture 模块为 debug 级别
```
