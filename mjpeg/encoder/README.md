# mjpeg/encoder

MJPEG（Motion JPEG）编码器，对应系列文章 B1。复用 `jpeg/encoder` 的全部模块，逐帧独立编码，再做简单的容器封装。MJPEG 解码器见 `mjpeg/decoder`（对应 B2）。

## 模块与文章对应关系

| 源文件 | 核心内容 |
|---|---|
| `frame_sequencer.{h,cpp}` | 逐帧调用 `jpeg/encoder` 的 `EncodeFrameToJpeg`，无帧间预测、无运动补偿 |
| `avi_muxer.{h,cpp}` | 极简 AVI 容器封装（RIFF/LIST hdrl/strl/movi + idx1 索引），让输出能被通用播放器识别 |
| `mjpeg_encoder_main.cpp` | 整合：读入 PPM 帧序列 → 逐帧 JPEG 编码 → AVI 封装 |
| `test_avi_muxer.cpp` | AVI 字节结构自校验（FOURCC / 长度回填 / movi+idx1 帧数自洽） |

可复用的编码入口是 `jpeg/encoder/jpeg_encode_api.h` 里的
`cfs::EncodeFrameToJpeg(const RgbImage&, int quality)`，A5 的 `encoder_main`
与本目录的逐帧编码共用同一份实现。

## 构建

```bash
mkdir build && cd build
cmake .. && make
ctest --output-on-failure        # 跑 avi_muxer 结构自测
# 读一个目录里的 frame_*.ppm，编码成 out.avi
./mjpeg_encoder ../../../samples/mjpeg_frames out.avi --quality 80 --fps 10
```

## 验证方式（真 .avi 证据）

```bash
file out.avi            # RIFF (little-endian) data, AVI, 192 x 144, 10.00 fps, video: Motion JPEG
ffprobe out.avi         # codec_name=mjpeg, nb_frames=20, 192x144, r_frame_rate=10/1, probe_score=100
ffmpeg -i out.avi -f null -   # 20 帧全部解码，无报错
```

逐帧提取并与原始 PPM 序列做 PSNR 对比（quality=80, 4:2:0 下约 30.7 dB 平均）：

```bash
ffmpeg -i out.avi -f rawvideo -pix_fmt rgb24 dec.rgb
ffmpeg -framerate 10 -i ../../../samples/mjpeg_frames/frame_%03d.ppm -f rawvideo -pix_fmt rgb24 orig.rgb
ffmpeg -f rawvideo -pix_fmt rgb24 -s 192x144 -i orig.rgb \
       -f rawvideo -pix_fmt rgb24 -s 192x144 -i dec.rgb -lavfi psnr -f null -
```
