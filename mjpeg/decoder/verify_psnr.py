#!/usr/bin/env python3
"""B2 验证脚本：计算逐帧 PSNR，并把数据写入 chart_data 供配图。

三组对比（全部来自真实 PPM 逐像素比对，不手编数字）：
1) 手搓解码 vs libjpeg（PIL）解码——同一份 JPEG 帧字节，逐帧。
   这是"手搓解码器还原得对不对"的硬证据：两者色度上采样算法一致，
   差异只来自 IDCT/取整细节，PSNR 很高（>50 dB）。
2) 手搓解码 vs ffmpeg 解码——同一份 out.avi，逐帧。
   ffmpeg 默认用了带平滑的色度上采样（fancy upsampling），和 libjpeg/
   本解码器的简单上采样不同，所以 PSNR 明显偏低（~25 dB）——差的不是
   "对不对"，而是"上采样风格不同"。列出来是为了说明这个坑。
3) 完整往返：原始帧 -> B1 编码 -> avi -> B2 解码，逐帧。
   叠加了 JPEG 有损压缩本身的损失，PSNR 在 24 dB 量级（quality=80）。

依赖：解封装出的每帧 JPEG 字节存放在 jpeg_frames/frame_00X.jpg
      （由 dump_frames 生成）；ffmpeg 解码帧在 ff_frames/。
"""
import os
import math
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
MINE = os.path.join(HERE, "out_frames")          # B2 手搓解码 frame_000..
FF = os.path.join(HERE, "ff_frames")             # ffmpeg 解码 frame_001..
JPGS = os.path.join(HERE, "jpeg_frames")         # 解封装出的每帧 JPEG 字节
ORIG = os.path.normpath(os.path.join(HERE, "../../samples/mjpeg_frames"))
CHART = os.path.join(HERE, "chart_data")
os.makedirs(CHART, exist_ok=True)


def load(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.float64)


def psnr(a, b):
    mse = np.mean((a - b) ** 2)
    if mse == 0:
        return math.inf
    return 10.0 * math.log10(255.0 * 255.0 / mse)


def main():
    n = 20
    vs_libjpeg = []
    vs_ff = []
    roundtrip = []
    for i in range(n):
        mine = load(os.path.join(MINE, f"frame_{i:03d}.ppm"))
        # libjpeg（PIL 内置）解同一份 JPEG 帧字节
        lib = load(os.path.join(JPGS, f"frame_{i:03d}.jpg"))
        ff = load(os.path.join(FF, f"frame_{i+1:03d}.ppm"))
        orig = load(os.path.join(ORIG, f"frame_{i+1:03d}.ppm"))
        vs_libjpeg.append(psnr(mine, lib))
        vs_ff.append(psnr(mine, ff))
        roundtrip.append(psnr(orig, mine))

    def fmt(v):
        return "inf" if math.isinf(v) else f"{v:.2f}"

    # 写 chart_data（每帧一行：帧号 vs_libjpeg vs_ffmpeg 往返）
    with open(os.path.join(CHART, "psnr_per_frame.txt"), "w") as f:
        f.write("# frame  psnr_vs_libjpeg_dB  psnr_vs_ffmpeg_dB  "
                "psnr_roundtrip_dB\n")
        for i in range(n):
            f.write(f"{i} {fmt(vs_libjpeg[i])} {fmt(vs_ff[i])} "
                    f"{fmt(roundtrip[i])}\n")

    def stats(name, arr):
        fin = [v for v in arr if not math.isinf(v)]
        avg = sum(fin) / len(fin) if fin else math.inf
        mn = min(fin) if fin else math.inf
        print(f"=== {name} ===")
        print("  per-frame:", " ".join(fmt(v) for v in arr))
        print(f"  平均={fmt(avg)} dB  最低={fmt(mn)} dB")

    stats("手搓解码 vs libjpeg（同一 JPEG 帧字节，逐帧）", vs_libjpeg)
    stats("手搓解码 vs ffmpeg（同一 out.avi，逐帧）", vs_ff)
    stats("完整往返 原始→编码→avi→解码（逐帧）", roundtrip)


if __name__ == "__main__":
    main()
