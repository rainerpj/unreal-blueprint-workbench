# Blueprint Workbench 维护交接

## 2026-11-27 本地 Schema／UMG 扩展（未发布）

用户为 FourthGuesthouse COLLECTION-01 明确授权扩展工具。全局核心源码新增 CreateSchemaAsset（新 enum／struct，空目标预览）、Widget Blueprint 创建、add_widget／set_widget_property、variable_metadata、set_delegate_function，以及 CreateWidget／GameInstanceSubsystem／FormatText／ParentCall／void Return 节点。没有游戏类型进入通用插件。实现分布于 BlueprintWorkbenchLibrary.cpp/.h 和 WorkbenchSchemaAssets.inl，格式与边界见 PATCH_FORMAT.md 的本地扩展章节。

原类 Self 传入外部 Widget 的精确组件类型会在复制预览中失配，因此增加 ApplyPatchInIsolatedCommandlet：commandlet 源对象内存预览不保存，退出后新进程备份写回，再独立重载。正常复制预览不变。调用者必须编排不同进程并检查预览；不可在同一进程连跑预览和应用，不得与加载相同资产的编辑器并发。

已安装至 FourthGuesthouse 项目 Plugins/BlueprintWorkbench，UE 5.5.4 Editor 构建通过；Scripts/TestSchemaWidgetAssets.py 的 15 项瞬态正／反例通过，退出 0。实际 Blueprint 资产11个编译／后台 BA 语义验证／独立重载均0错误0警告；基本 PIE／重启／恢复通过。证据位于该项目 Saved/BlueprintWorkbench/Collection01/。未执行 UE5.7 构建、发布打包或干净机器验收；既有0.5.0附件不含此次扩展。保留已有 Invoke-Assist.ps1 修改，未提交／推送。

自审已核对新资产拒绝覆盖、字段默认校验、UMG 单子项／重复名／属性可编辑限制、引擎原生节点与元数据、预览不保存和写回备份边界。通用创建、编辑和安全编排复用既有实现；不新增玩法运行模块或替代 BA 排版器。

更新：2026-11-25。维护仓库为 rainerpj/unreal-blueprint-workbench；当前版本为0.5.0，新增功能及UE5.5／5.7预编译分发的验证范围见兼容矩阵；旧0.4.1附件不包含本轮新增能力。

## 0.5.0 桥接补充分发

BlueprintWorkbenchAssist新增UE5.5.4／5.7.4 Win64预编译包、独立源码包及安装器，分别匹配用户另行安装的Blueprint Assist 4.2.22／4.5.3；不分发第三方源码、DLL或许可证。安装顺序、依赖矩阵、隐藏后台调用与源码版本追溯见桥接README。原v0.5.0核心标签和核心附件不变，桥接补充源码以附件清单中的提交为准。

两版UAT构建成功；独立纯蓝图宿主分别通过单函数预览、真实BA格式化、语义核对、PNG审阅、备份写回和独立重载，进程正常退出，编译／重载均0错误0警告。19项Python检查和5项合成安装契约通过，新增检查接入CI。完整日志、测试资产与备份仅存本地；公开精简摘要见兼容矩阵。未重新覆盖复杂批次、PIE、游戏打包、设备或干净机器。

正确性复核覆盖Build-AssistPackage→Install-Assist、依赖版本／BuildId、项目字段保留／备份／拒绝覆盖、GraphHandlers精确图匹配、可选设置恢复及runner退出顺序；必要性复核确认复用原生BA与现有语义验证，不另建布局算法。处理了BA4.2缺少GetAllGraphHandlers和bFormatAllAfterSaving，以及UE5.5蓝图toolkit晚于world销毁的退出断言。收尾仅关闭本次隔离会话的目标资产编辑器，不保存失败预览。最小commandlet宿主不能直接作为完整Slate宿主使用；本轮完整编辑器测试启用正常引擎默认插件，未改游戏项目配置。

## 后台整理工作流（0.5.0发布后文档更新）

蓝图连线／节点格式化默认通过隐藏的 BlueprintWorkbenchAssist 完整编辑器桥接执行，不使用 Computer Use 或模拟键鼠；桥接失败先排查依赖／构建，不自动切换UI或其他布局算法。调用约定见 docs/AGENT_USAGE.md 和桥接README。已核对 Invoke-Assist 的隐藏／RenderOffscreen启动及批量脚本入口；此轮仅同步指令文档，未改代码或资产、未重复运行引擎测试，未重打已有Release附件。

## 当前实现

- 普通蓝图／Function Library／Blueprint Interface 创建，接口实现、事件及消息；统一成员、局部和参数类型，Array／Set／Map、默认值和函数属性；自定义事件、Dispatcher、常用节点、引脚编辑、重命名和受控节点替换。
- 局部默认值、Call in Editor、Make Array；引用查询与显式调用方刷新。写入继续使用副本预览、编译、备份和独立进程重载；不自动改写其他资产。
- Blueprint Assist 可选桥接支持函数／入口批次整理、保守无用节点清理与独立重载。第三方插件须单独安装，不包含其源码或二进制。
- 操作格式与限制以 [PATCH_FORMAT](../Plugins/BlueprintWorkbench/Docs/PATCH_FORMAT.md) 为准，共享入口见 [ARCHITECTURE](../Plugins/BlueprintWorkbench/Docs/ARCHITECTURE.md)。

## Set Members 修复与复核

SetFieldsInStruct 继承 MakeStruct 的旧节点转换，Vector 的 HasNativeMake 导致重载时尝试转换为 MakeVector，但执行引脚不能映射。旧夹具无执行线，被编译器裁掉，不能证明可执行。创建时现复用原生 CanBeMade 判定，拒绝不支持的结构体；Vector／Rotator 使用原生 Make＋变量赋值。没有修改引擎、屏蔽日志或新增自定义节点类。

正确性复核覆盖创建→原生能力检查→编译／保存／重载、节点替换后的旧GUID解析、接口override复制身份、返回引脚默认值、硬／软引用校验及原生默认值拒绝结果。必要性复核确认复用原生节点、属性导入、引用索引及既有保存契约，无新增运行时模块或自动跨资产修改。

## 0.5.0 分发

UE5.5.4／5.7.4 的Win64预编译包分别通过UAT BuildPlugin，并从ZIP安装到新纯蓝图宿主，各124项集成＋7项CLI通过。发布源码ZIP与SHA256校验文件；源码标签为v0.5.0。构建过程中临时隔离的本机第三方PICO插件已恢复，发行包不包含第三方插件。

## 验证与交付

- UE5.5.4／5.7.4 原生宿主分别构建成功，各124项检查通过；安装到另一 UE5.5 宿主后124项通过，其两阶段独立重载0错误0警告。
- 回归包含支持的 Set Members 实际执行、未选字段保留、独立重载后执行、Vector 拒绝及源资产不变。验证器对兼容转换错误直接失败。
- 提交前本机运行 CI 对应49项 Python 测试以及分发安装契约检查，全部通过。远端 Actions 状态需按提交单独核对。
- [兼容矩阵](../Plugins/BlueprintWorkbench/Docs/COMPATIBILITY.md) 与 [精简验证摘要](../Plugins/BlueprintWorkbench/Docs/validation/2026-11-25-vector-fix-summary.json) 保留公开证据；完整日志、备份、历史交接和游戏资产仅存本地。
- 未验收所有类型组合、原生UI／PIE／设备／游戏打包。安装器拒绝覆盖已有插件；定制宿主须先审阅差异，并用匹配引擎重新编译，不能混用DLL。
