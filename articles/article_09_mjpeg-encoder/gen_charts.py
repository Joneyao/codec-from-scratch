#!/usr/bin/env python3
"""B1 MJPEG 编码器配图。

数据来源全部为 codec-from-scratch/mjpeg/encoder 真实跑出的结果：
  - 每帧 JPEG 字节数：mjpeg_encoder 的 stdout（chart_data/encode_stats.txt）
  - AVI 容器结构偏移：直接解析真实 out.avi 得到
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

plt.rcParams["font.family"] = "Noto Sans CJK JP"
plt.rcParams["axes.unicode_minus"] = False

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(
    HERE, "../../../../../third-party/codec-from-scratch"))
ENC = os.path.join(REPO, "mjpeg/encoder")
AVI = os.path.join(ENC, "out.avi")
STATS = os.path.join(ENC, "chart_data/encode_stats.txt")

C_BLUE = "#2563eb"
C_GREEN = "#16a34a"
C_ORANGE = "#ea580c"
C_GRAY = "#94a3b8"
C_PURPLE = "#7c3aed"


def load_per_frame_bytes():
    with open(STATS) as f:
        for line in f:
            if line.startswith("per-frame bytes:"):
                nums = line.split(":", 1)[1].split()
                return [int(x) for x in nums]
    raise RuntimeError("per-frame bytes not found in stats")


def sync_stage_map():
    import sys
    sys.path.insert(0, os.path.join(HERE, ".."))
    from pipeline_map import draw_mjpeg_map
    draw_mjpeg_map("encode", os.path.join(HERE, "stage_map.png"))
    print("stage_map.png done")


def chart_per_frame_size():
    """真实每帧 JPEG 字节数柱状图：体现帧间无预测，每帧都是"从头编"的大小。"""
    sizes = load_per_frame_bytes()
    n = len(sizes)
    avg = sum(sizes) / n
    fig, ax = plt.subplots(figsize=(9, 4.2))
    idx = np.arange(1, n + 1)
    ax.bar(idx, sizes, color=C_BLUE, width=0.72, zorder=3)
    ax.axhline(avg, color=C_ORANGE, linestyle="--", linewidth=1.6, zorder=4,
               label=f"平均 {avg:,.0f} 字节/帧")
    ax.set_xlabel("帧序号")
    ax.set_ylabel("该帧 JPEG 字节数")
    ax.set_title("每帧都从头编：20 帧各自的 JPEG 大小（quality=80）",
                 fontweight="bold")
    ax.set_xticks(idx)
    ax.set_xticklabels([str(i) for i in idx], fontsize=8)
    ax.set_ylim(0, max(sizes) * 1.18)
    ax.grid(axis="y", alpha=0.3, zorder=0)
    ax.legend(loc="upper right")
    # 标注最小/最大帧
    mn, mx = min(sizes), max(sizes)
    ax.annotate(f"最小 {mn:,}", (sizes.index(mn) + 1, mn),
                textcoords="offset points", xytext=(0, 6),
                ha="center", fontsize=8, color=C_GREEN)
    ax.annotate(f"最大 {mx:,}", (sizes.index(mx) + 1, mx),
                textcoords="offset points", xytext=(0, 6),
                ha="center", fontsize=8, color=C_ORANGE)
    plt.tight_layout()
    out = os.path.join(HERE, "per_frame_size.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("per_frame_size.png done  (avg=%.0f, min=%d, max=%d, total=%d)"
          % (avg, mn, mx, sum(sizes)))


def parse_avi_layout():
    """解析真实 out.avi，取关键 chunk 的字节偏移与长度。"""
    data = open(AVI, "rb").read()

    def u32(p):
        return struct.unpack("<I", data[p:p + 4])[0]

    layout = []
    total = len(data)
    layout.append(("RIFF 头 'AVI '", 0, 12))
    h = data.find(b"hdrl")
    hlen = u32(h - 4)
    layout.append(("hdrl 头部列表\n(avih+strh+strf)", h - 8, hlen + 8))
    m = data.find(b"movi")
    mlen = u32(m - 4)
    layout.append(("movi 数据列表\n(20 个 00dc 帧)", m - 8, mlen + 8))
    idx = data.find(b"idx1")
    ilen = u32(idx + 4)
    layout.append(("idx1 索引\n(每帧 16 字节)", idx, ilen + 8))
    return layout, total


def chart_avi_structure():
    """AVI 容器结构分层图：真实字节偏移 + 真实长度，等宽分段（不按比例，
    否则 12/200 字节的头相对 11 万字节的 movi 会看不见）。"""
    layout, total = parse_avi_layout()
    # (显示名, 偏移, 长度, 颜色, 补充说明)
    colors = [C_GRAY, C_GREEN, C_BLUE, C_PURPLE]
    fig, ax = plt.subplots(figsize=(9.6, 4.0))
    n = len(layout)
    seg_w = 1.0 / n
    for i, ((name, off, size), col) in enumerate(zip(layout, colors)):
        x = i * seg_w
        ax.add_patch(plt.Rectangle((x, 0.34), seg_w * 0.92, 0.34,
                                   facecolor=col, edgecolor="white",
                                   linewidth=2, zorder=3))
        cx = x + seg_w * 0.46
        ax.text(cx, 0.51, name, ha="center", va="center", color="white",
                fontsize=9.5, fontweight="bold", zorder=4)
        # 长度标在块下方，偏移标在块上方
        ax.text(cx, 0.26, f"长度 {size:,} 字节", ha="center", va="top",
                fontsize=9, color="#334155")
        ax.text(x, 0.72, f"@字节 {off:,}", ha="left", va="bottom",
                fontsize=8.5, color="#64748b")
        # 段间箭头（顺序拼接）
        if i < n - 1:
            ax.annotate("", xy=((i + 1) * seg_w, 0.51),
                        xytext=(x + seg_w * 0.92, 0.51),
                        arrowprops=dict(arrowstyle="->", color="#94a3b8",
                                        lw=1.4))
    ax.text(0.5, 0.06,
            f"文件总长 {total:,} 字节；movi 占 {layout[2][2]/total*100:.1f}%，"
            f"头部+索引不到 {(1-layout[2][2]/total)*100:.1f}%",
            ha="center", fontsize=9.5, color=C_ORANGE)
    ax.set_xlim(-0.02, 1.0)
    ax.set_ylim(0, 0.95)
    ax.axis("off")
    ax.set_title("out.avi 真实字节布局：容器只负责排队，不碰像素",
                 fontweight="bold", fontsize=13)
    plt.tight_layout()
    out = os.path.join(HERE, "avi_structure.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("avi_structure.png done  (total=%d bytes)" % total)


def chart_stack_illustration():
    """形象层：MJPEG 把视频当一叠独立 JPEG（帧间无依赖箭头），
    对比 H.264 帧间有预测箭头。"""
    fig, (axl, axr) = plt.subplots(1, 2, figsize=(9.6, 4.6))

    def draw_frame(ax, x, y, w, h, color, label):
        ax.add_patch(plt.Rectangle((x, y), w, h, facecolor=color,
                                   edgecolor="#1e293b", linewidth=1.4,
                                   zorder=3))
        ax.text(x + w / 2, y + h / 2, label, ha="center", va="center",
                fontsize=10, fontweight="bold", color="#1e293b", zorder=4)

    # 左：MJPEG —— 一叠独立 JPEG，无箭头
    for i in range(4):
        draw_frame(axl, 0.12 * i, 0.12 * i, 0.55, 0.42, "#bfdbfe",
                   f"JPEG\n第{i+1}帧")
    axl.set_title("MJPEG：一叠独立 JPEG\n每帧自成一体，无帧间依赖",
                  fontsize=11.5, fontweight="bold")
    axl.set_xlim(-0.1, 1.1)
    axl.set_ylim(-0.1, 1.1)
    axl.axis("off")

    # 右：H.264 —— 帧间有预测箭头（I 帧 + P 帧链）
    fy = 0.5
    draw_frame(axr, 0.02, fy, 0.2, 0.42, "#fde68a", "I 帧")
    labels = ["P 帧", "P 帧", "P 帧"]
    xs = [0.28, 0.54, 0.80]
    prev_cx = 0.02 + 0.1
    for lab, x in zip(labels, xs):
        draw_frame(axr, x, fy, 0.2, 0.42, "#fecaca", lab)
        cx = x + 0.1
        axr.annotate("", xy=(x, fy + 0.21), xytext=(prev_cx + 0.06, fy + 0.21),
                     arrowprops=dict(arrowstyle="->", color=C_ORANGE, lw=2))
        prev_cx = cx
    axr.text(0.5, 0.28, "只存与前帧的差异", ha="center", fontsize=9.5,
             color=C_ORANGE)
    axr.set_title("H.264：帧间预测\n后帧靠前帧推出，箭头即依赖",
                  fontsize=11.5, fontweight="bold")
    axr.set_xlim(-0.05, 1.05)
    axr.set_ylim(0.0, 1.15)
    axr.axis("off")

    plt.tight_layout()
    out = os.path.join(HERE, "mjpeg_vs_h264.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("mjpeg_vs_h264.png done")


if __name__ == "__main__":
    sync_stage_map()
    chart_per_frame_size()
    chart_avi_structure()
    chart_stack_illustration()
    print("all charts done")
