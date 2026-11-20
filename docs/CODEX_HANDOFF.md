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
