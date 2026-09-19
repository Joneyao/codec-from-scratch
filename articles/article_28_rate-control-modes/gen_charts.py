#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""E2 CBR/VBR/CRF 配图生成。

数据来自 ffmpeg/ffprobe 对真实文件的实测：
  - summary.txt：各模式总码率 + PSNR（ffmpeg libx264 preset medium，640x360/150帧）
  - frames_crf23.csv / frames_cbr.csv / frames_vbr.csv：每帧 pkt_size,pict_type
复现命令见文章正文与 README。
"""
import os
import sys
import csv
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

_dir = os.path.dirname(os.path.abspath(__file__))
while _dir != os.path.dirname(_dir):
    if os.path.isdir(os.path.join(_dir, "backend")):
        sys.path.insert(0, os.path.join(_dir, "backend"))
        break
    _dir = os.path.dirname(_dir)
try:
    from src.utils.font_helper import setup_cjk_font
    setup_cjk_font()
except Exception:
    plt.rcParams["font.family"] = "Noto Sans CJK JP"
plt.rcParams["axes.unicode_minus"] = False

OUT = os.path.dirname(os.path.abspath(__file__))
C_CRF = "#2f6690"
C_CBR = "#d1495b"
C_VBR = "#e6a817"
C_GREEN = "#4c9f70"


def load_frames(fn):
    sizes = []
    with open(os.path.join(OUT, fn)) as f:
        for row in csv.reader(f):
            if row and row[0].strip().isdigit():
                sizes.append(int(row[0]))
    return sizes


# 图1: 每帧码率波动曲线（CRF vs CBR vs VBR，真实每帧字节数）
def chart_perframe():
    crf = load_frames("frames_crf23.csv")
    cbr = load_frames("frames_cbr.csv")
    vbr = load_frames("frames_vbr.csv")
    fig, ax = plt.subplots(figsize=(9.2, 4.6))
    x = range(len(crf))
    ax.plot(x, [s/1024 for s in crf], color=C_CRF, lw=1.3, label="CRF 23（控质量）")
    ax.plot(range(len(cbr)), [s/1024 for s in cbr], color=C_CBR, lw=1.3,
            label="CBR 2M（控码率）")
    ax.plot(range(len(vbr)), [s/1024 for s in vbr], color=C_VBR, lw=1.1,
            label="VBR 2M（折中）", alpha=0.8)
    ax.set_xlabel("帧序号", fontsize=10)
    ax.set_ylabel("每帧大小 (KB)", fontsize=10)
    ax.set_title("每帧码率波动：CRF 随画面起伏，CBR 被压平（ffmpeg 实测 150 帧）",
                 fontsize=12, pad=10)
    ax.legend(fontsize=9.5)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "perframe_bitrate.png"), dpi=150,
                bbox_inches="tight")
    plt.close(fig)


# 图2: 码率-质量权衡散点（CRF 三档 + CBR + VBR）
def chart_rate_quality():
    data = [
        ("CRF 18", 1186601, 49.17, C_CRF),
        ("CRF 23", 800448, 44.19, C_CRF),
        ("CRF 28", 433649, 39.34, C_CRF),
        ("CBR 2M", 1980700, 57.54, C_CBR),
        ("VBR 2M", 1752761, 54.30, C_VBR),
    ]
    fig, ax = plt.subplots(figsize=(8.8, 5.0))
    for name, br, psnr, c in data:
        ax.scatter(br/1e6, psnr, s=160, color=c, zorder=3, edgecolor="white")
        ax.annotate(name, (br/1e6, psnr), xytext=(6, 6),
                    textcoords="offset points", fontsize=10)
    # CRF 三点连线
    crf_pts = sorted([(d[1]/1e6, d[2]) for d in data if d[0].startswith("CRF")])
    ax.plot([p[0] for p in crf_pts], [p[1] for p in crf_pts],
            color=C_CRF, ls="--", alpha=0.5, zorder=1)
    ax.set_xlabel("实际码率 (Mbps)", fontsize=10)
    ax.set_ylabel("PSNR (dB，越高越接近原始)", fontsize=10)
    ax.set_title("码率-质量权衡：CRF 沿曲线滑，CBR/VBR 钉在码率预算上",
                 fontsize=12, pad=10)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "rate_quality.png"), dpi=150,
                bbox_inches="tight")
    plt.close(fig)


# 图3: 波动幅度对比（各模式每帧大小的最大/最小/均值，量化"平不平"）
def chart_variation():
    modes = [("CRF 23", "frames_crf23.csv", C_CRF),
             ("VBR 2M", "frames_vbr.csv", C_VBR),
             ("CBR 2M", "frames_cbr.csv", C_CBR)]
    fig, ax = plt.subplots(figsize=(8.6, 4.4))
    for i, (name, fn, c) in enumerate(modes):
        s = [x/1024 for x in load_frames(fn)]
        lo, hi, mn = min(s), max(s), sum(s)/len(s)
        ax.plot([i, i], [lo, hi], color=c, lw=8, alpha=0.35,
                solid_capstyle="round")
        ax.plot([i], [mn], "o", color=c, markersize=11, zorder=3)
        ax.annotate(f"峰 {hi:.0f}", (i, hi), xytext=(10, 0),
                    textcoords="offset points", fontsize=9, va="center")
        ax.annotate(f"谷 {lo:.0f}", (i, lo), xytext=(10, 0),
                    textcoords="offset points", fontsize=9, va="center")
        ax.annotate(f"均值 {mn:.0f}", (i, mn), xytext=(-64, 0),
                    textcoords="offset points", fontsize=9, va="center",
                    color=c, fontweight="bold")
    ax.set_xticks(range(len(modes)))
    ax.set_xticklabels([m[0] for m in modes], fontsize=11)
    ax.set_ylabel("每帧大小 (KB)", fontsize=10)
    ax.set_title("峰谷跨度：CRF 起伏最大，CBR 被压得最平（实测）",
                 fontsize=12, pad=10)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "variation.png"), dpi=150, bbox_inches="tight")
    plt.close(fig)


if __name__ == "__main__":
    chart_perframe()
    chart_rate_quality()
    chart_variation()
    print("charts generated:",
          [f for f in os.listdir(OUT) if f.endswith(".png")])
