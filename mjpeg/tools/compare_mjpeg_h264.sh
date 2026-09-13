#!/usr/bin/env bash
# B3：MJPEG vs H.264 对比实验（薄封装，实际逻辑在同目录 .py 里）。
# 先确保我们的 MJPEG 编码器已构建，再跑 Python 测量脚本。
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENC_DIR="$HERE/../encoder"

# 构建我们的 MJPEG 编码器（若还没构建）
if [ ! -x "$ENC_DIR/build/mjpeg_encoder" ]; then
  echo "[build] configuring mjpeg encoder ..."
  cmake -S "$ENC_DIR" -B "$ENC_DIR/build" >/dev/null
  cmake --build "$ENC_DIR/build" -j >/dev/null
fi

python3 "$HERE/compare_mjpeg_h264.py"
