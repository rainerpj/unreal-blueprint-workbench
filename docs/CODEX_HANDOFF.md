# Blueprint Workbench 维护交接

更新：2026-11-25。维护仓库为 rainerpj/unreal-blueprint-workbench；当前版本为0.5.0，新增功能及UE5.5／5.7预编译分发的验证范围见兼容矩阵；旧0.4.1附件不包含本轮新增能力。

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
