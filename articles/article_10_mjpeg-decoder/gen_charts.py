#!/usr/bin/env python3
"""B2 MJPEG 解码器配图。

数据来源全部为 codec-from-scratch/mjpeg/decoder 真实跑出的结果：
  - AVI 解封装偏移：直接解析真实 out.avi 得到（与 C++ avi_demuxer 等价）
  - 逐帧 PSNR：decoder/chart_data/psnr_per_frame.txt（verify_psnr.py 产出）
  - 原始帧/解码帧：真实 PPM，用 PIL 读
不手编数字。
"""
import os
import struct

import matplotlib
matplotlib.use("Agg")
import matplotlib.font_manager as fm
fm._load_fontmanager(try_read_cache=False)
import matplotlib.pyplot as plt
import numpy as np
from PIL import Image

plt.rcParams["font.family"] = "Noto Sans CJK JP"
plt.rcParams["axes.unicode_minus"] = False

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(
    HERE, "../../../../../third-party/codec-from-scratch"))
DEC = os.path.join(REPO, "mjpeg/decoder")
ENC = os.path.join(REPO, "mjpeg/encoder")
AVI = os.path.join(ENC, "out.avi")
PSNR_TXT = os.path.join(DEC, "chart_data/psnr_per_frame.txt")
MINE = os.path.join(DEC, "out_frames")
ORIG = os.path.join(REPO, "samples/mjpeg_frames")

C_BLUE = "#2563eb"
C_GREEN = "#16a34a"
C_ORANGE = "#ea580c"
C_GRAY = "#94a3b8"
C_PURPLE = "#7c3aed"


def sync_stage_map():
    import sys
    sys.path.insert(0, os.path.join(HERE, ".."))
    from pipeline_map import draw_mjpeg_map
    draw_mjpeg_map("demux", os.path.join(HERE, "stage_map.png"))
    print("stage_map.png done")


def load_psnr():
    """读 psnr_per_frame.txt: frame libjpeg ffmpeg roundtrip。"""
    lib, ff, rt = [], [], []
    with open(PSNR_TXT) as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            parts = line.split()
            lib.append(float(parts[1]))
            ff.append(float(parts[2]))
            rt.append(float(parts[3]))
    return lib, ff, rt


def parse_movi_chunks(max_show=6):
    """解析真实 out.avi 的 movi 列表，返回前若干个 00dc chunk 的
    (绝对偏移, 数据长度)，用于画"从 movi 顺序遍历取帧"的示意。"""
    data = open(AVI, "rb").read()
    movi = data.find(b"movi")
    p = movi + 4
    end = data.find(b"idx1")
    if end < 0:
        end = len(data)
    chunks = []
    total = 0
    while p + 8 <= end:
        cc = data[p:p + 4]
        size = struct.unpack("<I", data[p + 4:p + 8])[0]
        if cc in (b"00dc", b"00db"):
            chunks.append((p, size))
            total += 1
        p = p + 8 + size + (size & 1)
    return movi, chunks, total


def chart_demux_process():
    """精确层1：AVI 解封装过程——从 movi 顺序遍历 00dc chunk 提取帧，
    标真实字节偏移与每帧长度。"""
    movi_off, chunks, total = parse_movi_chunks()
    show = chunks[:5]
    fig, ax = plt.subplots(figsize=(9.6, 4.6))

    # 顶部：movi 列表条
    ax.text(0.5, 0.93, f"movi 数据列表  @字节 {movi_off:,}  "
            f"（共 {total} 个 00dc 帧 chunk）",
            ha="center", fontsize=11, fontweight="bold", color=C_PURPLE)

    n = len(show)
    seg_w = 1.0 / n
    for i, (off, size) in enumerate(show):
        x = i * seg_w
        # chunk 头（FOURCC 4B + size 4B）
        ax.add_patch(plt.Rectangle((x + seg_w * 0.03, 0.55), seg_w * 0.30, 0.22,
                                   facecolor=C_GRAY, edgecolor="white",
                                   linewidth=1.5, zorder=3))
        ax.text(x + seg_w * 0.18, 0.66, "00dc\n+长度", ha="center",
                va="center", color="white", fontsize=7.5, zorder=4)
        # JPEG 数据体
        ax.add_patch(plt.Rectangle((x + seg_w * 0.35, 0.55), seg_w * 0.60, 0.22,
                                   facecolor=C_BLUE, edgecolor="white",
                                   linewidth=1.5, zorder=3))
        ax.text(x + seg_w * 0.65, 0.66, "JPEG\n字节流", ha="center",
                va="center", color="white", fontsize=8, fontweight="bold",
                zorder=4)
        # 偏移与长度标注
        ax.text(x + seg_w * 0.5, 0.80, f"@{off:,}", ha="center", va="bottom",
                fontsize=8, color="#64748b")
        ax.text(x + seg_w * 0.5, 0.50, f"{size:,} 字节", ha="center", va="top",
                fontsize=8.5, color="#334155")
        # 向下箭头：抠出成一帧
        ax.annotate("", xy=(x + seg_w * 0.5, 0.30),
                    xytext=(x + seg_w * 0.5, 0.48),
                    arrowprops=dict(arrowstyle="->", color=C_GREEN, lw=1.6))
        ax.add_patch(plt.Rectangle((x + seg_w * 0.28, 0.12), seg_w * 0.44, 0.16,
                                   facecolor="#dcfce7", edgecolor=C_GREEN,
                                   linewidth=1.4, zorder=3))
        ax.text(x + seg_w * 0.5, 0.20, f"第 {i} 帧", ha="center", va="center",
                fontsize=8.5, color=C_GREEN, fontweight="bold", zorder=4)
        if i < n - 1:
            ax.annotate("", xy=((i + 1) * seg_w + seg_w * 0.03, 0.66),
                        xytext=(x + seg_w * 0.95, 0.66),
                        arrowprops=dict(arrowstyle="->", color="#cbd5e1",
                                        lw=1.4))
    ax.text(0.5, 0.02, "demuxer 只按 chunk 头的长度往后跳，原样搬走 JPEG 字节——"
            "全程不碰一个像素", ha="center", fontsize=9.5, color=C_ORANGE)
    ax.set_xlim(0, 1.0)
    ax.set_ylim(0, 1.0)
    ax.axis("off")
    ax.set_title("AVI 解封装：顺着 movi 一个个 00dc chunk 把 JPEG 抠出来",
                 fontweight="bold", fontsize=13, pad=14)
    plt.tight_layout()
    out = os.path.join(HERE, "demux_process.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("demux_process.png done  (movi@%d, %d chunks)" % (movi_off, total))


def chart_psnr_bars():
    """精确层2：手搓解码逐帧 PSNR 柱状图。
    三组对比：vs libjpeg（>50 dB，还原正确）、vs ffmpeg（~25 dB，上采样风格不同）、
    完整往返（~24 dB，叠加 JPEG 有损）。"""
    lib, ff, rt = load_psnr()
    n = len(lib)
    idx = np.arange(n)
    fig, ax = plt.subplots(figsize=(9.6, 4.6))
    w = 0.27
    ax.bar(idx - w, lib, w, color=C_GREEN, zorder=3,
           label=f"vs libjpeg（同份 JPEG）平均 {np.mean(lib):.1f} dB")
    ax.bar(idx, ff, w, color=C_BLUE, zorder=3,
           label=f"vs ffmpeg（同份 avi）平均 {np.mean(ff):.1f} dB")
    ax.bar(idx + w, rt, w, color=C_ORANGE, zorder=3,
           label=f"完整往返（原始→解码）平均 {np.mean(rt):.1f} dB")
    ax.axhline(50, color=C_GRAY, linestyle=":", linewidth=1.2, zorder=2)
    ax.text(n - 0.5, 50.6, "50 dB 线", ha="right", fontsize=8, color="#64748b")
    ax.set_xlabel("帧序号")
    ax.set_ylabel("PSNR（dB，越高越接近）")
    ax.set_title("逐帧 PSNR：手搓解码和 libjpeg 几乎一模一样",
                 fontweight="bold", fontsize=13)
    ax.set_xticks(idx)
    ax.set_xticklabels([str(i) for i in idx], fontsize=7.5)
    ax.set_ylim(0, 65)
    ax.grid(axis="y", alpha=0.3, zorder=0)
    ax.legend(loc="center right", fontsize=9)
    plt.tight_layout()
    out = os.path.join(HERE, "psnr_bars.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("psnr_bars.png done  (libjpeg avg=%.2f, ff avg=%.2f, rt avg=%.2f)"
          % (np.mean(lib), np.mean(ff), np.mean(rt)))


def chart_roundtrip_compare():
    """精确层3：往返重建效果——原始帧 vs 手搓解码帧并排，标 PSNR。
    取中间一帧（第 10 帧）更有代表性。"""
    lib, ff, rt = load_psnr()
    k = 10
    orig = Image.open(os.path.join(ORIG, f"frame_{k+1:03d}.ppm")).convert("RGB")
    mine = Image.open(os.path.join(MINE, f"frame_{k:03d}.ppm")).convert("RGB")
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(9.2, 4.0))
    a1.imshow(np.asarray(orig))
    a1.set_title(f"原始第 {k} 帧（编码前）", fontsize=11, fontweight="bold")
    a1.axis("off")
    a2.imshow(np.asarray(mine))
    a2.set_title(f"手搓解码第 {k} 帧\n往返 PSNR = {rt[k]:.2f} dB",
                 fontsize=11, fontweight="bold")
    a2.axis("off")
    fig.suptitle("完整往返：原始帧 → B1 编码进 avi → B2 拆开逐帧解码 → 还原",
                 fontsize=12.5, fontweight="bold", y=1.02)
    plt.tight_layout()
    out = os.path.join(HERE, "roundtrip_compare.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("roundtrip_compare.png done  (frame %d, rt=%.2f dB)" % (k, rt[k]))


def chart_demux_vs_decode():
    """形象层：解封装与解码是两件事。
    拆容器（得到一叠 JPEG）→ 逐帧调 JPEG 解码器 → 得到画面。"""
    fig, ax = plt.subplots(figsize=(9.8, 4.4))

    # 左：一个 AVI 盒子
    ax.add_patch(plt.Rectangle((0.02, 0.28), 0.16, 0.46, facecolor="#e2e8f0",
                               edgecolor="#475569", linewidth=1.8, zorder=3))
    ax.text(0.10, 0.51, "out.avi\n容器", ha="center", va="center",
            fontsize=10, fontweight="bold", color="#334155", zorder=4)

    # 箭头1：解封装
    ax.annotate("", xy=(0.30, 0.51), xytext=(0.19, 0.51),
                arrowprops=dict(arrowstyle="-|>", color=C_PURPLE, lw=2.2))
    ax.text(0.245, 0.60, "① 解封装\ndemux", ha="center", fontsize=9,
            color=C_PURPLE, fontweight="bold")

    # 中：一叠独立 JPEG
    for i in range(4):
        ax.add_patch(plt.Rectangle((0.32 + 0.03 * i, 0.30 + 0.03 * i),
                                   0.16, 0.34, facecolor="#bfdbfe",
                                   edgecolor="#1e40af", linewidth=1.3,
                                   zorder=3 + i))
    ax.text(0.43, 0.50, "一叠\nJPEG 字节", ha="center", va="center",
            fontsize=9.5, fontweight="bold", color="#1e3a8a", zorder=8)

    # 箭头2：逐帧解码
    ax.annotate("", xy=(0.66, 0.51), xytext=(0.55, 0.51),
                arrowprops=dict(arrowstyle="-|>", color=C_GREEN, lw=2.2))
    ax.text(0.605, 0.60, "② 逐帧解码\n复用 JPEG 解码器", ha="center",
            fontsize=9, color=C_GREEN, fontweight="bold")

    # 右：一叠画面帧
    for i in range(4):
        ax.add_patch(plt.Rectangle((0.68 + 0.03 * i, 0.30 + 0.03 * i),
                                   0.16, 0.34, facecolor="#dcfce7",
                                   edgecolor="#15803d", linewidth=1.3,
                                   zorder=3 + i))
    ax.text(0.79, 0.50, "一叠\nRGB 画面", ha="center", va="center",
            fontsize=9.5, fontweight="bold", color="#14532d", zorder=8)

    ax.text(0.5, 0.14, "① 只拆容器、搬字节，不碰像素   "
            "② 才是真正把 JPEG 变回画面的地方",
            ha="center", fontsize=10, color=C_ORANGE)
    ax.text(0.5, 0.05, "MJPEG 解码 = 拆 AVI ＋ 循环调 JPEG 解码器",
            ha="center", fontsize=11.5, fontweight="bold", color="#0f172a")
    ax.set_xlim(0, 0.98)
    ax.set_ylim(0, 0.82)
    ax.axis("off")
    ax.set_title("解封装和解码，是两件独立的事",
                 fontweight="bold", fontsize=13.5, pad=10)
    plt.tight_layout()
    out = os.path.join(HERE, "demux_vs_decode.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("demux_vs_decode.png done")


if __name__ == "__main__":
    sync_stage_map()
    chart_demux_process()
    chart_psnr_bars()
    chart_roundtrip_compare()
    chart_demux_vs_decode()
    print("all charts done")
