#!/usr/bin/env python3
"""C4 配图：读取 intra_predict_demo 导出的真实数据绘图。

数据来源：third-party/codec-from-scratch/h264/decoder/chart_data/intra_dump.txt
全部预测块、SAD 均来自 C++ demo 对真实图像块跑 9 种帧内预测模式的实际输出。
"""
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.font_manager as fm
fm._load_fontmanager(try_read_cache=False)
import matplotlib.pyplot as plt
import numpy as np

# 共享 CJK 字体设置（向上查找 backend/）。
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

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(
    HERE, "../../../../../third-party/codec-from-scratch"))
DUMP = os.path.join(REPO, "h264/decoder/chart_data/intra_dump.txt")

# 9 种模式的中文名与简称（与 C++ Intra4x4ModeName 对应）。
MODE_CN = ["垂直", "水平", "DC 抹平", "左下对角", "右下对角",
           "右偏垂直", "下偏水平", "左偏垂直", "上偏水平"]
MODE_SHORT = ["V", "H", "DC", "DDL", "DDR", "VR", "HD", "VL", "HU"]


def parse_dump():
    """把 intra_dump.txt 解析成两个块的数据结构。

    返回 dict: {"tex": blk, "smooth": blk}，每个 blk 含 top/left/topleft/
    orig(4x4)/preds(9 个 4x4)/sads(9)/best。
    """
    with open(DUMP) as f:
        lines = [ln.rstrip("\n") for ln in f]
    i = 0
    blocks = {}
    cur = None
    while i < len(lines):
        ln = lines[i].strip()
        if ln.startswith("block "):
            _, tag, bx, by = ln.split()
            cur = {"tag": tag, "bx": int(bx), "by": int(by),
                   "preds": {}, "sads": {}}
            blocks[tag] = cur
        elif ln.startswith("top "):
            cur["top"] = [int(v) for v in ln.split()[1:]]
        elif ln.startswith("left "):
            cur["left"] = [int(v) for v in ln.split()[1:]]
        elif ln.startswith("topleft "):
            cur["topleft"] = int(ln.split()[1])
        elif ln == "orig":
            grid = [[int(v) for v in lines[i + 1 + r].split()]
                    for r in range(4)]
            cur["orig"] = np.array(grid)
            i += 4
        elif ln.startswith("pred "):
            _, m, sad = ln.split()
            m = int(m)
            grid = [[int(v) for v in lines[i + 1 + r].split()]
                    for r in range(4)]
            cur["preds"][m] = np.array(grid)
            cur["sads"][m] = int(sad)
            i += 4
        elif ln.startswith("best "):
            cur["best"] = int(ln.split()[1])
        i += 1
    return blocks


def grid_annotate(ax, mat, fs=11, vmin=0, vmax=255):
    for (r, c), v in np.ndenumerate(mat):
        norm = (v - vmin) / (vmax - vmin + 1e-9)
        color = "white" if norm < 0.45 else "black"
        ax.text(c, r, str(int(v)), ha="center", va="center",
                color=color, fontsize=fs)


# 图1：9 种 4x4 模式的方向示意（箭头指向预测来源方向，对应 spec Figure 8-1）
def chart_directions():
    # 每种模式的箭头方向（dx, dy）：指向"预测像素来自哪个方向"。
    # 角度沿用 H.264 定义：0 垂直(上)、1 水平(左)、DC 无方向、其余 6 个斜向。
    dirs = {
        0: (0, -1, "从上往下复制"),      # Vertical
        1: (-1, 0, "从左往右复制"),      # Horizontal
        3: (1, -1, "右上→左下"),         # Diagonal_Down_Left
        4: (-1, -1, "左上→右下"),        # Diagonal_Down_Right
        5: (-0.5, -1, "偏右的近垂直"),   # Vertical_Right
        6: (-1, -0.5, "偏下的近水平"),   # Horizontal_Down
        7: (0.5, -1, "偏左的近垂直"),    # Vertical_Left
        8: (-1, 0.5, "偏上的近水平"),    # Horizontal_Up
    }
    order = [0, 1, 3, 4, 5, 6, 7, 8, 2]  # DC 放最后单独画
    fig, axes = plt.subplots(3, 3, figsize=(8.6, 8.6))
    for idx, m in enumerate(order):
        ax = axes[idx // 3][idx % 3]
        ax.set_xlim(-1.3, 1.3)
        ax.set_ylim(-1.3, 1.3)
        ax.set_aspect("equal")
        ax.set_xticks([]); ax.set_yticks([])
        ax.add_patch(plt.Rectangle((-0.55, -0.55), 1.1, 1.1,
                     fc="#EAF2FB", ec="#2E86C1", lw=1.6))
        if m == 2:  # DC：画一个"抹平"的圈，无方向
            ax.add_patch(plt.Circle((0, 0), 0.34, fc="#F6C453",
                         ec="#B7791F", lw=1.8))
            ax.text(0, 0, "均值", ha="center", va="center", fontsize=11,
                    fontweight="bold")
            sub = "取上+左邻平均，无方向"
        else:
            dx, dy, sub = dirs[m]
            ax.annotate("", xy=(dx * 0.5, dy * 0.5),
                        xytext=(-dx * 0.5, -dy * 0.5),
                        arrowprops=dict(arrowstyle="-|>", lw=2.6,
                                        color="#C0392B"))
        ax.set_title(f"模式{m} {MODE_CN[m]}", fontsize=12, fontweight="bold")
        ax.set_xlabel(sub, fontsize=9.5, color="#566573")
    fig.suptitle("Intra_4x4 的 9 种预测方向：8 个方向 + 1 个 DC 抹平（无方向）",
                 fontsize=14, y=0.995)
    plt.tight_layout(rect=[0, 0, 1, 0.97])
    plt.savefig(os.path.join(HERE, "modes_direction.png"), dpi=110)
    plt.close()


# 图2：同一个真实纹理块，9 种模式各自的 4x4 预测结果（矩阵热力图 + 数值）
def chart_nine_predictions(blocks):
    blk = blocks["tex"]
    fig, axes = plt.subplots(2, 5, figsize=(14, 6.6))
    # 统一色标范围，便于横向对比亮暗。
    all_vals = np.concatenate([blk["preds"][m].ravel() for m in range(9)])
    vmin, vmax = int(all_vals.min()), int(all_vals.max())

    # 第一格放真实块，其余 9 格放 9 种模式预测。
    ax0 = axes[0][0]
    ax0.imshow(blk["orig"], cmap="gray", vmin=vmin, vmax=vmax)
    grid_annotate(ax0, blk["orig"], fs=10, vmin=vmin, vmax=vmax)
    ax0.set_title("真实块（要猜的目标）", fontsize=11, fontweight="bold",
                  color="#C0392B")
    ax0.set_xticks([]); ax0.set_yticks([])

    slots = [(0, 1), (0, 2), (0, 3), (0, 4),
             (1, 0), (1, 1), (1, 2), (1, 3), (1, 4)]
    for m, (r, c) in zip(range(9), slots):
        ax = axes[r][c]
        ax.imshow(blk["preds"][m], cmap="gray", vmin=vmin, vmax=vmax)
        grid_annotate(ax, blk["preds"][m], fs=10, vmin=vmin, vmax=vmax)
        is_best = (m == blk["best"])
        title = f"模式{m} {MODE_SHORT[m]}  SAD={blk['sads'][m]}"
        ax.set_title(title, fontsize=10.5,
                     fontweight="bold" if is_best else "normal",
                     color="#1E8449" if is_best else "black")
        ax.set_xticks([]); ax.set_yticks([])
        if is_best:
            for s in ax.spines.values():
                s.set_edgecolor("#1E8449"); s.set_linewidth(3)

    fig.suptitle("同一个真实块，9 种模式猜出的 4x4：垂直是竖条纹、水平是横条纹、"
                 "DC 是纯色（数字为代码实际输出）", fontsize=12.5)
    plt.tight_layout(rect=[0, 0, 1, 0.95])
    plt.subplots_adjust(hspace=0.35)
    plt.savefig(os.path.join(HERE, "nine_predictions.png"), dpi=110)
    plt.close()


# 图3：两个块各 9 种模式的 SAD 柱状图（哪种最准一目了然）
def chart_sad_bars(blocks):
    fig, axes = plt.subplots(1, 2, figsize=(13, 5))
    for ax, tag, title in zip(
            axes, ["tex", "smooth"],
            ["纹理块：方向模式（水平）完胜 DC",
             "平滑块：DC（抹平）残差最小"]):
        blk = blocks[tag]
        sads = [blk["sads"][m] for m in range(9)]
        best = blk["best"]
        colors = ["#1E8449" if m == best else
                  ("#F6C453" if m == 2 else "#AEB6BF") for m in range(9)]
        bars = ax.bar(range(9), sads, color=colors, edgecolor="#566573")
        ax.set_xticks(range(9))
        ax.set_xticklabels(MODE_SHORT, fontsize=10)
        ax.set_ylabel("SAD（绝对差之和，越小越准）")
        ax.set_title(title, fontsize=12)
        for m, b in enumerate(bars):
            ax.text(b.get_x() + b.get_width() / 2, b.get_height(),
                    str(sads[m]), ha="center", va="bottom", fontsize=9)
        ax.text(best, sads[best], "  最优", color="#1E8449",
                fontsize=10, fontweight="bold", va="bottom")
    fig.suptitle("同一批真实邻居像素，9 种模式的预测误差：块的纹理决定谁最准",
                 fontsize=13)
    plt.tight_layout(rect=[0, 0, 1, 0.95])
    plt.savefig(os.path.join(HERE, "sad_bars.png"), dpi=110)
    plt.close()


# 图4：DC 抹平在平滑块上——预测 vs 真实的贴合程度
def chart_dc_smooth(blocks):
    blk = blocks["smooth"]
    orig = blk["orig"]
    dc = blk["preds"][2]
    diff = orig - dc  # 残差
    fig, axes = plt.subplots(1, 3, figsize=(13, 4.6))

    axes[0].imshow(orig, cmap="gray", vmin=90, vmax=115)
    grid_annotate(axes[0], orig, fs=12, vmin=90, vmax=115)
    axes[0].set_title("真实平滑块", fontsize=12)

    axes[1].imshow(dc, cmap="gray", vmin=90, vmax=115)
    grid_annotate(axes[1], dc, fs=12, vmin=90, vmax=115)
    axes[1].set_title(f"DC 预测：全块抹成 {int(dc[0][0])}", fontsize=12)

    vmax = max(1, int(np.abs(diff).max()))
    axes[2].imshow(diff, cmap="RdBu_r", vmin=-vmax, vmax=vmax)
    for (r, c), v in np.ndenumerate(diff):
        axes[2].text(c, r, str(int(v)), ha="center", va="center",
                     fontsize=12, color="black")
    axes[2].set_title(f"残差 = 真实 − 预测（SAD={blk['sads'][2]}）", fontsize=12)

    for ax in axes:
        ax.set_xticks([]); ax.set_yticks([])
    fig.suptitle("平滑区域：直接取邻居平均值抹平，残差几乎全是个位数——"
                 "猜不出细节时，抹平最保险", fontsize=12.5)
    plt.tight_layout(rect=[0, 0, 1, 0.94])
    plt.savefig(os.path.join(HERE, "dc_smooth.png"), dpi=110)
    plt.close()


# 图5（形象层）：帧内预测原理——用已解码的上邻/左邻像素猜当前块
def chart_principle():
    fig, ax = plt.subplots(figsize=(9, 6.4))
    ax.set_xlim(0, 9)
    ax.set_ylim(0, 9)
    ax.set_aspect("equal")
    ax.axis("off")

    # 已解码区域（上方 + 左方），灰色实心。
    done = "#BDC3C7"
    # 上邻一行（当前块正上方 4 格 + 右上延伸）
    for i in range(8):
        ax.add_patch(plt.Rectangle((1 + i, 5), 1, 1, fc=done, ec="white"))
    # 左邻一列
    for j in range(4):
        ax.add_patch(plt.Rectangle((0, 1 + j), 1, 1, fc=done, ec="white"))
    # 左上角
    ax.add_patch(plt.Rectangle((0, 5), 1, 1, fc="#95A5A6", ec="white"))

    # 当前块 4x4（待预测），空心蓝框加问号。
    for j in range(4):
        for i in range(4):
            ax.add_patch(plt.Rectangle((1 + i, 1 + j), 1, 1,
                         fc="#EAF2FB", ec="#2E86C1", lw=1.4))
    ax.text(3, 3, "?", ha="center", va="center", fontsize=44,
            color="#2E86C1", fontweight="bold")

    # 箭头：上邻往下、左邻往右指向当前块。
    ax.annotate("", xy=(3, 4.9), xytext=(3, 5.6),
                arrowprops=dict(arrowstyle="-|>", lw=2.4, color="#C0392B"))
    ax.annotate("", xy=(1.1, 3), xytext=(0.4, 3),
                arrowprops=dict(arrowstyle="-|>", lw=2.4, color="#C0392B"))

    ax.text(5, 5.5, "上邻：已解码像素", fontsize=11, color="#566573")
    ax.text(0, 0.6, "左邻", fontsize=11, color="#566573")
    ax.text(3, 0.2, "当前 4×4 块：用旁边已知像素猜出这 16 个值",
            ha="center", fontsize=11.5, color="#2E86C1", fontweight="bold")
    ax.set_title("帧内预测：不存原始像素，只存用邻居怎么猜 + 猜错的那点残差",
                 fontsize=13, fontweight="bold")
    plt.tight_layout()
    plt.savefig(os.path.join(HERE, "intra_principle.png"), dpi=110)
    plt.close()


if __name__ == "__main__":
    # 进度地图
    sys.path.insert(0, os.path.join(HERE, ".."))
    from pipeline_map import draw_h264_decode_map
    draw_h264_decode_map("predict", os.path.join(HERE, "stage_map.png"))

    blocks = parse_dump()
    chart_directions()
    chart_nine_predictions(blocks)
    chart_sad_bars(blocks)
    chart_dc_smooth(blocks)
    chart_principle()
    print("charts generated in", HERE)
