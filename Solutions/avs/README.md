# avs 使用说明

## 1. 功能概览

`Solutions/avs` 在 RV1126B 上实现**双目相机 AVS 拼接 + RTSP 推流**，支持融合拼接、水平拼接和垂直拼接三种模式。

编译产物为 `avs` 可执行文件。

## 2. 数据流拓扑

```text
cam0 -> ISP0 -> VI[0] ---\
                          -> AVS_GRP0 -> chn0 -> VENC[0] -> RTSP /live/0 (主码流)
cam1 -> ISP1 -> VI[1] ---/           |
                                       +-> chn1 -> VENC[1] -> RTSP /live/1 (子码流)
```

- ISP 支持两种模式：`--ispLaunchMode 0` 单路 ISP 初始化（已验证工作正常）、`--ispLaunchMode 1` CamGroup 模式（VENC 可能收不到帧）
- VI 每路输出 `--vi_size` 指定分辨率，输入 AVS 拼接
- AVS 输出两路：chn0 主码流（拼接全景，尺寸由 `--avs_chn0_size` 指定）、chn1 子码流（缩放版本，尺寸由 `--avs_chn1_size` 指定）
- VENC 编码后通过 RTSP 推流

## 3. 目录结构

```
Solutions/avs/
├── build.sh                      # 编译与部署脚本
├── CMakeLists.txt
├── src/
│   └── sample_demo_vi_avs_venc.c # 主程序（VI + AVS + VENC + RTSP）
├── ko/                           # 内核模块（kernel 6.1.141）
│   ├── video_rkavsp.ko           # AVSP 硬件驱动（融合拼接必需）
│   └── video_rkfec.ko            # FEC 硬件驱动（融合拼接必需）
├── lib/
│   └── librkALG_avsCore.so       # AVS 核心算法库（aarch64）
├── avs_json/                     # AVS JSON 配置文件
│   ├── rk_2_1920x1080.json       # 双目 1920x1080 输入（常用）
│   └── rk_2_1920x1080.xml        # 标定 XML 参考
└── Release/                      # 编译产物
```

## 4. 编译与部署

> **环境变量 `SYSROOT`**：`deploy` 和 `all` 命令需要设置 `SYSROOT` 指向板端 NFS 根文件系统路径。
> 示例：`SYSROOT=/mnt ./build.sh deploy`

### 4.1 命令说明

| 命令 | 行为 | SYSROOT 要求 |
|------|------|:------------:|
| `./build.sh` | 仅编译，产物输出到 `Release/` | 可选（设置了则自动拷贝 binary + .ko 到板端） |
| `./build.sh deploy` | 部署算法库 + JSON 配置到 SYSROOT | **必需** |
| `./build.sh all` | 先 `deploy`，再编译 + 拷贝到板端 | **必需** |
| `./build.sh clear` | 清理 `build/` 和 `Release/` | — |

### 4.2 部署路径

| 本地文件 | 板端路径 |
|---------|---------|
| `lib/librkALG_avsCore.so` | `$SYSROOT/usr/lib/` |
| `avs_json/*.json` `avs_json/*.xml` | `$SYSROOT/oem/usr/share/avs_json/` |
| `Release/avs` | `$SYSROOT/userdata/Solu/avs/` |
| `Release/*.ko` | `$SYSROOT/userdata/Solu/avs/` |

### 4.3 首次加载驱动

融合拼接（`--avs_mode_blend 0`）需要在板端加载内核驱动（**只需执行一次**，重启后需重新加载）：

```bash
insmod /userdata/Solu/avs/video_rkavsp.ko
insmod /userdata/Solu/avs/video_rkfec.ko
```

水平/垂直拼接（`--avs_mode_blend 1/2`）**不需要**加载驱动。

## 5. 参数说明

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `--vi_size WxH` | 每路 VI 输入尺寸 | `1920x1080` |
| `--avs_chn0_size WxH` | AVS 主码流（chn0）输出尺寸 | `3840x1080` |
| `--avs_chn1_size WxH` | AVS 子码流（chn1）输出尺寸 | `1920x544` |
| `-a <path>` | AIQ iqfiles 路径 | `/etc/iqfiles/` |
| `-e <codec>` | 编码格式：`h264cbr` `h264vbr` `h265cbr` `h265vbr` | `h264cbr` |
| `-b <kbps>` | **主码流**（VENC[0]）编码码率 | `4096` |
| `-n <num>` | 相机数量 | `2` |
| `-F <fps>` | 目标帧率 | `15` |
| `-d <dist>` | 拼接距离（米） | `5.0` |
| `--hdr_mode <0/1/2>` | 0=Normal, 1=HDR2, 2=HDR3 | `0` |
| `--ispLaunchMode <0/1>` | ISP 初始化方式：0=单路，1=CamGroup | `1` |
| `--avs_mode_blend <0/1/2>` | AVS 拼接模式（见第 6 节） | `0` |
| `-J <file>` | AVS JSON 配置路径（融合拼接必需） | — |
| `--calib_file_path <file>` | 标定 XML 路径（非融合模式参考用） | `/oem/usr/share/avs_calib/calib_file.xml` |
| `--cam0_ldch_path <file>` | cam0 LDCH mesh 文件 | `/oem/usr/share/iqfiles/cam0_ldch_mesh.bin` |
| `--cam1_ldch_path <file>` | cam1 LDCH mesh 文件 | `/oem/usr/share/iqfiles/cam1_ldch_mesh.bin` |
| `--set_ldch < -1 / 1 / 2 >` | -1=禁用, 1=文件加载, 2=buffer 加载 | `2` |
| `--vi_buffcnt <num>` | VI buffer 数量 | `2` |
| `--vi_chnid <id>` | VI 通道 ID | `1` |
| `--osd_display <0/1>` | OSD 显示开关 | `1` |
| `--input_bmp_path <file>` | OSD 水印 BMP 图片路径 | — |
| `-l <N>` | 编码帧数上限（-1 无限） | `-1` |
| `-o <path>` | 编码码流本地保存路径 | — |

> **注意**：`-b` 参数仅控制主码流（VENC[0]）码率，子码流（VENC[1]）码率在代码中**硬编码为 1024 kbps**。

## 6. AVS 拼接模式

| `--avs_mode_blend` | 含义 | 说明 | 驱动要求 |
|:------------------:|------|------|:--------:|
| `0` | 融合拼接（BLEND） | 拼接缝处像素融合，画质最好，需 JSON 配置文件 | 需 insmod `video_rkavsp.ko` + `video_rkfec.ko` |
| `1` | 无融合垂直拼接（NOBLEND_VER） | 画面上下排布，如双目 1920x1080 → 1920x2160 | 不需要 |
| `2` | 无融合水平拼接（NOBLEND_HOR） | 画面左右排布，如双目 1920x1080 → 3840x1080 | 不需要 |

**输出尺寸计算**（`--avs_chn0_size`）：
- 水平拼接（mode=2）：输出宽度 ≈ 输入宽度 × 相机数，高度不变
- 垂直拼接（mode=1）：输出高度 ≈ 输入高度 × 相机数，宽度不变
- 融合拼接（mode=0）：由 JSON 中的 `DstWidth` / `DstHeight` 决定

## 7. RV1126B 融合拼接说明

RV1126B (aarch64) 的融合拼接与 RK3576 平台不同：

- RK3576 使用 GPU 加速库 `librkgfx_avs.so`，支持 `AVS_PARAM_SOURCE_CALIB` + XML
- RV1126B aarch64 的 `librockit.so` 在 CALIB 模式下会尝试 dlopen `librkAVS_genLutAndStitch.so`，但该库只有 32-bit ARM 版本，无法加载
- RV1126B aarch64 **必须通过 `AVS_GRP_ATTR_S.jsonPath` 字段传入 JSON 配置**，`librockit.so` 会 dlopen `librkALG_avsCore.so`（aarch64）并调用 `rkAlg_initAvs(jsonPath)`

### JSON 配置文件

| 文件 | 适用场景 |
|------|---------|
| `rk_2_1920x1080.json` | 双目 1920x1080 输入，融合拼接 |

JSON 文件中 `CalibFilePath` 字段指向板端的标定 XML。如需使用自己的标定结果，修改该字段即可。

> **融合拼接高度对齐**：`--avs_chn0_size` 的高度必须与 JSON 中 `DstHeight` 一致。
> 例如 `rk_2_1920x1080.json` 中 `DstHeight=1088`（16 字节对齐），所以命令行需使用 `--avs_chn0_size 3840x1088`。
> 若高度不匹配，RGA 会因 buffer 大小不足报错 `avs doBlit failed`。

## 8. ISP 模式说明

| `--ispLaunchMode` | 含义 | RV1126B 状态 |
|:-----------------:|------|:------------:|
| `0` | 单路 ISP 初始化（每路 VI 独立调用 `SAMPLE_COMM_ISP_Init` + `SAMPLE_COMM_ISP_Run`） | ✅ 已验证，工作正常 |
| `1` | CamGroup ISP 初始化（调用 `SAMPLE_COMM_ISP_CamGroup_Init`） | ⚠️ VENC 有时收不到帧（`0XA004800E` 超时），原因排查中 |

**建议使用 `--ispLaunchMode 0`**。

## 9. 运行示例

### 9.1 水平拼接（无融合，左右排布）

```bash
./avs \
    --vi_size 1920x1080 --avs_chn0_size 3840x1080 --avs_chn1_size 1920x544 \
    -a /etc/iqfiles/ -e h265cbr -b 4096 -n 2 \
    --avs_mode_blend 2 --ispLaunchMode 0 &
```

### 9.2 垂直拼接（无融合，上下排布）

```bash
./avs \
    --vi_size 1920x1080 --avs_chn0_size 1920x2160 --avs_chn1_size 1920x544 \
    -a /etc/iqfiles/ -e h265cbr -b 4096 -n 2 \
    --avs_mode_blend 1 --ispLaunchMode 0 &
```

> 垂直拼接时 `--avs_chn0_size` 的高度 = 输入高度 × 相机数（1080 × 2 = 2160）。

### 9.3 融合拼接（需加载驱动 + `-J` 参数）

```bash
# 1. 加载驱动（只需执行一次，重启后需重新加载）
insmod /userdata/Solu/avs/video_rkavsp.ko
insmod /userdata/Solu/avs/video_rkfec.ko

# 2. 运行（注意高度为 1088，与 JSON 中 DstHeight 对齐）
./avs \
    --vi_size 1920x1080 --avs_chn0_size 3840x1088 --avs_chn1_size 1920x544 \
    -a /etc/iqfiles/ -e h265cbr -b 4096 -n 2 \
    --avs_mode_blend 0 --ispLaunchMode 0 \
    -J /oem/usr/share/avs_json/rk_2_1920x1080.json &
```

## 10. RTSP 地址

| 码流 | RTSP 地址 |
|------|-----------|
| 主码流 | `rtsp://<board-ip>:554/live/0` |
| 子码流 | `rtsp://<board-ip>:554/live/1` |

VLC 拉流示例：

```bash
vlc rtsp://<board-ip>:554/live/0
vlc rtsp://<board-ip>:554/live/1
```

## 11. 注意事项

1. **子码流码率固定**：`-b` 参数仅控制主码流码率。子码流（VENC[1]）码率在代码中硬编码为 1024 kbps，如需修改可编辑 [`src/sample_demo_vi_avs_venc.c`](Solutions/avs/src/sample_demo_vi_avs_venc.c:608) 中 `venc[1].u32BitRate` 的值。
2. **CamGroup 模式**：`--ispLaunchMode 1` 在 RV1126B 上 VENC 可能收不到帧，建议使用 `--ispLaunchMode 0`。
3. **融合拼接高度**：使用融合拼接时，`--avs_chn0_size` 的高度必须与 JSON 中 `DstHeight` 对齐（16 字节对齐）。
4. **驱动加载**：融合拼接需要加载 `video_rkavsp.ko` 和 `video_rkfec.ko`，重启后需重新加载。水平/垂直拼接不需要。
5. **内核版本**：提供的 `.ko` 驱动适用于 kernel 6.1.141。
