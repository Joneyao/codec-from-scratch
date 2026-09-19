#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""E系列 #06（收官）配图生成：固定码率预算下 分辨率×帧率 的权衡。

所有数据来自 ffmpeg/ffprobe 对真实文件的实测（见同目录 measurements.txt）：
  - 源: testsrc2 1920x1080@30 10s，qp0 近无损参考
  - 编码预算: -b:v 2M -maxrate 2M -bufsize 4M, libx264 preset medium
  - PSNR/SSIM: 两套口径（投递到1080p30 / 隔离每帧空间清晰度）
  - bits-per-pixel = 2Mbps / (宽×高×帧率)

复现：measurements.txt 记录了全部 ffmpeg 命令；本脚本读实测数字绘图。
"""
import os
import sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

# CJK 字体（向上查找 backend/，用项目共享模块）
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
C_BLUE = "#2f6690"
C_RED = "#d1495b"
C_GREEN = "#4c9f70"
C_GRAY = "#b8b8b8"
C_ORANGE = "#e6a817"

# ---- 实测数据（来自 measurements.txt） ----
COMBOS = ["1080p30", "720p30", "1080p15", "720p60", "480p30"]
PSNR_DELIVER = [38.47, 35.17, 26.89, 35.14, 33.28]   # 口径A：投递到1080p30
PSNR_SPATIAL = [38.47, 35.17, 40.55, 35.14, 33.28]   # 口径B：隔离每帧空间
BPP = [0.0322, 0.0723, 0.0643, 0.0362, 0.1626]


# 图1：同预算下两套口径的质量对比（分组柱状 + 1080p15 的跷跷板高亮）
def chart_quality_seesaw():
    x = np.arange(len(COMBOS))
    w = 0.38
    fig, ax = plt.subplots(figsize=(9.6, 5.0))
    b1 = ax.bar(x - w/2, PSNR_SPATIAL, w, label="口径B：每帧空间清晰度",
                color=C_BLUE)
    b2 = ax.bar(x + w/2, PSNR_DELIVER, w, label="口径A：投递到1080p30(含时间)",
                color=C_ORANGE)
    for bars in (b1, b2):
        for b in bars:
            ax.text(b.get_x() + b.get_width()/2, b.get_height() + 0.3,
                    f"{b.get_height():.1f}", ha="center", fontsize=9)
    # 高亮 1080p15 的两极
    idx = COMBOS.index("1080p15")
    ax.annotate("最清晰\n(帧率砍半省给每帧)", (idx - w/2, PSNR_SPATIAL[idx]),
                xytext=(idx - 1.35, 41.5), fontsize=9.5, color=C_BLUE,
                ha="center", arrowprops=dict(arrowstyle="->", color=C_BLUE))
    ax.annotate("崩塌\n(运动补帧卡顿)", (idx + w/2, PSNR_DELIVER[idx]),
                xytext=(idx + 1.15, 30), fontsize=9.5, color=C_RED,
                ha="center", arrowprops=dict(arrowstyle="->", color=C_RED))
    ax.set_ylabel("PSNR (dB) — 越高越接近无损", fontsize=10)
    ax.set_ylim(24, 45)
    ax.set_xticks(x)
    ax.set_xticklabels(COMBOS, fontsize=10.5)
    ax.set_title("同样 2Mbps，换个评价维度，第一名变最后一名（ffmpeg 实测）",
                 fontsize=12.5, pad=10)
    ax.legend(fontsize=9.5, loc="lower left")
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    ax.grid(axis="y", alpha=0.25)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "quality_seesaw.png"), dpi=150,
                bbox_inches="tight")
    plt.close(fig)


# 图2：bits-per-pixel 概念图——同 2M 预算，分辨率/帧率越高每像素越"穷"
def chart_bits_per_pixel():
    order = sorted(range(len(COMBOS)), key=lambda i: BPP[i])
    combos = [COMBOS[i] for i in order]
    bpp = [BPP[i] for i in order]
    colors = [C_RED if c == "1080p30" else
              (C_GREEN if c == "480p30" else C_BLUE) for c in combos]
    fig, ax = plt.subplots(figsize=(9.2, 5.0))
    bars = ax.bar(combos, bpp, color=colors, width=0.6)
    for b, v in zip(bars, bpp):
        ax.text(b.get_x() + b.get_width()/2, b.get_height() + 0.003,
                f"{v:.4f}", ha="center", fontsize=10, fontweight="bold")
    # 参考线：1080p30 的 bpp
    base = BPP[COMBOS.index("1080p30")]
    ax.axhline(base, color=C_RED, ls="--", lw=1, alpha=0.7)
    ax.text(len(combos) - 0.5, base + 0.004,
            "1080p30 基准 0.032", color=C_RED, fontsize=9, ha="right")
    ax.set_ylabel("bits-per-pixel = 2Mbps / (宽×高×帧率)", fontsize=10)
    ax.set_ylim(0, 0.19)
    ax.set_title("同一桶水浇不同大小的地：像素越多，每个分到的比特越少",
                 fontsize=12.5, pad=10)
    ax.text(0.5, 0.17, "480p30 每像素 0.163 bit\n是 1080p30 的 5 倍 → 每帧最不糊",
            fontsize=9.5, color=C_GREEN,
            bbox=dict(boxstyle="round,pad=0.4", fc="#eef7f1", ec=C_GREEN))
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    ax.grid(axis="y", alpha=0.25)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "bits_per_pixel.png"), dpi=150,
                bbox_inches="tight")
    plt.close(fig)


# 图3：跷跷板概念图——固定码率预算，三者此消彼长（示意，非实测数字）
def chart_seesaw_concept():
    fig, ax = plt.subplots(figsize=(9.2, 4.4))
    # 一根固定长度的"预算条"，切成三段随场景变化
    scenarios = ["静态内容\n(风景/图文)", "均衡\n(通用)", "运动内容\n(体育/游戏)"]
    # 每行三段占比：分辨率 / 帧率 / 每帧质量（示意分配，和为100）
    res = [45, 38, 30]
    fps = [15, 32, 48]
    qual = [40, 30, 22]
    y = np.arange(len(scenarios))[::-1]
    h = 0.5
    ax.barh(y, res, h, color=C_BLUE, label="分给分辨率")
    ax.barh(y, fps, h, left=res, color=C_ORANGE, label="分给帧率")
    ax.barh(y, qual, h, left=[a + b for a, b in zip(res, fps)],
            color=C_GREEN, label="分给每帧质量")
    for i, yi in enumerate(y):
        ax.text(res[i] / 2, yi, "分辨率", ha="center", va="center",
                color="white", fontsize=9.5)
        ax.text(res[i] + fps[i] / 2, yi, "帧率", ha="center", va="center",
                color="white", fontsize=9.5)
        ax.text(res[i] + fps[i] + qual[i] / 2, yi, "每帧质量", ha="center",
                va="center", color="white", fontsize=9.5)
    ax.set_yticks(y)
    ax.set_yticklabels(scenarios, fontsize=10.5)
    ax.set_xlim(0, 100)
    ax.set_xlabel("固定码率预算（总长恒定）→ 一段涨，另两段必须让", fontsize=10)
    ax.set_title("码率 ≈ 分辨率 × 帧率 × 每像素比特：预算锁死时的三方分配",
                 fontsize=12.5, pad=10)
    ax.legend(fontsize=9, loc="lower center", ncol=3, bbox_to_anchor=(0.5, -0.32))
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    ax.set_xticks([])
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "seesaw_concept.png"), dpi=150,
                bbox_inches="tight")
    plt.close(fig)


# 图4：手搓系列全景收官——26篇 + 6篇实战，从"搓内部"到"用参数"
def chart_series_recap():
    fig, ax = plt.subplots(figsize=(9.6, 4.6))
    groups = [
        ("A · JPEG 编解码", 8, C_BLUE),
        ("B · MJPEG", 3, C_GREEN),
        ("C · H.264 解码", 9, C_ORANGE),
        ("D · H.264 编码", 6, C_RED),
        ("E · 实战篇", 6, "#7b5ea7"),
    ]
    left = 0
    for name, n, c in groups:
        ax.barh(0, n, left=left, height=0.55, color=c, edgecolor="white")
        ax.text(left + n / 2, 0, f"{name}\n{n}篇", ha="center", va="center",
                color="white", fontsize=9.5, fontweight="bold")
        left += n
    ax.annotate("亲手搓出编解码器内部（26篇）", (13, 0.42), ha="center",
                fontsize=10, color="#444")
    ax.annotate("看懂它暴露成参数怎么用（6篇）", (29, 0.42), ha="center",
                fontsize=10, color="#7b5ea7")
    ax.plot([0, 26], [0.33, 0.33], color="#444", lw=1)
    ax.plot([26, 32], [0.33, 0.33], color="#7b5ea7", lw=1)
    ax.set_xlim(0, 32)
    ax.set_ylim(-0.5, 0.6)
    ax.set_title("手搓系列全景：从比特流第一个字节，到命令行上的每个参数",
                 fontsize=12.5, pad=10)
    ax.axis("off")
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "series_recap.png"), dpi=150,
                bbox_inches="tight")
    plt.close(fig)


if __name__ == "__main__":
    chart_quality_seesaw()
    chart_bits_per_pixel()
    chart_seesaw_concept()
    chart_series_recap()
    print("charts generated:",
          [f for f in os.listdir(OUT) if f.endswith(".png")])
