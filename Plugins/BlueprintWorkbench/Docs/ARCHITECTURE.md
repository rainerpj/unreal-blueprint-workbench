# 架构与共享能力索引

通用源代码以本目录为维护入口。插件只在 Editor 加载；PowerShell 负责选择工程／引擎和启动进程，UE Python 负责流程编排，C++ 负责图操作及引擎编译／保存。项目配置与玩法资产归宿主项目所有。

| 入口 | 用途与限制 | 定义 | 代表性实际调用 |
|---|---|---|---|
| BlueprintWorkbenchLibrary.InspectBlueprint | 读取当前进程的图／变量／Widget 元数据，不编译保存 | Source/BlueprintWorkbench/Private/BlueprintWorkbenchLibrary.cpp | Scripts/RunWorkbench.py、TestWorkbench.py |
| ApplyPatch | 副本编辑、编译；显式新路径才保存 | 同上 | RunWorkbench.py、TestWorkbench.py |
| ApplyPatchToSavedAsset | commandlet 预检、备份、原包编译保存 | 同上 | RunWorkbench.py、TestWorkbench.py |
| Get-WorkbenchContext／Invoke-WorkbenchPython | 验证显式工程／引擎路径，启动对应 commandlet | Scripts/Workbench.Common.ps1 | Validate-BlueprintWorkbench.ps1、Build-BlueprintWorkbench.ps1 |
| WorkbenchLifecycle.register_copy／finalize | 中间副本登记、独立重载核对、引用检查后清理 | Scripts/WorkbenchLifecycle.py | RunWorkbench.py、FinalizeWorkbench.py、TestWorkbenchReload.py |
| WorkbenchPresentation.plan／verify_logic／verify_presentation | 显式分组布局、注释、候选清理及语义核对 | Scripts/WorkbenchPresentation.py | RunWorkbench.py、TestPresentation.py |
| Install／New-ValidationHost | 不覆盖安装；生成独立版本宿主 | Scripts/*.ps1 | 安装说明；本轮独立宿主实际使用记录见兼容矩阵 |
| Build-BlueprintWorkbenchPackage／Install | UAT 生成 Win64 包；FilterPlugin 保留脚本与文档；WorkbenchBuild 与引擎 BuildId 匹配后安装预编译文件 | Scripts/Build-BlueprintWorkbenchPackage.ps1、Install-BlueprintWorkbench.ps1、Config/FilterPlugin.ini | README 预编译安装命令；纯蓝图宿主分发验证 |

## Parkour 迁移边界

提取时 Parkour 工程已有未提交的插件扩展及玩法资产修改。原 `bciDemo/bciDemo/Plugins/BlueprintWorkbench` 和 `Tools/BlueprintWorkbench` 保留，继续由项目维护；不覆盖源插件、不重跑玩法写回。StageB、Parkour 类型测试、固定 HUD 构造操作不进入通用包；通用 add_component 保留引擎组件默认配置，不附加项目碰撞预设。

这意味着 Parkour 目前仍使用原项目版本，尚未迁移到通用 0.4.0；不能把提取包当作已替换现有调用。后续迁移需先把项目专用 HUD 操作移到项目编辑器脚本／模块，审阅后再替换插件。其他项目可直接安装通用包。保持同名反射类是为了 API 连续性，一个项目不可同时加载两个同名插件版本。

这是当前未提交工作并存时的明确隔离边界，不是长期双向同步方案。后续通用修复在本目录维护，不自动传播或覆盖项目版本。
