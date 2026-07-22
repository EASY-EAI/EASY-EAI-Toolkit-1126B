# dis 使用说明

## 1. 功能概览

`Solutions/dis` 在 RV1126B 上实现 **GDC DIS（Digital Image Stabilization，数字图像防抖）+ H265/H264 编码 + RTSP 推流**，通过 FEC 硬件加速完成镜头畸变网格变换，无需 IMU 即可实现画面防抖。

编译产物为 `dis` 可执行文件。

## 2. 数据流拓扑

```text
Sensor -> ISP -> VI[3](VPSS online) -> GDC(DIS) -> VENC[0] -> RTSP /live/0
```

- **VI 通道 3**：使用 VPSS online 路径（`rkvpss_scale0`），输出 TILE+RFBC 格式，FEC 硬件要求
- **GDC 通道 0**：DIS 模式（`LMO_GDC_MODE_DIS`），通过镜头标定 JSON 生成畸变网格，利用 FEC 硬件完成图像防抖变换
- **VENC 通道 0**：H265/H264 编码，输出到 RTSP
- **RTSP**：端口 554，路径 `/live/0`，库自动从首帧 IDR 解析 VPS/SPS/PPS

## 3. 目录结构

```
Solutions/dis/
├── build.sh                          # 编译与部署脚本
├── CMakeLists.txt
├── src/
│   └── main.c                        # 主程序（VI + GDC(DIS) + VENC + RTSP）
├── ko/                               # 内核模块
│   └── video_rkfec.ko               # FEC 硬件驱动（DIS 必需）
├── rkdis_config/                     # DIS 镜头标定 JSON 配置文件
│   ├── sc450ai_CRK4F4209_dis.json   # sc450ai 传感器（常用）
│   ├── sc200ai_CRK2F3537-V2_dis.json
│   └── sc850sl_CMK-OT2115-PC1_dis.json
└── Release/                          # 编译产物
```

## 4. 编译与部署

> **环境变量 `SYSROOT`**：`deploy` 和 `all` 命令需要设置 `SYSROOT` 指向板端 NFS 根文件系统路径。
> 示例：`SYSROOT=/mnt ./build.sh deploy`

### 4.1 命令说明

| 命令                  | 行为                                |                     SYSROOT 要求                     |
| --------------------- | ----------------------------------- | :--------------------------------------------------: |
| `./build.sh`        | 仅编译，产物输出到`Release/`      | 可选（设置了则自动拷贝 binary + .ko + .json 到板端） |
| `./build.sh deploy` | 部署 .ko 驱动 + JSON 配置到 SYSROOT |                    **必需**                    |
| `./build.sh all`    | 先`deploy`，再编译 + 拷贝到板端   |                    **必需**                    |
| `./build.sh clear`  | 清理`build/` 和 `Release/`      |                          —                          |

### 4.2 部署路径

| 本地文件                | 板端路径                                 |
| ----------------------- | ---------------------------------------- |
| `ko/video_rkfec.ko`   | `$SYSROOT/oem/usr/lib/`                |
| `rkdis_config/*.json` | `$SYSROOT/oem/usr/share/rkdis_config/` |
| `Release/dis`         | `$SYSROOT/userdata/Solu/dis/`          |
| `Release/*.ko`        | `$SYSROOT/userdata/Solu/dis/`          |
| `Release/*.json`      | `$SYSROOT/userdata/Solu/dis/`          |

### 4.3 首次加载驱动

DIS 依赖 FEC 硬件，需要在板端加载内核驱动（**只需执行一次**，重启后需重新加载）：

```bash
insmod /userdata/Solu/dis/video_rkfec.ko
```

> 可通过 `lsmod | grep rkfec` 确认是否已加载。

## 5. 参数说明

| 参数            | 说明                                                      | 默认值            |
| --------------- | --------------------------------------------------------- | ----------------- |
| `-w <width>`  | 视频宽度                                                  | `2688`          |
| `-h <height>` | 视频高度                                                  | `1520`          |
| `-f <fps>`    | 视频帧率                                                  | `30`            |
| `-r <file>`   | DIS 配置 JSON 文件（**必需**）                      | —                |
| `-a <path>`   | AIQ iqfiles 路径                                          | `/etc/iqfiles/` |
| `-e <codec>`  | 编码格式：`h264cbr` `h264vbr` `h265cbr` `h265vbr` | `h265cbr`       |
| `-b <kbps>`   | 编码码率（Kbps）                                          | `10240`         |
| `-P <port>`   | RTSP 服务端口                                             | `554`           |
| `-v <num>`    | VI buffer 数量                                            | `4`             |

## 6. DIS 配置文件

DIS 算法需要镜头标定参数，通过 JSON 文件提供。JSON 文件包含镜头内参（焦距、光心）和畸变系数。

| 配置文件                            | 适用传感器 | 分辨率     |
| ----------------------------------- | ---------- | ---------- |
| `sc450ai_CRK4F4209_dis.json`      | SC450AI    | 2688×1520 |
| `sc200ai_CRK2F3537-V2_dis.json`   | SC200AI    | 1920×1080 |
| `sc850sl_CMK-OT2115-PC1_dis.json` | SC850SL    | 3840×2160 |

> 如果使用其他传感器，需要通过镜头标定工具生成对应的 `_dis.json` 文件。

## 7. 运行示例

### 7.1 基本运行（sc450ai 传感器）

```bash
# 1. 加载 FEC 驱动（只需执行一次，重启后需重新加载）
insmod /userdata/Solu/dis/video_rkfec.ko

# 2. 运行 DIS 防抖推流
./dis -w 2688 -h 1520 -a /etc/iqfiles/ -r ./sc450ai_CRK4F4209_dis.json
```

### 7.2 使用 H264 编码

```bash
./dis -w 2688 -h 1520 -a /etc/iqfiles/ -r ./sc450ai_CRK4F4209_dis.json -e h264cbr
```

### 7.3 自定义 RTSP 端口

```bash
./dis -w 2688 -h 1520 -a /etc/iqfiles/ -r ./sc450ai_CRK4F4209_dis.json -P 554
```

## 8. RTSP 预览

| 码流   | RTSP 地址                        |
| ------ | -------------------------------- |
| 主码流 | `rtsp://<board-ip>:554/live/0` |

VLC 拉流示例：

```bash
vlc rtsp://<board-ip>:554/live/0
```

## 9. 技术说明

### 9.1 GDC DIS 模式

DIS（Digital Image Stabilization）通过 GDC（Geometric Distortion Correction）模块的 `LMO_GDC_MODE_DIS` 模式实现：

- **算法类型**：4-DOF GME（Global Motion Estimation），无需 IMU
- **硬件加速**：FEC（FishEye Correction）硬件引擎完成网格变换
- **输入格式**：TILE+RFBC 压缩格式（FEC 硬件要求）
- **输出格式**：YUV420SP + RFBC 压缩

### 9.2 FEC 硬件依赖

DIS 算法的网格变换依赖 FEC 硬件引擎（`/dev/video53`）：

1. `libRkDis.so` 初始化时生成畸变网格文件（`/tmp/test_mesh_0.bin`）
2. `libIspFec.so` 加载网格文件到 FEC 硬件
3. FEC 硬件对每帧图像进行实时几何变换

如果未加载 `video_rkfec.ko`，DIS 仍可初始化但会出现 `no fecHw exsist` 警告，防抖效果会退化。

## 10. 注意事项

1. **FEC 驱动**：运行前必须加载 `video_rkfec.ko`，否则 DIS 防抖效果退化。
2. **DIS 配置文件**：`-r` 参数为必需项，必须提供与传感器匹配的 `_dis.json` 标定文件。
3. **VI 通道**：使用通道 3（VPSS online 路径），TILE+RFBC 格式输出，FEC 硬件要求此格式。
4. **端口冲突**：默认 RTSP 端口 554，与其他 RTSP 服务冲突时可用 `-P` 修改。
5. **内核版本**：提供的 `video_rkfec.ko` 适用于 kernel 6.1.141。
