#!/usr/bin/env python3
"""生成 A6 解析用的真实风格 .jpg 样本。

- camera_photo.jpg：模拟相机拍的照片，带 EXIF（相机型号/拍摄参数），
  4:2:0 色度抽样，非 16 倍数尺寸（300x200），能展示边缘补齐与 EXIF 段。
- gradient.jpg：一张平滑渐变 + 噪声的图，quality=85，标准 JFIF。

这些图交给 C++ 解析器解析，展示"真实 jpg 比自己编码器产出的复杂"。
"""
import io
import os

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))


def make_photo_like(w, h, seed=42):
    """造一张有结构的图：渐变背景 + 几个色块 + 轻噪声，像真实照片。"""
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:h, 0:w]
    r = (120 + 100 * np.sin(xx / w * 3.14)).astype(float)
    g = (140 + 90 * np.cos(yy / h * 3.14)).astype(float)
    b = (100 + 80 * np.sin((xx + yy) / (w + h) * 6.28)).astype(float)
    img = np.stack([r, g, b], axis=-1)
    # 加几个色块
    img[30:90, 40:120] = [220, 60, 50]
    img[110:170, 180:270] = [40, 90, 200]
    img += rng.normal(0, 6, img.shape)
    return Image.fromarray(np.clip(img, 0, 255).astype("uint8"), "RGB")


def build_exif():
    """构造一段最小 EXIF（相机型号、软件、拍摄参数），PIL 直接写入 APP1。"""
    exif = Image.Exif()
    # 0x010F Make, 0x0110 Model, 0x0131 Software, 0x0132 DateTime
    exif[0x010F] = "Canon"
    exif[0x0110] = "Canon EOS 60D"
    exif[0x0131] = "codec-from-scratch sample"
    exif[0x0132] = "2026:09:12 10:30:00"
    return exif


def main():
    # 300x200：宽高都不是 16 的倍数，能展示 MCU 边缘补齐
    photo = make_photo_like(300, 200)
    # 用 quality=72：和我们编码器默认的 90 明显不同，量化表会不一样，
    # 正好展示"解码器要能应对别人用任意质量存的自定义量化表"。
    photo.save(
        os.path.join(HERE, "camera_photo.jpg"),
        format="JPEG",
        quality=72,
        subsampling=2,   # 2 = 4:2:0
        exif=build_exif(),
    )

    grad = make_photo_like(160, 128, seed=7)
    grad.save(
        os.path.join(HERE, "gradient.jpg"),
        format="JPEG",
        quality=85,
        subsampling=0,   # 0 = 4:4:4
    )

    # 带重启标记（DRI）的图：每 8 个 MCU 插一个 RSTn，用来演示 DRI 段解析。
    restart = make_photo_like(240, 160, seed=11)
    restart.save(
        os.path.join(HERE, "restart.jpg"),
        format="JPEG",
        quality=80,
        restart_marker_blocks=8,
    )

    for name in ("camera_photo.jpg", "gradient.jpg", "restart.jpg"):
        p = os.path.join(HERE, name)
        im = Image.open(p)
        print(f"{name}: {im.size} {im.mode} {os.path.getsize(p)} bytes "
              f"exif={'yes' if im.info.get('exif') else 'no'}")


if __name__ == "__main__":
    main()
