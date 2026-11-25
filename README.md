# Unreal Blueprint Workbench

通过 JSON 补丁检查、编辑、编译与备份写回 Unreal Engine 蓝图的 Editor 插件。当前版本 **0.5.0**，核心功能主要验证版本 **UE 5.5.4**，同时验证 **UE 5.7.4**。本仓库继续原 0.3 的提交历史，是唯一维护入口。预编译分发的验证情况单独记录在兼容矩阵中。

## 0.5.0 新增功能（2026-11-25）

- 普通蓝图、Function Library 和 Blueprint Interface 创建；接口实现／消息、类型容器与默认值、函数属性与参数、事件／Dispatcher、动态引脚及受控重构。见 [JSON 补丁格式](Plugins/BlueprintWorkbench/Docs/PATCH_FORMAT.md)。
- 局部变量默认值、Call in Editor、Make Array；Set Members 遵循原生结构体支持范围，Vector 等使用原生 Make＋赋值。
- 跨组长线与估算交叉检查、保形平移，以及受限的成员读取／Self就近复制；用法见[插件手册](Plugins/BlueprintWorkbench/README.md)。
- 可选 [Blueprint Assist 编辑器桥接](Plugins/BlueprintWorkbenchAssist/README.md)：直接调用另行安装的 Blueprint Assist 整理指定函数，另提供显式函数／事件／宏入口批次、保守无用节点清理和独立重载；支持普通／Widget及动画蓝图中的 K2 逻辑图，保留预览、逻辑核对和备份。

这些功能纳入 0.5.0；旧 0.4.1 Release 附件不包含它们。桥接另有UE5.5.4／5.7.4预编译附件，安装顺序和依赖版本见[桥接手册](Plugins/BlueprintWorkbenchAssist/README.md)；不附带第三方Blueprint Assist源码、二进制或许可证，需自行安装匹配版本。新增能力的验证范围见交接记录，不沿用旧版本的全部兼容性结论。

## 安装与使用

将 [Plugins/BlueprintWorkbench](Plugins/BlueprintWorkbench) 整个目录安装到目标项目的同名 Plugins 目录，使用对应引擎编译。通用脚本、测试和说明现在随插件一起分发，不依赖游戏项目类型或资产。

**纯蓝图项目 / 不想编译：**[v0.5.0 Release](https://github.com/rainerpj/unreal-blueprint-workbench/releases/tag/v0.5.0) 提供 UE5.5.4 和 UE5.7.4 的 Win64 预编译包及源码包；选择与完整引擎版本和 BuildId 匹配的附件。GitHub 自动生成的 Source code ZIP 不含 DLL。安装命令、PowerShell 7 要求和构建不匹配处理见[插件手册](Plugins/BlueprintWorkbench/README.md#安装)。当前工具由脚本驱动，不包含自然语言聊天面板；无需 Codex 也能调用。

完整安装、调用、备份及源码打包说明见 [插件手册](Plugins/BlueprintWorkbench/README.md)。例如从仓库根目录读取已保存蓝图：

```powershell
& ./Plugins/BlueprintWorkbench/Scripts/Invoke-BlueprintWorkbench.ps1 `
  -ProjectFile 'D:/MyGame/MyGame.uproject' `
  -EngineRoot 'C:/Program Files/Epic Games/UE_5.5' `
  -AssetPath '/Game/Blueprints/BP_Player'
```

Tools 下保留 Invoke 与 Validate 两个 PowerShell 转发入口；它们要求同样显式指定 ProjectFile、EngineRoot。旧 Python 导入、测试和示例路径改用插件内 Scripts，详见 [迁移说明](Tools/BlueprintWorkbench/README.md)。

## 能力与验证

普通 Blueprint／Widget K2 逻辑图的读取、变量、部分节点、显式连线与断线、副本编译保存、带备份的原资产写回、独立进程核对、入口分组排版／块注释及保守旧节点清理。不提供任意图编辑器或 MCP 服务。

- 当前源码：UE 5.5.4 与 5.7.4 分别重新构建，各 **124 项集成检查通过**，包括保存、备份写回和独立进程重载；另一个 UE5.5 安装宿主同样通过124项，两阶段重载0错误0警告。
- 提交前执行与 CI 对应的 **49 项 Python 测试**及源码／预编译安装契约检查，全部通过；CI 不构建 Unreal。
- 0.5.0 两版 Win64 预编译包均经 UAT BuildPlugin 构建，并从 ZIP 安装到无 C++ Source／Editor Target 的新纯蓝图宿主验证。UE5.5旧版环境插件阻断已在本轮隔离构建中解决，未把第三方插件纳入附件。
- 测试包含预期负例及宿主环境警告；未覆盖所有类型组合、原生 UI、PIE、游戏打包、设备或其他平台。精简证据见 [本轮验证摘要](Plugins/BlueprintWorkbench/Docs/validation/2026-11-25-vector-fix-summary.json)。

完整 [兼容矩阵](Plugins/BlueprintWorkbench/Docs/COMPATIBILITY.md)、[JSON 补丁格式](Plugins/BlueprintWorkbench/Docs/PATCH_FORMAT.md) 与 [AI 主程调用约定](docs/AGENT_USAGE.md)。Material、Niagara、Anim Blueprint 与通用 UMG Designer 编辑不在本版范围内。

## 开发验证

```powershell
python -m unittest discover -s Plugins/BlueprintWorkbench/Scripts -p TestCleanupLifecycle.py -v
python -m unittest discover -s Plugins/BlueprintWorkbench/Scripts -p TestPresentation.py -v
```

源码下载不附带二进制；预编译分发使用单独的版本附件，不能混用不同引擎构建的 DLL。备份、报告和隔离测试宿主保留在本机，不上传游戏资产或完整日志。项目代码采用 [MIT 许可证](LICENSE)，插件包内也包含许可文件；Unreal Engine 本身仍受 Epic 的授权条款约束。
