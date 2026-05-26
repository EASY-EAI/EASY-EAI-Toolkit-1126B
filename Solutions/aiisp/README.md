# aiisp 使用说明

## 1. 功能概览

当前 `Solutions/aiisp` 基于图式配置的媒体管线方案，实现 **AI-ISP 黑光增强 + 编码推流**。
AI-ISP 模型运行在 VPSS 模块上，对 VI 输入的图像进行低照度增强后再送入 VENC 编码。

支持：

- 单目 / 双目
- 主码流 / 子码流
- 两种绑定模式：`VI -> VPSS -> VENC`（推荐）和 `VI -> VENC`（直绑）

当前入口程序：

- `src/rkadk_aiisp_test.c`

运行调度：

- `src/aiisp_runner.c`

底层接口：

- `../../easyeai-api/media/rockit_adapter/camera/pipeline.h`
- `../../easyeai-api/media/rockit_adapter/platform/rockchip_cam.h`

## 2. 两种绑定模式

### 2.1 `VI -> VPSS -> VENC`（推荐，AI-ISP 必须用此模式）

VPSS 层承载 AI-ISP 模型，同时可分主/子码流。

```text
cam0
  ISP0
    |
    v
  VI0
    |
    v
  VPSS_GRP0 + AI-ISP 模型
    |------ chn0 ------> VENC0 ------> RTSP route0 (主码流)
    |
    |------ chn1 ------> VENC1 ------> RTSP route1 (子码流)

cam1（可选）
  ISP1
    |
    v
  VI1
    |
    v
  VPSS_GRP1 + AI-ISP 模型
    |------ chn0 ------> VENC2 ------> RTSP route2
    |
    |------ chn1 ------> VENC3 ------> RTSP route3
```

### 2.2 `VI -> VENC`

轻量直绑，不经过 VPSS，不支持 AI-ISP。

```text
cam0: ISP0 -> VI0 -> VENC0 -> RTSP route0
cam1: ISP1 -> VI1 -> VENC1 -> RTSP route1
```

## 3. 图式配置说明

通过 `CamPipeCfg_t` 的三张配置表描述完整媒体图：

```text
CamPipeCfg_t
|
+-- cameras[]   -> 每路 camera 的 ISP / VI / VPSS 配置
+-- outputs[]   -> 每路编码输出及编码参数
+-- binds[]     -> 模块间绑定关系 (VI->VPSS, VPSS->VENC, VI->VENC)
```

## 4. 默认拓扑

### 双目四路（主+子码流 × 2）

```text
cam0_main -> route0 -> VENC0 -> rtsp://<board-ip>:554/live/cam0_main
cam0_sub  -> route1 -> VENC1 -> rtsp://<board-ip>:555/live/cam0_sub
cam1_main -> route2 -> VENC2 -> rtsp://<board-ip>:556/live/cam1_main
cam1_sub  -> route3 -> VENC3 -> rtsp://<board-ip>:557/live/cam1_sub
```

## 5. 常用参数

| 参数 | 说明 |
|------|------|
| `-a` | AIQ `iqfiles` 路径 |
| `-I` | cam0 sensor id |
| `-J` | cam1 sensor id |
| `-n` | camera 数量，`1` 或 `2` |
| `-f` | 帧率 |
| `-m` | AI-ISP 模型目录或文件路径 |
| `-p` | RTSP 起始端口 |
| `--bind_mode` | `vpss`（推荐）或 `direct` |
| `--enable_sub_stream` | `0` 或 `1` |
| `--cam0_width` / `--cam0_height` | cam0 主码流尺寸 |
| `--cam0_sub_width` / `--cam0_sub_height` | cam0 子码流尺寸 |
| `--cam1_width` / `--cam1_height` | cam1 主码流尺寸 |
| `--cam1_sub_width` / `--cam1_sub_height` | cam1 子码流尺寸 |

## 6. 运行示例

### 6.1 单目主+子码流（AI-ISP）

```bash
./aiisp -n 1 -I 0 -a /etc/iqfiles -m /oem/usr/lib/ --bind_mode vpss --enable_sub_stream 1
```

输出：

```text
rtsp://<board-ip>:554/live/cam0_main
rtsp://<board-ip>:555/live/cam0_sub
```

### 6.2 双目四路（AI-ISP）

```bash
./aiisp -n 2 -I 0 -J 1 -a /etc/iqfiles -m /oem/usr/lib/ --bind_mode vpss --enable_sub_stream 1
```

输出：

```text
rtsp://<board-ip>:554/live/cam0_main
rtsp://<board-ip>:555/live/cam0_sub
rtsp://<board-ip>:556/live/cam1_main
rtsp://<board-ip>:557/live/cam1_sub
```

### 6.3 双目直绑（无 AI-ISP）

```bash
./aiisp -n 2 -I 0 -J 1 -a /etc/iqfiles --bind_mode direct
```

输出：

```text
rtsp://<board-ip>:554/live/cam0_main
rtsp://<board-ip>:555/live/cam1_main
```

## 7. 关键结构

### 7.1 `CamPipeCameraCfg_t`

一路 camera 的输入链路，包含：

- `isp_cfg` — ISP 配置
- `vi_cfg` — VI 配置
- `enable_vpss` — 是否启用 VPSS（AI-ISP 必须为 true）
- `vpss_cfg` — VPSS 配置，包括 `enable_aiisp`、`aiisp_model_path`、`aiisp_buf_cnt` 等

### 7.2 `CamPipeOutputCfg_t`

一路编码输出：

- `route_id` — RTSP 路由 ID
- `venc_cfg` — VENC 编码参数

### 7.3 `CamPipeBindCfg_t`

一条模块绑定关系（举例）：

```text
VI -> VPSS     (VI chn 绑到 VPSS grp)
VPSS -> VENC   (VPSS chn 绑到 VENC)
VI -> VENC     (直绑模式)
```

## 8. 扩展建议

多目 / 第三码流：

- 增加 `camera_count`
- 在 `vpss_cfg.channels[]` 增加新的 chn 用于第三码流
- 对应增加 `outputs[]` 和 `binds[]`
- 分配新的 RTSP 端口

## 9. 相关文件

| 文件 | 说明 |
|------|------|
| `src/rkadk_aiisp_test.c` | 入口，参数解析与图配置构建 |
| `src/aiisp_runner.c` | 运行调度，队列管理，RTSP 推流 |
| `../../easyeai-api/media/rockit_adapter/camera/pipeline.h` | 管线接口定义 |
| `../../easyeai-api/media/rockit_adapter/camera/pipeline.c` | 管线实现 |
| `../../easyeai-api/media/rockit_adapter/platform/rockchip_cam.h` | RV1126B 底层类型定义与接口 |
| `../../easyeai-api/media/rockit_adapter/platform/rockchip_cam.c` | RV1126B 底层实现 |
