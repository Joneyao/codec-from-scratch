#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""D5 手搓 H.264 编码器 #05：码率控制 配图生成。

所有数据来自仓库真实运行：
  third-party/codec-from-scratch/h264/encoder/chart_data/rate_control.txt
  由 RateController（rate_control.cpp）在 30 帧复杂度起伏序列上跑出的
  固定 QP=28 与 CBR（目标 2000kbps, fps=30, init_qp=28）两组对照数据。

列格式：frame mode qp bits buffer
  - fixed：QP 恒 28，bits 随复杂度起伏，buffer 恒 0（占位）。
  - cbr：QP 随缓冲区反向调整，buffer 为漏桶占用（比特）。
"""
import os
import sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.font_manager as fm

# --- CJK 字体（项目共享模块，向上查找 backend/）---
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
    fm._load_fontmanager(try_read_cache=False)
    plt.rcParams["font.family"] = "Noto Sans CJK JP"
plt.rcParams["axes.unicode_minus"] = False

HERE = os.path.dirname(os.path.abspath(__file__))

# 配色
C_FIXED = "#d1495b"   # 红：固定 QP（大起大落）
C_CBR = "#2f6690"     # 蓝：CBR（相对平稳）
C_TARGET = "#e6a817"  # 黄：目标码率线
C_BUF = "#4c9f70"     # 绿：缓冲区
C_GRAY = "#b8b8b8"

# 目标与常量（与 rate_control.txt 头部一致）
TARGET_BITS_PER_FRAME = 66666.7
BUFFER_SIZE = 4000000.0  # 2 秒码率 = target_bits_per_frame * fps * 2


def load_data():
    """读 rate_control.txt，返回 fixed / cbr 两组逐帧数据。"""
    path = os.path.join(
        HERE, "..", "..", "..", "..", "..",
        "third-party", "codec-from-scratch", "h264", "encoder",
        "chart_data", "rate_control.txt")
    path = os.path.abspath(path)
    fixed, cbr = [], []
    with open(path, "r") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            frame = int(parts[0])
            mode = parts[1]
            qp = int(parts[2])
            bits = int(parts[3])
            buf = float(parts[4])
            row = {"frame": frame, "qp": qp, "bits": bits, "buffer": buf}
            if mode == "fixed":
                fixed.append(row)
            else:
                cbr.append(row)
    fixed.sort(key=lambda r: r["frame"])
    cbr.sort(key=lambda r: r["frame"])
    return fixed, cbr


# ---------------------------------------------------------------------------
# 图0: 进度地图（复用系列共享骨架，D5 高亮"变换量化"环节 = QP 作用点）
# ---------------------------------------------------------------------------
def chart_stage_map():
    sys.path.insert(0, os.path.join(HERE, ".."))
    from pipeline_map import draw_h264_encode_map
    draw_h264_encode_map("transform", os.path.join(HERE, "stage_map.png"))


# ---------------------------------------------------------------------------
# 图1: 固定QP vs CBR 每帧比特数曲线（精确层：真实 30 帧数据）
# ---------------------------------------------------------------------------
def chart_bits_compare(fixed, cbr):
    frames = [r["frame"] for r in fixed]
    fixed_bits = [r["bits"] / 1000.0 for r in fixed]   # 转 kbit 便于读
    cbr_bits = [r["bits"] / 1000.0 for r in cbr]
    target_kbit = TARGET_BITS_PER_FRAME / 1000.0

    fig, ax = plt.subplots(figsize=(9, 4.6))
    ax.plot(frames, fixed_bits, "-o", color=C_FIXED, ms=4, lw=1.8,
            label="固定 QP=28（画质均匀，比特忽高忽低）")
    ax.plot(frames, cbr_bits, "-s", color=C_CBR, ms=4, lw=1.8,
            label="CBR 目标 2000kbps（比特相对平稳）")
    ax.axhline(target_kbit, color=C_TARGET, ls="--", lw=1.6,
               label=f"每帧平均预算 ≈ {target_kbit:.1f} kbit")

    # 标注固定QP 的峰值和谷值
    peak_i = max(range(len(fixed_bits)), key=lambda i: fixed_bits[i])
    ax.annotate(f"f{frames[peak_i]} 峰值 {fixed[peak_i]['bits']:,} bit",
                (frames[peak_i], fixed_bits[peak_i]),
                xytext=(frames[peak_i] - 1, fixed_bits[peak_i] + 8),
                fontsize=8.5, color=C_FIXED,
                arrowprops=dict(arrowstyle="->", color=C_FIXED))
    ax.annotate("f22-24 静止段谷值 2000 bit",
                (23, fixed_bits[23]),
                xytext=(16, 20), fontsize=8.5, color=C_FIXED,
                arrowprops=dict(arrowstyle="->", color=C_FIXED))

    ax.set_title("同一段视频，两种码率控制：固定 QP 大起大落，CBR 被拉平",
                 fontsize=12, pad=10)
    ax.set_xlabel("帧序号", fontsize=10)
    ax.set_ylabel("每帧编码比特数（kbit）", fontsize=10)
    ax.legend(fontsize=9, loc="upper right")
    ax.grid(True, ls=":", alpha=0.4)
    ax.set_xlim(-1, 30)
    for spine in ("top", "right"):
        ax.spines[spine].set_visible(False)
    fig.tight_layout()
    fig.savefig(os.path.join(HERE, "bits_compare.png"), dpi=150,
                bbox_inches="tight")
    plt.close(fig)


# ---------------------------------------------------------------------------
# 图2: CBR 的帧级 QP 曲线 + 缓冲区占用曲线（上下子图，反向调整可视化）
# ---------------------------------------------------------------------------
def chart_cbr_qp_buffer(cbr):
    frames = [r["frame"] for r in cbr]
    qps = [r["qp"] for r in cbr]
    bufs = [r["buffer"] / 1e6 for r in cbr]  # 转 Mbit

    fig, (ax1, ax2) = plt.subplots(
        2, 1, figsize=(9, 5.6), sharex=True,
        gridspec_kw={"height_ratios": [1, 1], "hspace": 0.12})

    # 上：QP
    ax1.plot(frames, qps, "-o", color=C_CBR, ms=4, lw=1.8)
    ax1.axhline(28, color=C_GRAY, ls="--", lw=1.2, label="init_qp=28")
    ax1.annotate("运动段升到 29\n压比特", (16, 29),
                 xytext=(11, 24), fontsize=8.5, color=C_FIXED,
                 arrowprops=dict(arrowstyle="->", color=C_FIXED))
    ax1.annotate("静止段降到 12\n提画质", (23, 12),
                 xytext=(24.5, 16), fontsize=8.5, color=C_BUF,
                 arrowprops=dict(arrowstyle="->", color=C_BUF))
    ax1.set_ylabel("帧级 QP", fontsize=10)
    ax1.set_title("CBR：QP 随缓冲区反向调整，缓冲区始终落在安全区间",
                  fontsize=12, pad=8)
    ax1.legend(fontsize=8.5, loc="lower left")
    ax1.grid(True, ls=":", alpha=0.4)
    ax1.set_ylim(8, 33)
    for spine in ("top", "right"):
        ax1.spines[spine].set_visible(False)

    # 下：缓冲区占用
    ax2.fill_between(frames, 0, bufs, color=C_BUF, alpha=0.18)
    ax2.plot(frames, bufs, "-s", color=C_BUF, ms=4, lw=1.8, label="缓冲区占用")
    ax2.axhline(BUFFER_SIZE / 1e6, color=C_FIXED, ls="--", lw=1.4,
                label=f"上限 {BUFFER_SIZE/1e6:.0f}M（满则丢帧）")
    ax2.axhline(0, color=C_GRAY, ls="--", lw=1.0, label="下限 0（空则浪费）")
    ax2.set_ylabel("缓冲区占用（Mbit）", fontsize=10)
    ax2.set_xlabel("帧序号", fontsize=10)
    ax2.legend(fontsize=8.5, loc="center right")
    ax2.grid(True, ls=":", alpha=0.4)
    ax2.set_ylim(-0.2, 4.3)
    ax2.set_xlim(-1, 30)
    for spine in ("top", "right"):
        ax2.spines[spine].set_visible(False)

    fig.savefig(os.path.join(HERE, "cbr_qp_buffer.png"), dpi=150,
                bbox_inches="tight")
    plt.close(fig)


if __name__ == "__main__":
    fixed, cbr = load_data()
    # 打印几个关键量，供文章引用核对
    f_bits = [r["bits"] for r in fixed]
    c_bits = [r["bits"] for r in cbr]
    c_qp = [r["qp"] for r in cbr]
    c_buf = [r["buffer"] for r in cbr]
    fps = 30
    fixed_avg_kbps = sum(f_bits) / len(f_bits) * fps / 1000.0
    cbr_avg_kbps = sum(c_bits) / len(c_bits) * fps / 1000.0
    print(f"fixed bits range: [{min(f_bits)}, {max(f_bits)}]")
    print(f"cbr bits range:   [{min(c_bits)}, {max(c_bits)}]")
    print(f"cbr qp range:     [{min(c_qp)}, {max(c_qp)}]")
    print(f"cbr buffer range: [{min(c_buf):.0f}, {max(c_buf):.0f}]")
    print(f"fixed avg kbps:   {fixed_avg_kbps:.1f}")
    print(f"cbr avg kbps:     {cbr_avg_kbps:.1f}  "
          f"(err {abs(cbr_avg_kbps-2000)/2000*100:.1f}%)")

    chart_stage_map()
    chart_bits_compare(fixed, cbr)
    chart_cbr_qp_buffer(cbr)
    print("charts generated:",
          [f for f in os.listdir(HERE) if f.endswith(".png")])
