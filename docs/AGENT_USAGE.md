# AI 主程调用约定

工具独立维护；主程技能负责选择资产、生成补丁和验证玩法。技能应链接本仓库并记录使用的提交版本，不复制另一份工具源码长期维护。

1. 核对目标工程、UE 版本、插件加载情况和现有工作树。先 Inspect 获取当前节点 GUID、图路径和引脚；不要凭旧报告猜测。
2. 默认在内存副本预览；需要审阅资产时保存新副本。只有任务明确授权修改原资产时使用 `-WriteBack`，并确认交互式编辑器已保存和关闭。
3. 明确保留哪些入口和连接，补丁逐项表达修改。不得用工具能力替代玩法设计授权。
4. 检查 `success`、`compiled`、`compile_errors`、`compile_warnings` 和 `saved`；错误时停止依赖步骤，不把返回 JSON 当作成功。
5. 写回后核对独立进程的 finalize 报告。需要自动清理的审阅副本必须使用 `-TemporaryCopy` 登记；普通交付副本不登记。不自行扫描目录推断哪些资产可以删除。
6. 分别报告已实现、编译、保存图重载、可视编辑器审阅、PIE 和设备验证。原资产备份及报告留在目标工程，不上传到工具仓库。

工具手册位于 `Plugins/BlueprintWorkbench/README.md`，调用与测试脚本位于插件内 `Scripts/`。命令显式指定 ProjectFile 与 EngineRoot；Tools 下仅保留两个 PowerShell 转发入口。项目专用批量脚本留在对应项目，调用这里的通用接口。


## 后台桥接整理约定

整理蓝图连线、节点布局或执行 Blueprint Assist 格式化时，默认通过 BlueprintWorkbenchAssist 在后台直接调用 Blueprint Assist；不使用 Computer Use 点击画布、模拟按键或拖动节点。除非用户明确改变执行方式，不自动退回 UI 操作。

- 单函数使用 `Scripts/Invoke-Assist.ps1`；显式批次使用 `Scripts/RunBatchAssist.py` 和 `WORKBENCH_BATCH_REQUEST`，随后独立运行 `FinalizeBatchAssist.py`。脚本位于 BlueprintWorkbenchAssist 插件目录。
- 格式化需要带真实 Slate 的完整编辑器：后台启动 `UnrealEditor.exe`，使用 `-RenderOffscreen`，PowerShell `Start-Process -WindowStyle Hidden`。后台不等于 commandlet／NullRHI；不要用后两者执行 BA 格式化。
- 先核对目标项目、插件与匹配版本的 Blueprint Assist。保留预览、语义核对、备份、显式写回和独立重载；视觉核对优先读取桥接生成的原生图 PNG，不为截图改用 Computer Use。
- 缺少桥接、依赖或能力时，先在授权范围内修复安装／构建；仍不可用则报告具体阻塞，不自行改成鼠标操作或其他排版算法。写回前处理编辑器占用和未保存内容，不强关用户编辑器。

## 可读性验收补充

- 仅当用户明确要求算法式可读性重构（区别于默认桥接格式化）时，一读多用且距离较远的处理使用 `mode=readability`，优先考虑引擎认证的直接变量读取／Self就近副本；不能以 Pure 标志推断函数可复制。
- 长线与交叉必须同时审查，查看 report 的 estimated_crossings、long_edges、copies、skipped；无法安全处理就明确保留，不通过复制执行节点或缓存变量改变语义。
- 保留原有精确逻辑核对；读取副本必须通过专门等价归一化核对。几何交叉指标不是原生画布验收。仅改工具的任务不得顺带修改游戏资产。

## 资产创作与受控重构

函数库／接口／普通蓝图新建使用 CreateBlueprint 或 CLI -CreateKind；先预览，再明确 DestinationPackage，CLI 保存后独立重载验证。signature 的 entry_id/result_id 可用于同补丁接线。类型、默认值及新节点格式以 PATCH_FORMAT.md 为准。修改公共签名前先 FindMemberReferences/Inspect，按报告显式更新每个定义、实现与调用资产；refresh_interfaces 与 refresh_calls 不会替用户自动写回其他资产。引用扫描仅覆盖已保存包与支持的K2成员引用，不能把零匹配当作无任何外部依赖。
