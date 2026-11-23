## 2026-11-23 GitHub 源码同步

- 用户授权将更新后的工具提交并推送原仓库 main。范围：跨组长线／交叉整理、受限读取就近复制、可选Blueprint Assist桥接（含Widget函数识别）、测试与文档；不上传游戏资产、备份、引擎／第三方插件或预编译产物，不新建Release／标签。
- 本次重新运行20项布局／readability测试及6项桥接语义测试，全部通过；新增三组测试纳入GitHub Actions，根README明确现有0.4.1附件不含新源码。此前UE5.7.4构建、原生画布及真实保存重载证据见前述记录，本次未重复引擎构建或PIE。
- 后续实际批次：16函数调用BA，15布局变化保存、1无变化不重复保存，7资产累计逻辑及非目标图位置核对通过。个别函数交叉略增、断开旧链保留；Enum to String已接线输入未使用默认值的空／None重载差异通过确切节点单独复核，通用验证器未放宽。项目明细保存在项目Saved，未随源码上传。

## 2026-11-23 Blueprint Assist Widget 函数编辑器识别

- Parkour批量函数整理发现 WB_Base 的 Update Button Sate 在 OpenGraph 被旧的 BlueprintEditor 名称限制拒绝，失败未保存。根据 UE5.7 UMGEditor 的 FWidgetBlueprintEditor : FBlueprintEditor、GetToolkitFName 返回 WidgetBlueprintEditor，增加该确切类型白名单；没有接受任意编辑器或修改 BA 算法。
- 通用源与 Parkour 安装源同步；UE5.7.4 Editor 增量构建成功。Widget函数原生画布预览、真实BA调用、逻辑核对、保存、独立重载编译均成功，0错误0警告。证据 Parkour Saved/BlueprintWorkbench/AssistFunctions20261123/07/{preview2,saved}，构建日志同批次 widget-bridge-build.log。没有调整Designer控件的工具操作。
- 正确性与必要性复核：仅扩展已确认继承关系的编辑器名称判断，复用现有 JumpToHyperlink、BA缓存与排版、原语义验证链；未引入新的运行时依赖或放宽通用语义比较。未测试其他UE版本、PIE或设备；未提交／推送。

## 2026-11-23 Blueprint Assist 编辑器桥接与 UpdateTurning 验证

- 新增独立可选 BlueprintWorkbenchAssist（EditorNoCommandlet），通用源码位于 C:/Users/tangc/.codex/tools/blueprint-workbench/Plugins/BlueprintWorkbenchAssist，Parkour 安装于 C:/Users/tangc/.codex/tools/ue-project-tools/parkour/Plugins/BlueprintWorkbenchAssist。项目启用该插件；未修改 Blueprint Assist 源码、未替换项目定制 Workbench。
- Scripts/Invoke-Assist.ps1 在完整编辑器中打开指定函数，等待真实 Slate 尺寸缓存，直接调用 Blueprint Assist FormatNodes。v0.1 限单入口函数图；先预览，再指定成功 PreviewDirectory 写回；备份、语义核对、编译、保存及独立进程重载均由入口串联。拒绝已有编辑器进程、过期预览及非目标图变化。
- BP_AutoRunCharacter.UpdateTurning 已保存：77个既有节点移动，新增15个透明 reroute，91→106节点；未复制玩法节点或修改逻辑。核对透明转接后的终端引脚连接、类型、默认值与元数据，并验证其他图位置不变。仅归一化编译器生成的 InputKeyEvent 函数数字后缀，仍检查按键和处理函数前缀。
- 估算直线交叉191→37，超过3000图坐标单位线段16→2；执行线超长6→0。仍有2条长数据线，最大数据线段约6292；这些指标不等同于原生样条交叉计数。已查看原生 Slate 整理前后画布，未以重绘示意图替代视觉检查。
- UE5.7.4 Editor 构建通过，6项语义验证单元测试通过；preview6、saved1及独立重载均成功，蓝图编译0错误0警告。早期运行因设置反射名称、reroute尺寸等待及生成绑定后缀导致失败，均未保存；修复后重新验证成功。
- 证据：Parkour Saved/BlueprintWorkbench/AssistUpdateTurning/preview6 与 saved1；saved1/result.json、reload.json、before.png、after.png。原资产备份为 saved1/BP_AutoRunCharacter.uasset。当前资产已经整理，后续须重新预览，不能直接重放旧写回请求。
- 正确性审阅覆盖精确图定位→缓存→真实插件排版→语义验证→编译→保存→重载；必要性审阅确认复用现有 BA 算法及缓存，独立编辑器模块不耦合原 commandlet，无运行时新增系统。未验证 UE5.5、PIE、设备或打包；未提交／推送。

## 2026-11-23 读取就近复制与交叉评分（仅工具）

- 用户后续明确要求“先不用改蓝图了，就改工具”。本轮未调用Parkour资产编辑／保存，也未修改项目内文档或运行中插件二进制。通用源码及迁出的Parkour工具源码已更新；项目扩展DLL仍需以后关闭编辑器再构建，不能用通用DLL替换定制版。
- 新增 `WorkbenchReadability.py`、presentation `mode=readability`，默认1200距离阈值、最多32副本。一读多用时保留最近消费者，远端消费者改接引擎认证的直接成员读取／Self副本。C++ Inspect增加localizable_read，新增local_read受限操作；拒绝任意纯函数、执行／Validated Get、BlueprintGetter、外部上下文、容器、拆分引脚和引用消费者。未认证旧导出不能触发复制。
- 跨组及常规分组排版共同考虑估算交叉与长度，拒绝交叉增加的移动；readability进一步对纯节点在原注释归属内小范围挪位。若复制增加估算交叉，返回仅平移后备方案。指标为估算引脚间直线交叉，不等价于UE样条交叉，不含共线重叠。
- 语义核对仅归一化声明的等价读取副本，然后核对原执行边、数据依赖、默认值和其他图；新增副本类型／引脚形状、预期位置与指标单独核对。不会以“纯函数”标签作为复制许可。
- 验证：20项Python测试；独立WorkbenchHost UE5.7.4构建成功；新UE夹具中getter复制、Self复制、拒绝执行节点、编译、保存副本、独立进程重载语义核对和重编译预览通过（蓝图0错误0警告）；原TestWorkbench本轮55项通过。宿主启动有缺少ACL默认压缩资源警告，负向测试有预期不存在类型警告，不称全进程零警告。首次集成发现source_node未纳入字段白名单，修复重构建后通过。
- 证据在 `C:/Users/tangc/.codex/tools/blueprint-workbench-validation/20261123-readability/`：build.log、integration.log、regression.log、reload.log、Saved/BlueprintWorkbench/readability-{integration,reload}.json。只使用隔离夹具资产。
- 对上轮已导出的439节点快照离线规划：21.83秒、133位置操作、估算交叉60→30，无复制（旧快照不含认证字段）。没有重新读取／保存实际游戏蓝图；该结果不是资产落地或原生画布视觉验收。
- 未验证5.5构建、运行中Parkour扩展二进制、原生Slate画布、PIE或设备；未提交、推送、发布。下次获准整理资产时先重构建外部定制插件、Inspect最新资产、预览并验证后再写回。

## 2026-11-23 跨组长线排版修复

- WorkbenchCrossGroup.py 新增全图二维连线审计、注释区域／相邻孤岛保形平移；原 plan 在组内排版后同时优化组间及组到固定外部节点连线。RunWorkbench 支持显式 cross_group 模式，输出剩余长线列表；不增删节点、不改引脚或连接。通用源修复已同步到 Parkour 公共扩展的两个排版模块，未覆盖插件和其他扩展。
- BP_AutoRunCharacter.EventGraph 已保存：439节点中147个节点／注释框移动，65个保形组，409条连接保持。阈值3000图坐标单位：跨组超长16→1，跨组最长14242.24→3563.61；执行线超长13→0；全图超长17→2（剩余为数据线，其中一条组内）。全图逆向执行连接20→14；没有强行重接消除旧逆向线。
- 15项排版单元测试通过；UE5.7.4 保持原类身份内存预览、原包备份保存、独立进程重载再编译均0错误0警告。逐图核对保留节点的类型、引脚、默认值、连接、变量和组件外部元数据；所有非目标图位置不变，目标组内相对位置不变。差异检查通过。无C++改动，未重跑引擎构建／PIE／打包／设备。
- 普通复制预览因已知Self→In Pawn具体类型不兼容失败且未保存；切换项目已有PreviewSavedAssetPatch独立进程验证后通过。首次严格JSON整体相等检查受导出差异阻挡，改为逐节点语义及坐标前置核对后通过，没有忽略编译失败。
- 证据：项目 Saved/BlueprintWorkbench/cross-group-{before,patch,plan,preview,save,reload}.json、*-verified.json、cross-group-comparison.png；复现脚本 ValidateCrossGroup.py（preview/save/reload分进程）和 RenderCrossGroup.py。备份：Saved/BlueprintWorkbench/Backups/CC914A03477D26EEFDEDDB9AD4C8F21A/BP_AutoRunCharacter.uasset。
- 已目视几何预览；未打开原生UE画布进行Slate视觉验收。尺寸仍为估计，旧组内重叠未处理。当前资产已整理，不要直接重放依赖旧before快照的保存脚本；再次整理必须重新Inspect。未提交／推送。

# Blueprint Workbench 交接

更新：2026-11-20。

## 0.4.1：Codex 全局入口与 GitHub 分发

- 全局维护 checkout 位于 `<CODEX_HOME>/tools/blueprint-workbench`，仍指向原 `rainerpj/unreal-blueprint-workbench` 仓库，基于 `4d090a1`。新增全局 `blueprint-workbench` 技能，并在 `ue-lead-programmer` 中添加调用引用。原 Parkour 项目与旧本地 checkout 保留；后续通用修复在全局 checkout 维护，不双向同步项目定制版。
- 用户选择 MIT 许可证；根目录和插件目录提供 LICENSE，源码安装、ZIP 与 UAT 包包含许可文件。Fab 上架不再是本轮目标。
- 新增 Build-BlueprintWorkbenchPackage 和 FilterPlugin.ini，生成按 UE 构建区分的 Win64 分发 ZIP；修复 UAT 漏打 Scripts/Docs/Config 和安装器漏复制二进制的问题。预编译安装检查引擎 BuildId，拒绝覆盖现有插件。
- 修复 UE Python 的 Windows 反斜杠路径转义问题；提供显式 `-MemoryCache` 处理已诊断的本机 DDC/Zen 无可写缓存错误，不修改引擎配置。文档明确 PowerShell 7、源码与预编译附件区别，以及脚本驱动的产品边界。
- 验证：5.7.4 UAT Win64 构建通过；全新无项目 C++ 模块的纯蓝图宿主使用包内脚本通过 65 项集成和 7 项 CLI 检查（预览、新副本、备份写回、独立重载、排版、登记清理）。5.5.4 原隔离宿主用全局脚本重新通过 65 项集成；5.5 UAT 被本机默认 PicoSpatialAudio 插件阻断，不交付未经验证的 5.5 二进制包。原生 C++ 图逻辑本轮未修改。
- 17 项 Python 测试、源码/二进制安装契约及源码 ZIP 检查通过；新增 Windows CI 安装检查，尚未远端执行。详细范围见插件 COMPATIBILITY.md 和 validation/2026-11-20-summary.json。
- 正确性复核覆盖 BuildPlugin→过滤→ZIP→安装、BuildId 拒绝、CLI→RunWorkbench→Finalize 及失败退出；真实分发验证发现并修复 Config 漏包和反斜杠转义。必要性复核：保留现有 CLI/API，未新增 MCP、常驻服务、游戏模块或另一套图操作；缓存开关仅显式启用，源码安装继续复用 Copy-WorkbenchSource。
- 用户已于 2026-11-20 授权提交、推送和发布 0.4.1。发布内容为源码 ZIP 与 UE 5.7.4 Win64 预编译 ZIP；发布状态和下载以[原仓库 v0.4.1 Release](https://github.com/rainerpj/unreal-blueprint-workbench/releases/tag/v0.4.1)为准，CI 以对应提交的 Actions 结果为准。实际原生编辑器画布、玩法 PIE、设备、干净机器及其他引擎版本尚未验收；不宣称所有代码无问题或所有工程即插即用。

## 历史记录

## 0.4.0 通用化与 UE 5.5 优先验收

本次更新继续原 `rainerpj/unreal-blueprint-workbench` 仓库历史，未重新初始化或覆盖远端历史。误建的 `rainerpj/blueprint-workbench` 不再作为维护入口。

- 安装路径仍为 `Plugins/BlueprintWorkbench`；新版通用脚本、文档及测试随插件分发。Tools 下仅保留原两个 PowerShell 命令的转发入口，避免维护第二份实现；旧 Python 导入和示例路径需迁移。
- UE 5.5.4 与 5.7.4 分别构建、加载及 65 项集成检查通过；5.5.4 另有 7 项 CLI 流程通过。Python 排版 8 项、清理边界 9 项通过。详见 [当前兼容矩阵](../Plugins/BlueprintWorkbench/Docs/COMPATIBILITY.md)。
- 新增安装、独立宿主生成、源码打包及显式工程／引擎选择。5.5 注释说明元数据适配；通用组件保留引擎默认配置，刷新调用限制为显式函数列表。
- 迁移后从旧 Validate 转发入口再次执行 UE 5.5.4 的 65 项检查通过；新版目录下 Python 17 项通过，10 个 PowerShell 文件语法与仓库 Markdown 链接核对通过。
- 已有 CI 更新为插件内测试路径。没有将游戏工程、资产、日志、备份、宿主或构建产物加入仓库。
- UE 5.4 安装不完整；其他补丁、5.6、纯蓝图项目 UAT 分发及正式项目迁移／PIE／设备仍未验证。没有选择开源许可证。

## 历史：0.3 初次提取


- 目标：将 Blueprint Workbench 作为独立公有 GitHub 仓库维护，主程技能通过调用约定接入。
- 已完成：提取 v0.3 Editor 插件、通用调用／清理／排版脚本、示例和测试；PowerShell 改为必填 `ProjectFile`；补充安装、能力边界和 AI 调用文档。
- 排除：原游戏资产、项目专用接线脚本、原项目继承测试、二进制、缓存、备份和本机报告。
- 验证：Python 17 项及现有 UE 5.7.4 宿主中的 47 项集成检查通过。详见 `VALIDATION.md`。
- 后续：验证全新工程安装和独立构建；选择开源许可证；按具体需求再将调用入口接入本地主程技能。此次未修改本地主程技能。
- 提交和远端状态以 Git 为准；不在文档内复制认证信息或机器绝对路径。
