# Windows 下载与安装

## 选哪个下载包

- **MuseScore-VSL-Integration-v0.1.0-Windows-x64.zip**：完整自定义 MuseScore，含 Qt 运行库、基础音色、奏法选择器、VSL 映射和三款配套 VST3。正常使用选择这个包，不需要另外编译，也不必再下载插件包。
- **VSL-Companion-Plugins-v0.1.0-Windows-x64.zip**：只有三款 Synchron 专用桥接／效果插件及说明。适合已有兼容宿主、只需要这些插件的用户。
- **musescore-vsl-integration-v0.1.0-complete-source.zip**：完整对应源码及第三方构建依赖源码，供研究和重新构建，不是安装包。

下载入口：[GitHub Release v0.1.0](https://github.com/OliverCherryZ/musescore-vsl-integration/releases/tag/v0.1.0)。SHA256 校验文件随下载包提供。此版本仍标记为预发布。

## 直接运行自定义 MuseScore

1. 使用 **Windows 10 1809 或更新版本的 64 位系统**。建议将 ZIP 完整解压到可写的普通目录，例如 `C:\MusicApps\MuseScore-VSL`。
2. 双击根目录的 `Start-MuseScore.cmd`，或打开 `bin\MuseScore4.exe`。不要在 ZIP 预览中运行，也不要只移出 EXE；`bin`、`qml`、`sound` 等文件夹必须保持相对位置。
3. 可直接使用内附的 **MuseScore Basic** 音色进行记谱、回放和导出。使用其他 VST3 音源时，按下一节配置自己的插件。

这是免安装 ZIP 分发，**不等于所有用户数据都写在解压文件夹内**。应用仍使用 Windows 用户目录保存设置、缓存和最近文件，应用名为 `MuseScore4VSL`，与标准 MuseScore 安装分开。解压包不携带开发者的用户设置、登录信息或私人曲谱。

## 使用自己的其他 VST3 音源

1. 安装并授权你自己的 **Windows x64 VST3** 音源。
2. 在 MuseScore 的 **编辑 → 偏好设置 → VST** 中，把它所在的目录加入扫描路径，然后按界面要求重新扫描或重启。
3. 打开混音器，为目标谱表选择该 VST3，打开音源界面并载入需要的音色。
4. 先检查普通音符、力度、回放及导出。如果需要由谱面控制专用奏法，再配置与该插件资源 ID、乐器 ID 和真实键位相符的表达映射。

普通 VST3 回放不要求安装 Vienna 音源，也通常不需要本项目的三个 Synchron 配套插件。随包的 `active.json` 只匹配指定的 Smooth Bridge 和乐器，不会把其中的 VSL 切换键发给任意其他插件。

表达映射的通用入口是 `expression-maps\active.json`，也可以通过启动进程的 `MUSESCORE_VST_EXPRESSION_MAP` 环境变量选择另一份文件；修改后重新启动应用。一个文件可用 `profiles` 数组容纳多个精确匹配的插件配置。参见 [项目功能详解](PROJECT-OVERVIEW.zh-CN.md) 与默认禁用的 [键位映射示例](../examples/generic-keyswitch-map.json)。示例中的 ID 和 MIDI 音符编号都是占位值，必须按自己的音源修改后才可开启。

当前映射以每路线 1–16 个 keyswitch 为基础，可附带离散 CC 值。它不是连续 CC 曲线编辑器，也不自动识别任意音库的私有奏法。现有选择器目录与按需启用采样实现针对 Synchron Solo Strings；其他音源不能直接套用该目录。

## 使用随包的 Synchron 配套组件

1. 自行安装并授权兼容的 **Vienna Synchron Player** 与 **Synchron Solo Strings**。历史验证使用 Player core **1.3.0.3022**。
2. 当前桥接器查找官方 VST3 入口：`C:\Program Files\Common Files\VST3\Vienna Synchron Player.vst3`。播放器核心 DLL 和采样目录不是这个入口。当前二进制没有自定义入口路径选项。
3. 在偏好设置的 VST 搜索路径中加入解压包内的 `Companion-Plugins\VST3`。保留该目录内三个插件的文件名，尤其让 Smooth Bridge 和 Smoother 留在同一目录。
4. 目标谱表音源选 **Synchron Smooth Bridge**，在其界面中载入对应 Velocity 预设；把 **VSL Host Dynamics Smoother** 放在同轨效果器链的第一位。保留宿主单音动态。
5. 在插件管理器启用 **VSL Articulations**。可自行设置快捷键 `Ctrl+Alt+V`，然后按 [奏法使用说明](USAGE.zh-CN.md) 使用。

**Synchron Offline Bridge Fix** 用于引用旧桥接类的工程兼容，新的常规配置优先使用 Smooth Bridge + Smoother。如果只下载了独立插件 ZIP，先解压，再把其中 `VST3` 目录加入宿主搜索路径，其他步骤相同。

商业播放器、音库和工厂预设状态不随本项目分发。使用者在自己的播放器中载入预设即可，不需要编译代码。初始采样启用集合和内存占用取决于用户载入的预设，不保证与开发者的精简模板相同。

## 首次检查与已知情况

- 建议先用几小节简单乐谱，确认普通回放、`p → f → p`、一次奏法切换和直接 WAV 导出，再继续大型作品。
- 当前自定义宿主保留开发构建标识。此次迁移启动检查出现过“无法加载 MuseSounds”提示；关闭提示后，MuseScore Basic 的记谱与 PDF/WAV 导出正常。**本发布未确认 MuseSounds 兼容性**；MuseSounds 不包含在下载包内。
- GUI、曲谱导入和导出是在开发机的新目录下检查，未宣称完成另一台全新 Windows 机器或所有第三方 VST3 的兼容测试。
- 配套插件重新从公开源码以 MSVC 构建，接口和自动测试通过；原生音乐听感记录来自此前开发过程，不能据此说这批新插件二进制已完成全量听感验收。
- 修改映射或更换音源后应重启并检查。不要用旧官方 MuseScore 重存带精确绑定的唯一副本；MusicXML 也不保存这些自定义字段。

详细目的和功能见 [项目详解](PROJECT-OVERVIEW.zh-CN.md)，完整验证边界见 [验证记录](VALIDATION.md)。

## English quick start

Download the Windows x64 application ZIP, extract every file, and run `Start-MuseScore.cmd` or `bin/MuseScore4.exe`. Qt runtime libraries and the MuseScore Basic soundfont are included. User settings still use Windows user folders. The app supports compatible third-party VST3 instruments; those instruments and their licenses are supplied by you. Register their directory in MuseScore's VST preferences and select the instrument in the mixer.

The three companion plugins are specifically for Vienna Synchron Player. Add `Companion-Plugins/VST3` as a VST search directory, use Smooth Bridge as the instrument, and put Dynamics Smoother first in that track's effect chain. Licensed Vienna software and samples are required for this workflow. No source compilation is needed for either ZIP. This is a prerelease, and MuseSounds compatibility has not been established.
