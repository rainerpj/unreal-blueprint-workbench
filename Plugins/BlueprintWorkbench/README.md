# Blueprint Workbench 0.4.0

独立 UE Editor 源码插件：用 JSON 补丁检查、编辑普通 Blueprint 和 Widget Blueprint 的 K2 逻辑图。通过 UE Python／PowerShell 调用，不依赖 Parkour、联网服务或 Computer Use，不增加游戏运行时模块。

当前版本验收结果见 [兼容矩阵](Docs/COMPATIBILITY.md)；工作交接见 [CODEX_HANDOFF.md](Docs/CODEX_HANDOFF.md)。不同 UE 小版本需要分别构建，不能混用插件 DLL 或将高版本测试资产降级打开。

## 安装

环境：Windows x64、目标 UE 的 C++ 构建工具链；源码包需要编译。插件声明 PythonScriptPlugin 与 EditorScriptingUtilities 依赖。

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

纯蓝图项目也不能直接加载未编译的源码插件。可先添加 C++ 类获得 Editor Target，再构建；或用对应引擎的 `RunUAT.bat BuildPlugin -Plugin=<绝对路径>/BlueprintWorkbench.uplugin -Package=<新的输出目录> -TargetPlatforms=Win64` 生成该引擎专用二进制包，再安装输出插件。本轮源码交付与测试不代表已验证 UAT 预编译分发路径或所有纯蓝图项目。

## 使用

所有 CLI 显式指定目标工程和引擎，不从当前目录猜测工程，不硬编码引擎安装路径。

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
& ./Scripts/Package-BlueprintWorkbench.ps1 -OutputFile 'D:/Deliveries/BlueprintWorkbench-0.4.0-source.zip'
```

验证器只修改唯一命名的测试资产；成功后清理本轮保存的测试资产，保留日志／备份。失败现场保留供诊断。它不是玩法接线、PIE、设备或正式项目迁移验收。独立宿主默认禁用其他引擎插件，避免全局安装的第三方插件影响测试，不修改引擎安装。
