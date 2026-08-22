# rtspIPCamera — RTSP IP 摄像头方案

基于 RK_MPI（Rockchip Media Process Interface）的 RTSP IP 摄像头方案，支持人员检测 OSD 叠加。

## 1. 功能

- MIPI sensor 采集 → VI → VENC 硬件编码 → RTSP 推流
- 支持两种工作模式（通过 `SrcCfg_tab` 中 `bOsdEnabled` 配置）：
  - **直通模式** (`false`)：VI → VENC 直接绑定，低延迟
  - **OSD 叠加模式** (`true`)：VI → RGA 转 BGR888 → 人员检测 → OSD 画框 → RGA 转 NV12 → VENC 编码
- 编码码流同时推送到 RTSP 和保存为 H264/H265 文件
- 全程零拷贝：analyzer DMA-BUF → RGA → MB_BLK → VENC，通过 fd 模式流转

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
| `librga.so` | RGA 图形加速库 |
| `librknnrt.so` | RKNN 运行时库 |
| OpenCV | 图像处理库 |

### 3.3 模型文件

程序运行时从当前目录加载 `person_detect.model` 模型文件，需用户自行下载放置：

```bash
# 从 EasyEAI 官网下载 person_detect 模型，放置到运行目录
cp person_detect.model /userdata/Solu/rtspIPCamera/
```

> 模型文件不在本仓库中管理，请前往 [EasyEAI 官网](http://www.easy-eai.com/) 获取对应平台的 `person_detect.model`。

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
        .bOsdEnabled  = true,   /* true=OSD叠加, false=直通 */
    },
};

static StreamCfg_t StreamCfg_tab[] = {
    {
        .rtspPort  = 8554,
        .rtspPath  = "/live/0",
        .savePath  = "/userdata/output.h264",  /* NULL 表示不保存 */
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
