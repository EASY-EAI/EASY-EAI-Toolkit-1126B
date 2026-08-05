# AOV (Always-On Video) 低功耗常亮录像方案

## 1. 功能概览

- **AOV 状态管理** — IDLE / ACTIVE 两态切换
- **休眠 / 定时唤醒** — 写 `/sys/power/state` 进入深度睡眠，MCU 定时器唤醒
- **帧模式切换** — 单帧模式 (低功耗 1fps) ↔ 多帧模式 (正常录像 15fps) 自动切换
- **双 Sensor 拼接** — AVS 三模式：融合 (BLEND) / 垂直 (VERTICAL) / 水平 (HORIZONTAL)
- **H.264/H.265 编码** — 主码流 + 子码流双通道
- **MP4 录像** — fMP4 封装后写入 SD 卡，自动切片
- **OSD 叠加** — 32-bit BGRA BMP 点阵水印 (背景透明)，AOV 单帧模式右下角显示标记
- **外设热管理** — 休眠前自动解绑 USB / Ethernet / SDIO / 声卡驱动降漏电

## 2. 数据流拓扑

```
┌──────────┐    ┌────┐    ┌─────────────┐    ┌──────────┐    ┌─────────┐    ┌──────┐
│ sensor0  │───▶│    │───▶│             │───▶│          │───▶│         │───▶│  SD  │
│ sensor1  │───▶│ VI │───▶│  AVS (拼接) │───▶│  VENC    │───▶│  Muxer  │───▶│      │
└──────────┘    └────┘    └─────────────┘    └──────────┘    └─────────┘    └──────┘
                                   │                │
                                   ▼                ▼
                              chn0 (全分辨率)   chn1 (半分辨率)
                              → VENC MAIN       → VENC SUB
                              → MP4 录像         → MP4 录像
```

## 3. 目录结构

```
aov/
├── src/
│   └── aov.c                       # AOV 全功能 Demo (状态机 + 录像 + OSD + 命令行)
├── osd_aov.bmp                     # OSD 叠加水印 (32-bit BGRA BMP, 128×48, 背景透明)
├── gen_osd_bmp.py                  # OSD BMP 生成工具 (纯 Python 标准库)
├── build.sh                        # 编译 / 部署脚本
├── CMakeLists.txt                  # CMake 构建配置
└── README.md
```


## 4. 模块职责（AOV API 库）

AOV API 位于 `easyeai-api/media/aov/`，通过 `api.cmake` 引入：

| 头文件 | 职责 |
|--------|------|
| `aov.h` | AOV 状态管理、回调注册 |
| `aov_video.h` | 视频管线统一 API (VI / AVS / VENC) |
| `aov_record.h` | 录像文件管理 |
| `aov_record_queue.h` | 录像帧缓冲队列 (抗 SD 卡抖动) |
| `aov_isp.h` | ISP 初始化 + pause/resume |
| `aov_helper.h` | 外设驱动管理、CPU 热插拔 |
| `aov_error.h` | 错误码定义 |

应用层 `aov.c` 负责组装上述模块，实现完整 AOV Demo 流程。

## 5. 入口说明

| 入口 | 功能 |
|------|------|
| [`aov.c`](src/aov.c) | 完整 AOV 流程：ISP→SYS→录像→OSD→VENC→命令行测试序列 |

## 6. 编译与部署

### 6.1 编译

```bash
cd Solutions/aov
./build.sh          # 编译，输出到 Release/aov，并自动部署到板端
./build.sh clear    # 清除编译产物
./build.sh cpres    # 仅拷贝 Release/ 到板端 (不编译)
./build.sh all      # 编译 + 拷贝到板端 (含 osd_aov.bmp)
```

### 6.2 CMake 选项

| 选项 | 说明 | 默认 |
|------|------|------|
| `-DUSE_RKAIQ=ON` | 启用 rkaiq (ISP AIQ v2) | ON |
| `-DENABLE_AOV=ON` | 启用 AOV 功能宏 | OFF |
| `-DOS_LINUX=ON` | 目标系统 Linux | OFF |

### 6.3 依赖

- CMake ≥ 3.10.2
- ARM 交叉编译工具链 (`$HOME/configs/cross.cmake`)
- Rockchip MPP 库：`rga`, `rtsp`
- rkaiq 库 (板端 SDK)
- easyeai-api 模块 (同仓库 `../../easyeai-api`)
  - `media/aov/` — AOV API 库 (`libaov.a`)
  - `media/lmo_adapter/` — LMO 媒体适配器 (OSD 区域管理)

## 7. 运行

### 7.1 命令行用法

```bash
# 默认序列: 拼接后端运行 60 秒后永久退出 AOV，程序保持后台
./aov

# 指定后端
./aov splice            # AVS 水平拼接 (默认)
./aov multi             # 双 sensor 独立录像 (非拼接)

# 动作序列
./aov splice wait:60 exit_forever
./aov multi wait:120 exit_timed:10 wait:60 exit_forever
./aov splice enter      # 立即进入 AOV 并保持，直到 Ctrl+C
```

### 7.2 支持的动作

| 动作 | 简写 | 说明 |
|------|------|------|
| `wait:N` | `w:N` | 等待 N 秒 |
| `enter` | `e` | 立即进入 AOV 模式 |
| `exit_forever` | `ef` | 永久退出 AOV 模式 |
| `exit_timed:N` | `et:N` | 退出 AOV，N 秒后自动重入 |

### 7.3 环境变量

| 变量 | 用途 | 默认值 |
|------|------|--------|
| `IQ_FILE_DIR` | IQ XML 配置文件目录 | `/etc/iqfiles/` |

## 8. AVS 拼接模式

| 模式 | 枚举值 | 效果 | chn0 分辨率 (2×sensor) |
|------|--------|------|------------------------|
| `AVS_SPLICE_BLEND` | 0 | 融合拼接 | 3840×1080 |
| `AVS_SPLICE_VERTICAL` | 1 | 垂直拼接 (上下) | 1920×2160 |
| `AVS_SPLICE_HORIZONTAL` | 2 | 水平拼接 (左右) | 3840×1080 |

在 [`aov.c`](src/aov.c) 中通过 `vi_attr.avs_mode` 设置视频管线拼接模式。

## 9. OSD 叠加

AOV 单帧模式 (1fps 低功耗) 时在画面右下角显示 OSD 水印标记。

### 9.1 生成 OSD BMP

使用 `gen_osd_bmp.py` 生成 **32-bit BGRA BMP** 文件（纯 Python 标准库，无需第三方依赖）。

脚本生成 32-bit BGRA BMP，背景像素 `alpha=0`（透明）、文字像素 `alpha=0xFF`（不透明）。

```bash
cd Solutions/aov

# 默认: osd_aov.bmp 128×48 文字 "AOV" 蓝色 scale=4
python3 gen_osd_bmp.py

# 指定输出路径
python3 gen_osd_bmp.py -o ./osd_aov.bmp

# 自定义文字、颜色和尺寸
python3 gen_osd_bmp.py -t "REC" -c 255,0,0 -W 80 -H 32 -s 2

# 完整参数示例
python3 gen_osd_bmp.py -o ./osd_aov.bmp -W 128 -H 48 -t AOV -s 4 -c 0,0,255 -b 0,0,0
```

| 参数 | 说明 | 默认 |
|------|------|------|
| `-o` | 输出文件路径 | `osd_aov.bmp` |
| `-W` | 图像宽度 | 128 |
| `-H` | 图像高度 | 48 |
| `-t` | 文字内容 | `AOV` |
| `-s` | 字体缩放倍数 (原 5×8 点阵 × N) | 4 |
| `-c` | 文字颜色 R,G,B | `0,0,255` (蓝) |
| `-b` | 背景颜色 R,G,B | `0,0,0` (黑) |

> **透明原理**：BMP 使用 32-bit BGRA 格式，背景像素 alpha=0（透明），文字像素 alpha=0xFF（不透明）。
> `-b` 参数控制背景 RGB 值，但 alpha 固定为 0，背景始终透明。

### 9.2 运行时加载

`aov.c` 中通过 `LMO_COMM_RGN_Create()` 加载 `./osd_aov.bmp`（像素格式 `LMO_FMT_BGRA8888`），
在 VENC 启动前完成 OSD 区域注册，确保首帧就有水印覆盖。AOV 进入时显示水印，退出时隐藏。

## 10. 初始化流程

```
Step 1: ISP 初始化          aov_isp_init()
Step 2: SYS 初始化          RK_MPI_SYS_Init()
Step 3: SD 卡准备            record_release_system_sdcard → Aov_BindSdcard → record_wait_sd_storage
Step 4: 视频管线构建         build_video_pipeline() (VI init + VENC init)
Step 5: 录像队列 + drain 线程  record_queue_create → record_drain_start
Step 6: 录像模块初始化       aov_record_init(cfg) × (sensor × channel)
Step 7: OSD 初始化           user_osd_init_all() → LMO_COMM_RGN_Create()
Step 8: VENC 启动            aov_video_venc_start(cb)
Step 9: AOV 初始化           aov_init(action_t)  → 注册回调
Step 10: 执行测试序列        run_test_sequence(argc, argv)
Step 11: 等待退出信号        while(g_running) sleep(1)
Step 12: 清理               restore_peripherals → osd_deinit → destroy_pipeline
          → record_deinit → queue_deinit → aov_deinit → SYS_Exit → isp_deinit
```

## 11. AOV 休眠/唤醒流程

```
录像中 → AOV 进入事件
  ├── 解绑外设驱动 (USB / Ethernet / SDIO / 声卡)
  ├── 关闭非引导 CPU 核
  ├── 设置 MCU 唤醒时间
  └── 写 /sys/power/state "mem" → 系统休眠
                                    ↓
                              MCU 定时器唤醒
                                    ↓
AOV 退出事件 ←──────────────────────┘
  ├── 恢复 CPU 核
  ├── 重新绑定外设驱动
  └── ISP resume → 恢复录像
```

## 12. 注意事项

- ISP 初始化必须在 `RK_MPI_SYS_Init()` 之前
- `osd_aov.bmp` 需与可执行文件放在同一目录，缺失时 OSD 自动禁用但不影响录像
- 录像模块只接收最终编码分辨率，不再关心 sensor 数量或 AVS 拼接模式
- 休眠流程中外设解绑是可选的，不调用不影响核心录像功能
- `cpres` / `all` 模式会自动将 `osd_aov.bmp` 拷贝到 `Release/` 一并部署到板端
