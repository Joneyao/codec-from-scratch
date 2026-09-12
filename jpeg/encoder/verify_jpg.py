#!/usr/bin/env python3
"""A5 集成验证：编码多个 quality 的 .jpg，测量文件大小并对原图算 PSNR。

用法: python3 verify_jpg.py <encoder_main> <input.ppm> <out_dir>
输出人类可读日志，同时把结果写到 out_dir/verify_results.txt 供配图脚本读取。
"""
import subprocess
import sys
import math
from PIL import Image


def psnr(a: Image.Image, b: Image.Image) -> float:
    pa = a.convert("RGB").tobytes()
    pb = b.convert("RGB").tobytes()
    n = min(len(pa), len(pb))
    se = 0
    for i in range(n):
        d = pa[i] - pb[i]
        se += d * d
    mse = se / n
    if mse == 0:
        return float("inf")
    return 10.0 * math.log10(255.0 * 255.0 / mse)


def main():
    encoder, ppm, out_dir = sys.argv[1], sys.argv[2], sys.argv[3]
    if not out_dir.endswith("/"):
        out_dir += "/"
    orig = Image.open(ppm).convert("RGB")
    qualities = [30, 50, 70, 85, 95]
    rows = []
    for q in qualities:
        outp = f"{out_dir}q{q}.jpg"
        r = subprocess.run([encoder, ppm, outp, str(q)],
                           capture_output=True, text=True)
        print(r.stdout.strip())
        assert r.returncode == 0, r.stderr
        # 验证能被 PIL 打开并解码。
        dec = Image.open(outp)
        dec.load()
        import os
        size = os.path.getsize(outp)
        p = psnr(orig, dec)
        rows.append((q, size, p))
        print(f"  quality={q:2d}  size={size:6d}B  PSNR={p:6.2f} dB  "
              f"decoded={dec.size} {dec.format}")
    with open(out_dir + "verify_results.txt", "w") as f:
        f.write("# quality size_bytes psnr_db\n")
        for q, s, p in rows:
            f.write(f"{q} {s} {p:.3f}\n")
    print(f"\nwrote {out_dir}verify_results.txt")


if __name__ == "__main__":
    main()
