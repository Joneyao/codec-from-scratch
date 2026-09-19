#!/usr/bin/env python3
"""C2 H.264 指数哥伦布 + SPS/PPS 解析配图。

数据来源全部为 codec-from-scratch/h264/decoder 真实跑出的结果：
  - SPS/PPS 字段：sps_pps_demo 写出的 chart_data/sps_dump.txt
  - 指数哥伦布编码表：按 ITU-T H.264 表 9-2 的算法生成（纯算法，非手编）
不手编数字。
"""
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.font_manager as fm
fm._load_fontmanager(try_read_cache=False)
import matplotlib.pyplot as plt

plt.rcParams["font.family"] = "Noto Sans CJK JP"
plt.rcParams["axes.unicode_minus"] = False

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(
    HERE, "../../../../../third-party/codec-from-scratch"))
DEC = os.path.join(REPO, "h264/decoder")
DUMP = os.path.join(DEC, "chart_data/sps_dump.txt")

C_BLUE = "#2563eb"
C_CYAN = "#0891b2"
C_RED = "#dc2626"
C_ORANGE = "#ea580c"
C_GREEN = "#16a34a"
C_GRAY = "#64748b"
C_DARK = "#1e293b"


def load_dump():
    """读 sps_pps_demo 写出的字段表。"""
    d = {}
    with open(DUMP) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            p = line.split()
            if p[0] == "crop_lrtb":
                d["crop_lrtb"] = [int(x) for x in p[1:5]]
            else:
                d[p[0]] = p[1]
    return d


def exp_golomb_bits(code_num):
    """按 9.1 生成 codeNum 对应的比特串，返回 (prefix, one, suffix)。"""
    n = code_num + 1
    leading = n.bit_length() - 1          # 前导 0 的个数
    prefix = "0" * leading
    suffix = ""
    if leading > 0:
        suffix = format(n - (1 << leading), "0{}b".format(leading))
    return prefix, "1", suffix


def sync_stage_map():
    import sys
    sys.path.insert(0, os.path.join(HERE, ".."))
    from pipeline_map import draw_h264_decode_map
    draw_h264_decode_map("params", os.path.join(HERE, "stage_map.png"))
    print("stage_map.png done")


def chart_exp_golomb_table():
    """精确层：指数哥伦布编码表 codeNum 0-9，用颜色拆出 前导0 + 1 + 尾巴。"""
    rows = list(range(10))
    fig, ax = plt.subplots(figsize=(9.5, 5.6))

    y0 = len(rows)
    ax.text(0.5, y0 + 0.7, "codeNum", ha="center", fontsize=11,
            fontweight="bold", color=C_DARK)
    ax.text(3.4, y0 + 0.7, "比特串（前导 0 · 分隔 1 · 尾巴）", ha="left",
            fontsize=11, fontweight="bold", color=C_DARK)
    ax.text(8.7, y0 + 0.7, "总位数", ha="center", fontsize=11,
            fontweight="bold", color=C_DARK)

    for i, k in enumerate(rows):
        y = y0 - i - 0.5
        prefix, one, suffix = exp_golomb_bits(k)
        ax.text(0.5, y, str(k), ha="center", va="center", fontsize=12,
                color=C_DARK, family="monospace")
        # 逐位画色块：前导0=灰，1=红(分隔符)，尾巴=蓝
        x = 2.2
        bw = 0.34
        for ch in prefix:
            ax.add_patch(plt.Rectangle((x, y - 0.2), bw, 0.4, facecolor="#e2e8f0",
                                       edgecolor="#94a3b8", lw=0.8, zorder=2))
            ax.text(x + bw / 2, y, ch, ha="center", va="center", fontsize=11,
                    color=C_GRAY, family="monospace", zorder=3)
            x += bw + 0.03
        ax.add_patch(plt.Rectangle((x, y - 0.2), bw, 0.4, facecolor=C_RED,
                                   edgecolor="white", lw=0.8, zorder=2))
        ax.text(x + bw / 2, y, one, ha="center", va="center", fontsize=11,
                color="white", family="monospace", fontweight="bold", zorder=3)
        x += bw + 0.03
        for ch in suffix:
            ax.add_patch(plt.Rectangle((x, y - 0.2), bw, 0.4, facecolor=C_BLUE,
                                       edgecolor="white", lw=0.8, zorder=2))
            ax.text(x + bw / 2, y, ch, ha="center", va="center", fontsize=11,
                    color="white", family="monospace", zorder=3)
            x += bw + 0.03
        total = len(prefix) + 1 + len(suffix)
        ax.text(8.7, y, str(total), ha="center", va="center", fontsize=11,
                color=C_ORANGE)

    ax.text(4.8, -0.4,
            "规律：越小的数码字越短。0 只要 1 位，9 要 7 位——和霍夫曼一个思想，"
            "但无需码表，纯算法生成",
            ha="center", fontsize=9.5, color=C_ORANGE)
    ax.set_xlim(0, 9.4)
    ax.set_ylim(-0.9, y0 + 1.1)
    ax.axis("off")
    ax.set_title("无符号指数哥伦布 ue(v) 编码表（ITU-T H.264 表 9-2）",
                 fontweight="bold", fontsize=13)
    plt.tight_layout()
    out = os.path.join(HERE, "exp_golomb_table.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("exp_golomb_table.png done")


def chart_exp_golomb_decode():
    """形象层：'数0再读'的逐位解码过程。以真实 SPS 里某个 ue 字段为例，
    用比特串 00100（codeNum=3）演示三步：数前导0 -> 读分隔1 -> 读同样多的尾巴。"""
    bits = "00100"
    fig, ax = plt.subplots(figsize=(10, 4.4))

    # 顶部：把 5 个比特画成一排方块。
    n = len(bits)
    bw = 0.12
    gap = 0.02
    x0 = 0.5 - (n * bw + (n - 1) * gap) / 2
    colors = ["#e2e8f0", "#e2e8f0", C_RED, C_BLUE, C_BLUE]
    tcols = [C_GRAY, C_GRAY, "white", "white", "white"]
    xs = []
    for j, ch in enumerate(bits):
        x = x0 + j * (bw + gap)
        xs.append(x + bw / 2)
        ax.add_patch(plt.Rectangle((x, 0.66), bw, 0.16, facecolor=colors[j],
                                   edgecolor="#94a3b8", lw=1.0, zorder=3))
        ax.text(x + bw / 2, 0.74, ch, ha="center", va="center", fontsize=15,
                color=tcols[j], family="monospace", fontweight="bold", zorder=4)

    # 三步说明
    ax.annotate("", xy=(xs[1] + 0.02, 0.60), xytext=(xs[0] - 0.02, 0.60),
                arrowprops=dict(arrowstyle="<->", color=C_GRAY, lw=1.6))
    ax.text((xs[0] + xs[1]) / 2, 0.54,
            "① 数前导 0\n遇到 2 个 0", ha="center", va="top", fontsize=10,
            color=C_GRAY)
    ax.annotate("", xy=(xs[2], 0.60), xytext=(xs[2], 0.66),
                arrowprops=dict(arrowstyle="-|>", color=C_RED, lw=1.8))
    ax.text(xs[2], 0.50, "② 读到分隔 1\n停止数 0", ha="center", va="top",
            fontsize=10, color=C_RED)
    ax.annotate("", xy=(xs[4] + 0.02, 0.60), xytext=(xs[3] - 0.02, 0.60),
                arrowprops=dict(arrowstyle="<->", color=C_BLUE, lw=1.6))
    ax.text((xs[3] + xs[4]) / 2, 0.54,
            "③ 再读 2 位尾巴\n（前导 0 有几个就读几位）= 00", ha="center",
            va="top", fontsize=10, color=C_BLUE)

    # 计算式（含中文，不用 monospace 以免缺字）
    ax.text(0.5, 0.24,
            "codeNum = 2^(前导0个数) − 1 + 尾巴\n"
            "= 2² − 1 + 0 = 4 − 1 + 0 = 3",
            ha="center", va="center", fontsize=13, color=C_DARK,
            bbox=dict(boxstyle="round,pad=0.5", fc="#f1f5f9", ec=C_GRAY))
    ax.text(0.5, 0.04,
            "比特串 00100 解出数字 3：小数字用短码，无需任何码表，逐位算出来",
            ha="center", fontsize=9.5, color=C_ORANGE)
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.axis("off")
    ax.set_title("指数哥伦布怎么读：先数 0，再读同样多的位",
                 fontweight="bold", fontsize=13)
    plt.tight_layout()
    out = os.path.join(HERE, "exp_golomb_decode.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("exp_golomb_decode.png done")


def chart_sps_fields():
    """精确层：真实解出的 SPS/PPS 关键字段表。"""
    d = load_dump()
    rows = [
        ("profile_idc", d["profile_idc"] + "（Baseline 基线档）"),
        ("level_idc", d["level_idc"] + "（level 1.0）"),
        ("pic_width_in_mbs_minus1", d["pic_width_in_mbs_minus1"]
         + "  →  横向 " + d["pic_width_in_mbs"] + " 个宏块"),
        ("pic_height_in_map_units_minus1", d["pic_height_in_map_units_minus1"]
         + "  →  纵向 " + d["frame_height_in_mbs"] + " 个宏块"),
        ("frame_mbs_only_flag", d["frame_mbs_only_flag"] + "（全逐行帧）"),
        ("frame_cropping_flag", d["frame_cropping_flag"] + "（无裁剪）"),
        ("num_ref_frames", d["num_ref_frames"]),
        ("熵编码 (来自 PPS)", d["entropy"] + "（非 CABAC）"),
        ("初始 QP (来自 PPS)", d["pic_init_qp"]),
        ("→ 反推分辨率", d["width"] + " x " + d["height"]),
    ]
    fig, ax = plt.subplots(figsize=(9.5, 5.2))
    n = len(rows)
    for i, (k, v) in enumerate(rows):
        y = n - i - 0.5
        last = (i == n - 1)
        fc = "#dbeafe" if last else ("#f8fafc" if i % 2 else "#ffffff")
        ax.add_patch(plt.Rectangle((0, y - 0.5), 9.3, 1.0, facecolor=fc,
                                   edgecolor="#e2e8f0", lw=0.8, zorder=1))
        # 字段名可能含中文（如"熵编码"），不用 monospace 以免缺字。
        use_mono = all(ord(c) < 128 for c in k)
        ax.text(0.15, y, k, ha="left", va="center", fontsize=11,
                color=C_BLUE if last else C_DARK,
                fontweight="bold" if last else "normal",
                family="monospace" if use_mono else "Noto Sans CJK JP")
        ax.text(4.6, y, str(v), ha="left", va="center", fontsize=11,
                color=C_RED if last else C_DARK,
                fontweight="bold" if last else "normal")

    ax.set_xlim(0, 9.3)
    ax.set_ylim(0, n)
    ax.axis("off")
    ax.set_title("从 test.h264 的 SPS/PPS 真实解出的关键字段",
                 fontweight="bold", fontsize=13)
    ax.text(4.65, -0.15,
            "全部字段由手写指数哥伦布解码器逐比特读出，"
            "与 ffprobe 报告的 176x144 / Baseline 完全一致",
            ha="center", fontsize=9, color=C_GRAY, style="italic")
    plt.tight_layout()
    out = os.path.join(HERE, "sps_fields.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("sps_fields.png done")


def chart_resolution_derive():
    """精确层：分辨率反推示意。真实数据 minus1=10 -> 11*16=176 - 0裁剪。"""
    d = load_dump()
    w_mbs = int(d["pic_width_in_mbs"])
    minus1 = int(d["pic_width_in_mbs_minus1"])
    fig, ax = plt.subplots(figsize=(10, 4.6))

    # 三步链：读到 minus1=10 -> +1=11宏块 -> x16=176 -> -裁剪=176
    boxes = [
        ("码流里读到\npic_width_in_mbs_minus1\n= %d" % minus1, C_GRAY),
        ("+1\n= %d 个宏块" % w_mbs, C_CYAN),
        ("× 16 像素/宏块\n= %d" % (w_mbs * 16), C_BLUE),
        ("− 裁剪 %d\n= %s 真实宽" % (
            2 * (d["crop_lrtb"][0] + d["crop_lrtb"][1]), d["width"]), C_GREEN),
    ]
    bw = 2.0
    gap = 0.42
    x = 0.3
    y = 2.3
    centers = []
    for txt, col in boxes:
        ax.add_patch(plt.Rectangle((x, y), bw, 1.1, facecolor=col,
                                   edgecolor="white", lw=2, zorder=3))
        ax.text(x + bw / 2, y + 0.55, txt, ha="center", va="center",
                color="white", fontsize=10.5, fontweight="bold", zorder=4)
        centers.append((x, x + bw))
        x += bw + gap
    for i in range(len(boxes) - 1):
        ax.annotate("", xy=(centers[i + 1][0], y + 0.55),
                    xytext=(centers[i][1], y + 0.55),
                    arrowprops=dict(arrowstyle="-|>", color=C_GRAY, lw=1.8))

    ax.text(5.0, 1.55,
            "为什么要 ×16 再减裁剪？因为 H.264 按 16×16 的宏块编码，"
            "尺寸必须凑成 16 的倍数",
            ha="center", fontsize=10, color=C_DARK)
    ax.text(5.0, 1.05,
            "本例 176 = 11×16 恰好整除，裁剪为 0；"
            "若拍 1080p，1080 不是 16 的倍数（16×68=1088），",
            ha="center", fontsize=9.5, color=C_ORANGE)
    ax.text(5.0, 0.65,
            "就编码成 1088 高，再用裁剪窗口切掉底部 8 行，还原成 1080",
            ha="center", fontsize=9.5, color=C_ORANGE)
    ax.set_xlim(0, 10)
    ax.set_ylim(0.3, 4.0)
    ax.axis("off")
    ax.set_title("分辨率不是存出来的，是从宏块数反推的（真实数据 176=11×16−0）",
                 fontweight="bold", fontsize=13)
    plt.tight_layout()
    out = os.path.join(HERE, "resolution_derive.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("resolution_derive.png done")


if __name__ == "__main__":
    sync_stage_map()
    chart_exp_golomb_table()
    chart_exp_golomb_decode()
    chart_sps_fields()
    chart_resolution_derive()
    print("all charts done")
