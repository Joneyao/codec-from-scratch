# Project-level Agents

本目录存放 codec-from-scratch 项目专用的 subagent 定义，随仓库版本管理。

## 计划中的 agents

| Agent | 用途 |
|---|---|
| `codec-module-developer` | 按"读 spec 条款 → 写 C++ → 单元测试 → 导出调试数据"流程开发单个编解码模块的子代理 |
| `codec-verifier` | 用 ffmpeg/libjpeg 对同一输入做逐字节/逐像素对照验证的子代理 |

独立模块尽量用这些 agent 以 subagent 方式并行开发。
