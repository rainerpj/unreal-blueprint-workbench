# Unreal Blueprint Workbench

通过 JSON 补丁读取、编辑、编译 Unreal Engine 蓝图的轻量 Editor 插件，供 UE Python、PowerShell 和 AI 编程助手调用。当前版本 **0.3**，目标引擎 **UE 5.7.4**。

支持普通 Blueprint 和 Widget Blueprint 的 K2 图：导出节点／引脚／连接，添加变量和部分节点，显式连线与断线，生成副本，备份后写回原资产，以及按明确分组排版、添加块注释和保守清理旧节点。不是任意蓝图节点编辑器，也不提供 MCP 服务。

## 安装

1. 克隆本仓库，将 `Plugins/BlueprintWorkbench` 复制到目标工程的 `Plugins/BlueprintWorkbench`。
2. 在目标工程启用 **Python Editor Script Plugin** 和 **Editor Scripting Utilities**。Blueprint Workbench 默认启用；若项目显式禁用了它，也需启用。
3. 使用 UE 5.7.4 和匹配的 C++ 编译工具链编译目标工程的 Editor target。源码插件不附带二进制；纯蓝图项目可先添加一个 C++ 类以建立编译目标。
4. 保存工作并重新打开编辑器以加载新插件。检查 UE Python 中存在 `unreal.BlueprintWorkbenchLibrary`。

例如，已经有 `MyGameEditor` target 的 Windows 工程：

```powershell
& 'C:/Program Files/Epic Games/UE_5.7/Engine/Build/BatchFiles/Build.bat' `
  MyGameEditor Win64 Development '-Project=D:/Projects/MyGame/MyGame.uproject' `
  -WaitMutex -NoHotReloadFromIDE
```

脚本可以留在独立仓库运行，不必复制到工程。`-ProjectFile` 必须指向目标 `.uproject`；引擎不在默认目录时传 `-EngineRoot`。

## 快速开始

从本仓库根目录执行，替换示例工程与资产路径：

```powershell
# 读取已保存资产，结果写入目标工程 Saved/BlueprintWorkbench。
& ./Tools/BlueprintWorkbench/Invoke-BlueprintWorkbench.ps1 `
  -ProjectFile 'D:/Projects/MyGame/MyGame.uproject' `
  -AssetPath '/Game/Examples/BP_Example'

# 在内存副本上预览和编译，不保存资产。
& ./Tools/BlueprintWorkbench/Invoke-BlueprintWorkbench.ps1 `
  -ProjectFile 'D:/Projects/MyGame/MyGame.uproject' `
  -AssetPath '/Game/Examples/BP_Example' `
  -PatchFile ./Tools/BlueprintWorkbench/example.patch.json
```

添加 `-DestinationPackage '/Game/Workbench/BP_Example_Copy'` 才保存新副本，目标必须不存在。显式 `-WriteBack` 才会修改原资产：先关闭交互式编辑器，工具会预检、备份、编译保存，并另起进程重载核对。批量写回不是跨资产原子事务。

完整 API、补丁字段、排版及临时副本清理说明见 [工具手册](Tools/BlueprintWorkbench/README.md)。AI 主程技能的接入约定见 [调用约定](docs/AGENT_USAGE.md)。

## 验证

无需 UE 的 Python 标准库测试：

```powershell
python -m unittest discover -s Tools/BlueprintWorkbench -p TestCleanupLifecycle.py -v
python -m unittest discover -s Tools/BlueprintWorkbench -p TestPresentation.py -v
```

安装并构建插件后，在目标工程执行集成测试。测试创建唯一命名的隔离资产，检查副本编辑、错误输入拒绝、备份写回、独立进程重载和登记副本清理；不编辑既有玩法资产。报告和备份保留在目标工程 `Saved/BlueprintWorkbench`，成功时移除本轮测试资产。

```powershell
& ./Tools/BlueprintWorkbench/Validate-BlueprintWorkbench.ps1 `
  -ProjectFile 'D:/Projects/MyGame/MyGame.uproject'
```

详细实测范围见 [验证记录](docs/VALIDATION.md)。编译或图结构检查通过不等于 PIE、交互布局或设备验证通过。

## 当前限制

- 尚未验证其他 UE 版本、非 Windows 系统及全新机器安装。
- 不支持 Anim Blueprint、接口、宏库、函数库的修改；不编辑 UMG Designer、组件、数组／Map 或委托绑定。
- 外部 commandlet 只能看到已保存资产；不会读取另一个编辑器窗口的未保存修改。
- 排版尺寸为估算，不是 Slate 实测；循环图需要人工处理。
- 不包含引擎源码、游戏内容资产、项目专用接线脚本或构建产物。
- 当前尚未选择开源许可证；公开可见不代表授予开源使用许可。
