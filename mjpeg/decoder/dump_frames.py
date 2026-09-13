#!/usr/bin/env python3
"""从 out.avi 里把每帧 JPEG 字节原样抠出来，存成 jpeg_frames/frame_00X.jpg。

用途：给 verify_psnr.py 提供"同一份 JPEG 帧字节"，让 libjpeg(PIL) 解同一份
数据与手搓解码器逐像素比对。解析逻辑与 C++ avi_demuxer 完全等价（顺序遍历
movi 里的 00dc chunk）。
"""
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
AVI = os.path.join(HERE, "../encoder/out.avi")
OUT = os.path.join(HERE, "jpeg_frames")
os.makedirs(OUT, exist_ok=True)


def main():
    data = open(AVI, "rb").read()
    movi = data.find(b"movi")
    assert movi > 0, "no movi"
    p = movi + 4
    idx = 0
    # movi 列表结束位置：由外层 LIST size 决定，这里简单走到 idx1 前。
    end = data.find(b"idx1")
    if end < 0:
        end = len(data)
    while p + 8 <= end:
        cc = data[p:p + 4]
        size = struct.unpack("<I", data[p + 4:p + 8])[0]
        body = p + 8
        if cc in (b"00dc", b"00db"):
            frame = data[body:body + size]
            open(os.path.join(OUT, f"frame_{idx:03d}.jpg"), "wb").write(frame)
            idx += 1
        p = body + size + (size & 1)
    print(f"dumped {idx} JPEG frames -> {OUT}")


if __name__ == "__main__":
    main()
