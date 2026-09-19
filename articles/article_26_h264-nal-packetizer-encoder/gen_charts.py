#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""D6 手搓 H.264 编码器 #06 配图生成（NAL 封装 + 端到端自洽）。

所有数据来自仓库真实运行结果：
  third-party/codec-from-scratch/h264/encoder/build/h264_encoder
  third-party/codec-from-scratch/h264/encoder/chart_data/{nal_layout,encode_verify}.txt

真实数据来源（camera_photo.ppm, QP=28 为 canonical 样例）：
  - nal_layout.txt: SPS=13, PPS=8, IDR_slice=3074, 总 3095 字节；
    编码 304x208（真实 300x200），247 宏块全 I_4x4，1 个 IDR 帧。
  - encode_verify.txt: 自解码器 OK / ffmpeg OK；
    编码写入非零亮度系数=1152，自解=1152（逐系数一致）；
    PSNR self=36.16 / ffmpeg=36.08 / 编码器内部重建=36.28 dB。
  - QP 扫描: QP20→9562B/37.28dB / QP28→3095B/36.16dB / QP36→1657B/34.91dB，
    三档 self+ffmpeg 全 OK。
"""
import os
import sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.font_manager as fm
from matplotlib.patches import Rectangle

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

OUT = os.path.dirname(os.path.abspath(__file__))

# 配色
C_OK = "#4c9f70"      # 绿：通过/自洽
C_BAD = "#d1495b"     # 红：代价/丢弃
C_BLUE = "#2f6690"    # 蓝：主色
C_GRAY = "#b8b8b8"    # 灰
C_YELLOW = "#e6a817"  # 黄：强调
C_SPS = "#8e6fb0"     # 紫：SPS
C_PPS = "#e6934f"     # 橙：PPS


# ---------------------------------------------------------------------------
# 图0: 进度地图（复用系列共享骨架，D6 高亮 "pack" = NAL 封装环节）
# ---------------------------------------------------------------------------
def chart_stage_map():
    sys.path.insert(0, os.path.join(OUT, ".."))
    from pipeline_map import draw_h264_encode_map
    draw_h264_encode_map("pack", os.path.join(OUT, "stage_map.png"))
    # 系列（编码链）总览图，收官用
    draw_h264_encode_map("__all__", os.path.join(OUT, "encode_overview.png"))


# ---------------------------------------------------------------------------
# 图1: 码流 NAL 结构分解（SPS13 / PPS8 / IDR3074 字节，slice 占绝大部分）
# ---------------------------------------------------------------------------
def chart_nal_layout():
    parts = [("SPS", 13, C_SPS), ("PPS", 8, C_PPS), ("IDR slice", 3074, C_BLUE)]
    total = sum(p[1] for p in parts)  # 3095

    fig, (axb, axp) = plt.subplots(
        1, 2, figsize=(9.6, 3.8), gridspec_kw={"width_ratios": [2.0, 1.0]})

    # 左：水平堆叠条（按真实字节比例，slice 一眼占满）
    left = 0
    for name, val, color in parts:
        axb.barh(0, val, left=left, height=0.55, color=color,
                 edgecolor="white")
        # SPS/PPS 太小，标签放条外上方引线
        if val >= 200:
            axb.text(left + val / 2, 0, f"{name}\n{val} 字节", ha="center",
                     va="center", color="white", fontsize=11,
                     fontweight="bold")
        left += val
    # SPS/PPS 引线标注
    axb.annotate("SPS 13 字节", xy=(6.5, 0.28), xytext=(300, 0.75),
                 fontsize=9.5, color=C_SPS,
                 arrowprops=dict(arrowstyle="->", color=C_SPS))
    axb.annotate("PPS 8 字节", xy=(17, 0.28), xytext=(300, 0.5),
                 fontsize=9.5, color=C_PPS,
                 arrowprops=dict(arrowstyle="->", color=C_PPS))
    axb.set_xlim(0, total)
    axb.set_ylim(-0.5, 1.0)
    axb.set_yticks([])
    axb.set_xlabel("字节偏移", fontsize=10)
    axb.set_title("一条 .h264 码流的字节构成（共 3095 字节）",
                  fontsize=11.5, pad=8)
    for s in ("top", "right", "left"):
        axb.spines[s].set_visible(False)

    # 右：饼图（参数集合计 21 字节 vs slice 3074 字节）
    sizes = [21, 3074]
    labels = ["SPS+PPS\n21 字节 (0.7%)", "IDR slice\n3074 字节 (99.3%)"]
    axp.pie(sizes, labels=labels, colors=[C_PPS, C_BLUE],
            autopct="", startangle=90, counterclock=False,
            wedgeprops=dict(edgecolor="white", linewidth=1.5),
            textprops=dict(fontsize=9.5))
    axp.set_title("参数集只占零头，载荷是绝对主体", fontsize=11, pad=8)

    fig.suptitle("自己编的码流：13B 头 + 8B 头 + 3074B 载荷 = 3095B",
                 fontsize=12.5, fontweight="bold", y=1.02)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "nal_layout.png"), dpi=150,
                bbox_inches="tight")
    plt.close(fig)


# ---------------------------------------------------------------------------
# 图2: 端到端自洽性——同一条码流，自解码器与 ffmpeg 双双认得 + 系数逐个对齐
# ---------------------------------------------------------------------------
def chart_self_consistency():
    fig, (ax1, ax2) = plt.subplots(
        1, 2, figsize=(9.6, 4.2), gridspec_kw={"width_ratios": [1.15, 1.0]})

    # 左：三方 PSNR 柱（同量级，差 < 0.5 dB）
    names = ["编码器\n内部重建", "自解码器\n(本仓库)", "ffmpeg\n(外部)"]
    psnr = [36.28, 36.16, 36.08]
    colors = [C_YELLOW, C_OK, C_BLUE]
    bars = ax1.bar(names, psnr, color=colors, edgecolor="white", width=0.6)
    for b, v in zip(bars, psnr):
        ax1.text(b.get_x() + b.get_width() / 2, v + 0.02, f"{v:.2f} dB",
                 ha="center", va="bottom", fontsize=10.5, fontweight="bold")
    ax1.set_ylim(35.5, 36.6)
    ax1.set_ylabel("亮度 PSNR (dB)", fontsize=10)
    ax1.set_title("三方 PSNR 落在同一量级（差 < 0.5 dB）", fontsize=11, pad=8)
    ax1.axhline(36.16, color=C_GRAY, ls="--", lw=0.8, zorder=0)
    for s in ("top", "right"):
        ax1.spines[s].set_visible(False)

    # 右：比 PSNR 更硬的比特级证据——非零亮度系数逐个对齐
    ax2.axis("off")
    ax2.text(0.5, 0.93, "比 PSNR 更硬的证据", ha="center",
             transform=ax2.transAxes, fontsize=12, fontweight="bold",
             color=C_BLUE)
    # 两个大数字并排 + 等号
    ax2.text(0.24, 0.55, "1152", ha="center", va="center",
             transform=ax2.transAxes, fontsize=34, fontweight="bold",
             color=C_OK)
    ax2.text(0.5, 0.55, "=", ha="center", va="center",
             transform=ax2.transAxes, fontsize=30, color="#333")
    ax2.text(0.76, 0.55, "1152", ha="center", va="center",
             transform=ax2.transAxes, fontsize=34, fontweight="bold",
             color=C_OK)
    ax2.text(0.24, 0.34, "编码写入的\n非零亮度系数", ha="center", va="center",
             transform=ax2.transAxes, fontsize=9.5, color="#555")
    ax2.text(0.76, 0.34, "自解码器解出的\n非零亮度系数", ha="center", va="center",
             transform=ax2.transAxes, fontsize=9.5, color="#555")
    ax2.text(0.5, 0.12, "逐系数一致 —— 熵解码原样还原了每一个量化系数",
             ha="center", va="center", transform=ax2.transAxes,
             fontsize=9.5, color=C_OK, fontweight="bold")
    ax2.add_patch(Rectangle((0.03, 0.05), 0.94, 0.9, transform=ax2.transAxes,
                            facecolor=C_OK, alpha=0.08, edgecolor=C_OK,
                            lw=1.5))

    fig.suptitle("自己编的码流：自解码器认、ffmpeg 也认（camera_photo, QP=28）",
                 fontsize=12.5, fontweight="bold", y=1.02)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "self_consistency.png"), dpi=150,
                bbox_inches="tight")
    plt.close(fig)


# ---------------------------------------------------------------------------
# 图3: QP 扫描的码率(字节) vs PSNR 权衡曲线（QP20/28/36 三点，双轴）
# ---------------------------------------------------------------------------
def chart_qp_tradeoff():
    qps = [20, 28, 36]
    bytes_ = [9562, 3095, 1657]
    psnr = [37.28, 36.16, 34.91]

    fig, ax1 = plt.subplots(figsize=(8.6, 4.6))
    ax2 = ax1.twinx()

    # 左轴：码率（字节）柱
    bars = ax1.bar([q - 0.0 for q in qps], bytes_, width=3.2, color=C_BLUE,
                   alpha=0.32, edgecolor=C_BLUE, label="码流大小 (字节)")
    for q, b in zip(qps, bytes_):
        ax1.text(q, b + 120, f"{b}B", ha="center", va="bottom",
                 fontsize=10, color=C_BLUE, fontweight="bold")
    ax1.set_xlabel("QP（量化参数，唯一码率旋钮）", fontsize=10.5)
    ax1.set_ylabel("码流大小（字节）", fontsize=10.5, color=C_BLUE)
    ax1.tick_params(axis="y", labelcolor=C_BLUE)
    ax1.set_ylim(0, 10800)
    ax1.set_xticks(qps)

    # 右轴：PSNR 折线
    ax2.plot(qps, psnr, "-o", color=C_BAD, lw=2.2, markersize=9,
             label="亮度 PSNR (dB)")
    for q, p in zip(qps, psnr):
        ax2.text(q, p + 0.12, f"{p:.2f}dB", ha="center", va="bottom",
                 fontsize=10, color=C_BAD, fontweight="bold")
    ax2.set_ylabel("亮度 PSNR（dB，self-decoder）", fontsize=10.5, color=C_BAD)
    ax2.tick_params(axis="y", labelcolor=C_BAD)
    ax2.set_ylim(34.0, 38.2)

    # 方向标注
    ax1.annotate("QP 拧大 → 更小、更糊", xy=(36, 1657), xytext=(30, 6500),
                 fontsize=10, color="#555",
                 arrowprops=dict(arrowstyle="->", color="#888"))

    ax1.set_title("QP 扫描：码率与画质的真实权衡（camera，三档 self+ffmpeg 全 OK）",
                  fontsize=11.5, pad=10)
    for s in ("top",):
        ax1.spines[s].set_visible(False)
        ax2.spines[s].set_visible(False)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "qp_tradeoff.png"), dpi=150,
                bbox_inches="tight")
    plt.close(fig)


# ---------------------------------------------------------------------------
# 图4: 两个正向编码细节——ue(v) 码字长度阶梯 + emulation prevention 转义示意
# （形象层：不含真实测量数据，纯规则演示，帮助读者理解封装层两件核心工艺）
# ---------------------------------------------------------------------------
def chart_encoding_tricks():
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(8.8, 5.4),
                                   gridspec_kw={"height_ratios": [1.0, 0.85]})

    # 上：ue(v) 码字，小数字更短
    codes = [
        (0, "1"),
        (1, "010"),
        (2, "011"),
        (3, "00100"),
        (4, "00101"),
        (5, "00110"),
        (6, "00111"),
    ]
    for i, (cn, bits) in enumerate(codes):
        length = len(bits)
        ax1.barh(len(codes) - 1 - i, length, height=0.62, color=C_BLUE,
                 alpha=0.30, edgecolor=C_BLUE)
        ax1.text(-0.25, len(codes) - 1 - i, f"code_num={cn}", ha="right",
                 va="center", fontsize=9.5, color="#444")
        ax1.text(length + 0.15, len(codes) - 1 - i, bits, ha="left",
                 va="center", fontsize=10.5, family="monospace",
                 color=C_BLUE, fontweight="bold")
    ax1.set_xlim(-2.6, 8.5)
    ax1.set_ylim(-0.6, len(codes) - 0.2)
    ax1.set_yticks([])
    ax1.set_xlabel("码字位数", fontsize=10)
    ax1.set_title("指数哥伦布 ue(v)：数字越小，码字越短", fontsize=11.5, pad=8)
    for s in ("top", "right", "left"):
        ax1.spines[s].set_visible(False)

    # 下：emulation prevention 转义示意
    ax2.axis("off")
    ax2.set_xlim(0, 12)
    ax2.set_ylim(0, 3)
    ax2.text(6, 2.72, "emulation prevention：插 0x03 防止载荷冒充起始码",
             ha="center", fontsize=11.5, fontweight="bold")

    def draw_bytes(y, seq, hi_idx, color, label):
        ax2.text(0.1, y + 0.30, label, ha="left", va="center", fontsize=9.5,
                 color="#555")
        x0 = 3.4
        for j, bt in enumerate(seq):
            fc = color if j == hi_idx else "#eef2f5"
            tc = "white" if j == hi_idx else "#333"
            ax2.add_patch(Rectangle((x0 + j * 1.15, y), 1.02, 0.6,
                                    facecolor=fc, edgecolor="#b8c4cc"))
            ax2.text(x0 + j * 1.15 + 0.51, y + 0.30, bt, ha="center",
                     va="center", fontsize=10, family="monospace",
                     color=tc, fontweight="bold")

    draw_bytes(1.55, ["00", "00", "01"], None, C_BAD, "编码前（危险：撞起始码）")
    draw_bytes(0.45, ["00", "00", "03", "01"], 2, C_OK, "转义后（插入 0x03）")
    ax2.annotate("", xy=(6.0, 1.15), xytext=(6.0, 1.5),
                 arrowprops=dict(arrowstyle="-|>", color="#888", lw=1.6))
    ax2.text(9.0, 1.32, "解码端读到 00 00 03\n把 0x03 吞掉还原",
             ha="left", va="center", fontsize=9, color=C_OK)

    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "encoding_tricks.png"), dpi=150,
                bbox_inches="tight")
    plt.close(fig)


if __name__ == "__main__":
    chart_stage_map()
    chart_nal_layout()
    chart_encoding_tricks()
    chart_self_consistency()
    chart_qp_tradeoff()
    print("charts generated:",
          sorted(f for f in os.listdir(OUT) if f.endswith(".png")))
