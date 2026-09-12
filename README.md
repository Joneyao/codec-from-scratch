# codec-from-scratch

从 0 到 1 手搓视频/图像编解码器 —— C++ 教学实现，配套《从0到1手搓H.264编解码器》系列文章。

## 目标

不追求性能和完整 profile 覆盖，追求**把标准里的每一个核心语法元素、每一步数学运算都亲手实现一遍**，并且能跑通真实的最小样例（用 ffmpeg/libjpeg 的输出做像素级校对）。

## 目录结构

```
codec-from-scratch/
├── h264/
│   ├── decoder/        # H.264 解码器：NAL 解析 → 熵解码 → 预测 → 变换 → 滤波
│   └── encoder/        # H.264 编码器：预测 → 变换/量化 → 熵编码 → 码流封装
├── jpeg/
│   └── encoder/        # 基线 JPEG 编码器：DCT → 量化 → 霍夫曼编码
├── mjpeg/
│   └── encoder/        # MJPEG：逐帧 JPEG + 简单容器封装
├── common/             # 位读写、YUV/RGB 转换等共享工具
├── tests/              # 单元测试 + 与参考实现（ffmpeg/libjpeg）的像素级对比
├── samples/            # 测试用的短视频/图片样本
└── docs/               # 各篇文章对应的设计笔记（不含版权受限的 spec 原文）
```

## 系列文章

配套 WeChat 公众号系列《从0到1手搓H.264编解码器》，每篇对应本仓库的一个或若干个模块的实现过程。文章链接见各子目录 README。

## 参考资料

代码是根据公开标准文本（ITU-T H.264、ITU-T T.81）从零重新实现的原创代码，不包含任何第三方开源项目的源码摘抄。以下项目仅作为**思路参考和正确性校验**，未直接复制其代码：

- [ITU-T H.264 Recommendation](https://www.itu.int/rec/T-REC-H.264) — 视频编码标准正式文本
- [ITU-T T.81 (JPEG)](https://www.itu.int/rec/T-REC-T.81) — 静态图像编码标准正式文本
- [aizvorski/h264bitstream](https://github.com/aizvorski/h264bitstream)（LGPL）— NAL/SPS/PPS 语法解析参考
- [lieff/minih264](https://github.com/lieff/minih264) — 单头文件编码器实现思路参考
- [balbekov/PyH264](https://github.com/balbekov/PyH264) — 教学向实现思路参考
- FFmpeg / libjpeg-turbo — 仅用于生成对比基准（PSNR 校对），不引用其源码

## 构建

每个子模块下都有独立的 CMakeLists.txt，可单独编译：

```bash
cd h264/decoder
mkdir build && cd build
cmake .. && make
```

## License

MIT License，见 [LICENSE](LICENSE)。标准文本本身（ITU-T Recommendations）版权归 ITU 所有，不包含在本仓库内。
