# 配置与使用

这套工程把自定义 MuseScore、Synchron 桥接插件、动态平滑效果器及谱面奏法选择器连接在一起。源码发布面向能自行编译和配置 VST 的用户。要发声，仍需自行安装并授权 Vienna Synchron Player 与 Synchron Solo Strings。

## 准备和安装

1. 按 [MuseScore 构建说明](BUILD-MUSESCORE.md) 构建本仓库补丁对应的定制宿主。普通官方版本不认识这里的精确奏法元数据。
2. 按 [VST 构建说明](BUILD-PLUGINS.md) 构建并安装插件。当前主要组合是 `Synchron Smooth Bridge` 音源和 `VSL Host Dynamics Smoother` 效果器；`Synchron Offline Bridge Fix` 用于已有旧工程的兼容。
3. 将 `runtime/expression-maps/active.json` 放到定制宿主安装目录的 `expression-maps/active.json`。默认路径相对可执行文件为 `../expression-maps/active.json`。也可在启动宿主前，将环境变量 `MUSESCORE_VST_EXPRESSION_MAP` 设为该 JSON 的绝对路径；兼容别名为 `MUSESCORE_VSL_ARTICULATION_MAP`。
4. 将 `runtime/plugins/vsl_articulation_selector` 整个文件夹放入 MuseScore 的插件目录，在插件管理器中启用 **VSL Articulations**。三个文件必须放在同一目录。可在快捷键设置中为其分配 `Ctrl+Alt+V`；复制文件本身不会自动设置快捷键。
5. 新建两把小提琴、中提琴、大提琴的工程，分别选择独立的 Smooth Bridge 音源实例，并在每个播放器中载入对应的 Synchron Solo Strings **Velocity** 工厂预设：Violin、Violin 2、Viola、Violoncello。映射依赖该预设的维度结构与键位；不要把其他预设当作等价替代。
6. 各轨添加 Host Dynamics Smoother，并保持单音动态开启。原四重奏验证使用的平滑时间依次为 `50 / 10 / 10 / 15 ms`，可作为初始参照。插件参数的详细设置以构建说明和实际界面为准。

本仓库不附带嵌入商业播放器状态的 MSCZ 模板、工厂预设或音库。原验证模板每部从 20 个已启用采样叶开始；自行载入完整工厂预设时，初始内存和启用集合可能不同。后续按需启用机制不会把不同工厂预设自动改造成正确的目标预设。

建议先用四部各一个长音、`p → f → p` 及一次 `pizz. → arco` 检查设置，再开始复杂作品。该版本的验证环境为 Windows x64、MuseScore 4.7.7 基线与 Synchron Player core 1.3.0.3022。版本或库布局变化后，应重新验证。

## 常规记谱

正常输入力度与渐强渐弱。常用奏法可以由谱面符号或准确谱表文字触发，无需插入控制音或手动同步。

| 记号 | 对应行为 |
| --- | --- |
| `arco`、`ord.`、`ordinario` | 恢复普通拉弦路线 |
| `pizz.`、`pizzicato` | 持续拨弦，直至后续奏法指令 |
| `spicc.`、`spiccato` | 默认跳弓 |
| `staccato`、断奏点 | 默认断奏；符号保留宿主时值处理 |
| `detache`、`détaché` | 分弓 |
| 连音线 | 在适用的单线旋律上使用连奏；和弦有保护性回退 |
| `legato`、`portamento` | 持续连奏或默认滑奏连音路线 |
| `snap pizzicato`、巴托克拨弦符号 | Snap pizzicato |
| `col legno`、`col legno battuto` | 弓杆击弦 |
| 泛音圆圈、`harmonics` | 持续泛音；与断奏组合可使用短泛音 |
| `senza vibrato`、`non vibrato` | 无揉弦长音 |
| `molto vibrato` | 强揉弦长音 |
| `ricochet`、`ricochet a1`、`saltando` | 对应默认录制路线 |

普通文字指令不区分大小写，接受末尾句点。歌词、标题和包含说明句子的任意文字不会成为奏法命令。不要任意拼接多个词，期望它自动匹配录制音色。

标准斜线震音和标准 `tr` 颤音由 MuseScore 展开实际音符，保留节奏与音高；明确的 `tremolo`、`harmonic-tremolo`、`trill-half-tone`、`trill-whole-tone` 文字则选择录制片段。需要严格计量时，应选择标准符号，并试听核对。

## 选择精确奏法

选中一个普通音符、和弦或休止符，打开 **VSL Articulations**。搜索或筛选，查看短标记及完整英文路径，然后双击结果或点击 **Insert articulation**。要替换已有绑定，先选中相应谱表文字。

谱面显示短英文／意大利语标记，完整路线保存在隐藏的 `vslArticulationToken` 和 `vslCaptionSnapshot` 字段中。549 个目录分支使用 404 个不同短标记；有些短名相同，选择时须看完整详情，不能仅凭可见短名反推精确路线。

- 乐器筛选仅用于浏览；写入时会核对目标谱表的真实乐器。大提琴专用特效不能写入小提琴谱表。无法确认乐器、跨多个位置的选择和装饰音目标会被拒绝。
- 一次插入或替换构成一个撤销步骤。重新打开选择器时，按有效的隐藏绑定预选。
- 把可见文字手改为 `arco`、`pizz.` 等并提交后，旧绑定失效，宿主重新按普通规则解释。高级短名通常不能直接手写代替选择器绑定。
- 精确绑定的优先级高于普通局部奏法切换；使用 `arco` 等普通指令退出高级路线，再恢复常规符号控制。真实颤音／震音展开和单声路线遇和弦时，仍有宿主保护规则。

新路线按实际需要增量启用采样。删除标记不会立即卸载已加载采样；广泛试用会提高内存占用，工程也可能保存扩大的启用集合。首次使用新路线时，播放或导出可能等待加载准备。

## 保存与导出

使用此定制版保存 MSCZ／MSCX，才能保留精确绑定。MusicXML 不保存本项目的自定义字段；官方旧版重存也可能丢失它们。PDF 只包含可见标记。

正常使用 **文件 → 导出 → WAV**。历史验证使用 48 kHz、立体声、32 位浮点。谱面修改提交后，下一次回放或导出自动使用更新的奏法计划；不需要额外同步。修改外部映射或选择器目录后，保存工程并重启宿主。

## 已知边界

- 549 表示目录分支，包括速度选择、混合和序列；不等于 549 个互不重复的采样，也不表示逐条完成试听。见 [验证范围](VALIDATION.md)。
- 混合／XF 分支不等于自动生成 CC 曲线。当前映射不自动扫过所有混合端点；自动速度和序列由音源内部维度处理。
- Ricochet 的 a1–a4 是音色名称，不承诺固定反弹次数。录制力度过渡、录制定速震音不保证适配任意谱面速度。
- 当前四套预设未接通专用 mute／con sordino、sul ponticello、sul tasto 采样。上弓／下弓符号不保证强制指定录制弓向。
- 泛音圆圈使用宿主送出的音高，不自动把任意人工泛音指法换算成实音。一个 VST 实例上的同时音不能独立要求互相冲突的音色。
- 加载准备观察包含进程 IO／内存启发式，不是 VSL SDK 提供的 sample-ready 信号。不同机器、冷缓存、播放器版本或复杂多声部仍需自行验证。

本文与本项目代码按 GPL-3.0-only 提供。MuseScore、VSL 与 Synchron 的商标和独立产品权利属于各自权利人。
