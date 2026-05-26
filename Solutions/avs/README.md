# avs 使用说明

## 1. 入口说明

当前 `Solutions/avs` 已切换为基于新 `CamPipeCfg_t` 的图式配置入口，不再手写：

- `CamAdapter_IspInit()`
- `CamAdapter_ViInit()`
- `CamAdapter_AvsInit()`
- `CamAdapter_BindViToAvs()`
- `CamAdapter_BindAvsToVenc()`

当前入口文件：

- `src/rkadk_avs_test.c`

当前运行调度：

- `src/avs_runner.c`

## 2. 默认图式拓扑

默认跑的是双目 AVS 拼接：

```text
cam0 -> ISP0 -> VI0 ---\
                        \
                         -> AVS_GRP0 -> chn0 -> VENC0 -> RTSP main
                        /
cam1 -> ISP1 -> VI1 ---/

如果开启子码流：

cam0 -> ISP0 -> VI0 ---\
                        \
                         -> AVS_GRP0 -> chn0 -> VENC0 -> RTSP main
                        |            |
cam1 -> ISP1 -> VI1 ---/             +-> chn1 -> VENC1 -> RTSP sub
```

## 3. 常用参数

- `-a`
  - AIQ `iqfiles` 路径
- `-I`
  - `cam0` sensor id
- `-J`
  - `cam1` sensor id
- `-n`
  - camera 数量，当前入口默认按 `2` 路拼接实现
- `-f`
  - fps
- `-b`
  - 码率 kbps
- `-p`
  - RTSP 起始端口
- `--vi_size`
  - 输入 VI 尺寸，例如 `1920x1080`
- `--avs_main_size`
  - AVS 主码流尺寸，例如 `3840x1080`
- `--avs_sub_size`
  - AVS 子码流尺寸，例如 `1920x544`
- `--calib_file`
  - AVS 标定文件
- `--distance`
  - 拼接距离
- `--enable_sub_stream`
  - `0/1`
- `--codec`
  - `h264` 或 `h265`

## 4. 运行示例

双目主+子码流：

```bash
./avs -n 2 -I 0 -J 1 -a /etc/iqfiles \
  --vi_size 1920x1080 \
  --avs_main_size 3840x1080 \
  --avs_sub_size 1920x544 \
  --calib_file /oem/usr/share/avs_calib/calib_file.xml \
  --enable_sub_stream 1 \
  --codec h265
```

## 5. 当前可跑通模式说明

当前在 RV1126B 板端已经先验证跑通的是"无融合直拼"模式。

### 5.1 AVS 拼接模式概览

底层对应 `AVS_MODE_E` 枚举，当前 Toolkit 入口通过适配层 `blend_mode` 字段控制：

| blend_mode | 枚举值 | 含义 | 是否需标定文件 |
|-----------|--------|------|:-----------:|
| 0 | `AVS_MODE_BLEND` | 融合拼接，拼接缝处像素融合 | 是 |
| 1 | `AVS_MODE_NOBLEND_VER` | 无融合-垂直拼接（上下排布） | **否** |
| 2 | `AVS_MODE_NOBLEND_HOR` | 无融合-水平拼接（左右排布） | **否** |

### 5.2 无标定模式核心逻辑（上下拼接 vs 左右拼接）

"无融合直拼"模式 (`NOBLEND_VER=1` / `NOBLEND_HOR=2`) 的核心特点：

- 不需要标定文件，不加载 `librkAVS_genLutAndStitch.so` 或 `librkALG_avsCore.so`
- AVS 只负责把多路输入画面按固定方向简单排布，拼接缝处不做像素融合
- 适用于快速验证 `VI -> AVS -> VENC -> RTSP` 这条媒体链路是否通畅

**上下拼接 (NOBLEND_VER, blend_mode=1)**：
- 多路输入画面垂直堆叠
- 输出分辨率 = `src_width × (src_height × N)`，N 为相机数
- 例如两路 1920x1080 输入 → 输出 1920x2160

```
双目 (2路):              三目 (3路):
+-----------+            +-----------+
|  camera0  |            |  camera0  |
+-----------+            +-----------+
|  camera1  |            |  camera1  |
+-----------+            +-----------+
                         |  camera2  |
                         +-----------+
```

**左右拼接 (NOBLEND_HOR, blend_mode=2)**：
- 多路输入画面水平并排
- 输出分辨率 = `(src_width × N) × src_height`，N 为相机数
- 例如两路 1920x1080 输入 → 输出 3840x1080，三路 1920x1080 → 输出 5760x1080

```
双目 (2路):
+-----------+-----------+
|  camera0  |  camera1  |
+-----------+-----------+

三目 (3路):
+-----------+-----------+-----------+
|  camera0  |  camera1  |  camera2  |
+-----------+-----------+-----------+
```

### 5.3 决定拼接位置的参数

在无标定直拼模式下，每路画面在最终画布上的位置由以下参数共同决定：

| 编号 | 参数 | 作用 |
|:---:|------|------|
| [1] | `blend_mode` | 排布方向：`1`=垂直堆叠，`2`=水平并排 |
| [2] | `src_width × src_height` | 每路输入画面的原始尺寸 |
| [3] | `avs_main_width × avs_main_height` | 最终输出画布大小，应能容纳所有相机画面 |
| [4] | VI→AVS 绑定的 `dst.chn_id` | 决定相机对应画面中的第几行/列：`chn_id=0`=第一行/列，`1`=第二行/列... |
| [5] | `cam_num` | 相机数量，决定总行数/列数 |
| [6] | `RK_MPI_AVS_SetPipeAttr` <br> (可选微调) | `stDstRect` 精确指定某路画面在画布上的矩形区域，实现画中画等自定义排布 |

**关键理解：**
- 垂直拼接 (`blend_mode=1`)：pipe 0 在最上方，pipe 1 在其下，以此类推
- 水平拼接 (`blend_mode=2`)：pipe 0 在最左侧，pipe 1 在其右，以此类推
- 要交换两个相机位置，只需交换它们绑定时的 `dst.chn_id` 即可

### 5.4 已验证跑通的模式及执行参数

当前在 RV1126B 板端已验证跑通的是 **垂直拼接 (blend_mode=1)**，对应参数
`--blend_mode 1`。

#### 5.4.1 垂直拼接（已验证跑通）

```bash
./avs -n 2 -I 0 -J 1 -a /etc/iqfiles \
  --vi_size 1920x1080 \
  --avs_main_size 1920x2160 \
  --avs_sub_size 960x1080 \
  --blend_mode 1 \
  --enable_sub_stream 1 \
  --codec h265
```

**注意**：垂直拼接时，输出高度 = 输入高度 × 相机数。
例如两路 `1920x1080` → 主码流 `1920x2160`，子码流 `960x1080`（宽度按比例缩小）。

输出 RTSP：
- 主码流：`rtsp://<board-ip>:554/live/avs_main`
- 子码流：`rtsp://<board-ip>:555/live/avs_sub`

验证结论：
- VLC 能正常预览，双路取流、AVS 排布、编码和 RTSP 推流都是通的
- 不触发 AVS 融合算法库加载，无需标定文件

#### 5.4.2 水平拼接

```bash
./avs -n 2 -I 0 -J 1 -a /etc/iqfiles \
  --vi_size 1920x1080 \
  --avs_main_size 3840x1080 \
  --avs_sub_size 1920x544 \
  --blend_mode 2 \
  --enable_sub_stream 1 \
  --codec h265
```

**注意**：水平拼接时，输出宽度 = 输入宽度 × 相机数。
例如两路 `1920x1080` → 主码流 `3840x1080`，子码流 `1920x544`（高度按比例缩小）。

输出 RTSP：
- 主码流：`rtsp://<board-ip>:554/live/avs_main`
- 子码流：`rtsp://<board-ip>:555/live/avs_sub`

#### 5.4.3 融合拼接（需要标定文件）

```bash
./avs -n 2 -I 0 -J 1 -a /etc/iqfiles \
  --vi_size 1920x1080 \
  --avs_main_size 3840x1080 \
  --avs_sub_size 1920x544 \
  --calib_file /oem/usr/share/avs_calib/calib_file.xml \
  --blend_mode 0 \
  --enable_sub_stream 1 \
  --codec h265
```

**注意**：此模式需要有效的标定文件，且板端需要存在动态库
`librkAVS_genLutAndStitch.so` 或 `librkALG_avsCore.so`。

#### 5.4.4 只开主码流（任意模式）

在以上任意命令后加 `--enable_sub_stream 0` 即可只输出一路主码流。

例如垂直拼接只出主码流：

```bash
./avs -n 2 -I 0 -J 1 -a /etc/iqfiles \
  --vi_size 1920x1080 \
  --avs_main_size 1920x2160 \
  --blend_mode 1 \
  --enable_sub_stream 0 \
  --codec h265
```

## 6. 默认 RTSP：

```text
rtsp://<board-ip>:554/live/avs_main
rtsp://<board-ip>:555/live/avs_sub
```

只开主码流：

```bash
./avs -n 2 -I 0 -J 1 -a /etc/iqfiles \
  --vi_size 1920x1080 \
  --avs_main_size 3840x1080 \
  --calib_file /oem/usr/share/avs_calib/calib_file.xml \
  --enable_sub_stream 0
```
