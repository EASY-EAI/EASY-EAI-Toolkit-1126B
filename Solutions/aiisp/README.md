# aiisp 使用说明

## 1. 功能概览

`Solutions/aiisp` 在 rv1126b 上通过 **rkaiq AIBNR 链路**启用 AI-ISP 黑光降噪，
支持**单摄 / 双摄**同时跑 AIISP，不依赖 `librkpostisp.so`（该库在 rv1126b SDK 中未提供）。

AIBNR 激活后以 10fps 对 Bayer 域进行 NPU 推理降噪，再由 ISP 完成后续处理。

数据流：

```
单摄 -n 1:
  sensor0(sc450ai) -> ISP0(rkaiq AIBNR) -> VI[0] -> VPSS[0] -+-> VENC[0] -> RTSP /live/0  (主码流)
                                                                 |
                                                                 +-> VENC[1] -> RTSP /live/1  (子码流)

双摄 -n 2 (默认):
  sensor0(sc450ai) -> ISP0(rkaiq AIBNR) -> VI[0] -> VPSS[0] -+-> VENC[0] -> RTSP /live/0  (主码流)
                                                                 |
                                                                 +-> VENC[1] -> RTSP /live/1  (子码流)

  sensor1(sc450ai) -> ISP1(rkaiq AIBNR) -> VI[1] -> VPSS[1] -+-> VENC[2] -> RTSP /live/2  (主码流)
                                                                 |
                                                                 +-> VENC[3] -> RTSP /live/3  (子码流)
```

## 2. 目录结构

```
aiisp/
├── src/
│   └── aiisp_rtsp.c                       # 主程序
├── res/
│   └── iqfiles/
│       ├── sc450ai_CRK4F4209_styleTstP0.json   # IQ 文件(aibnr.en=1，P0 风格)
│       ├── sc450ai_CRK4F4209_styleTstP1.json   # IQ 文件(aibnr.en=1，P1 风格)
│       ├── sc450ai_CRK4F4209_styleTstP2.json   # IQ 文件(aibnr.en=1，P2 风格)
│       └── sc450ai/bnr/combo_x1_G8/
│           └── iso*.bin                        # AIBNR 模型 blob (iso50~iso204800)
├── CMakeLists.txt
├── build.sh
└── README.md
```

## 3. AIISP 依赖说明

| 文件 | 板子路径 | 说明 |
|------|---------|------|
| `sc450ai_CRK4F4209_styleTstP0.json` | `/etc/iqfiles/sc450ai_default_default.json`（软链） | aibnr.en=1，rkaiq 按 sensor module 名自动加载 |
| `iso*.bin` | `/etc/iqfiles/sc450ai/bnr/combo_x1_G8/` | rkaiq readModel 时按 ISO 档位加载 |

> **注意**：`sc450ai_default_default.json` 是软链，指向上述三份 json 之一（默认 P0）。
> 板子上原有的 `common/` 版本 aibnr.en=0，不会激活 AIISP。

## 4. 编译与部署

```bash
# 编译 + 自动部署可执行文件到板端（需配置好 SYSROOT 交叉编译环境）
./build.sh

# 编译 + 部署可执行文件 + 部署 IQ/AIBNR 模型到板端
./build.sh all

# 仅部署 IQ json + AIBNR 模型 blob 到板端（不含可执行文件）
./build.sh deploy

# 清理编译产物
./build.sh clear
```

各命令行为：

| 命令 | 编译 | 部署可执行文件 | 部署 IQ 配置 | 部署 AIBNR 模型 |
|------|:----:|:-------------:|:------------:|:---------------:|
| 无参数 / `all` | ✓ | ✓ | (`all`) | (`all`) |
| `deploy` | | | ✓ | ✓ |
| `clear` | 清理 `build/` + `Release/aiisp` | | | |

## 5. 参数说明

### 通用参数

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `-n` | 传感器数量 (1 或 2) | `2` |
| `-f` | 帧率 fps | `25` |
| `-a` | AIQ iqfiles 路径 | `/etc/iqfiles/` |
| `-e` | 编码格式：`h264cbr` \| `h265cbr` | `h265cbr` |
| `-l` | 循环帧数，`-1` 无限 | `-1` |

### Sensor 0 参数 (camId=0)

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `-w` | 主码流宽度（须等于 sensor 输出宽度） | `2688` |
| `-h` | 主码流高度 | `1520` |
| `-W` | 子码流宽度 | `640` |
| `-H` | 子码流高度 | `360` |
| `-b` | 主码流编码码率 kbps | `8192` |
| `-B` | 子码流编码码率 kbps | `768` |

### Sensor 1 参数 (camId=1)

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `--w1` | 主码流宽度 | `2688` |
| `--h1` | 主码流高度 | `1520` |
| `--W1` | 子码流宽度 | `640` |
| `--H1` | 子码流高度 | `360` |
| `--b1` | 主码流编码码率 kbps | `8192` |
| `--B1` | 子码流编码码率 kbps | `768` |

> **注意**：子码流通过 VPSS chn1 硬件缩放实现，分辨率会直接影响系统负载。
> 单摄模式下 `-n 1` 仅初始化 sensor0，不启动 sensor1 管线。

## 6. 运行示例

```bash
cd /userdata/Solu/aiisp

# 双摄默认运行（两路 sensor 都 2688x1520 H.265）
./aiisp -a /etc/iqfiles/ &

# 单摄运行
./aiisp -n 1 -a /etc/iqfiles/ &

# 两路不同分辨率
./aiisp -a /etc/iqfiles/ -w 2688 -h 1520 --w1 1920 --h1 1080 &

# 自定义码率
./aiisp -a /etc/iqfiles/ -b 4096 -B 512 --b1 2048 --B1 384 &

# H.264 编码 + 运行 1000 帧后退出
./aiisp -e h264cbr -l 1000
```

## 7. RTSP 预览

```bash
# 双摄模式 — Sensor0
vlc rtsp://<board-ip>:554/live/0   # 主码流
vlc rtsp://<board-ip>:554/live/1   # 子码流

# 双摄模式 — Sensor1
vlc rtsp://<board-ip>:554/live/2   # 主码流
vlc rtsp://<board-ip>:554/live/3   # 子码流

# 单摄模式 (-n 1) 仅有
vlc rtsp://<board-ip>:554/live/0   # 主码流
vlc rtsp://<board-ip>:554/live/1   # 子码流
```

## 8. 验证 AIISP 是否激活

AIBNR 受 ISO 阈值（`autoSwOn_thred = isoIdx3`）控制，**只在暗光/高 ISO 下激活**。

验证方法：遮住镜头或放暗光环境，观察程序输出：

```
# rkaiq 日志出现以下内容说明已激活：
AIBNR:K:switch to aiisp mode, iso 528
AIBNR:K:AibnrManager_notify_sof: switch aiisp complete, doAiisp_en 1
CAMHW:K:aiisp mode is 1 wr_linecnt is 1520 rd_linecnt is 1520

# monitor 线程轮流查两路 sensor，frame_id 持续递增（AIBNR 以 10fps 推理）：
[monitor_s0 #43] run_idx=1 frame_id=950 frm_rate=102 algo=0(AIBNR) hw_state=1 => AIISP 正在推理 ✓
[monitor_s1 #43] run_idx=1 frame_id=830 frm_rate=102 algo=0(AIBNR) hw_state=1 => AIISP 正在推理 ✓
```

强光下 ISO 低，AIBNR 不工作，frame_id 不变属正常。
