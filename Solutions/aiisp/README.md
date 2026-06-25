# aiisp 使用说明

## 1. 功能概览

`Solutions/aiisp` 在 rv1126b 上通过 **rkaiq 内部 AIBNR 链路**启用 AI-ISP 黑光降噪，
不依赖 `librkpostisp.so`（该库在 rv1126b SDK 中未提供），不调用 `RK_MPI_VPSS_SetGrpAIISPAttr`。

AIBNR 激活后以 10fps 对 Bayer 域进行 NPU 推理降噪，再由 ISP 完成后续处理。

数据流：

```
sensor(sc450ai) -> ISP(rkaiq AIBNR) -> VI[0] -> VPSS[0] -+-> VENC[0] -> RTSP /live/0  (主码流)
                                                           |
                                                           +-> VENC[1] -> RTSP /live/1  (子码流)
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
# 仅编译（需配置好交叉编译环境 SYSROOT）
./build.sh

# 编译 + 部署可执行文件到板子
./build.sh cpres

# cpres 额外执行：
#   1. 部署三份 ainr IQ json 到 $SYSROOT/etc/iqfiles/
#   2. 创建软链 sc450ai_default_default.json -> sc450ai_CRK4F4209_styleTstP0.json
#   3. 部署模型 blob 到 $SYSROOT/etc/iqfiles/sc450ai/bnr/combo_x1_G8/

# 清理编译产物
./build.sh clear
```

## 5. 参数说明

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `-w` | 主码流宽度（须等于 sensor 输出宽度） | `2688` |
| `-h` | 主码流高度 | `1520` |
| `-W` | 子码流宽度 | `640` |
| `-H` | 子码流高度 | `360` |
| `-a` | AIQ iqfiles 路径 | `/etc/iqfiles/` |
| `-e` | 编码格式：`h264cbr` \| `h265cbr` | `h265cbr` |
| `-b` | 主码流编码码率 kbps | `4096` |
| `-B` | 子码流编码码率 kbps | `512` |
| `-l` | 循环帧数，`-1` 无限 | `-1` |

> **注意**：子码流通过 VPSS chn1 硬件缩放实现，分辨率会直接影响系统负载。

## 6. 运行示例

```bash
cd /userdata/Solu/aiisp

# 默认参数运行（sc450ai 2688x1520 H.265，子码流 640x360）
./aiisp -a /etc/iqfiles/ &

# 自定义主码流和子码流参数
./aiisp -a /etc/iqfiles/ -w 1920 -h 1080 -b 2048 -W 640 -H 360 -B 384
```

## 7. RTSP 预览

```bash
# VLC 打开主码流
vlc rtsp://<board-ip>:554/live/0

# VLC 打开子码流
vlc rtsp://<board-ip>:554/live/1
```

## 8. 验证 AIISP 是否激活

AIBNR 受 ISO 阈值（`autoSwOn_thred = isoIdx3`）控制，**只在暗光/高 ISO 下激活**。

验证方法：遮住镜头或放暗光环境，观察程序输出：

```
# rkaiq 日志出现以下内容说明已激活：
AIBNR:K:switch to aiisp mode, iso 528
AIBNR:K:AibnrManager_notify_sof: switch aiisp complete, doAiisp_en 1
CAMHW:K:aiisp mode is 1 wr_linecnt is 1520 rd_linecnt is 1520

# monitor 线程 frame_id 持续递增（AIBNR 以 10fps 推理）：
[monitor #43] run_idx=1 frame_id=950 frm_rate=102 algo=0(AIBNR) hw_state=1 => AIISP 正在推理 ✓
```

强光下 ISO 低，AIBNR 不工作，frame_id 不变属正常。
