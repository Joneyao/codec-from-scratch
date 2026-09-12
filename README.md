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
│   ├── encoder/        # 基线 JPEG 编码器：DCT → 量化 → 霍夫曼编码
│   └── decoder/        # 基线 JPEG 解码器：解析真实 .jpg → IDCT → 还原像素
├── mjpeg/
│   ├── encoder/        # MJPEG 编码：逐帧 JPEG + AVI 容器封装
│   └── decoder/        # MJPEG 解码：解 AVI 容器 + 逐帧 JPEG 解码
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

## 开发约定

- **代码先行 + 真实数据配图：** 每个模块先实现、编译、跑通，调试过程产生的真实中间数据（系数矩阵、量化前后、PSNR 曲线等）直接作为配套文章的配图，可追溯到代码输出。
- **参考不摘抄：** 参考 libjpeg-turbo / h264bitstream / minih264 / x264 / JM / FFmpeg 的思路和边界处理，代码从 spec 条款重新实现，不复制 GPL/LGPL 源码。用参考实现做逐字节/逐像素对照验证。
- **项目级 skills/agents：** 开发中沉淀的可复用工作流存放在 `.claude/skills/` 和 `.claude/agents/`，随仓库版本管理。
- **superpowers 工作流：** brainstorming → writing-plans → TDD → systematic-debugging → verification。独立模块用 subagent 并行开发。

## License

MIT License，见 [LICENSE](LICENSE)。标准文本本身（ITU-T Recommendations）版权归 ITU 所有，收录于 `docs/spec-reference/` 仅供学习参考。
