# Unreal Blueprint Workbench

通过 JSON 补丁检查、编辑、编译与备份写回 Unreal Engine 蓝图的 Editor 插件。当前版本 **0.4.1**，核心功能主要验证版本 **UE 5.5.4**，同时验证 **UE 5.7.4**。本仓库继续原 0.3 的提交历史，是唯一维护入口。预编译分发的验证情况单独记录在兼容矩阵中。

## 安装与使用

将 [Plugins/BlueprintWorkbench](Plugins/BlueprintWorkbench) 整个目录安装到目标项目的同名 Plugins 目录，使用对应引擎编译。通用脚本、测试和说明现在随插件一起分发，不依赖游戏项目类型或资产。

**纯蓝图项目 / 不想编译：**使用 Releases 中匹配 UE 完整版本与引擎构建的 Win64 预编译附件（只有实际上传的附件才可下载）。GitHub 自动生成的 Source code ZIP 不含 DLL。安装命令、PowerShell 7 要求和构建不匹配处理见[插件手册](Plugins/BlueprintWorkbench/README.md#安装)。当前工具由脚本驱动，不包含自然语言聊天面板；无需 Codex 也能调用。

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

- UE 5.5.4：构建／加载及 65 项集成检查、7 项 CLI 流程通过。
- UE 5.7.4：构建／加载及 65 项集成检查通过；0.4.1 UAT Win64 包在全新纯蓝图宿主通过 65 项集成和 7 项 CLI 检查。
- Python 算法／清理边界测试：17 项通过；CI 不构建 Unreal。
- UE 5.5.4 的 UAT 分发被本机 PicoSpatialAudio 阻断；5.4 环境不完整；其他版本、其他平台、原生画布视觉、正式项目玩法／PIE／设备未验。

完整 [兼容矩阵](Plugins/BlueprintWorkbench/Docs/COMPATIBILITY.md)、[JSON 补丁格式](Plugins/BlueprintWorkbench/Docs/PATCH_FORMAT.md) 与 [AI 主程调用约定](docs/AGENT_USAGE.md)。Material、Niagara、Anim Blueprint 与通用 UMG Designer 编辑不在本版范围内。

## 开发验证

```powershell
python -m unittest discover -s Plugins/BlueprintWorkbench/Scripts -p TestCleanupLifecycle.py -v
python -m unittest discover -s Plugins/BlueprintWorkbench/Scripts -p TestPresentation.py -v
```

源码下载不附带二进制；预编译分发使用单独的版本附件，不能混用不同引擎构建的 DLL。备份、报告和隔离测试宿主保留在本机，不上传游戏资产或完整日志。项目代码采用 [MIT 许可证](LICENSE)，插件包内也包含许可文件；Unreal Engine 本身仍受 Epic 的授权条款约束。
