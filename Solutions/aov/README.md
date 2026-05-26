# aov 使用说明

## 1. 简介

AOV（Always-On Video）低功耗常亮录像方案，基于 rockit_adapter 图式配置重构，不再依赖旧的 rkadk 框架。

- 支持进入 AOV 休眠/定时唤醒，休眠期间系统进入低功耗模式
- 唤醒后自动恢复录像，切片写入 SD 卡原始 H.264/H.265 文件（当前版本暂未启用 MP4 封装）
- 编码支持 H.264 / H.265，文件扩展名根据 `--codec` 参数自动生成 `.h264` 或 `.h265`
- 数据流采用 DataQueue 直写 SD 卡，无中间缓冲层，简化链路

## 2. 入口说明

当前入口文件：

- `src/rkadk_aov_record_test.c` — 命令行参数解析、组装配置

调度与辅助模块：

- `src/aov_runner.c` / `src/aov_runner.h` — 录像调度器，负责管线创建与录像管理
- `src/aov_helper.c` / `src/aov_helper.h` — AOV 休眠/唤醒、寄存器读写、CPU 控制
- `src/sdcard.c` / `src/sdcard.h` — SD 卡驱动绑定/解绑、挂载/卸载

## 3. 数据链路

```
ISP -> VI -> VPSS -> VENC -> DataQueue -> fopen/fwrite -> SD卡 .h264/.h265
```

## 4. 默认图式拓扑

```
cam -> ISP -> VI -> VPSS(ch) -> VENC(ch)
                                    ↓  frame_callback
                                 DataQueue
                                    ↓
                              fopen/fwrite → SD卡 .h264/.h265
```

## 5. 常用参数

| 参数                 | 说明                          | 默认值               |
|---------------------|-------------------------------|---------------------|
| `-a`                | AIQ iqfiles 路径              | `/etc/iqfiles`      |
| `-I`                | sensor id                     | `0`                 |
| `-n`                | camera 数量 (1 或 2)          | `1`                 |
| `-f`                | 帧率 fps                      | `25`                |
| `--width`           | 主码流宽度                    | `1920`              |
| `--height`          | 主码流高度                    | `1080`              |
| `--bitrate`         | 码率 kbps                     | `4096`              |
| `--gop`             | GOP 关键帧间隔 (帧数)         | 等于 fps            |
| `--bind_mode`       | 绑定模式 (vpss / direct)      | `vpss`              |
| `--rec_dir`         | SD 卡录像存储路径             | `/mnt/sdcard/video` |
| `--rec_prefix`      | 录像文件名前缀                | `cam0`              |
| `--enable_aov`      | 是否启用 AOV 低功耗休眠 (0/1) | `1`                 |
| `-S`                | AOV 休眠时间 (ms)             | `1000`              |
| `--codec`           | 编码格式 (h264 / h265)        | `h264`              |
| `--loop_count`      | 循环切换次数 (-1 不切换)       | `-1`                |
| `--loop_duration`   | 循环切换间隔 (秒)             | `30`                |

## 6. 运行示例

基础录像（默认 H.264 编码，输出 `.h264` 文件）：

```bash
./aov -I 0 -a /etc/iqfiles --width 1920 --height 1080
```

指定 H.265 编码（输出 `.h265` 文件）：

```bash
./aov -I 0 -a /etc/iqfiles --width 1920 --height 1080 --codec h265
```

关闭 AOV 休眠，纯录像模式：

```bash
./aov -I 0 -a /etc/iqfiles --width 1920 --height 1080 --enable_aov 0
```

指定存储路径和文件名前缀：

```bash
./aov -I 0 -a /etc/iqfiles --width 1920 --height 1080 --rec_dir /mnt/sdcard/video --rec_prefix front
```

播放原始流：

```bash
ffplay cam0_20260514_153000.h264
```

## 7. AOV 休眠流程

```
录像中 -> AOV 休眠事件 -> 停止录像 -> 卸载 SD 卡 -> 进入系统休眠
                                                        ↓
                                                  定时器唤醒
                                                        ↓
                                              挂载 SD 卡 -> 恢复录像
```

## 8. 编译

```bash
./build.sh
```

清除编译产物：

```bash
./build.sh clear
```

### 8.1 编译依赖

- CMake >= 3.10.2
- ARM 交叉编译工具链（通过 `$HOME/configs/cross.cmake` 配置）
- easyeai-api 模块（`../easyeai-api` 目录下）
- rkaiq 库（板端 SDK）

### 8.2 CMake 选项

| 选项                 | 说明            |
|---------------------|-----------------|
| `-DUSE_RKAIQ=ON`    | 启用 rkaiq 支持  |
| `-DENABLE_AOV=ON`   | 启用 AOV 功能    |
| `-DOS_LINUX=ON`     | 目标系统为 Linux |

## 9. 文件结构

```
aov/
├── src/
│   ├── aov_helper.h          # 休眠唤醒接口
│   ├── aov_helper.c          # 休眠唤醒、寄存器读写、CPU控制 实现
│   ├── sdcard.h              # SD 卡挂载接口
│   ├── sdcard.c              # SD 卡驱动绑定/解绑、挂载/卸载 实现
│   ├── aov_runner.h          # AOV 运行器配置
│   ├── aov_runner.c          # AOV 录像调度主循环
│   └── rkadk_aov_record_test.c  # 命令行入口
├── build.sh
├── CMakeLists.txt
└── README.md
```

## 10. 配置文件

> 当前版本已不再依赖 `rkadk` 框架的 ini 配置文件。所有参数通过命令行 `--width` / `--height` / `--codec` 等直接传入。
>
> `config/` 目录下的 `rkadk_defsetting*.ini` 为旧版遗留文件，新架构不再解析。


