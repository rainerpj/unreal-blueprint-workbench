# Blueprint Workbench v0.3

UE 5.7.4 的小型 C++ Editor 插件，通过 UE Python 或 PowerShell 调用，不需要 Computer Use。插件源文件位于 `Plugins/BlueprintWorkbench`，不进入游戏运行时模块。插件默认启用；新启动的编辑器／commandlet 自动发现。已经打开的编辑器需要正常保存工作并重启后才可调用新类，不要为了命令行检查关闭正在工作的编辑器。

## 范围与所有权

- `InspectBlueprint`：读取 Blueprint 父类、本类成员变量、所有图、节点 GUID／类／标题／位置、引脚 GUID／类型／默认值／连线。变量列表不展开继承成员；引脚完整 UE 类型文本包含 Map 值类型等细节。
- `ApplyPatch`：复制源 Blueprint 后按顺序修改、编译，返回 JSON。源图不做编辑或保存。目标为空时仅在内存预览；指定新的 `/Game/.../BP_Name` 包名才保存副本。禁止覆盖磁盘或当前进程中已有的包。
- `ApplyPatchToSavedAsset`（显式 `-WriteBack`）：仅在 commandlet 内可用；先预检副本，备份原 `.uasset` 到 `Saved/BlueprintWorkbench/Backups/`，再修改、编译、保存原包。仅支持已保存 `/Game` 资产，不支持分离的 bulk 包。修改／编译失败不保存源资产；保存失败会尝试从备份恢复，报告 `backup_restored`，失败后须退出该进程再重试。批量写回不是跨资产原子事务。
- 支持普通 `UBlueprint` 和 `UWidgetBlueprint` 的 K2 图修改。Widget 使用 UE 注册的 Widget 编译器；Designer 控件树、Binding、动画仅复制和检查，不提供编辑操作。Anim Blueprint、接口、宏库、函数库仍不支持修改。
- v0.2 修复复制时 UE 重建节点 GUID 导致旧节点无法定位的问题：利用 UE 复制对象映射解析源节点 GUID，保留引擎生成的副本节点 GUID。成功报告的 `source_nodes` 将 `源图相对路径|源节点GUID` 映射到副本节点 GUID。引脚按名称或当前导出的 GUID 解析；编译可能重建个别动态引脚，下一次操作应重新 Inspect。
- 不自动替换 GameMode、关卡实例或其他资产引用。默认副本操作不覆盖现有角色；生成副本不是完成主工程接入，玩法仍需图审阅与 PIE 验证。
- 上述副本限制适用于 `ApplyPatch`。显式写回会修改原资产，必须先保存工作并关闭交互式编辑器；PowerShell 入口检查 `UnrealEditor` 进程，直接 Python 调用时由调用方保证。后台预览／副本保存无需关闭编辑器。
- 外部 commandlet 读取磁盘上的已保存资产，看不到另一个编辑器窗口的未保存修改。在编辑器 Python 中调用则读取当前进程里的资产。
- 有错误的 patch 不保存目标；已有不相关连线不会被自动断开，也不会自动插入转换节点。显式 `disconnect` 后才能重新接被占用的引脚。编译警告返回报告，不被隐藏；编译错误阻止保存。

## 调用

以下命令在工程根目录执行。默认引擎位置为 `C:/Program Files/Epic Games/UE_5.7`，可用 `-EngineRoot` 改写。安装和构建步骤见仓库根目录 README。所有 PowerShell 调用必须加 `-ProjectFile <目标工程.uproject>`。

```powershell
# 只读导出角色；结果和日志默认放在 Saved/BlueprintWorkbench。
& ./Tools/BlueprintWorkbench/Invoke-BlueprintWorkbench.ps1 `
  -ProjectFile 'D:/Projects/MyGame/MyGame.uproject' `
  -AssetPath '/Game/Examples/BP_Example'

# 预览示例 patch，编译内存副本，不保存任何资产。
& ./Tools/BlueprintWorkbench/Invoke-BlueprintWorkbench.ps1 `
  -ProjectFile 'D:/Projects/MyGame/MyGame.uproject' `
  -AssetPath '/Game/Examples/BP_Example' `
  -PatchFile './Tools/BlueprintWorkbench/example.patch.json'

# 确定要保留一个新副本时，显式给出新包名；该路径必须不存在。
& ./Tools/BlueprintWorkbench/Invoke-BlueprintWorkbench.ps1 `
  -ProjectFile 'D:/Projects/MyGame/MyGame.uproject' `
  -AssetPath '/Game/Examples/BP_Example' `
  -PatchFile './Tools/BlueprintWorkbench/example.patch.json' `
  -DestinationPackage '/Game/Workbench/BP_Example_Copy'
```

在已加载插件的 UE Python 中也可以直接调用：

```python
import json, unreal
description = json.loads(unreal.BlueprintWorkbenchLibrary.inspect_blueprint(asset_path))
preview = json.loads(unreal.BlueprintWorkbenchLibrary.apply_patch(asset_path, json.dumps(patch)))
copy = json.loads(unreal.BlueprintWorkbenchLibrary.apply_patch(asset_path, json.dumps(patch), new_package))
# 仅限已关闭交互式编辑器的 commandlet；返回 backup_file。
written = json.loads(unreal.BlueprintWorkbenchLibrary.apply_patch_to_saved_asset(asset_path, json.dumps(patch)))
```

返回字段：`success` 表示整个请求成功；`compiled` 表示执行过最终编译，是否通过看 `compile_errors` 和 `success`；`saved` 表示实际保存成功。错误包含 `error`，操作失败时包含零起始 `failed_operation`；编译报告包含错误／警告数量及 `compile_messages`；成功结果含 `blueprint` 结构和新增节点 `aliases` 到 GUID 的映射。读操作的 `compiled/saved` 都是 false。

## Patch 格式

根对象只有 `operations` 数组，操作顺序执行。先创建变量／覆盖函数，再创建使用它们的节点。图使用导出的 `path`，也可使用唯一 `name`；重名图必须使用路径。节点使用已导出的 GUID 或本次请求创建的 `id` 别名。别名不能是 GUID，作用域仅为当前请求。

| op | 字段 | 作用 |
| --- | --- | --- |
| `add_variable` | `name`, `type`，必要时 `object` | 添加本类成员；type 支持 bool/int/float/double/string/name/struct/object。struct/object 的 object 是准确的脚本结构体／类对象路径，例如 `/Script/CoreUObject.Guid`。使用类型零默认值，不支持容器或自定义成员初值 |
| `add_override` | `name`；事件另传 `graph` | 按父类真实签名创建覆盖。带返回值时创建同名函数图及 Entry/Result；无返回且可作为事件时放到指定 EventGraph。已有覆盖拒绝 |
| `add_node` | `graph`, `id`, `kind`，可选 `x/y` | kind 为 branch、call、get、set、custom_event、break_struct、enum_to_string、comment |
| `set_default` | `graph`, `node`, `pin` 或 `pin_id`, `value` | 设置未连接的数据输入引脚，value 必须是字符串。布尔值严格使用 `true` / `false`；其余支持 int/real/string/name/struct/byte，按 UE 验证。暂不支持对象／本地化文本默认值 |
| `connect` / `disconnect` | `graph`, `from`, `to` | from 为输出、to 为输入，各含 node 与 pin 或 pin_id；只操作这条连接 |
| `move_node` | `graph`, `node`, `x`, `y` | 只移动节点，不改引脚／默认值／连接；坐标须为有限数值 |
| `remove_unused_nodes` | `graph`, `nodes` | 显式候选集合的保守删除；禁止事件／入口、潜伏调用、宏、时间轴和未知节点；集合不能有外部执行／委托输入或外部消费者，允许来自共享数据生产者的数据输入 |

`add_node` 补充字段：

- `call`：`function` 为反射函数名，`owner` 可选。省略 owner 查副本自己的类（包括继承函数）；指定时用类对象路径，如 `/Script/Engine.KismetSystemLibrary`。只支持普通函数调用，专用节点、ArrayParm、自定义结构参数、内部函数不在首版范围。
- `get` / `set`：`variable` 为可见成员名；省略 `owner` 使用本对象，提供类对象路径则生成外部属性访问并显式连接 `self`。不能写 BlueprintReadOnly 属性。
- `break_struct`：`struct` 为 Blueprint 可用结构体路径，使用目标工程实际反射路径（`/Script/Module.StructName`）；具有原生 Break 函数的结构体须使用对应函数，不走此通用节点。
- `enum_to_string`：生成 Enum to String 节点；连接枚举到 `Enumerator`，从 `ReturnValue` 取得字符串。只在枚举连接完成后编译。
- `custom_event`：`name`；创建无自定义参数的事件，要求 EventGraph。
- `branch`：无额外字段。
- `comment`：`text` 为块标题，`details` 为说明，`width/height` 为包围尺寸；可传 `members` 节点 ID 数组。创建可整体拖动的块状注释。Inspect 同时导出尺寸、注释／Details、Pure／Latent 属性，供排版和清理判断使用。

节点创建后可用结果中的 GUID 再发第二次 patch；第二次必须以已经保存的副本为 source，或在同一 patch 中使用别名。预览副本及其新节点不会在下一请求中保留。新增覆盖图里的 Entry/Result 可先在预览结果确认其引脚名，再在保存的副本上按 GUID 继续操作。

`example.patch.json` 仅增加一个独立示例事件及 Bool 赋值，不接入任何现有玩法入口。目前未提供引脚拆分、数组／Map、委托绑定、组件编辑、UMG Designer 或任意节点强制删除。`ApplyPatch` 仍拒绝覆盖已有包；原资产写回必须使用独立的显式入口。

## 排版、块状注释与无用节点清理

`WorkbenchPresentation.py` 接受明确的入口分组和本次改接留下的旧链候选，自动生成只含移动、块状注释及保守删除的补丁。执行／数据依赖从左到右，Pure 节点尽量靠近消费者；每组在原入口附近寻找避开其他节点／注释的空位，必要时连入口一起移动。节点尺寸采用导出尺寸或保守估计，不是 Slate 实测尺寸；共享节点保留原位，闭环图拒绝自动排版并要求单独审阅。不会为排版修改连线、插入 Reroute 或折叠函数。

通用调用：`Invoke-BlueprintWorkbench.ps1 -AssetPath <资产> -PresentationFile <分组.json>` 默认预览；加 `-WriteBack` 才写回并执行独立重载／清理流程。与 `-PatchFile` 互斥。分组文件结构：

```json
{"graph":"EventGraph","groups":[{"entry":"入口节点GUID","nodes":["入口节点GUID","本组节点GUID"],"title":"开始训练","details":"检查会话后提交开局请求；拒绝时显示原因"}],"candidates":["本次改接后旧链的候选GUID"]}
```

标题／说明由调用方根据入口职责生成，普通布局器不猜测任意业务语义。保存前核对全部保留节点的引脚类型、默认值、连接，以及控件、Binding、动画；注释和坐标变化不计作逻辑变化。删除只处理显式候选中未被执行链或数据消费者使用的普通节点；未知／潜伏／事件节点和共享节点保留，不对全工程做“无连线就删”。

## 完成后的自动清理

接线流程固定为：保存临时副本并登记 → 原资产写回（保留备份）→ 独立进程重载、核对图与编译 → 查询副本引用 → 删除本次匹配的临时副本，保留原资产备份、补丁、验证和清理报告。

- 中间副本通过 `Invoke-BlueprintWorkbench.ps1 -PatchFile ... -DestinationPackage /Game/Workbench/... -TemporaryCopy` 创建；普通交付副本不加该标志，不会自动删除。登记记录逐份保存于 `Saved/BlueprintWorkbench/TemporaryCopies/`，不会因下一批覆盖报告而丢失。
- `Invoke-BlueprintWorkbench.ps1 -WriteBack` 成功后自动启动第二个 UE 进程执行 `FinalizeWorkbench.py`；结果在原报告路径后加 `.finalize.json`。只匹配同一原资产、同一补丁的已登记临时副本，不扫描目录猜测、不删除未登记资产。
- 原资产保存失败、重载图不一致、编译失败或备份不存在时，不进入清理。副本仍被其他资产引用时保留并报告；清理可重试，已删除项不会再次删除。清理不删除备份或验证报告，也不自动清空目录。
- 批量接线脚本复用 `WorkbenchLifecycle.register_copy` 与 `finalize`。直接调用底层 C++ API 只执行单项编辑／保存，不自动结束整批任务；脚本必须在独立进程核对后调用 finalize，不能把副本当作核对通过的原资产。
