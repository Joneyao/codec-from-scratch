#!/usr/bin/env python3
"""A6 配图：读取 parser_demo 解析真实 .jpg 导出的真实数据绘图。

数据来源：third-party/codec-from-scratch/jpeg/decoder/chart_data/
"""
import os

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
DATA = os.path.join(REPO, "jpeg/decoder/chart_data")


def load_markers(name):
    rows = []
    with open(os.path.join(DATA, name)) as f:
        for line in f:
            p = line.split()
            rows.append((int(p[0]), p[1], p[2], int(p[3])))
    return rows


# 图1：真实相机 .jpg 的 marker 序列时间线（含 EXIF）
def chart_marker_timeline():
    cam = load_markers("markers_camera_photo.txt")
    out = load_markers("markers_out.txt")

    fig, axes = plt.subplots(2, 1, figsize=(11.5, 5.6))
    palette = {
        "SOI": "#7D3C98", "APP0": "#2E86C1", "APP1(EXIF)": "#E74C3C",
        "DQT": "#F39C12", "SOF0": "#27AE60", "DHT": "#16A085",
        "SOS": "#8E44AD", "EOI": "#7D3C98",
    }

    for ax, rows, title in zip(
            axes, [cam, out],
            ["相机拍的 camera_photo.jpg（300×200，冒出了 EXIF）",
             "我们编码器产出的 out.jpg（256×256，没有 EXIF）"]):
        x = 0
        for off, code, name, length in rows:
            w = max(length, 6)
            color = palette.get(name, "#95A5A6")
            ax.barh(0, w, left=x, height=0.6, color=color,
                    edgecolor="white")
            label = name.replace("(EXIF)", "\nEXIF")
            ax.text(x + w / 2, 0, label, ha="center", va="center",
                    fontsize=8, color="white", rotation=0)
            x += w + 3
        ax.set_title(title, fontsize=12)
        ax.set_yticks([])
        ax.set_xlim(-5, x + 5)
        ax.set_xlabel("按段排列（宽度示意段长，非真实比例）", fontsize=9)
        for s in ["top", "right", "left"]:
            ax.spines[s].set_visible(False)

    fig.suptitle("解码器读一张 .jpg：先扫 marker，再按段类型各自处理",
                 fontsize=13)
    plt.tight_layout(rect=[0, 0, 1, 0.95])
    plt.savefig(os.path.join(HERE, "marker_timeline.png"), dpi=110)
    plt.close()


# 图2：从真实 jpg 解出的量化表热力图（相机自定义表 vs 我们的表）
def chart_quant_tables():
    cam = np.loadtxt(os.path.join(DATA, "quant0_camera_photo.txt"))
    out = np.loadtxt(os.path.join(DATA, "quant0_out.txt"))
    fig, axes = plt.subplots(1, 2, figsize=(11, 5)) 
    for ax, q, t in zip(
            axes, [cam, out],
            ["相机 .jpg 里解出的亮度量化表", "我们 out.jpg 的亮度量化表"]):
        im = ax.imshow(q, cmap="YlOrRd")
        for (i, j), v in np.ndenumerate(q):
            ax.text(j, i, str(int(v)), ha="center", va="center",
                    fontsize=8,
                    color="white" if v > q.max() * 0.6 else "black")
        ax.set_title(t, fontsize=11)
        ax.set_xticks([]); ax.set_yticks([])
    fig.suptitle("量化表必须从文件里读出来——每个 .jpg 可能都不一样（真实解析数据）",
                 fontsize=12)
    plt.tight_layout(rect=[0, 0, 1, 0.95])
    plt.savefig(os.path.join(HERE, "quant_from_file.png"), dpi=110)
    plt.close()


# 图3：霍夫曼表 BITS 数组（码长分布）
def chart_huffman_bits():
    bits = {}
    with open(os.path.join(DATA, "huff_bits_camera_photo.txt")) as f:
        for line in f:
            p = line.split()
            bits[p[0]] = [int(v) for v in p[1:17]]

    fig, ax = plt.subplots(figsize=(9, 4.6))
    lens = np.arange(1, 17)
    width = 0.2
    colors = {"DC0": "#2E86C1", "AC0": "#E74C3C",
              "DC1": "#27AE60", "AC1": "#F39C12"}
    for i, (name, arr) in enumerate(bits.items()):
        ax.bar(lens + (i - 1.5) * width, arr, width,
               label=name, color=colors.get(name))
    ax.set_xlabel("霍夫曼码字长度（bit）")
    ax.set_ylabel("该码长的符号个数")
    ax.set_title("从 DHT 段重建霍夫曼表：BITS 数组说清每种码长有几个符号")
    ax.set_xticks(lens)
    ax.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(HERE, "huffman_bits.png"), dpi=110)
    plt.close()


# 图4（形象层）：解码器"读信"流程
def chart_parse_flow():
    from matplotlib.patches import FancyArrowPatch, FancyBboxPatch
    fig, ax = plt.subplots(figsize=(11, 3.4))
    ax.set_xlim(0, 12); ax.set_ylim(0, 3); ax.axis("off")

    def box(x, text, fc, ec, w=2.2):
        ax.add_patch(FancyBboxPatch((x, 1.0), w, 1.2,
                     boxstyle="round,pad=0.05,rounding_size=0.1",
                     fc=fc, ec=ec, lw=1.8))
        ax.text(x + w / 2, 1.6, text, ha="center", va="center", fontsize=10.5)

    def arrow(x1, x2):
        ax.add_patch(FancyArrowPatch((x1, 1.6), (x2, 1.6),
                     arrowstyle="-|>", mutation_scale=15, color="#555", lw=1.6))

    ax.text(6, 2.75, "解码器读 .jpg 像读一封格式固定的信", ha="center",
            fontsize=13, fontweight="bold")
    box(0.2, "看到 0xFF\n一个段来了", "#EBDEF0", "#7D3C98")
    box(2.9, "读第二字节\n判段类型", "#D6EAF8", "#2E86C1", w=2.4)
    box(5.9, "读两字节段长\n知道多长", "#FCF3CF", "#B7950B", w=2.4)
    box(8.9, "处理或跳过\n→ 下一段", "#D5F5E3", "#27AE60", w=2.6)
    arrow(2.4, 2.9); arrow(5.3, 5.9); arrow(8.3, 8.9)
    plt.tight_layout()
    plt.savefig(os.path.join(HERE, "parse_flow.png"), dpi=110)
    plt.close()


if __name__ == "__main__":
    chart_marker_timeline()
    chart_quant_tables()
    chart_huffman_bits()
    chart_parse_flow()
    print("charts generated in", HERE)
