#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""C9 手搓 H.264 解码器 #09 配图生成。

所有数据来自仓库真实运行：
  third-party/codec-from-scratch/h264/decoder/build/h264_decoder
  在 samples/test.h264（176x144, Constrained Baseline, 10帧）上的实测结果。

真实数据来源：
  - ffprobe -show_frames：帧结构 I P P P P I P P P P（2 I帧, 8 P帧）
  - sps_pps_demo：176x144 => 11x9 = 99 宏块/帧，QP=23，CAVLC
  - CFS_DBG=1 ./h264_decoder：逐宏块解析轨迹，mb0-11 干净，mb2 色度模式已越界，
    mb12 读出垃圾 mb_type=41 后报错退出
  - ctest：C1-C8 八个单元测试全过
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
C_OK = "#4c9f70"      # 绿：通过/干净
C_BAD = "#d1495b"     # 红：错位/失败
C_BLUE = "#2f6690"    # 蓝：主色
C_GRAY = "#b8b8b8"    # 灰：未解
C_YELLOW = "#e6a817"  # 黄：警告


# ---------------------------------------------------------------------------
# 图1: 八个模块拼成一台机器（形象层：模块串联 + 单测状态）
# ---------------------------------------------------------------------------
def chart_pipeline_assemble():
    modules = [
        ("C1 NAL 切分", "test_nal_splitter"),
        ("C2 SPS/PPS", "test_sps_pps"),
        ("C3 宏块网格", "test_macroblock"),
        ("C4 帧内预测", "test_intra_predict"),
        ("C5 整数变换", "test_transform"),
        ("C6 CAVLC", "test_cavlc"),
        ("C7 运动补偿", "test_motion"),
        ("C8 去块滤波", "test_deblock"),
    ]
    fig, ax = plt.subplots(figsize=(9, 4.2))
    n = len(modules)
    for i, (name, _) in enumerate(modules):
        x = i * 1.15
        ax.add_patch(Rectangle((x, 1.0), 1.0, 1.0, facecolor=C_OK,
                               edgecolor="white", alpha=0.9))
        ax.text(x + 0.5, 1.5, name, ha="center", va="center",
                color="white", fontsize=9, fontweight="bold")
        ax.text(x + 0.5, 0.75, "单测 PASS", ha="center", va="center",
                color=C_OK, fontsize=8)
        if i < n - 1:
            ax.annotate("", xy=(x + 1.13, 1.5), xytext=(x + 1.0, 1.5),
                        arrowprops=dict(arrowstyle="->", color=C_GRAY, lw=1.5))
    # 汇入 C9
    cx = n * 1.15 + 0.2
    ax.add_patch(Rectangle((cx, 0.7), 1.6, 1.6, facecolor=C_BLUE,
                           edgecolor="white"))
    ax.text(cx + 0.8, 1.5, "C9\n完整解码器", ha="center", va="center",
            color="white", fontsize=11, fontweight="bold")
    ax.annotate("", xy=(cx, 1.5), xytext=(cx - 0.15, 1.5),
                arrowprops=dict(arrowstyle="->", color=C_YELLOW, lw=2.2))
    ax.text(n * 1.15 / 2, 2.55,
            "8 个模块各自单测全过 —— 但拼起来是另一回事",
            ha="center", va="center", fontsize=11, color="#333")
    ax.set_xlim(-0.3, cx + 2.0)
    ax.set_ylim(0.3, 2.9)
    ax.axis("off")
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "pipeline_assemble.png"), dpi=150,
                bbox_inches="tight")
    plt.close(fig)


# ---------------------------------------------------------------------------
# 图2: 这条码流的真实结构（ffmpeg ground truth: I P P P P I P P P P）
# ---------------------------------------------------------------------------
def chart_frame_structure():
    frames = ["I", "P", "P", "P", "P", "I", "P", "P", "P", "P"]
    fig, ax = plt.subplots(figsize=(9, 2.6))
    for i, t in enumerate(frames):
        color = C_BLUE if t == "I" else C_GRAY
        ax.add_patch(Rectangle((i * 1.0, 0), 0.9, 1.0, facecolor=color,
                               edgecolor="white"))
        ax.text(i * 1.0 + 0.45, 0.5, t, ha="center", va="center",
                color="white", fontsize=14, fontweight="bold")
        ax.text(i * 1.0 + 0.45, -0.28, f"帧{i}", ha="center", va="center",
                fontsize=8, color="#666")
    ax.text(5.0, 1.45, "ffprobe 实测：10 帧 = 2 个 I 帧 + 8 个 P 帧",
            ha="center", fontsize=11, color="#333")
    ax.text(5.0, -0.62,
            "本篇解码器只完整处理 I 帧（蓝色），P 帧的帧间解码未打通，如实跳过",
            ha="center", fontsize=9.5, color=C_YELLOW)
    ax.set_xlim(-0.3, 10.3)
    ax.set_ylim(-0.9, 1.7)
    ax.axis("off")
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "frame_structure.png"), dpi=150,
                bbox_inches="tight")
    plt.close(fig)


# ---------------------------------------------------------------------------
# 图3: 第一个 I 帧的宏块解析进度（99 个 MB，前12个解析、mb2起色度模式越界、mb12崩）
# 精确层：真实逐宏块解析轨迹
# ---------------------------------------------------------------------------
def chart_mb_progress():
    # 真实数据：11x9=99 宏块。CFS_DBG 轨迹：
    #   mb0-11 能读出 mb_type；mb2/mb5 的 chroma_mode=4（越界，说明比特已错位）；
    #   mb12 读出垃圾 mb_type=41 -> 报错退出。
    mbw, mbh = 11, 9
    # 状态：0=未解(灰) 1=解析且无明显异常(绿) 2=解析但已现错位征兆(黄) 3=崩溃点(红)
    status = [0] * (mbw * mbh)
    for i in range(12):          # mb0-11 都读出了 mb_type
        status[i] = 1
    status[2] = 2                # mb2 chroma_mode=4 越界
    status[5] = 2                # mb5 chroma_mode=4 越界
    status[12] = 3               # mb12 读出垃圾 mb_type=41，崩

    color_map = {0: C_GRAY, 1: C_OK, 2: C_YELLOW, 3: C_BAD}
    fig, ax = plt.subplots(figsize=(8.4, 7.2))
    for idx in range(mbw * mbh):
        mx, my = idx % mbw, idx // mbw
        ax.add_patch(Rectangle((mx, mbh - 1 - my), 0.92, 0.92,
                               facecolor=color_map[status[idx]],
                               edgecolor="white"))
        if status[idx] in (2, 3):
            ax.text(mx + 0.46, mbh - 1 - my + 0.46, str(idx), ha="center",
                    va="center", color="white", fontsize=8, fontweight="bold")
    handles = [
        Rectangle((0, 0), 1, 1, facecolor=C_OK, label="读出 mb_type（12 个）"),
        Rectangle((0, 0), 1, 1, facecolor=C_YELLOW,
                  label="色度模式越界=4（错位征兆，mb2/mb5）"),
        Rectangle((0, 0), 1, 1, facecolor=C_BAD, label="垃圾 mb_type=41，崩溃（mb12）"),
        Rectangle((0, 0), 1, 1, facecolor=C_GRAY, label="从未解到（87 个）"),
    ]
    ax.legend(handles=handles, loc="upper center",
              bbox_to_anchor=(0.5, -0.02), ncol=2, fontsize=9, frameon=False)
    ax.set_title("第一个 I 帧 99 个宏块：解码器只走到第 12 个就崩了",
                 fontsize=12, pad=12)
    ax.set_xlim(-0.2, mbw + 0.2)
    ax.set_ylim(-1.2, mbh + 0.2)
    ax.set_aspect("equal")
    ax.axis("off")
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "mb_progress.png"), dpi=150,
                bbox_inches="tight")
    plt.close(fig)


# ---------------------------------------------------------------------------
# 图4: 解析层 vs 重建层——对得上和对不上的分界（诚实边界）
# ---------------------------------------------------------------------------
def chart_honest_boundary():
    layers = [
        ("Annex B 切 NAL", 1),
        ("SPS/PPS 参数", 1),
        ("slice header", 1),
        ("宏块网格划分", 1),
        ("mb_type / 预测模式信令", 1),
        ("CBP 映射", 1),
        ("CAVLC 残差→像素", 0),
        ("复杂宏块 bit 对齐", 0),
        ("P 帧帧间重建", 0),
    ]
    fig, ax = plt.subplots(figsize=(8.6, 5.2))
    y = len(layers)
    for name, ok in layers:
        y -= 1
        color = C_OK if ok else C_BAD
        mark = "对得上" if ok else "还没对上"
        ax.add_patch(Rectangle((0, y), 6.5, 0.82, facecolor=color, alpha=0.85,
                               edgecolor="white"))
        ax.text(0.2, y + 0.41, name, va="center", ha="left", color="white",
                fontsize=10.5, fontweight="bold")
        ax.text(6.3, y + 0.41, mark, va="center", ha="right", color="white",
                fontsize=10)
    ax.axhline(y=3.0, color="#333", lw=1.6, ls="--")
    ax.text(3.25, 3.0, "  分界线：解析框架 vs 逐像素重建  ",
            ha="center", va="center", fontsize=10.5,
            bbox=dict(boxstyle="round", fc="white", ec="#333"))
    ax.set_title("这台解码器：解析层全打通，重建的最后一公里仍有偏差",
                 fontsize=12, pad=10)
    ax.set_xlim(-0.2, 6.7)
    ax.set_ylim(-0.3, len(layers) + 0.2)
    ax.axis("off")
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "honest_boundary.png"), dpi=150,
                bbox_inches="tight")
    plt.close(fig)


# ---------------------------------------------------------------------------
# 图0: 进度地图（复用系列共享骨架，C9 高亮"输出"环节 = 整合收尾）
# ---------------------------------------------------------------------------
def chart_stage_map():
    sys.path.insert(0, os.path.join(OUT, ".."))
    from pipeline_map import draw_h264_decode_map
    draw_h264_decode_map("output", os.path.join(OUT, "stage_map.png"))
    # 系列总览图（所有环节高亮，收尾篇用）
    draw_h264_decode_map("__all__", os.path.join(OUT, "series_overview.png"))


# ---------------------------------------------------------------------------
# 图5: 出错点 vs 崩溃点——比特指针的漂移（错位早发生，崩溃晚发生）
# ---------------------------------------------------------------------------
def chart_error_vs_crash():
    # 真实数据：mb2 色度模式越界=4（错位征兆首次出现），mb12 读出 mb_type=41 崩溃。
    mbs = list(range(13))
    # 用"异常程度"示意：0=正常，越界处标记
    fig, ax = plt.subplots(figsize=(9, 3.6))
    xs = mbs
    # 正常基线
    ax.plot(xs, [0] * len(xs), color=C_GRAY, lw=1, zorder=1)
    # 标记 mb2、mb5 越界征兆
    for m in (2, 5):
        ax.scatter([m], [0.5], color=C_YELLOW, s=140, zorder=3)
        ax.annotate(f"mb{m}\n色度模式=4\n(越界征兆)", (m, 0.5),
                    xytext=(m, 1.15), ha="center", fontsize=8.5,
                    color="#a07800",
                    arrowprops=dict(arrowstyle="->", color=C_YELLOW))
    # 崩溃点 mb12
    ax.scatter([12], [0.5], color=C_BAD, s=220, marker="X", zorder=3)
    ax.annotate("mb12\nmb_type=41\n崩溃退出", (12, 0.5), xytext=(12, 1.15),
                ha="center", fontsize=9, color=C_BAD, fontweight="bold",
                arrowprops=dict(arrowstyle="->", color=C_BAD))
    # 干净区间
    ax.axvspan(-0.5, 1.5, color=C_OK, alpha=0.12)
    ax.text(0.5, -0.8, "mb0-1\n能对上", ha="center", fontsize=8.5, color=C_OK)
    ax.annotate("", xy=(12, -0.45), xytext=(2, -0.45),
                arrowprops=dict(arrowstyle="<->", color="#888", lw=1.3))
    ax.text(7, -0.75, "错位累积的 10 个宏块：程序还在跑，指针已经偏了",
            ha="center", fontsize=9.5, color="#555")
    ax.set_title("崩溃点(mb12)不是出错点(mb2)：变长解码的错误会延迟暴露",
                 fontsize=12, pad=10)
    ax.set_xlabel("宏块解码顺序", fontsize=10)
    ax.set_xlim(-1, 13)
    ax.set_ylim(-1.1, 1.6)
    ax.set_yticks([])
    for spine in ("top", "right", "left"):
        ax.spines[spine].set_visible(False)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "error_vs_crash.png"), dpi=150,
                bbox_inches="tight")
    plt.close(fig)


# ---------------------------------------------------------------------------
# 图6: 半成品的价值 vs 局限（信息卡）
# ---------------------------------------------------------------------------
def chart_value_card():
    fig, ax = plt.subplots(figsize=(9, 4.4))
    # 左：价值
    ax.add_patch(Rectangle((0.03, 0.05), 0.44, 0.9, transform=ax.transAxes,
                           facecolor=C_OK, alpha=0.12, edgecolor=C_OK, lw=2))
    ax.text(0.25, 0.87, "这台解码器给了什么", transform=ax.transAxes,
            ha="center", fontsize=12.5, fontweight="bold", color=C_OK)
    values = [
        "把 H.264 解码从黑箱变成玻璃箱",
        "1000 行明码，看清 NAL/参数/宏块",
        "精确指出真正的难点在哪",
        "解析框架与 ffmpeg 结构一致",
    ]
    for i, v in enumerate(values):
        ax.text(0.06, 0.72 - i * 0.15, "• " + v, transform=ax.transAxes,
                ha="left", fontsize=10.3, color="#2c5f3f")
    # 右：局限
    ax.add_patch(Rectangle((0.53, 0.05), 0.44, 0.9, transform=ax.transAxes,
                           facecolor=C_BAD, alpha=0.12, edgecolor=C_BAD, lw=2))
    ax.text(0.75, 0.87, "还差哪几步才能用", transform=ax.transAxes,
            ha="center", fontsize=12.5, fontweight="bold", color=C_BAD)
    limits = [
        "复杂宏块残差 bit 级对齐",
        "P 帧帧间重建未打通",
        "不支持 CABAC / B 帧 / 多参考帧",
        "需逐宏块 diff ffmpeg 定位偏移",
    ]
    for i, v in enumerate(limits):
        ax.text(0.56, 0.72 - i * 0.15, "• " + v, transform=ax.transAxes,
                ha="left", fontsize=10.3, color="#8a2f3b")
    ax.set_title("从能跑到能用：知道差在哪，比假装搞定了更有用",
                 fontsize=12, pad=12)
    ax.axis("off")
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "value_card.png"), dpi=150,
                bbox_inches="tight")
    plt.close(fig)


if __name__ == "__main__":
    chart_stage_map()
    chart_pipeline_assemble()
    chart_frame_structure()
    chart_mb_progress()
    chart_honest_boundary()
    chart_error_vs_crash()
    chart_value_card()
    print("charts generated:",
          [f for f in os.listdir(OUT) if f.endswith(".png")])
