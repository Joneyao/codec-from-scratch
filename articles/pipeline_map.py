#!/usr/bin/env python3
"""JPEG 系列共享的"进度地图"绘图函数。

每篇文章开头放一张同骨架的流程图，高亮当前篇所处的环节，
让读者一眼看到"本篇在整个编码/解码流程的哪一步"。全系列复用同一骨架，
只是高亮的环节不同——连着读会形成"地图记忆"。

用法（在各篇 gen_charts.py 里）：
    import sys, os
    sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
    from pipeline_map import draw_encode_map, draw_decode_map
    draw_encode_map("dct", os.path.join(HERE, "stage_map.png"))
"""
import matplotlib
matplotlib.use("Agg")
import matplotlib.font_manager as fm
fm._load_fontmanager(try_read_cache=False)
import matplotlib.pyplot as plt
from matplotlib.patches import FancyArrowPatch, FancyBboxPatch

plt.rcParams["font.family"] = "Noto Sans CJK JP"
plt.rcParams["axes.unicode_minus"] = False

# 编码链五环节：key -> 显示名（两行）
ENCODE_STAGES = [
    ("color", "色彩转换\nRGB→YCbCr"),
    ("dct", "DCT 变换\n空间→频域"),
    ("quant", "量化\n丢高频"),
    ("entropy", "熵编码\nZigZag+霍夫曼"),
    ("jfif", "JFIF 封装\n拼成 .jpg"),
]

# 解码链四环节
DECODE_STAGES = [
    ("parse", "解析\nmarker+表"),
    ("entropy", "熵解码\n变长码字"),
    ("idct", "反量化\n+IDCT"),
    ("assemble", "上采样\n+转 RGB"),
]

HL_FC = "#2E86C1"      # 高亮填充
HL_TXT = "white"
DIM_FC = "#EAECEE"     # 灰化填充
DIM_EC = "#BFC9CA"
DIM_TXT = "#7F8C8D"


def _draw(stages, highlight, out_path, title, in_label, out_label):
    # highlight == "__all__" 时作总览：所有环节都用高亮色（不区分某一篇）
    overview = (highlight == "__all__")
    n = len(stages)
    fig_w = 2.0 * n + 2.2
    fig, ax = plt.subplots(figsize=(fig_w, 2.2))
    ax.set_xlim(0, fig_w)
    ax.set_ylim(0, 2.2)
    ax.axis("off")

    box_w, box_h, gap = 1.75, 1.05, 0.55
    y = 0.55
    x = 0.15

    # 输入标签
    ax.text(x + 0.15, y + box_h / 2, in_label, ha="left", va="center",
            fontsize=10, color="#566573", style="italic")
    x += 1.05

    centers = []
    for key, name in stages:
        hit = overview or (key == highlight)
        fc = HL_FC if hit else DIM_FC
        ec = HL_FC if hit else DIM_EC
        txt = HL_TXT if hit else DIM_TXT
        lw = 2.4 if hit else 1.2
        ax.add_patch(FancyBboxPatch(
            (x, y), box_w, box_h,
            boxstyle="round,pad=0.04,rounding_size=0.12",
            fc=fc, ec=ec, lw=lw, zorder=2))
        ax.text(x + box_w / 2, y + box_h / 2, name, ha="center",
                va="center", fontsize=10.5,
                fontweight="bold" if hit else "normal", color=txt, zorder=3)
        if hit and not overview:
            ax.text(x + box_w / 2, y + box_h + 0.12, "▲ 本篇",
                    ha="center", va="bottom", fontsize=10,
                    color=HL_FC, fontweight="bold")
        centers.append((x, x + box_w))
        x += box_w + gap

    # 箭头连接
    for i in range(n - 1):
        ax.add_patch(FancyArrowPatch(
            (centers[i][1], y + box_h / 2),
            (centers[i + 1][0], y + box_h / 2),
            arrowstyle="-|>", mutation_scale=13, color="#95A5A6",
            lw=1.5, zorder=1))
    # 首尾箭头
    ax.add_patch(FancyArrowPatch(
        (1.0, y + box_h / 2), (centers[0][0], y + box_h / 2),
        arrowstyle="-|>", mutation_scale=13, color="#95A5A6", lw=1.5))
    ax.text(x + 0.05, y + box_h / 2, out_label, ha="left", va="center",
            fontsize=10, color="#566573", style="italic")

    ax.text(fig_w / 2, 2.0, title, ha="center", va="center",
            fontsize=12.5, fontweight="bold")
    plt.tight_layout()
    plt.savefig(out_path, dpi=115, bbox_inches="tight")
    plt.close()


def draw_encode_map(highlight, out_path):
    _draw(ENCODE_STAGES, highlight, out_path,
          "JPEG 编码流程 · 本篇位置", "像素", ".jpg")


def draw_decode_map(highlight, out_path):
    _draw(DECODE_STAGES, highlight, out_path,
          "JPEG 解码流程 · 本篇位置", ".jpg", "像素")


# MJPEG 链：把视频当成一叠 JPEG，逐帧编/解 + 容器封装
MJPEG_STAGES = [
    ("encode", "逐帧编码\n每帧独立 JPEG"),
    ("mux", "AVI 封装\n帧排进容器"),
    ("demux", "AVI 解封装\n取出每帧"),
    ("decode", "逐帧解码\n还原画面"),
]


def draw_mjpeg_map(highlight, out_path):
    title = ("MJPEG 完整流程 · 系列总览" if highlight in ("__all__", "all", "")
             else "MJPEG 流程 · 本篇位置")
    key = "__all__" if highlight in ("all", "", "__all__") else highlight
    _draw(MJPEG_STAGES, key, out_path, title, "视频帧", "视频帧")


# H.264 解码链七环节（C1-C9 映射到这些环节）
H264_DECODE_STAGES = [
    ("nal", "NAL 切分\n找起始码"),
    ("params", "SPS/PPS\n读参数"),
    ("slice", "Slice/宏块\n划分网格"),
    ("predict", "预测\n帧内/帧间"),
    ("transform", "反变换\n重建残差"),
    ("deblock", "去块滤波\n消方块"),
    ("output", "输出\nYUV 帧"),
]


def draw_h264_decode_map(highlight, out_path):
    title = ("H.264 解码完整流程 · 系列总览"
             if highlight in ("__all__", "all", "")
             else "H.264 解码流程 · 本篇位置")
    key = "__all__" if highlight in ("all", "", "__all__") else highlight
    _draw(H264_DECODE_STAGES, key, out_path, title, ".h264", "YUV")


# H.264 编码链五环节（D1-D6）
H264_ENCODE_STAGES = [
    ("predict", "预测决策\n模式/运动"),
    ("transform", "变换量化\nQP 控码率"),
    ("entropy", "熵编码\nCAVLC"),
    ("recon", "重建帧\n供后续参考"),
    ("pack", "NAL 封装\n出码流"),
]


def draw_h264_encode_map(highlight, out_path):
    title = ("H.264 编码完整流程 · 系列总览"
             if highlight in ("__all__", "all", "")
             else "H.264 编码流程 · 本篇位置")
    key = "__all__" if highlight in ("all", "", "__all__") else highlight
    _draw(H264_ENCODE_STAGES, key, out_path, title, "YUV", ".h264")


if __name__ == "__main__":
    # 自测：生成全部环节的预览
    import os
    d = os.path.dirname(os.path.abspath(__file__))
    for k, _ in ENCODE_STAGES:
        draw_encode_map(k, os.path.join(d, f"_preview_enc_{k}.png"))
    for k, _ in DECODE_STAGES:
        draw_decode_map(k, os.path.join(d, f"_preview_dec_{k}.png"))
    print("preview maps generated")
