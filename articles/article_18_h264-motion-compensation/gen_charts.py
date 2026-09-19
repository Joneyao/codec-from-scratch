#!/usr/bin/env python3
"""C7 H.264 运动补偿 + 亚像素插值配图。

数据来源全部为 codec-from-scratch/h264/decoder 真实跑出的结果：
  third-party/codec-from-scratch/h264/decoder/chart_data/motion_dump.txt
不手编数字。参考帧取自真实测试图 camera_photo.ppm 的亮度平面。
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
DUMP = os.path.join(REPO, "h264/decoder/chart_data/motion_dump.txt")

C_BLUE = "#2563eb"
C_GREEN = "#16a34a"
C_ORANGE = "#ea580c"
C_RED = "#dc2626"
C_GRAY = "#94a3b8"
C_PURPLE = "#7c3aed"


def load_dump():
    """把 motion_dump.txt 解析成 dict。块像素按出现顺序读 8 行。"""
    d = {"scalar": {}, "blocks": {}, "mv": {}}
    lines = [ln.rstrip("\n") for ln in open(DUMP)]
    i = 0
    cur_block = None
    while i < len(lines):
        ln = lines[i].strip()
        i += 1
        if not ln or ln.startswith("#"):
            continue
        p = ln.split()
        tag = p[0]
        if tag == "block":
            d["scalar"]["bx"], d["scalar"]["by"] = int(p[1]), int(p[2])
            d["scalar"]["w"], d["scalar"]["h"] = int(p[3]), int(p[4])
        elif tag == "true_mv":
            d["mv"]["true"] = (int(p[1]), int(p[2]))
        elif tag == "sad":
            d["scalar"]["sad_" + p[1]] = (int(p[2]), int(p[3]), int(p[4]))
        elif tag in ("current", "pred_int", "pred_half", "pred_qpel"):
            w = d["scalar"]["w"]
            h = d["scalar"]["h"]
            grid = []
            for _ in range(h):
                grid.append([int(x) for x in lines[i].split()])
                i += 1
            d["blocks"][tag] = np.array(grid)
        elif tag == "taps":
            d["scalar"]["taps"] = [int(x) for x in p[1:]]
        elif tag == "sixtap":
            d["scalar"]["b1"] = int(p[2])
            d["scalar"]["half"] = int(p[4])
            d["scalar"]["qpel_a"] = int(p[6])
            d["scalar"]["G"] = int(p[8])
        elif tag in ("mv_a", "mv_b", "mv_c", "mvp", "mvd", "mv_final"):
            d["mv"][tag] = (int(p[1]), int(p[2]))
    return d


def sync_stage_map():
    import sys
    sys.path.insert(0, os.path.join(HERE, ".."))
    from pipeline_map import draw_h264_decode_map
    # 运动补偿是帧间预测，属 predict 环节。
    draw_h264_decode_map("predict", os.path.join(HERE, "stage_map.png"))
    print("stage_map.png done")


def chart_sixtap():
    """精确层①：6 个真实整像素 → 6-tap 加权 → 半像素 → 1/4 像素。标真实数值。"""
    d = load_dump()
    taps = d["scalar"]["taps"]
    b1 = d["scalar"]["b1"]
    half = d["scalar"]["half"]
    qa = d["scalar"]["qpel_a"]
    G = d["scalar"]["G"]
    weights = [1, -5, 20, 20, -5, 1]
    names = ["E", "F", "G", "H", "I", "J"]

    fig, ax = plt.subplots(figsize=(9.6, 5.4))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 10)
    ax.axis("off")

    ax.text(5, 9.4, "亮度半像素：对 6 个整像素做 6-tap 加权（真实数据）",
            ha="center", fontsize=12.5, fontweight="bold")

    # 6 个整像素方块 + 权重。
    x0, w = 1.0, 1.3
    for k in range(6):
        x = x0 + k * w
        ax.add_patch(plt.Rectangle((x, 6.6), w * 0.86, 1.0, facecolor="#dbeafe",
                                   edgecolor=C_BLUE, lw=1.6))
        ax.text(x + w * 0.43, 7.1, str(taps[k]), ha="center", va="center",
                fontsize=13, fontweight="bold", color="#1e293b")
        ax.text(x + w * 0.43, 7.85, names[k], ha="center", fontsize=9,
                color=C_GRAY)
        wcol = C_GREEN if weights[k] > 0 else C_RED
        ax.text(x + w * 0.43, 6.25, f"×{weights[k]}", ha="center", fontsize=10,
                fontweight="bold", color=wcol)

    # 半像素结果块（居中在 G-H 之间）。
    hx = x0 + 2.5 * w
    ax.annotate("", xy=(hx + w * 0.43, 4.4), xytext=(hx + w * 0.43, 6.15),
                arrowprops=dict(arrowstyle="->", color=C_ORANGE, lw=2.2))
    ax.text(5.0, 5.35,
            f"b1 = 1×{taps[0]} −5×{taps[1]} +20×{taps[2]} +20×{taps[3]} "
            f"−5×{taps[4]} +1×{taps[5]} = {b1}",
            ha="center", fontsize=10, color="#334155",
            bbox=dict(boxstyle="round,pad=0.4", fc="#fff7ed", ec=C_ORANGE))

    ax.add_patch(plt.Rectangle((hx, 3.4), w * 0.86, 1.0, facecolor="#fed7aa",
                               edgecolor=C_ORANGE, lw=2))
    ax.text(hx + w * 0.43, 3.9, str(half), ha="center", va="center",
            fontsize=14, fontweight="bold", color=C_ORANGE)
    ax.text(hx + w * 0.43, 4.55, "半像素 b", ha="center", fontsize=9.5,
            color=C_ORANGE)
    ax.text(5.0, 2.9, f"半像素 b = Clip1(({b1} + 16) >> 5) = {half}",
            ha="center", fontsize=10.5, color=C_ORANGE, fontweight="bold")

    # 1/4 像素 = 整像素 G 与半像素 b 平均。
    ax.text(5.0, 1.9,
            f"1/4 像素 a = (整像素 G={G} + 半像素 b={half} + 1) >> 1 = {qa}",
            ha="center", fontsize=10.5, color=C_PURPLE, fontweight="bold")
    ax.text(5.0, 1.1,
            "半像素靠 6-tap 插（抓边缘更准），1/4 像素只需与最近整/半像素平均",
            ha="center", fontsize=9.5, color="#475569")

    plt.tight_layout()
    out = os.path.join(HERE, "sixtap_interp.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("sixtap_interp.png done")


def chart_subpel_compare():
    """精确层②：整/半/四分之一像素 MV 的预测块 SAD 对比 + 残差热图。"""
    d = load_dump()
    cur = d["blocks"]["current"]
    labels = ["整像素\nMV=%s" % str(d["scalar"]["sad_int"][:2]),
              "半像素\nMV=%s" % str(d["scalar"]["sad_half"][:2]),
              "1/4 像素\nMV=%s" % str(d["scalar"]["sad_qpel"][:2])]
    keys = ["pred_int", "pred_half", "pred_qpel"]
    sads = [d["scalar"]["sad_int"][2], d["scalar"]["sad_half"][2],
            d["scalar"]["sad_qpel"][2]]
    cols = [C_RED, C_ORANGE, C_GREEN]

    fig = plt.figure(figsize=(11.0, 5.0))
    gs = fig.add_gridspec(1, 4, width_ratios=[1, 1, 1, 1.25], wspace=0.32)

    # 三张残差热图（|pred - current|），色标统一。
    diffs = [np.abs(d["blocks"][k].astype(int) - cur.astype(int)) for k in keys]
    vmax = max(df.max() for df in diffs)
    for j, (k, lab, col) in enumerate(zip(keys, labels, cols)):
        ax = fig.add_subplot(gs[0, j])
        im = ax.imshow(diffs[j], cmap="magma", vmin=0, vmax=vmax)
        ax.set_title(lab, fontsize=10, fontweight="bold", color=col)
        ax.set_xticks([])
        ax.set_yticks([])
        ax.set_xlabel(f"SAD={sads[j]}", fontsize=10.5, fontweight="bold",
                      color=col)

    # 右：SAD 柱状对比。
    axb = fig.add_subplot(gs[0, 3])
    xs = np.arange(3)
    axb.bar(xs, sads, color=cols, width=0.62, zorder=3)
    for x, s in zip(xs, sads):
        axb.text(x, s + vmax * 0.15 + 1, str(s), ha="center", fontsize=11,
                 fontweight="bold", color="#1e293b")
    axb.set_xticks(xs)
    axb.set_xticklabels(["整像素", "半像素", "1/4像素"], fontsize=9.5)
    axb.set_ylabel("预测残差 SAD（越小越准）")
    axb.set_ylim(0, max(sads) * 1.3 + 2)
    axb.grid(axis="y", alpha=0.3, zorder=0)
    axb.set_title("对齐精度越高，残差越小", fontsize=10.5, fontweight="bold")

    fig.suptitle("同一个当前块，用不同精度 MV 预测：亚像素插值把残差压下去",
                 fontsize=13, fontweight="bold", y=1.02)
    out = os.path.join(HERE, "subpel_compare.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("subpel_compare.png done  (SAD int=%d half=%d qpel=%d)" % tuple(sads))


def chart_subpel_grid():
    """精确层③：亚像素位置网格——整/半/四分之一像素在整像素之间的哪儿。"""
    fig, ax = plt.subplots(figsize=(7.4, 5.4))
    ax.set_xlim(-0.6, 3.6)
    ax.set_ylim(-0.7, 3.5)
    ax.invert_yaxis()
    ax.set_aspect("equal")
    ax.axis("off")

    # 以左上整像素 G 为原点的 4x4 相位网格（相位 0..3）：
    #   (0,0) 整像素；(偶,偶) 半像素；其余 1/4 像素。
    for py in range(4):
        for px in range(4):
            cx, cy = px, py
            if px == 0 and py == 0:
                col, r = C_BLUE, 0.17       # 整像素 G
            elif px % 2 == 0 and py % 2 == 0:
                col, r = C_ORANGE, 0.14     # 半像素
            else:
                col, r = C_PURPLE, 0.10     # 1/4 像素
            ax.add_patch(plt.Circle((cx, cy), r, facecolor=col,
                                    edgecolor="white", lw=1.0, zorder=3))
    # 整像素 G 用方框强调。
    ax.add_patch(plt.Rectangle((-0.32, -0.32), 0.64, 0.64, fill=False,
                               edgecolor=C_BLUE, lw=2, zorder=2))
    ax.text(0, -0.55, "整像素 G", ha="center", fontsize=10, color=C_BLUE,
            fontweight="bold")

    # 图例。
    from matplotlib.lines import Line2D
    legend = [
        Line2D([0], [0], marker="o", color="w", markerfacecolor=C_BLUE,
               markersize=12, label="整像素（原始样本）"),
        Line2D([0], [0], marker="o", color="w", markerfacecolor=C_ORANGE,
               markersize=11, label="半像素（6-tap 插值）"),
        Line2D([0], [0], marker="o", color="w", markerfacecolor=C_PURPLE,
               markersize=9, label="1/4 像素（整/半平均）"),
    ]
    ax.legend(handles=legend, loc="lower center", bbox_to_anchor=(0.5, -0.1),
              ncol=3, fontsize=9.5, frameon=False)

    ax.set_title("一个整像素周围的 16 个相位：整 1、半 3、1/4 共 12",
                 fontsize=12.5, fontweight="bold", pad=10)
    plt.tight_layout()
    out = os.path.join(HERE, "subpel_grid.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("subpel_grid.png done")


def chart_mv_median():
    """精确层④：MV 中值预测——三邻居 MV 分量各取中值 → 预测 MV → +mvd。"""
    d = load_dump()
    a, b, c = d["mv"]["mv_a"], d["mv"]["mv_b"], d["mv"]["mv_c"]
    mvp, mvd, mvf = d["mv"]["mvp"], d["mv"]["mvd"], d["mv"]["mv_final"]

    fig, ax = plt.subplots(figsize=(9.6, 5.6))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 10)
    ax.axis("off")

    def mvbox(x, y, name, mv, color):
        ax.add_patch(plt.Rectangle((x, y), 2.0, 1.0, facecolor=color, alpha=0.15,
                                   edgecolor=color, lw=2))
        ax.text(x + 1.0, y + 0.66, name, ha="center", fontsize=10.5,
                fontweight="bold", color=color)
        ax.text(x + 1.0, y + 0.28, f"MV=({mv[0]},{mv[1]})", ha="center",
                fontsize=10, color="#1e293b")

    ax.text(5, 9.4, "MV 也差分编码：预测值取三邻居 MV 的分量中值",
            ha="center", fontsize=12.5, fontweight="bold")

    mvbox(0.6, 7.2, "左邻 A", a, C_BLUE)
    mvbox(3.9, 7.2, "上邻 B", b, C_GREEN)
    mvbox(7.2, 7.2, "右上邻 C", c, C_ORANGE)

    for x in (1.6, 4.9, 8.2):
        ax.annotate("", xy=(5.0, 6.1), xytext=(x, 7.15),
                    arrowprops=dict(arrowstyle="->", color=C_GRAY, lw=1.4))

    ax.text(5.0, 5.55,
            f"x: Median({a[0]},{b[0]},{c[0]})={mvp[0]}   "
            f"y: Median({a[1]},{b[1]},{c[1]})={mvp[1]}",
            ha="center", fontsize=10.5, color="#334155",
            bbox=dict(boxstyle="round,pad=0.4", fc="#eef2ff", ec=C_PURPLE))

    ax.add_patch(plt.Rectangle((3.9, 3.7), 2.2, 1.0, facecolor="#ede9fe",
                               edgecolor=C_PURPLE, lw=2))
    ax.text(5.0, 4.36, "预测 MV（mvp）", ha="center", fontsize=10,
            fontweight="bold", color=C_PURPLE)
    ax.text(5.0, 3.98, f"=({mvp[0]},{mvp[1]})", ha="center", fontsize=11,
            color="#1e293b")

    ax.annotate("", xy=(5.0, 2.9), xytext=(5.0, 3.65),
                arrowprops=dict(arrowstyle="->", color=C_PURPLE, lw=2))
    ax.text(5.0, 2.5,
            f"码流只传 mvd=({mvd[0]},{mvd[1]})  →  真正 MV = mvp + mvd "
            f"= ({mvf[0]},{mvf[1]})",
            ha="center", fontsize=10.5, fontweight="bold", color="#1e293b")
    ax.text(5.0, 1.5,
            "相邻块运动往往一致 → 中值预测很准 → mvd 只剩一点点 → 省码字",
            ha="center", fontsize=9.8, color=C_ORANGE)

    plt.tight_layout()
    out = os.path.join(HERE, "mv_median.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("mv_median.png done")


def chart_mc_idea():
    """形象层：运动补偿原理——当前块从参考帧某位置"搬"像素过来，MV 指方向。"""
    fig, ax = plt.subplots(figsize=(9.8, 5.2))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 10)
    ax.axis("off")

    # 两帧：左=参考帧（上一帧），右=当前帧。
    def frame(x, title, color):
        ax.add_patch(plt.Rectangle((x, 2.2), 3.4, 5.2, facecolor="#f8fafc",
                                   edgecolor=color, lw=2.2))
        ax.text(x + 1.7, 7.75, title, ha="center", fontsize=11,
                fontweight="bold", color=color)

    frame(0.7, "参考帧（上一帧）", C_BLUE)
    frame(5.9, "当前帧（P 帧）", C_GREEN)

    # 参考帧里的源块（物体旧位置）。
    ax.add_patch(plt.Rectangle((1.6, 4.4), 1.1, 1.1, facecolor=C_ORANGE,
                               alpha=0.55, edgecolor=C_ORANGE, lw=1.8))
    ax.text(2.15, 4.95, "源块", ha="center", va="center", fontsize=8.5,
            color="white", fontweight="bold")

    # 当前帧里的当前块（物体新位置，稍微挪动）。
    ax.add_patch(plt.Rectangle((7.4, 3.6), 1.1, 1.1, facecolor=C_GREEN,
                               alpha=0.4, edgecolor=C_GREEN, lw=1.8))
    ax.text(7.95, 4.15, "当前块", ha="center", va="center", fontsize=8,
            color="#1e293b", fontweight="bold")

    # MV 箭头：从源块位置指向当前块位置（跨帧示意）。
    ax.annotate("", xy=(7.4, 4.15), xytext=(2.7, 4.95),
                arrowprops=dict(arrowstyle="-|>", color=C_RED, lw=2.4,
                                connectionstyle="arc3,rad=-0.12"))
    ax.text(5.0, 3.9, "运动矢量 MV\n（这块从参考帧哪里搬、往哪搬）",
            ha="center", va="center", fontsize=9.5, color=C_RED,
            fontweight="bold")

    ax.text(5.0, 1.4,
            "帧间预测吃时间冗余：前后帧高度相似，只记 “运动矢量 + 一点残差”，"
            "而不是把整块像素重编一遍",
            ha="center", fontsize=10, color="#475569")
    ax.text(5.0, 0.7,
            "MV 落在亚像素位置时，先插值再搬 —— 这就是上一步 6-tap 的用武之地",
            ha="center", fontsize=9.8, color=C_PURPLE, fontweight="bold")

    ax.set_title("运动补偿：把上一帧的一块像素，按运动矢量搬到当前位置",
                 fontsize=13, fontweight="bold", pad=8)
    plt.tight_layout()
    out = os.path.join(HERE, "mc_idea.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("mc_idea.png done")


if __name__ == "__main__":
    sync_stage_map()
    chart_mc_idea()
    chart_mv_median()
    chart_sixtap()
    chart_subpel_compare()
    chart_subpel_grid()
    print("all charts done")
