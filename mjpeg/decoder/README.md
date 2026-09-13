# mjpeg/decoder

MJPEG 解码器，对应系列文章 B2。解开 AVI 容器，逐帧提取后复用 `jpeg/decoder`
还原成画面。核心认知：**解封装（demux）和解码（decode）是两件独立的事**——
demuxer 只拆容器、搬字节，真正还原画面的是复用的 JPEG 解码器。

    MJPEG 解码 = 拆 AVI 容器（avi_demuxer） + 循环调 JPEG 解码器（jpeg/decoder）

## 模块与文章对应关系

| 源文件 | 核心内容 |
|---|---|
| `avi_demuxer.{h,cpp}` | 解析 RIFF/AVI 结构，读出宽高/帧率/帧数，提取 movi 里每个 `00dc` chunk 的 JPEG 字节流 |
| `mjpeg_decoder_main.cpp` | 解封装 → 逐帧调 `ParseJpeg`+`DecodeJpeg` → 输出逐帧 PPM 或 I420 裸流 |
| `test_avi_demuxer.cpp` | 解析真实 out.avi，校验 192x144 / 20 帧 / fps 10，每帧 FFD8…FFD9 合法 |

## 构建与运行

```bash
mkdir build && cd build
cmake .. && make
ctest --output-on-failure          # 跑 avi_demuxer 单元测试

# 解 B1 产出的 out.avi -> 逐帧 PPM
./mjpeg_decoder ../../encoder/out.avi out_frames/
# 或输出 I420 裸流
./mjpeg_decoder ../../encoder/out.avi out.yuv --yuv
```

## 验证（可复现）

```bash
# 1) demuxer 抠出的每帧 JPEG 字节（供 libjpeg 对照）
python3 dump_frames.py
# 2) ffmpeg 解同一 out.avi 作对照帧
mkdir -p ff_frames && ffmpeg -v error -i ../encoder/out.avi \
    -pix_fmt rgb24 ff_frames/frame_%03d.ppm
# 3) 逐帧 PSNR：手搓 vs libjpeg / vs ffmpeg / 完整往返
python3 verify_psnr.py
# 4) ffprobe 交叉核对容器信息
ffprobe -v error -count_frames -select_streams v:0 \
    -show_entries stream=codec_name,width,height,nb_read_frames,avg_frame_rate \
    ../encoder/out.avi
```

实测结果（out.avi，192x144，20 帧，quality=80）：

| 对比 | 平均 PSNR | 说明 |
|---|---|---|
| 手搓解码 vs libjpeg（同份 JPEG） | 56.1 dB | 还原正确，几乎逐像素一致 |
| 手搓解码 vs ffmpeg（同份 avi） | 25.4 dB | 色度上采样风格不同（ffmpeg 带平滑，本实现同 libjpeg 用最近邻）；仅亮度比为 43.9 dB |
| 完整往返（原始→B1 编码→avi→B2 解码） | 24.5 dB | 叠加 JPEG 有损压缩本身的损失 |

ffprobe 交叉验证：`codec_name=mjpeg, 192x144, nb_read_frames=20, 10/1 fps`，
与 demuxer 解出的信息一字不差。
