# h264/decoder

H.264 Baseline Profile 解码器，逐篇对应《从0到1手搓H.264解码器》系列文章。

## 模块与文章对应关系

| 源文件 | 对应文章 | 核心内容 |
|---|---|---|
| `nal_splitter.cpp` | #01 | Annex B 起始码切分、Emulation Prevention 转义还原 |
| `rbsp_bit_reader.cpp` + `sps_pps_parser.cpp` | #02 | 指数哥伦布(ue/se)解码、SPS/PPS 语法解析、分辨率反推 |
| `slice_header.cpp` + `macroblock_layout.cpp` | #03 | Slice 结构、宏块网格划分 |
| `intra_predict.cpp` | #04 | 9 种 4x4 + 4 种 16x16 帧内预测模式 |
| `transform_quant.cpp` | #05 | 4x4 整数变换、反量化 |
| `cavlc_decode.cpp` | #06 | CAVLC 熵解码 |
| `motion_compensate.cpp` | #07 | 运动矢量预测、1/4 像素插值 |
| `deblock_filter.cpp` | #08 | 环路滤波（去块效应） |
| `decoder_main.cpp` | #09 | 整合以上模块，输出 YUV，与 ffmpeg 做 PSNR 校对 |

## 构建

```bash
mkdir build && cd build
cmake .. && make
./h264_decoder ../../samples/baseline_sample.h264 output.yuv
```

## 已知限制

只覆盖 Baseline Profile 的 I/P 帧，不支持 B 帧、CABAC、多参考帧、Slice Group、错误恢复等生产级解码器必须处理的场景。这些留在正文里明确标注为"生产级解码器还要做的事"，不在本仓库范围内。
