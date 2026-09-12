# mjpeg/decoder

MJPEG 解码器，对应系列文章 B2。解开 AVI 容器，逐帧提取后复用 `jpeg/decoder` 还原成画面。

## 模块与文章对应关系

| 源文件 | 核心内容 |
|---|---|
| `avi_demuxer.cpp` | 解析 RIFF/AVI 结构，提取 movi chunk 中的每一帧 JPEG |
| `mjpeg_decoder_main.cpp` | 逐帧调用 jpeg/decoder，输出 YUV 序列 |

## 构建

```bash
mkdir build && cd build
cmake .. && make
./mjpeg_decoder ../../samples/output.avi decoded.yuv
```

## 验证方式

解码 B1 编码出的 .avi，与原始 YUV 序列逐帧做 PSNR 对比；再用 ffmpeg 解码同一 .avi 交叉验证容器解析正确。
