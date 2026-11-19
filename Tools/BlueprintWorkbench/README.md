# 旧入口兼容目录

0.4.0 起，通用脚本、测试与示例随插件位于 `Plugins/BlueprintWorkbench/Scripts/`。

本目录只保留 Invoke-BlueprintWorkbench.ps1 和 Validate-BlueprintWorkbench.ps1 的转发入口，参数交给新版脚本处理，必须显式指定 ProjectFile 与 EngineRoot。没有第二份编辑、清理或测试实现。

完整手册见 [插件说明](../../Plugins/BlueprintWorkbench/README.md)。旧版 Python 导入和示例路径请迁移到插件 Scripts 目录。
