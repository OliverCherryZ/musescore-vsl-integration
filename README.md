# MuseScore VSL Integration

面向 **MuseScore Studio 4.7.7 / Windows x64** 的 VSL 奏法、动态平滑和原生音频导出集成。这里公开自制 VST3 插件、定制宿主的完整源码补丁，以及奏法选择器和映射配置。

**这是开发者源码发布。** 目标音源是用户自行安装并授权的 **Vienna Synchron Player / Synchron Solo Strings**。本仓库不提供该商业播放器、采样音库或已封装的个人预设。当前版本基于实际使用的 Windows 工程整理；跨平台和其他音源的兼容性尚未验证。

This is an independent, source-only integration for MuseScore Studio 4.7.7 on Windows x64. It contains custom VST3 bridge/effect sources, a complete patch against a pinned upstream MuseScore revision, and an articulation selector with expression maps. A separately installed, licensed Vienna Synchron Player and matching sample library are required. This project is not an official MuseScore or Vienna Symphonic Library distribution.

## 包含的组件

| 组件 | 用途 | 源码位置 |
| --- | --- | --- |
| Synchron Smooth Bridge | 包装已安装的 Synchron Player，衔接回放与离线处理，并为动态平滑提供原始参考音频 | [`plugins/`](plugins/) |
| VSL Host Dynamics Smoother | 配合 Smooth Bridge，在保留宿主单音动态的工作流中平滑动态变化 | [`plugins/`](plugins/) |
| Synchron Offline Bridge Fix | 保留既有使用该桥接器的工程兼容路线 | [`plugins/`](plugins/) |
| 定制 MuseScore | 传递奏法语义、调度 VST 切换、处理按需加载与原生导出准备，并保存精确音色绑定 | [`musescore/`](musescore/) |
| VSL Articulations 选择器 | 通过短谱面文字与隐藏绑定选择完整音色路线 | [`runtime/`](runtime/) |

## 主要行为

- 常规谱面记号和受支持的奏法文字可驱动音色切换；已提交的谱面编辑在后续播放时更新，无需手工同步。
- 选择器使用易读的短标记显示音色，完整绑定保存在 MSCZ / MSCX 中，并支持撤销与重做。
- 当前目录包含 **549 条可选分支**：两把小提琴及中提琴各适用 539 条，大提琴另含 10 条特效。分支包含固定音色、速度、混合与序列结构，不等于 549 份独立采样。
- 按路线增量启用需要的采样；回放和原生 WAV 导出共用准备流程。
- 区分宿主按记谱节奏展开的震音／颤音与音源内录制的持续片段。

## 从源码开始

1. 查看 [MuseScore 构建与还原说明](docs/BUILD-MUSESCORE.md)，取得固定的上游版本并应用本仓库补丁。
2. 查看 [VST3 插件构建说明](docs/BUILD-PLUGINS.md)，构建自制插件。
3. 按 [使用与配置说明](docs/USAGE.zh-CN.md) 安装映射、选择器，并在自己的已授权音源中配置相符的音色树。
4. 阅读 [验证范围](docs/VALIDATION.md)，在自己的机器和作品中检查回放及导出。

上游基线：[`musescore/MuseScore`](https://github.com/musescore/MuseScore)，tag `v4.7.7`，commit `b750c43e316cf6a00e70451870b231b12f400bda`。本项目将修改保存为可应用的源码补丁，保留上游归属和精确基线；不会用整套开发工具链或安装目录替代源码。

已使用的音源版本为 **Vienna Synchron Player core 1.3.0.3022**。桥接器当前依赖 Windows 默认 VST3 安装位置；其他版本和不同安装结构需要核对构建说明与源码。

## 验证与限制

2026-11-17 的本地开发记录包含代表奏法的回放、原生 WAV 导出及用户听感确认，另有选择器、保存重开、撤销重做和退出生命周期检查。各组历史音频测试并非全部在同一最终宿主二进制上重新执行。发布整理阶段的检查与既有听感记录分别列在 [验证说明](docs/VALIDATION.md) 中。

全部 549 条目录分支及其任意组合尚未逐条试听。混合分支不等于自动生成连续 CC 扫描；录制 Ricochet 的 a1–a4 名称不承诺固定反弹次数。旧官方 MuseScore 重存可能丢失新增绑定，MusicXML 不保存这些自定义字段。

## 许可与归属

本项目自制代码及 MuseScore 修改按 **GNU GPL v3 only（GPL-3.0-only）** 发布。第三方文件保留各自的版权和许可；具体见 [LICENSE](LICENSE)、[第三方声明](THIRD_PARTY_NOTICES.md)。

MuseScore Studio 属于其上游项目及贡献者；VST 属于 Steinberg；Vienna Synchron Player 和 Synchron Solo Strings 属于 Vienna Symphonic Library。提及这些名称仅用于说明兼容目标。商业播放器、采样内容及其授权不由本项目授予。

Maintained by [OliverCherryZ](https://github.com/OliverCherryZ).
