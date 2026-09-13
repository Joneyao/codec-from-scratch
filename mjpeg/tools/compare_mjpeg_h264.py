#!/usr/bin/env python3
"""B3：MJPEG vs H.264 对比实验。

用同一批运动测试帧，跑几种编码方式，量出真实的体积/耗时差距：
  1. 我们手搓的 MJPEG 编码器 -> out.avi
  2. ffmpeg libx264（默认 medium/crf23，有帧间预测）-> h264_inter.mp4
  3. ffmpeg libx264 全 I 帧（keyint=1，每帧独立）-> h264_allintra.mp4
  4. ffmpeg 自带 MJPEG 编码器 -> ffmpeg_mjpeg.avi（旁证）

对比 2 和 3 最有说服力：同一个 x264 编码器，唯一差别就是"允不允许帧间预测"，
它们的体积差，就是"帧间预测"这一个技术省下来的码率。

结果写到 chart_data/compare.json，配图脚本 gen_charts.py 直接读它。
所有数字都是这台机器上的真实测量，不是估算。

用法（在本目录或仓库根都行）：
  python3 mjpeg/tools/compare_mjpeg_h264.py
"""
import json
import os
import shutil
import subprocess
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ENC_DIR = os.path.join(HERE, "..", "encoder")
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
FRAMES_DIR = os.path.join(REPO, "samples", "mjpeg_frames")
CHART_DATA = os.path.join(ENC_DIR, "chart_data")
WORK = os.path.join(HERE, "work")   # 中间产物，可再生成，进 .gitignore

FPS = 10
QUALITY = 80          # 我们的 MJPEG 编码器质量档，与 B1 一致
CRF = 23              # H.264 恒定质量因子（数字越小画质越高、体积越大）


def run(cmd, **kw):
    """跑一条命令，返回 (壁钟耗时秒, 输出文本)。"""
    t0 = time.perf_counter()
    p = subprocess.run(cmd, capture_output=True, text=True, **kw)
    dt = time.perf_counter() - t0
    if p.returncode != 0:
        raise RuntimeError("命令失败: %s\n%s" % (" ".join(cmd), p.stderr[-800:]))
    return dt, (p.stdout + p.stderr)


def ensure_frames():
    """确保有 20 帧运动测试序列，没有就用 ffmpeg 重新生成。"""
    os.makedirs(FRAMES_DIR, exist_ok=True)
    have = len([f for f in os.listdir(FRAMES_DIR) if f.endswith(".ppm")])
    if have >= 10:
        return have
    run(["ffmpeg", "-y", "-f", "lavfi", "-i",
         "testsrc2=size=192x144:rate=10:duration=2",
         "-pix_fmt", "rgb24", os.path.join(FRAMES_DIR, "frame_%03d.ppm")])
    return len([f for f in os.listdir(FRAMES_DIR) if f.endswith(".ppm")])


def ffmpeg_encoder_id():
    """确认用的是哪个 H.264 编码器（优先 libx264）。"""
    _, out = run(["ffmpeg", "-hide_banner", "-encoders"])
    if "libx264" in out:
        return "libx264"
    if "libopenh264" in out:
        return "libopenh264"
    return "mpeg4"   # 最后兜底：MPEG-4 part 2 也有帧间预测，可作近似


def measure_avg_frame_bytes():
    """跑我们的 MJPEG 编码器，拿逐帧 JPEG 字节数（体现 MJPEG 每帧大小均匀）。"""
    enc = os.path.join(ENC_DIR, "build", "mjpeg_encoder")
    out_avi = os.path.join(WORK, "our_mjpeg.avi")
    dt, out = run([enc, FRAMES_DIR, out_avi,
                   "--quality", str(QUALITY), "--fps", str(FPS)])
    per_frame = []
    for line in out.splitlines():
        if line.strip().startswith("per-frame bytes:"):
            nums = line.split(":", 1)[1].split()
            per_frame = [int(x) for x in nums]
    size = os.path.getsize(out_avi)
    return dt, size, per_frame


def main():
    os.makedirs(WORK, exist_ok=True)
    os.makedirs(CHART_DATA, exist_ok=True)
    n = ensure_frames()
    h264enc = ffmpeg_encoder_id()
    pattern = os.path.join(FRAMES_DIR, "frame_%03d.ppm")
    result = {"n_frames": n, "size": "192x144", "fps": FPS,
              "h264_encoder": h264enc, "quality_mjpeg": QUALITY, "crf": CRF}

    # 1) 我们手搓的 MJPEG
    dt_our, size_our, per_frame = measure_avg_frame_bytes()
    result["our_mjpeg"] = {"bytes": size_our, "encode_sec": round(dt_our, 4),
                           "per_frame_bytes": per_frame}

    # ffmpeg 输入：帧号从 001 起，用 -start_number 1
    common_in = ["ffmpeg", "-y", "-framerate", str(FPS),
                 "-start_number", "1", "-i", pattern]

    # 2) H.264 帧间预测（默认 medium + crf）
    out_inter = os.path.join(WORK, "h264_inter.mp4")
    dt_inter, _ = run(common_in + ["-c:v", h264enc, "-preset", "medium",
                                   "-crf", str(CRF), "-pix_fmt", "yuv420p",
                                   out_inter])
    result["h264_inter"] = {"bytes": os.path.getsize(out_inter),
                            "encode_sec": round(dt_inter, 4)}

    # 3) H.264 全 I 帧（keyint=1，每帧独立，禁掉帧间预测）
    out_intra = os.path.join(WORK, "h264_allintra.mp4")
    if h264enc == "libx264":
        extra = ["-x264-params", "keyint=1:scenecut=0"]
    else:
        extra = ["-g", "1"]   # 通用参数：GOP=1 即每帧关键帧
    dt_intra, _ = run(common_in + ["-c:v", h264enc, "-preset", "medium",
                                   "-crf", str(CRF), "-pix_fmt", "yuv420p"]
                      + extra + [out_intra])
    result["h264_allintra"] = {"bytes": os.path.getsize(out_intra),
                               "encode_sec": round(dt_intra, 4)}

    # 4) ffmpeg 自带 MJPEG（旁证：成熟实现的 MJPEG 大概多大）
    out_ffmjpeg = os.path.join(WORK, "ffmpeg_mjpeg.avi")
    # -q:v 3 是 ffmpeg mjpeg 的常用高质量档（2-31，越小越好）
    dt_ffm, _ = run(common_in + ["-c:v", "mjpeg", "-q:v", "3", out_ffmjpeg])
    result["ffmpeg_mjpeg"] = {"bytes": os.path.getsize(out_ffmjpeg),
                              "encode_sec": round(dt_ffm, 4)}

    # 5) H.264 帧间预测的逐帧字节数（体现 I/P 帧悬殊）
    #    用 ffprobe 拿每个包的大小（近似每帧编码后大小）
    try:
        _, probe = run(["ffprobe", "-hide_banner", "-select_streams", "v",
                        "-show_entries", "packet=size,flags",
                        "-of", "csv=p=0", out_inter])
        sizes, flags = [], []
        for line in probe.splitlines():
            parts = line.split(",")
            if len(parts) >= 2 and parts[0].isdigit():
                sizes.append(int(parts[0]))
                flags.append("I" if "K" in parts[1] else "P")
        result["h264_inter"]["per_frame_bytes"] = sizes
        result["h264_inter"]["per_frame_type"] = flags
    except Exception as e:
        result["h264_inter"]["probe_error"] = str(e)

    out_json = os.path.join(CHART_DATA, "compare.json")
    with open(out_json, "w") as f:
        json.dump(result, f, indent=2, ensure_ascii=False)

    # 控制台摘要
    print("=== MJPEG vs H.264 对比（真实测量）===")
    print("帧序列: %d 帧 @ %s, fps=%d" % (n, result["size"], FPS))
    print("H.264 编码器: %s, CRF=%d" % (h264enc, CRF))
    b = lambda k: result[k]["bytes"]
    print("我们的 MJPEG      : %7d B  (%.3fs)" % (b("our_mjpeg"), dt_our))
    print("ffmpeg MJPEG     : %7d B  (%.3fs)" % (b("ffmpeg_mjpeg"), dt_ffm))
    print("H.264 全 I 帧     : %7d B  (%.3fs)" % (b("h264_allintra"), dt_intra))
    print("H.264 帧间预测    : %7d B  (%.3fs)" % (b("h264_inter"), dt_inter))
    print("---")
    print("我们的MJPEG / H.264帧间 = %.1fx" % (b("our_mjpeg") / b("h264_inter")))
    print("全I帧 / 帧间预测        = %.1fx  <- 帧间预测省下的" %
          (b("h264_allintra") / b("h264_inter")))
    print("写入: %s" % out_json)


if __name__ == "__main__":
    main()
