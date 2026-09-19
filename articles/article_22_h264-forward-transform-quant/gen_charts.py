#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""D2 手搓 H.264 编码器 #02 配图生成（正向变换量化）。

所有数据来自仓库真实运行结果：
  third-party/codec-from-scratch/h264/encoder/chart_data/
    - fwd_transform.txt   ：QP=28 单个真实残差块的 残差/变换后/量化后 三个 4x4
    - qp_vs_nonzero.txt    ：同一块 QP 从 20 到 40 的非零系数个数曲线

生成：
  stage_map.png        —— 编码流程地图，高亮"变换量化"环节
  transform_stages.png —— 残差→变换后→量化后 三个 4x4 数值热力图并排
  qp_vs_nonzero.png    —— QP vs 非零系数个数曲线（D2 高潮图）
"""
import os
import sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

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
    import matplotlib.font_manager as fm
    fm._load_fontmanager(try_read_cache=False)
    plt.rcParams["font.family"] = "Noto Sans CJK JP"
plt.rcParams["axes.unicode_minus"] = False

HERE = os.path.dirname(os.path.abspath(__file__))

# ---- 真实数据（来自 fwd_transform.txt，QP=28）----
RESIDUAL = np.array([
    [31, 24, -9, -14],
    [18, 12, -6, -11],
    [-8, -5, 7, 10],
    [-13, -9, 12, 16],
])
FORWARD_W = np.array([
    [55, 72, 3, -9],
    [61, 528, 5, -81],
    [21, 16, 1, -7],
    [8, -46, 0, -8],
])
QUANT_C = np.array([
    [1, 1, 0, 0],
    [0, 3, 0, 0],
    [0, 0, 0, 0],
    [0, 0, 0, 0],
])

# ---- 真实数据（来自 qp_vs_nonzero.txt）----
QP_LIST = list(range(20, 41))
NONZERO = [7, 6, 5, 5, 5, 5, 4, 4, 3, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]


def _draw_matrix(ax, mat, title, cmap):
    """一个 4x4 数值热力图：颜色深浅表绝对值大小，每格标真实数字。"""
    absmat = np.abs(mat).astype(float)
    vmax = absmat.max() if absmat.max() > 0 else 1.0
    ax.imshow(absmat, cmap=cmap, vmin=0, vmax=vmax)
    for i in range(4):
        for j in range(4):
            v = mat[i][j]
            # 深色格用白字，浅色格用黑字
            shade = absmat[i][j] / vmax
            color = "white" if shade > 0.55 else "#2C3E50"
            ax.text(j, i, str(v), ha="center", va="center",
                    fontsize=13, fontweight="bold", color=color)
    ax.set_title(title, fontsize=12, fontweight="bold", pad=8)
    ax.set_xticks([]); ax.set_yticks([])
    for spine in ax.spines.values():
        spine.set_edgecolor("#BFC9CA")


def draw_transform_stages():
    """残差→变换后→量化后 三个 4x4 并排，直观看到能量集中 + 大片变 0。"""
    fig, axes = plt.subplots(1, 3, figsize=(11, 4.0))
    _draw_matrix(axes[0], RESIDUAL, "① 残差块\n(能量分散)", "Blues")
    _draw_matrix(axes[1], FORWARD_W, "② 正向变换后 W\n(能量集中左上角)", "Oranges")
    _draw_matrix(axes[2], QUANT_C, "③ 量化后 (QP=28)\n(高频大片变 0)", "Greens")
    fig.suptitle("一个真实残差块的变换量化三步：15 个非零系数 → 3 个",
                 fontsize=13.5, fontweight="bold", y=1.02)
    plt.tight_layout()
    out = os.path.join(HERE, "transform_stages.png")
    plt.savefig(out, dpi=120, bbox_inches="tight")
    plt.close()
    print("wrote", out)


def draw_transform_pair():
    """残差→变换后 两个 4x4 并排，用在"正向变换"小节，看能量集中。"""
    fig, axes = plt.subplots(1, 2, figsize=(7.6, 4.0))
    _draw_matrix(axes[0], RESIDUAL, "残差块\n(能量分散在各处)", "Blues")
    _draw_matrix(axes[1], FORWARD_W, "正向变换后 W\n(能量集中到左上角)", "Oranges")
    fig.suptitle("整数变换把残差能量攥到左上角低频区",
                 fontsize=13, fontweight="bold", y=1.02)
    plt.tight_layout()
    out = os.path.join(HERE, "transform_pair.png")
    plt.savefig(out, dpi=120, bbox_inches="tight")
    plt.close()
    print("wrote", out)


def draw_quant_pair():
    """变换后→量化后 两个 4x4 并排，用在"量化"小节，看大片变 0。"""
    fig, axes = plt.subplots(1, 2, figsize=(7.6, 4.0))
    _draw_matrix(axes[0], FORWARD_W, "变换后 W\n(15 个非零系数)", "Oranges")
    _draw_matrix(axes[1], QUANT_C, "量化后 (QP=28)\n(只剩 3 个非零)", "Greens")
    fig.suptitle("量化一刀切下去：高频小系数大片归零",
                 fontsize=13, fontweight="bold", y=1.02)
    plt.tight_layout()
    out = os.path.join(HERE, "quant_pair.png")
    plt.savefig(out, dpi=120, bbox_inches="tight")
    plt.close()
    print("wrote", out)


def draw_qp_vs_nonzero():
    """QP vs 非零系数个数曲线：QP 拧大→保留系数骤减→码率降。D2 高潮图。"""
    fig, ax = plt.subplots(figsize=(9, 4.6))
    ax.plot(QP_LIST, NONZERO, "-o", color="#2E86C1", lw=2.2,
            markersize=5, markerfacecolor="white", markeredgewidth=1.6,
            markeredgecolor="#2E86C1", zorder=3)

    # 标出 QP=28 这个点（本篇示例块所用 QP）
    ax.scatter([28], [3], s=170, color="#E74C3C", zorder=5)
    ax.annotate("QP=28 → 3 个非零系数\n(本篇示例块)",
                xy=(28, 3), xytext=(30.5, 5.4),
                fontsize=11, color="#C0392B", fontweight="bold",
                arrowprops=dict(arrowstyle="->", color="#C0392B", lw=1.6))

    # 标注"每 +6，非零数量级减半"的三个锚点
    for qp, nz in [(20, 7), (26, 4), (31, 1)]:
        ax.annotate(f"QP{qp}={nz}", xy=(qp, nz), xytext=(qp - 0.3, nz + 0.6),
                    fontsize=9.5, color="#566573")

    ax.set_xlabel("QP（量化参数，越大量化越粗）", fontsize=11.5)
    ax.set_ylabel("保留的非零系数个数", fontsize=11.5)
    ax.set_title("QP 每加约 6，非零系数量级减半 —— 码率被 QP 拧出来",
                 fontsize=13, fontweight="bold", pad=10)
    ax.set_xticks(range(20, 41, 2))
    ax.set_ylim(0, 8)
    ax.grid(True, ls="--", alpha=0.4)
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    plt.tight_layout()
    out = os.path.join(HERE, "qp_vs_nonzero.png")
    plt.savefig(out, dpi=120, bbox_inches="tight")
    plt.close()
    print("wrote", out)


def draw_stage_map():
    sys.path.insert(0, os.path.join(HERE, ".."))
    from pipeline_map import draw_h264_encode_map
    out = os.path.join(HERE, "stage_map.png")
    draw_h264_encode_map("transform", out)
    print("wrote", out)


if __name__ == "__main__":
    draw_stage_map()
    draw_transform_stages()
    draw_transform_pair()
    draw_quant_pair()
    draw_qp_vs_nonzero()
    print("all charts done")
