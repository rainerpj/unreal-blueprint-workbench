# Blueprint Workbench 0.4.1

本插件代码采用 [MIT 许可证](LICENSE)。Unreal Engine 本身不包含在本授权中。

独立 UE Editor 源码插件：用 JSON 补丁检查、编辑普通 Blueprint 和 Widget Blueprint 的 K2 逻辑图。通过 UE Python／PowerShell 调用，不依赖 Parkour、联网服务或 Computer Use，不增加游戏运行时模块。

当前版本验收结果见 [兼容矩阵](Docs/COMPATIBILITY.md)；工作交接见 [CODEX_HANDOFF.md](Docs/CODEX_HANDOFF.md)。不同 UE 小版本需要分别构建，不能混用插件 DLL 或将高版本测试资产降级打开。

## 安装

### 预编译 ZIP（无需 Visual Studio）

从仓库 Releases 选择与 **UE 完整版本及引擎构建一致的 Win64 包**。GitHub 的 `Source code.zip` 是源码，不是预编译包。如果尚未发布所需版本的附件，请使用下面的源码安装流程，不要混用相邻版本 DLL。

关闭目标项目编辑器，解压发行 ZIP，在解压出的插件目录运行（需要 PowerShell 7）：

```powershell
& ./Scripts/Install-BlueprintWorkbench.ps1 -ProjectFile 'D:/MyGame/MyGame.uproject' -EngineRoot 'C:/Program Files/Epic Games/UE_5.7'
```

安装器检查引擎 BuildId 并复制 DLL、预编译数据和脚本，可用于纯蓝图项目；拒绝覆盖已有插件。也可以把解压出的 `BlueprintWorkbench` 整个目录放入项目 `Plugins/`，但需自行保证引擎构建匹配。插件或 PythonScriptPlugin、EditorScriptingUtilities 如果被项目显式禁用，需要启用后重启编辑器。

安装完成后使用项目插件内 `Scripts/Invoke-BlueprintWorkbench.ps1`，命令见下文。这是脚本驱动的蓝图工具，没有聊天窗口、MCP 服务或自动理解自然语言的编辑器面板；Codex 是可选调用方，不是运行依赖。

### 源码安装（需要 C++ 工具链）

环境：Windows x64、PowerShell 7、目标 UE 的 C++ 构建工具链；源码包需要编译。UE 内使用引擎自带 Python，不需要安装系统 Python。插件声明 PythonScriptPlugin 与 EditorScriptingUtilities 依赖。

1. 关闭目标项目编辑器，将整个目录放到 `<项目>/Plugins/BlueprintWorkbench`。也可在本源码目录执行：

```powershell
& ./Scripts/Install-BlueprintWorkbench.ps1 -ProjectFile 'D:/MyGame/MyGame.uproject'
```

安装器拒绝覆盖已有插件，不修改 `.uproject` 或玩法资产。已有插件须先审阅迁移差异。插件默认启用；若项目显式禁用了插件或其依赖，需要在项目配置中启用。

2. C++ 项目用目标引擎构建自己的 Editor Target：

```powershell
& ./Scripts/Build-BlueprintWorkbench.ps1 `
  -ProjectFile 'D:/MyGame/MyGame.uproject' `
  -EngineRoot 'C:/Program Files/Epic Games/UE_5.5' -EditorTarget MyGameEditor
```

纯蓝图项目不能直接加载未编译的源码插件。优先使用匹配的预编译包；没有对应包时，可添加 C++ 类获得 Editor Target，再构建，或由具备工具链的开发者按下文生成分发包。实际验证范围以兼容矩阵为准。

## 使用

所有 CLI 显式指定目标工程和引擎，不从当前目录猜测工程，不硬编码引擎安装路径。

如果引擎启动报 DDC/Zen 没有可写缓存节点，可显式添加 `-MemoryCache` 使用本进程内存缓存。它也适用于 Validate 和 Test-WorkbenchCli；不是默认开启，不修改引擎或项目缓存配置。

```powershell
# Inspect：读取已保存资产；另一编辑器窗口未保存的内容不可见。
& ./Scripts/Invoke-BlueprintWorkbench.ps1 `
  -ProjectFile 'D:/MyGame/MyGame.uproject' `
  -EngineRoot 'C:/Program Files/Epic Games/UE_5.5' `
  -AssetPath '/Game/Blueprints/BP_Player'

# 默认预览：修改内存副本并编译，不保存资产。
& ./Scripts/Invoke-BlueprintWorkbench.ps1 `
  -ProjectFile 'D:/MyGame/MyGame.uproject' `
  -EngineRoot 'C:/Program Files/Epic Games/UE_5.5' `
  -AssetPath '/Game/Blueprints/BP_Player' -PatchFile './Scripts/example.patch.json'
```

添加 `-DestinationPackage '/Game/Workbench/BP_Player_Review'` 保存新副本，拒绝覆盖。只有本次中间审阅副本需要后续清理时才添加 `-TemporaryCopy`；普通交付副本不自动删除。

添加 `-WriteBack` 修改原资产：必须先保存并关闭交互式 UE 编辑器。流程为副本预检 → 原文件备份 → 修改／编译／保存 → 另一进程重载核对与重编译 → 查询引用 → 清理同源同补丁已登记的无引用临时副本。原资产备份及诊断报告保留在目标项目 `Saved/BlueprintWorkbench/`。批量写回不提供跨资产原子事务；失败后退出该 commandlet，检查报告及备份后再重试。

`-PresentationFile` 接受明确入口分组和旧链候选，与 `-PatchFile` 互斥；支持入口附近布局、块状注释和保守清理。尺寸是估计值，闭环布局会拒绝，复杂图仍需编辑器目视检查。结构示例：

```json
{"graph":"EventGraph","groups":[{"entry":"入口节点GUID","nodes":["入口节点GUID","相关节点GUID"],"title":"开始游戏","details":"提交开局请求"}],"candidates":["本次旧链节点GUID"]}
```

## API 与限制

```python
import json, unreal
lib = unreal.BlueprintWorkbenchLibrary
description = json.loads(lib.inspect_blueprint('/Game/Blueprints/BP_Player'))
preview = json.loads(lib.apply_patch('/Game/Blueprints/BP_Player', json.dumps({'operations': []})))
```

公开 API 与主要 JSON 操作保持原有名字。直接 C++／Python 写回调用只执行单项操作，调用方需自己保证其他编辑器已关闭，并在新进程调用 `WorkbenchLifecycle.finalize` 完成核对与登记副本清理；推荐使用 CLI。

`success` 表示请求成功，`compiled` 表示执行过编译，`compile_errors`／`compile_warnings` 是编译诊断，`saved` 表示实际保存成功。不能只凭 `compiled=true` 判断通过。成功输出包含图、节点、引脚、源节点 GUID 映射及新增别名；编译可能重建动态引脚，后续编辑应重新 Inspect。

支持范围及格式见 [PATCH_FORMAT.md](Docs/PATCH_FORMAT.md)。不支持 Material、Niagara、Anim Blueprint、蓝图接口／宏库／函数库编辑；不提供通用 UMG Designer 编辑、任意节点删除、自动引用替换或自动插入转换节点。普通 Blueprint 中相关资产的函数调用不等于修改这些资产内部图。

## 验证与交付

维护者生成可供纯蓝图项目使用的 Win64 包（需要对应 UE 和 C++ 工具链，输出目录必须不存在）：

```powershell
& ./Scripts/Build-BlueprintWorkbenchPackage.ps1 -EngineRoot 'C:/Program Files/Epic Games/UE_5.7' -OutputDirectory 'D:/Deliveries/Workbench-UE57'
```

此命令执行 UAT BuildPlugin，按 FilterPlugin.ini 保留必需脚本、配置和文档，并产生版本标识及 ZIP。ZIP 不包含 PDB 调试符号；源码和 UAT 预编译数据保留。构建后必须在全新纯蓝图项目中测试安装、加载、预览、写回与独立重载。UAT 构建成功不等于已发布 GitHub Release。引擎全局第三方插件可能影响 UAT 自动生成的宿主；遇到此类错误请检查构建日志，在干净的相应引擎环境构建，不要替换其他版本的 DLL。

```powershell
# 创建独立的原生最小宿主；目录必须不存在。不会打开或降级现有项目。
& ./Scripts/New-ValidationHost.ps1 -HostDirectory 'D:/WorkbenchTests/UE55' `
  -EngineRoot 'C:/Program Files/Epic Games/UE_5.5'
# 对宿主执行 Build-BlueprintWorkbench.ps1，EditorTarget 为 WorkbenchHostEditor，然后：
& ./Scripts/Validate-BlueprintWorkbench.ps1 -ProjectFile 'D:/WorkbenchTests/UE55/WorkbenchHost.uproject' `
  -EngineRoot 'C:/Program Files/Epic Games/UE_5.5'
# 真实 CLI 端到端流程（同样只操作本次创建的测试资产）：
& ./Scripts/Test-WorkbenchCli.ps1 -ProjectFile 'D:/WorkbenchTests/UE55/WorkbenchHost.uproject' `
  -EngineRoot 'C:/Program Files/Epic Games/UE_5.5'
# 生成源码 ZIP，排除构建产物、测试宿主、缓存和项目资产；不覆盖已有 ZIP。
& ./Scripts/Package-BlueprintWorkbench.ps1 -OutputFile 'D:/Deliveries/BlueprintWorkbench-0.4.1-source.zip'
```

验证器只修改唯一命名的测试资产；成功后清理本轮保存的测试资产，保留日志／备份。失败现场保留供诊断。它不是玩法接线、PIE、设备或正式项目迁移验收。独立宿主默认禁用其他引擎插件，避免全局安装的第三方插件影响测试，不修改引擎安装。


### Cross-group layout (2026-11-23)

Normal presentation plans now translate their completed blocks using connections to other blocks and fixed outside nodes. Whole-graph audits include both execution and data wires, two-dimensional node-origin distance, and remaining edges over 3000 graph units. This is a diagnostic threshold, not a promise that all wires can fit below it.

To compact an existing graph without rebuilding its internal layout, use a presentation file:

```json
{"graph":"EventGraph","mode":"cross_group","threshold":3000}
```

Existing comment regions and nearby ungrouped connected islands are translated rigidly. Because Inspect does not export authoritative comment membership, automatic groups use spatial containment; review the generated `.plan.json`. Alternatively supply `groups` as disjoint arrays of node IDs and `movable` as their zero-based indices. Nodes omitted from explicit groups are fixed when `movable` is supplied. No topology edits, reroutes, deletion, or comment text changes occur in this mode. Accepted moves reduce a combined crossing/length score, do not increase estimated crossings, avoid estimated block overlaps, and do not increase the original maximum cross-wire length. Existing internal overlaps and long internal wires remain unchanged.

The report's `.presentation.json` records whole-graph before/after metrics and unresolved long edges. Node dimensions are estimates, not Slate measurements; a geometric preview is not native-editor visual acceptance. Blueprints with concrete self-type references may fail ordinary duplicate-class previews; hosts that expose `PreviewSavedAssetPatch` must preflight in a separate process before saving, as in the Parkour validation script. Do not ignore preview compiler errors or save in the same in-memory preview process.


### Readability rules: local reads and crossing reduction

For an explicitly authorized graph-wide readability pass, use:

```json
{"graph":"EventGraph","mode":"readability","threshold":1200,"max_copies":32}
```

* When a direct read feeds distant consumers, keep the closest consumer on the original and place a copy near each distant consumer. `max_copies` bounds growth. Sources that cannot be certified, or cannot fit nearby, are listed as skipped.
* Only engine-certified direct member-variable getters and Self nodes qualify. No arbitrary pure functions (including random/time reads), execution nodes, validated getters, custom BlueprintGetter functions, externally connected contexts, split pins, containers, local variables, or reference consumers are copied. The C++ operation rechecks eligibility rather than trusting the JSON flag.
* Existing `cross_group` and normal block layout now consider wire crossings as well as length. `readability` additionally adjusts pure-node positions inside existing comment membership to reduce internal crossings. It does not move execution nodes during this local refinement.
* Crossings are strict intersections of estimated pin-to-pin straight segments, excluding shared-node fan-out/fan-in. They are an optimization proxy, not exact UE Bezier intersections; collinear overlap is not included. Remaining crossings and long wires are reported. If localization increases this metric, return the translation-only fallback instead.
* Verification collapses only the declared equivalent read copies back to their sources and checks every original execution/data dependency and default. It also checks expected node positions and leaves other graphs untouched. This mode intentionally changes data-wire endpoints while preserving their source meaning; it is not the old exact-topology-only mode.

Requires rebuilt plugin code for `localizable_read` inspection and `add_node(kind=local_read, source_node=...)`. Old plugin exports do not certify nodes and therefore cannot enable copying silently. Do not hot-replace a loaded editor DLL. Source updates are not deployment of a running project's binary.

Tests: `python -m unittest TestPresentation TestCrossGroup TestReadability`; isolated `WorkbenchHost` UE scripts `TestReadabilityUE.py` and then `TestReadabilityReload.py`. Never point fixture tests at a production project. Actual Slate visual quality still needs editor inspection on a future authorized asset pass.
