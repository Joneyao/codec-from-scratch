# jpeg/decoder

基线 JPEG 解码器，对应《手搓 JPEG 解码器》系列文章（A6-A8）。复用 `jpeg/encoder` 里的量化表、Zig-Zag 表等定义，实现完整的"读懂真实 .jpg"能力。

## 模块与文章对应关系

| 源文件 | 对应文章 | 核心内容 |
|---|---|---|
| `jfif_parser.cpp` | A6 | 扫描 marker、解析 DQT/DHT/SOF/SOS、EXIF/APP 段 |
| `huffman_decode.cpp` | A6 | 构建霍夫曼查表、变长解码 |
| `entropy_decode.cpp` | A7 | 熵解码一个块的 64 个系数（变长解码 + 幅值还原） |
| `block_reconstruct.cpp` | A7 | 反 Zig-Zag、反量化、IDCT，把系数还原成 8x8 像素块 |
| `jpeg_decoder.cpp` / `jpeg_decoder_main.cpp` | A8 | 逐 MCU 调度、DC 差分按分量独立、重启标记、色度上采样（双线性）、YCbCr→RGB、非 16 倍数裁剪、整机 + PSNR 校对 |

## 构建

```bash
mkdir build && cd build
cmake .. && make
./jpeg_decoder ../../samples/camera_photo.jpg output.ppm
```

## 验证方式

解码真实相机/Photoshop 生成的 .jpg，输出 PPM，与 libjpeg-turbo 的 `djpeg` 解码同一文件的结果做逐像素 PSNR 对比。重点验证重启标记（RST marker）、4:2:0/4:2:2 不同色度抽样的正确处理。
