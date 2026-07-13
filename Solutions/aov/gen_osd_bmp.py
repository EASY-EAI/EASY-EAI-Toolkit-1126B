#!/usr/bin/env python3
"""
gen_osd_bmp.py — 生成 OSD 叠加用的 32-bit BGRA BMP 文件

不依赖任何第三方库，纯 Python 标准库实现。
loadbmp.c 中的 load_bmp_ex() 对 32-bit BMP (Bpp=4) 走 memcpy 直接拷贝路径，
alpha 通道原样保留。因此背景像素 alpha=0 (透明)、文字像素 alpha=0xFF (不透明)，
无需任何色键处理。

用法:
  python3 gen_osd_bmp.py                              # 默认: osd_aov.bmp 128x48 "AOV" scale=4
  python3 gen_osd_bmp.py -o out.bmp -W 80 -H 32 -s 2  # 自定义尺寸和缩放
  python3 gen_osd_bmp.py -t "AOV" -c 0,128,255        # 自定义文字和颜色(R,G,B)

-s/--scale 参数将 5x8 点阵字体放大 N 倍，适配高分辨率视频。
背景像素 alpha=0 (透明)，文字像素 alpha=0xFF (不透明)，透明效果直接内嵌于 BMP。

生成后将 .bmp 文件拷贝到板子可执行文件同级目录即可。
"""

import struct
import argparse
import os

# ---- 5x8 点阵字体 (常用字符子集) ----
# 每个字符 5 列 x 8 行, 每列 1 byte, bit0=顶行
FONT = {
    ' ': [0x00,0x00,0x00,0x00,0x00],
    '!': [0x00,0x00,0x5F,0x00,0x00],
    '0': [0x3E,0x51,0x49,0x45,0x3E],
    '1': [0x00,0x42,0x7F,0x40,0x00],
    '2': [0x42,0x61,0x51,0x49,0x46],
    '3': [0x21,0x41,0x45,0x4B,0x31],
    '4': [0x18,0x14,0x12,0x7F,0x10],
    '5': [0x27,0x45,0x45,0x45,0x39],
    '6': [0x3C,0x4A,0x49,0x49,0x30],
    '7': [0x01,0x71,0x09,0x05,0x03],
    '8': [0x36,0x49,0x49,0x49,0x36],
    '9': [0x06,0x49,0x49,0x29,0x1E],
    'A': [0x7E,0x11,0x11,0x11,0x7E],
    'B': [0x7F,0x49,0x49,0x49,0x36],
    'C': [0x3E,0x41,0x41,0x41,0x22],
    'D': [0x7F,0x41,0x41,0x22,0x1C],
    'E': [0x7F,0x49,0x49,0x49,0x41],
    'F': [0x7F,0x09,0x09,0x09,0x01],
    'G': [0x3E,0x41,0x49,0x49,0x7A],
    'H': [0x7F,0x08,0x08,0x08,0x7F],
    'I': [0x00,0x41,0x7F,0x41,0x00],
    'J': [0x20,0x40,0x41,0x3F,0x01],
    'K': [0x7F,0x08,0x14,0x22,0x41],
    'L': [0x7F,0x40,0x40,0x40,0x40],
    'M': [0x7F,0x02,0x0C,0x02,0x7F],
    'N': [0x7F,0x04,0x08,0x10,0x7F],
    'O': [0x3E,0x41,0x41,0x41,0x3E],
    'P': [0x7F,0x09,0x09,0x09,0x06],
    'Q': [0x3E,0x41,0x51,0x21,0x5E],
    'R': [0x7F,0x09,0x19,0x29,0x46],
    'S': [0x46,0x49,0x49,0x49,0x31],
    'T': [0x01,0x01,0x7F,0x01,0x01],
    'U': [0x3F,0x40,0x40,0x40,0x3F],
    'V': [0x1F,0x20,0x40,0x20,0x1F],
    'W': [0x3F,0x40,0x38,0x40,0x3F],
    'X': [0x63,0x14,0x08,0x14,0x63],
    'Y': [0x07,0x08,0x70,0x08,0x07],
    'Z': [0x61,0x51,0x49,0x45,0x43],
    'a': [0x20,0x54,0x54,0x54,0x78],
    'b': [0x7F,0x48,0x44,0x44,0x38],
    'c': [0x38,0x44,0x44,0x44,0x20],
    'd': [0x38,0x44,0x44,0x48,0x7F],
    'e': [0x38,0x54,0x54,0x54,0x18],
    'f': [0x08,0x7E,0x09,0x01,0x02],
    'g': [0x0C,0x52,0x52,0x52,0x3E],
    'h': [0x7F,0x08,0x04,0x04,0x78],
    'i': [0x00,0x44,0x7D,0x40,0x00],
    'j': [0x20,0x40,0x44,0x3D,0x00],
    'k': [0x7F,0x10,0x28,0x44,0x00],
    'l': [0x00,0x41,0x7F,0x40,0x00],
    'm': [0x7C,0x04,0x18,0x04,0x78],
    'n': [0x7C,0x08,0x04,0x04,0x78],
    'o': [0x38,0x44,0x44,0x44,0x38],
    'p': [0x7C,0x14,0x14,0x14,0x08],
    'q': [0x08,0x14,0x14,0x18,0x7C],
    'r': [0x7C,0x08,0x04,0x04,0x08],
    's': [0x48,0x54,0x54,0x54,0x20],
    't': [0x04,0x3F,0x44,0x40,0x20],
    'u': [0x3C,0x40,0x40,0x20,0x7C],
    'v': [0x1C,0x20,0x40,0x20,0x1C],
    'w': [0x3C,0x40,0x30,0x40,0x3C],
    'x': [0x44,0x28,0x10,0x28,0x44],
    'y': [0x0C,0x50,0x50,0x50,0x3C],
    'z': [0x44,0x64,0x54,0x4C,0x44],
    '-': [0x08,0x08,0x08,0x08,0x08],
    ':': [0x00,0x36,0x36,0x00,0x00],
    '/': [0x20,0x10,0x08,0x04,0x02],
    '.': [0x00,0x60,0x60,0x00,0x00],
}

CHAR_W = 5
CHAR_H = 8
CHAR_GAP = 1


def generate_bmp(width, height, text, fg_rgb, bg_rgb, scale=1):
    """生成 32-bit BGRA BMP 字节数据 (bottom-up)，支持字体缩放

    每像素 4 字节: B, G, R, A
      背景像素: A=0   (透明)
      文字像素: A=0xFF (不透明)
    load_bmp_ex 对 Bpp=4 走 memcpy 路径，alpha 原样保留。
    """

    sc = max(1, scale)
    char_w = CHAR_W * sc
    char_h = CHAR_H * sc
    char_gap = CHAR_GAP * sc

    # 32-bit BMP: 每像素 4 字节，行宽天然 4 字节对齐
    row_stride = width * 4
    pixel_size = row_stride * height

    # 初始化背景色 (BGRA 顺序, alpha=0 透明)
    pixels = bytearray(pixel_size)
    for y in range(height):
        for x in range(width):
            off = y * row_stride + x * 4
            pixels[off + 0] = bg_rgb[2]  # B
            pixels[off + 1] = bg_rgb[1]  # G
            pixels[off + 2] = bg_rgb[0]  # R
            pixels[off + 3] = 0x00       # A (透明)

    # 计算文字居中位置
    text_w = len(text) * (char_w + char_gap) - char_gap
    start_x = max(0, (width - text_w) // 2)
    start_y = max(0, (height - char_h) // 2)

    # 渲染文字 (逐像素缩放)
    for i, ch in enumerate(text):
        glyph = FONT.get(ch, FONT[' '])
        cx = start_x + i * (char_w + char_gap)
        for col in range(CHAR_W):
            bits = glyph[col]
            for row in range(CHAR_H):
                if bits & (1 << row):
                    # 将原始 1 像素扩展为 sc×sc 块
                    for dy in range(sc):
                        for dx in range(sc):
                            px = cx + col * sc + dx
                            py = start_y + row * sc + dy
                            if 0 <= px < width and 0 <= py < height:
                                # BMP bottom-up: 文件第 0 行 = 图像最底行
                                file_row = height - 1 - py
                                off = file_row * row_stride + px * 4
                                pixels[off + 0] = fg_rgb[2]  # B
                                pixels[off + 1] = fg_rgb[1]  # G
                                pixels[off + 2] = fg_rgb[0]  # R
                                pixels[off + 3] = 0xFF       # A (不透明)

    # BMP 文件头
    bf_type = 0x4D42  # "BM"
    bf_size = 14 + 40 + pixel_size
    bf_offbits = 14 + 40

    file_header = struct.pack('<HIHHI',
        bf_type, bf_size, 0, 0, bf_offbits)

    info_header = struct.pack('<IiiHHIIiiII',
        40,              # biSize
        width,           # biWidth
        height,          # biHeight (正数=bottom-up)
        1,               # biPlanes
        32,              # biBitCount (32-bit BGRA)
        0,               # biCompression (BI_RGB)
        pixel_size,      # biSizeImage
        2835,            # biXPelsPerMeter (~72 DPI)
        2835,            # biYPelsPerMeter
        0,               # biClrUsed
        0,               # biClrImportant
    )

    return file_header + info_header + bytes(pixels)


def main():
    parser = argparse.ArgumentParser(description='生成 OSD 叠加用的 BMP 文件')
    parser.add_argument('-o', '--output', default='osd_aov.bmp',
                        help='输出文件路径 (默认: osd_aov.bmp)')
    parser.add_argument('-W', '--width', type=int, default=128,
                        help='图像宽度 (默认: 128)')
    parser.add_argument('-H', '--height', type=int, default=48,
                        help='图像高度 (默认: 48)')
    parser.add_argument('-t', '--text', default='AOV',
                        help='文字内容 (默认: AOV)')
    parser.add_argument('-s', '--scale', type=int, default=4,
                        help='字体缩放倍数 (默认: 4, 原 5x8 → 20x32)')
    parser.add_argument('-c', '--color', default='0,0,255',
                        help='文字颜色 R,G,B (默认: 0,0,255 蓝色)')
    parser.add_argument('-b', '--bg', default='0,0,0',
                        help='背景颜色 R,G,B (默认: 0,0,0 黑色)')
    args = parser.parse_args()

    fg_rgb = tuple(int(v) for v in args.color.split(','))
    bg_rgb = tuple(int(v) for v in args.bg.split(','))

    # 如果输出路径是目录，在目录内生成默认文件名
    out_path = args.output
    if os.path.isdir(out_path):
        out_path = os.path.join(out_path, 'osd_aov.bmp')

    data = generate_bmp(args.width, args.height, args.text, fg_rgb, bg_rgb, args.scale)

    with open(out_path, 'wb') as f:
        f.write(data)

    print(f'生成: {out_path} ({args.width}x{args.height}, 32-bit BGRA BMP, '
          f'文字="{args.text}" scale={args.scale} '
          f'颜色=({fg_rgb[0]},{fg_rgb[1]},{fg_rgb[2]}) '
          f'{os.path.getsize(out_path)} bytes)')


if __name__ == '__main__':
    main()
