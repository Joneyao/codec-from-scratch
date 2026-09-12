# h264/encoder

H.264 Baseline Profile 编码器，逐篇对应《从0到1手搓H.264编码器》系列文章。编码器复用 `h264/decoder` 里已经写好的反变换/反量化/去块滤波（编码器内部要做重建帧，逻辑和解码器重建路径一致）。

## 模块与文章对应关系

| 源文件 | 对应文章 | 核心内容 |
|---|---|---|
| `intra_mode_decision.cpp` | #01 | 帧内预测模式选择（RDO 简化版：SAD 最小） |
| `forward_transform_quant.cpp` | #02 | 正向整数变换、量化、QP 与码率的关系 |
| `cavlc_encode.cpp` | #03 | CAVLC 熵编码（系数扫描、VLC 表选择） |
| `motion_search.cpp` | #04 | 运动估计（全搜索 + 菱形搜索对比） |
| `rate_control.cpp` | #05 | 简单的固定 QP / 恒定码率控制 |
| `nal_packetizer.cpp` | #06 | SPS/PPS 生成、NAL 封装、Annex B 起始码写入 |
| `encoder_main.cpp` | #06 | 整合以上模块，编码真实 YUV 序列，用本仓库的 decoder 解码验证一致性 |

## 构建

```bash
mkdir build && cd build
cmake .. && make
./h264_encoder ../../samples/input.yuv output.h264 --qp 28
```

## 验证方式

编码后的码流用本仓库的 `h264/decoder` 解码，比对重建帧与编码器内部重建帧是否逐像素一致（这是编解码器最基本的自洽性要求）；再用 ffmpeg 解码同一份码流，确认能被生产级解码器正确识别。
