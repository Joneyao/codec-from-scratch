# 配套文章

《从 0 到 1 手搓编解码器》系列共 32 篇：**手搓主线 26 篇**（A JPEG / B MJPEG / C H.264 解码 / D H.264 编码）与本仓库代码一一对应，遵循"**代码先行**"——先实现模块、编译跑通，调试产生的真实中间数据（系数矩阵、量化前后、PSNR、码流 dump）直接作为文章配图，图上每个数字都是代码真跑出来的；外加 **实战篇 E 系列 6 篇**，讲工程里那些天天见的编解码参数和踩坑，配图数据来自 ffmpeg/ffprobe 对真实文件的实测。

每篇文章是一个独立目录，含 `article.md` 正文、配图 PNG、以及生成精确层配图的 `gen_charts.py`。GitHub 上直接点开 `article.md` 即可阅读，图片内嵌可见。

## 阅读顺序

四个子系列，编解码成对闭环，学习曲线层层递进：

```
子系列 A：JPEG 编码 + 解码（图像编码入门，8 篇）
   ↓ DCT / 量化 / 熵编码 —— 这三样是 H.264 的地基
子系列 B：MJPEG 编码 + 解码（过渡，3 篇）
   ↓ 逐帧 JPEG，理解"没有帧间预测"的代价
子系列 C：H.264 解码（先学会读，9 篇）
   ↓ 读比写简单，先把码流拆开看懂
子系列 D：H.264 编码（最难，要做决策，6 篇）
```

## 子系列 A · JPEG 编解码（8 篇）

对应代码：`jpeg/encoder/`（A1-A5）、`jpeg/decoder/`（A6-A8）

| # | 文章 | 对应模块 |
|---|---|---|
| A1 | [为什么压缩图片第一步是把颜色"掰开"](article_01_jpeg-color-transform/article.md) | `jpeg/encoder/color_transform.cpp` |
| A2 | [8×8 的 DCT 到底把图片变成了什么](article_02_jpeg-dct/article.md) | `jpeg/encoder/dct8x8.cpp` |
| A3 | [量化——JPEG 唯一"有损"的一步](article_03_jpeg-quantization/article.md) | `jpeg/encoder/quantize.cpp` |
| A4 | [Zig-Zag + 霍夫曼，把一堆零榨成几个比特](article_04_jpeg-entropy-coding/article.md) | `jpeg/encoder/zigzag_rle.cpp` + `huffman_encode.cpp` |
| A5 | [拼出一个真能被系统打开的 .jpg](article_05_jpeg-jfif-writer/article.md) | `jpeg/encoder/jfif_writer.cpp` + `encoder_main.cpp` |
| A6 | [解析一张相机拍的真实 .jpg，标记段里藏了什么](article_06_jpeg-decoder-parse/article.md) | `jpeg/decoder/jfif_parser.cpp` + `huffman_decode.cpp` |
| A7 | [从熵编码数据一路还原到 8×8 像素块](article_07_jpeg-decoder-idct/article.md) | `jpeg/decoder/idct8x8.cpp` + `dequantize.cpp` |
| A8 | [拼出完整解码器，和 libjpeg 逐像素对齐](article_08_jpeg-decoder-complete/article.md) | `jpeg/decoder/jpeg_decoder_main.cpp` |

## 子系列 B · MJPEG 编解码（3 篇）

对应代码：`mjpeg/encoder/`、`mjpeg/decoder/`（复用 jpeg 的编解码模块）

| # | 文章 | 对应模块 |
|---|---|---|
| B1 | [把视频当成一叠 JPEG，能有多简单](article_09_mjpeg-encoder/article.md) | `mjpeg/encoder/avi_muxer.cpp` + `mjpeg_encoder_main.cpp` |
| B2 | [解开 AVI 容器，逐帧还原成画面](article_10_mjpeg-decoder/article.md) | `mjpeg/decoder/avi_demuxer.cpp` + `mjpeg_decoder_main.cpp` |
| B3 | [比 H.264 大 5-10 倍，代价买来了什么](article_11_mjpeg-vs-h264/article.md) | 对比脚本（复用 B1/B2） |

## 子系列 C · H.264 解码（9 篇）

对应代码：`h264/decoder/`。范围：Baseline Profile 的 I/P 帧，不含 B 帧、CABAC、多参考帧。

| # | 文章 | 对应模块 |
|---|---|---|
| C1 | [从字节流里切出第一个 NAL Unit](article_12_h264-nal-splitter/article.md) | `h264/decoder/nal_splitter.cpp` |
| C2 | [指数哥伦布解码，读出视频的真实分辨率](article_13_h264-exp-golomb-sps/article.md) | `rbsp_bit_reader.cpp` + `sps_pps_parser.cpp` |
| C3 | [一帧画面怎么被切成 16×16 的宏块网格](article_14_h264-slice-macroblock/article.md) | `slice_header.cpp` + `macroblock_layout.cpp` |
| C4 | [9 种帧内预测模式，猜不出细节就抹平](article_15_h264-intra-prediction/article.md) | `intra_predict.cpp` |
| C5 | [H.264 为什么不用标准 DCT，自己发明整数变换](article_16_h264-integer-transform/article.md) | `transform_quant.cpp` |
| C6 | [CAVLC 怎么用几个比特说完一堆零](article_17_h264-cavlc/article.md) | `cavlc_decode.cpp` |
| C7 | [一个运动矢量，怎么把上一帧的像素"偷"过来](article_18_h264-motion-compensation/article.md) | `motion_compensate.cpp` |
| C8 | [解完一个宏块，为什么还要再"缝一遍"](article_19_h264-deblocking/article.md) | `deblock_filter.cpp` |
| C9 | [拼出最小解码器，然后撞上"从能跑到能用"那堵墙](article_20_h264-decoder-complete/article.md) | `decoder_main.cpp` + `h264_decoder.cpp` |

## 子系列 D · H.264 编码（6 篇）

对应代码：`h264/encoder/`。范围：I_4x4 帧内 I 帧编码。

| # | 文章 | 对应模块 |
|---|---|---|
| D1 | [标准只规定怎么解码，编码器凭什么做选择](article_21_h264-intra-mode-decision/article.md) | `h264/encoder/intra_mode_decision.cpp` |
| D2 | [正向变换量化，码率是怎么被 QP 拧出来的](article_22_h264-forward-transform-quant/article.md) | `forward_transform_quant.cpp` |
| D3 | [CAVLC 编码，把系数塞进最少的比特](article_23_h264-cavlc-encode/article.md) | `cavlc_encode.cpp` |
| D4 | [运动估计，编码器 90% 的时间都花在这](article_24_h264-motion-search/article.md) | `motion_search.cpp` |
| D5 | [码率控制，怎么让视频不超出带宽预算](article_25_h264-rate-control/article.md) | `rate_control.cpp` |
| D6 | [封装码流，让自己的编码器和 ffmpeg 对话](article_26_h264-nal-packetizer-encoder/article.md) | `nal_packetizer.cpp` + `encoder_main.cpp` |

## 实战篇 · E 系列（6 篇）

前面 26 篇是"从比特第一个字节亲手搓出来"，讲编解码器**内部怎么工作**。实战篇转向另一个视角：**当你在真实项目里用别人的编解码器（ffmpeg、硬件编码器、播放器）时，那些天天见、却容易搞混的参数到底是什么，配错了会踩什么坑。** 每篇围绕一个高频参数落到一个真实项目问题上。

和前 26 篇不同，E 系列**没有对应的 C++ 模块**——配图数据来自 ffmpeg/ffprobe 对真实文件的实测（各篇目录留有 `.txt`/`.csv` 实测数据 + `gen_charts.py` 可复现）。每篇都埋了一个和手搓系列的呼应点。

| # | 文章 | 参数/概念 | 呼应 |
|---|---|---|---|
| E1 | [视频画面发灰，十有八九是这两个参数没对上](article_27_color-space-range/article.md) | color range + BT.601/709 矩阵 | A1 RGB→YCbCr |
| E2 | [同样是"码率 2M"，为什么两个文件质量差一大截](article_28_rate-control-modes/article.md) | CBR / VBR / CRF | D5 码率控制 |
| E3 | [拖进度条为什么会卡？关键帧间隔在作怪](article_29_gop-keyframe-seek/article.md) | GOP / 关键帧间隔 / seek | B3 MJPEG、C 系列 I/P 帧 |
| E4 | [为什么老电视解不了你的普通 1080p](article_30_profile-level/article.md) | Profile / Level | C 系列 Baseline、D6 写 SPS |
| E5 | [时间戳为什么会乱？B 帧让解码顺序不等于显示顺序](article_31_bframe-pts-dts/article.md) | B 帧 / PTS / DTS | C 系列（不做 B 帧，这里补上为什么复杂） |
| E6 | [带宽只有 2M，清晰度的钱该花在哪](article_32_bitrate-resolution-framerate/article.md) | 码率 / 分辨率 / 帧率 | 串起全系列收官 |

## 诚实边界说明

本系列的价值在于诚实——不粉饰"教学实现"与"生产级实现"之间的差距。几处真实边界如实记录在对应文章里：

- **C9（解码器收尾）**：整合解码器的解析层完全打通，宏块网格与 ffmpeg 报告一致；但残差 CAVLC 解到像素时，比特级对齐在复杂宏块上仍有错位（当前样本解到第 12 个宏块即因错位累积报错退出），P 帧的帧间解码未打通故跳过。文章如实呈现"从能跑到能用，隔着的不是原理，是成千上万个边界情况"。
- **D6（编码器收尾）**：编码器输出的 .h264 能被本仓库 decoder 与 ffmpeg 双双识别（Baseline，PSNR≈36 dB @ QP28），编码写入的非零系数与自解逐系数一致——这是编解码自洽性的硬证据。范围收窄为 I_4x4 帧内 I 帧，不做 P 帧 / Intra_16x16，色度不编码残差。
- **一处同源问题**：本仓库 decoder 的 CAVLC 码表有两处历史笔误（`coeff_token` 的 4≤nC<8 档、`total_zeros` 的 total_coeff==4 行），这些非标准码字 ffmpeg 也不认。这既是 C9 解码复杂宏块错位的根因，也是 D6 编码器主动回避这些块（丢残差换双端自洽）的原因。修好这两张表，是把 C9 推到逐像素对齐、把 D6 残差丢弃降到 0 的前提。
- **D4 运动估计、D5 码率控制**：是编码侧的独立模块与原理演示，尚未接入 `encoder_main` 主循环（编码器目前只做 I 帧）。文章均如实标注。

## 复现配图

每篇的精确层配图由对应模块的真实运行数据生成。文章目录里的 `gen_charts.py` 读取这些真实数据（部分数据由 `h264/encoder/chart_data/`、各模块 demo 导出）后用 matplotlib 绘图：

```bash
cd articles/article_XX_xxx
python3 gen_charts.py   # 重新生成该篇的 PNG
```

`pipeline_map.py`（本目录）是手搓 26 篇共享的"流程地图"骨架，每篇开头那张高亮当前环节的流程图由它生成。中文字体优先用 Noto Sans CJK，缺失时脚本会回退。

E 系列（实战篇）的配图数据来自 ffmpeg/ffprobe 实测——各篇目录里的 `.txt`/`.csv` 是实测结果，`gen_charts.py` 读它们绘图；README 或脚本注释里记录了产生这些数据的 ffmpeg 命令，可完整复现。

## 说明

文章正文为中文，面向公众号读者，风格是"原理 + C++ 代码 + 精确配图"。代码片段与本仓库真实代码一致，spec 依据标注到 ITU-T 条款号。
