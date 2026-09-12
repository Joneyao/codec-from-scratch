# mjpeg/encoder

MJPEG（Motion JPEG）编码器，对应系列文章 B1。复用 `jpeg/encoder` 的全部模块，逐帧独立编码，再做简单的容器封装。MJPEG 解码器见 `mjpeg/decoder`（对应 B2）。

## 模块与文章对应关系

| 源文件 | 核心内容 |
|---|---|
| `frame_sequencer.cpp` | 逐帧调用 `jpeg/encoder`，无帧间预测、无运动补偿 |
| `avi_muxer.cpp` | 极简 AVI 容器封装（RIFF/LIST/JUNK/movi chunk），让输出能被通用播放器识别 |
| `mjpeg_main.cpp` | 整合：读入 YUV 序列 → 逐帧 JPEG 编码 → AVI 封装 |

## 构建

```bash
mkdir build && cd build
cmake .. && make
./mjpeg_encoder ../../samples/input.yuv output.avi --quality 80 --fps 25
```

## 验证方式

输出的 .avi 用 ffplay/VLC 播放确认可读；用 ffmpeg 逐帧提取并和原始 YUV 帧做 PSNR 对比；同时和同码率下的 H.264 编码结果做体积对比，量化"没有帧间预测"的代价。
