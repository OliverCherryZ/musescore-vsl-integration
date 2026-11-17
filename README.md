# MuseScore VSL Integration

让谱面上的奏法更直接地驱动第三方 VST3 音源，并把这套控制接入 MuseScore 的原生回放与音频导出。

本项目基于 **MuseScore Studio 4.7.7**，包含自定义宿主、可配置表达映射，以及针对 **Vienna Synchron Player / Synchron Solo Strings** 的桥接插件、动态平滑效果器和奏法选择器。**官方 MuseScore 本来就支持 VST3**；本项目重点补充的是从记谱语义到音源奏法的衔接、编辑后的自动更新，以及特定音源的回放／导出问题处理。

宿主可以使用其他兼容 VST3 音源。当前随包提供的完整音库映射、549 条目录选择器、采样按需启用和三款配套插件则针对 Synchron，不能直接套用到任意音库。

## 直接下载使用

当前提供 **Windows x64 预编译包**，普通使用者不需要自己编译。

| 下载 | 适合谁 | 内容 |
| --- | --- | --- |
| [完整 MuseScore Windows 包](https://github.com/OliverCherryZ/musescore-vsl-integration/releases/download/v0.1.0/MuseScore-VSL-Integration-v0.1.0-Windows-x64.zip) | 首次使用，或需要自定义宿主 | MuseScore 应用、三款 VST3 插件、表达映射、奏法选择器及说明 |
| [独立 VST3 插件包](https://github.com/OliverCherryZ/musescore-vsl-integration/releases/download/v0.1.0/VSL-Companion-Plugins-v0.1.0-Windows-x64.zip) | 已有匹配宿主，只需要 Synchron 配套插件 | Smooth Bridge、Dynamics Smoother、旧版 Offline Bridge Fix |
| [Release 页面与校验信息](https://github.com/OliverCherryZ/musescore-vsl-integration/releases/tag/v0.1.0) | 查看版本说明、源码及 SHA-256 | 所有下载文件和已知限制 |

**完整包已经包含三款插件，无需再下载独立插件包。** 解压后按 [Windows 安装说明](docs/WINDOWS-INSTALL.zh-CN.md) 或包内说明启动，添加自己的 VST3 音源即可。第三方商业播放器、音库及授权需要自行安装；使用其他 VST3 音源不要求购买 VSL。

## 为什么开发

写 `pizz.`、连音线、断奏点时，我们希望回放能跟随谱面选择相应音色。第三方音库往往有自己的切换键和控制规则，普通记谱信息并不会自动变成它所需的指令。逐个补控制音、另写同步脚本，既增加维护工作，也容易在改谱后留下过期的播放状态。

这次改写把受支持的奏法信息直接送入原生播放链，再由配置转换成目标音源的控制命令。谱面修改提交后，下一次播放和原生导出使用更新后的计划；不需要先运行一遍额外同步工具。Synchron 配套部分还处理了宿主动态增益的突变、回放／离线处理模式衔接，以及新采样尚未准备好便开始播放或导出的问题。

更多设计动机、工作方式与使用场景见 **[项目目的与功能详解](docs/PROJECT-OVERVIEW.zh-CN.md)**。

## 能实现什么

| 能力 | 实际用途 | 适用范围 |
| --- | --- | --- |
| 谱面奏法驱动音色切换 | 将受支持的符号、准确奏法文字与音库的切换键对应起来 | 有匹配表达映射的 VST3 音源 |
| 可配置的键位与离散 CC | 通过 JSON 定义插件／乐器匹配、切换键组合及附带的固定 CC 值 | 取决于音库控制方式和 VST3 接口；不是连续 CC 曲线编辑器 |
| 编辑后自动更新 | 插入、替换、删除奏法标记后，提交修改即可用于后续回放与导出 | 定制宿主的原生播放链 |
| 精确奏法选择器 | 谱面显示简短标记，文件保存完整音色绑定；支持搜索、撤销／重做和重新打开 | 当前内置 Synchron Solo Strings 目录 |
| 保留单音动态并平滑增益变化 | 用音源原始参考音频识别宿主增益变化，再使增益过渡连续 | Synchron Smooth Bridge + Host Dynamics Smoother |
| 按需启用采样与导出准备 | 根据用到的路线增量启用采样，播放与导出共用准备流程 | 已适配的 Synchron 预设结构 |
| 区分记谱展开与录制片段 | 严格计量震音／颤音继续由宿主展开；明确指定时可选录制片段 | 支持的记谱规则与对应映射 |

选择器当前有 **549 条目录分支**：小提琴与中提琴各适用 539 条，大提琴另有 10 条特效。它们包括固定音色、速度、混合和序列结构；目录数不等于独立采样数，也不表示全部分支已经逐条试听。

## 怎么用

- **使用其他 VST3 音源：**下载完整包，在宿主中添加插件并选择音源，先按普通 VST3 工作流写谱、回放和导出。需要谱面自动切换奏法时，再配置该音源的表达映射。当前没有为所有第三方音库附带现成映射，通常也不需要 Synchron 专用桥接器或平滑器。
- **使用 Synchron Solo Strings：**下载完整包，安装并授权自己的播放器和音库；每轨使用 Smooth Bridge，首个效果器使用 Dynamics Smoother，载入匹配的 Velocity 工厂预设，再启用配套映射和选择器。按 [中文配置说明](docs/USAGE.zh-CN.md) 完成设置。
- **需要修改或扩展：**源码、固定上游补丁和构建脚本都在仓库内。见 [MuseScore 构建说明](docs/BUILD-MUSESCORE.md) 与 [插件构建说明](docs/BUILD-PLUGINS.md)。配置能够表达的键位映射可通过 JSON 调整，不必为每个音库重新编译宿主。

## 组件与源码

| 组件 | 作用 | 位置 |
| --- | --- | --- |
| 定制 MuseScore | 传递奏法语义、排序控制命令、保存精确绑定、协调加载与导出 | [`musescore/`](musescore/) |
| 表达映射与 VSL Articulations | 目标音源控制配置、目录搜索与短谱面标记 | [`runtime/`](runtime/) |
| Synchron Smooth Bridge | 包装已安装播放器，协调处理模式并提供原始参考音频 | [`plugins/`](plugins/) |
| VSL Host Dynamics Smoother | 平滑经核对的宿主增益变化，保留单音动态工作流 | [`plugins/`](plugins/) |
| Synchron Offline Bridge Fix | 保留旧工程使用的桥接兼容路线 | [`plugins/`](plugins/) |

上游基线：[`musescore/MuseScore`](https://github.com/musescore/MuseScore)，tag `v4.7.7`，commit `b750c43e316cf6a00e70451870b231b12f400bda`。Synchron 专用部分的既有验证使用 Player core **1.3.0.3022**。

## 当前边界与验证

这是独立集成的初期公开版本。当前打包宿主已完成 MS Basic 的开谱及 PDF／WAV 导出检查；启动时可能出现“无法加载 MuseSounds”的提示，MuseSounds 兼容性尚未确认。其他普通 VST3 音源也未逐一测试。

通用表达映射可以扩展，但其他音库的特定奏法、加载方式和声音结果需要各自验证；VSL 选择器不是通用音库浏览器。动态平滑器只处理匹配参考音频中的宿主增益变化，不会替任意音源生成音色交叉渐变或重新塑造采样音头。

MSCZ／MSCX 可保存新增精确绑定；MusicXML 不保存这些字段，旧官方版本重存也可能丢失。录制 Ricochet 的 a1–a4 不承诺固定反弹次数，混合分支不代表自动生成连续 CC 扫描。

源码还原、插件编译、回归测试与此前的原生听感记录分别列在 [验证说明](docs/VALIDATION.md)。历史各组测试并非全部在同一最终二进制上重跑；未声称 549 条目录及任意组合均已完成听感验收。

## 许可与归属

自制代码及 MuseScore 修改按 **GPL-3.0-only** 发布。第三方文件保留原许可，见 [LICENSE](LICENSE) 和 [第三方声明](THIRD_PARTY_NOTICES.md)。MuseScore Studio、VST、Vienna Synchron Player 和 Synchron Solo Strings 属于各自项目及权利人。本项目不代表这些厂商，亦不授予商业音库的使用或再分发权利。

**English overview:** An independent MuseScore Studio 4.7.7 integration that connects notation semantics to configurable VST3 articulation switching. Upstream MuseScore already provides VST3 hosting. This project adds native articulation routing and update/export coordination, plus Synchron-specific bridge/effect plugins, sample preparation, and a detailed selector. Prebuilt Windows x64 downloads are available above. Other compatible VST3 instruments can use the host without VSL; automatic articulation switching needs a matching map. Commercial players and samples are supplied separately by the user.

Maintained by [OliverCherryZ](https://github.com/OliverCherryZ).
