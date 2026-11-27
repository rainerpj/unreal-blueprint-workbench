# JSON 补丁与能力边界

根对象只接受 `operations` 数组，按顺序执行。图用 Inspect 输出的 `path` 或唯一 `name`；同名图须使用路径。节点用已导出的 GUID 或本次请求创建的非 GUID 别名。引脚用 `pin` 名或 `pin_id`。别名仅在本请求有效，预览副本不在下一进程保留。

## 核心操作

| op | 字段与行为 |
|---|---|
| add_variable | name、type；结构／对象类型的 object 为准确反射路径，例如 /Script/CoreUObject.Guid。可选字符串 default、editable；完整类型和容器规则见下文“统一类型”。成员不接受 reference/const |
| add_override | name；事件另传 graph。按父类真实签名创建覆盖，带返回值创建函数图和 Entry/Result；已有覆盖拒绝 |
| add_node | graph、id、kind，位置 x/y 可选；节点种类见下表 |
| set_default | graph、node、pin 或 pin_id、value。value 必须为字符串；只支持未连线数据输入，bool 严格 true/false；其余支持 int/int64/real/string/name/struct/byte、object/class/interface、soft_object/soft_class 和 text。硬引用值为真实对象／类路径或 None；text 为显示文本（不提供指定本地化 namespace/key） |
| set_local_default | graph 为函数图、variable 为局部变量名、value 为字符串。修改已有局部变量初值；采用统一类型及 UE 属性文本导入校验，支持容器和引用类型；bool 严格 true/false |
| set_call_in_editor | graph、value（JSON boolean）；函数图可省略 node 自动选 Entry；自定义事件必须指定 node（GUID 或本次别名）。支持启用和取消，其他节点拒绝；不自动调用函数 |
| connect / disconnect | graph、from、to；两端含 node 与 pin 或 pin_id。from 为输出、to 为输入；只操作该连线，不自动断开其他线、插入转换节点 |
| move_node | graph、node、x、y；有限坐标，绝对值不大于 1,000,000，不改逻辑 |
| remove_unused_nodes | graph、nodes；仅显式候选的保守删除。禁止入口／事件、潜伏节点、宏、时间轴与未知类型；不能跨删除集合边界切断消费者或外部执行／委托输入 |

清理白名单还覆盖 Self、MakeStruct、Select、EnumEquality、SwitchInteger、CallDelegate、CreateDelegate、SpawnActorFromClass、CreateWidget，以及已支持函数调用节点的数学／数组／父函数／材质参数集合派生类型。仍须满足不可达及无外部消费者边界；多播委托输入同样受保护，不会因节点看似断开就删除事件或宏。

| add_node kind | 补充字段与边界 |
|---|---|
| make_array | 可选 num_inputs，默认 1，整数范围 0–1024；生成原生 Make Array，输入 [0]、[1]…，输出 Array。必须在同次补丁连接有类型的输入或数组消费者完成定型，再 set_default；未定型不能视为可用的数组 |
| branch | 无额外字段 |
| call | function；owner 为可选类对象路径，缺省使用本蓝图类。普通 BlueprintCallable/Pure；自定义参数／内部专用函数需专门节点 |
| get / set | variable；owner 缺省为本对象，外部属性访问需准确类路径及显式 self 连线。不能写 BlueprintReadOnly 属性 |
| custom_event | name；仅 EventGraph；可选 parameters，参数为统一类型且仅 input，不带 default |
| break_struct | struct 为 Blueprint 可用结构体路径；具有 HasNativeBreak 的结构体必须调用对应原生函数 |
| enum_to_string | 连枚举到 Enumerator，从 ReturnValue 取字符串；枚举引脚未定型时不编译 |
| comment | text、details、width、height，可选 members 节点 ID 列表。尺寸范围 100–100000。创建 GroupMovement 块注释，不包含 Slate 实测布局 |

## 原有通用扩展入口

提取源码还保留以下已有通用操作；它们不等于支持任意引擎专用节点。兼容矩阵的测试数量不表示下表每种组合、类型和生命周期都已验收，复杂委托／容器图须先预览并核对引脚。

| 操作或节点 | 格式／限制 |
|---|---|
| signature | op、name、parameters。创建或扩展函数；每项 name、type、direction，object/struct 另有 object 路径；type 使用统一类型，direction 为 input/output/local；input/local 可带字符串 default；input 默认值还须通过原生 K2 引脚校验（拒绝时整个补丁失败，不静默忽略）；output 默认值通过 set_default 设置返回节点输入。可选 entry_id/result_id 为本请求的节点别名，允许在同一补丁创建并接线。Dispatcher 签名仅 input 且无 default；接口禁止 local |
| add_component | op、name、class；普通蓝图 SCS 组件。保留引擎组件类默认配置，项目碰撞／UI配置由项目负责 |
| refresh_calls | op、functions 非空函数名列表；仅重构显式列出的函数调用节点，须审阅其动态引脚变化 |
| add_node self / cast | cast 的 owner 为目标类路径；对象来源与实际静态类型仍需核对 |
| add_node make_struct | struct 路径 |
| add_node return | 在已有函数返回签名下增加 Result 节点 |
| add_node local_get / local_set | variable，graph 为局部变量所属函数图 |
| add_node delegate | function；创建 Create Delegate，仍需连接正确的对象／委托上下文 |
| add_node bind / unbind | owner 为类路径、name 为反射多播委托属性 |
| add_node component_event | variable 为蓝图组件属性，name 为组件多播委托属性 |
| add_node foreach | 引擎 StandardMacros 的 ForEachLoop，需实际数组连线完成定型 |
| add_node container_call | owner、function；允许容器函数的特殊元数据，ArrayParm 使用专用数组调用节点。不能保证所有 CustomThunk 函数正确，应逐项核对 |

## 注释跨版本适配

有原生 `NodeDetails` 字段时直接使用其 FText。UE 5.5 没有此字段，说明存到注释节点对应的包元数据，在 Inspect JSON 中读取；保存、写回、独立重载与再次复制均须保留。5.5 原生 UI 没有 5.7 的 Details 字段，不能将元数据持久化宣称为相同的原生编辑器显示效果。注释类仍是引擎原生类，不增加自定义节点资产依赖。

## 不提供的能力

不包含 定制宿主 hud_buttons、StageB 批处理或游戏类型。Widget Designer 仅提供下述控件添加与属性编辑，不提供 Binding／动画的通用编辑。Material、Niagara、Anim Blueprint、宏库的图修改暂不支持；不支持任意节点强制删除。函数库支持函数图；接口仅支持签名、参数和函数元数据，不允许实现逻辑。

## 2026-11-27 本地扩展（尚未发布）

仅在 UE 5.5.4 项目构建及实际资产上验证；不属于已有 0.5.0 二进制包，未验证 UE 5.7。

- `CreateBlueprint` 新增 `kind="widget"`，父类默认 UserWidget。
- `CreateSchemaAsset(kind, schema_json, destination_package="")` 创建新 enum／struct；空目标只在临时包预览，有目标只允许不存在的 `/Game/` 包，不改已有枚举／结构。enum 使用 `{"values":["Item","Document"]}`；struct 使用 `{"fields":[{"name":"Id","type":"name"},{"name":"Number","type":"int","default":"1"}]}`，类型沿用统一类型，可带 tooltip。返回 `members` 的 name 是实际内部字段／枚举名，display 是显示名。调用方先预览，再指定目标保存，保存后独立进程加载验证。
- `add_widget`：name、class、可选 parent；无 parent 建根，已有根拒绝；parent 必须是可容纳子项的 PanelWidget。重复名称、抽象类、不存在的父控件、占用的单子项容器拒绝。新增控件标记为变量。
- `set_widget_property`：name、property、字符串 value、可选 bool slot；只允许反射中的可编辑非 EditConst 属性，以 UE 属性文本导入。slot=true 编辑所属 Slot。
- `variable_metadata`：name、可选 tooltip、expose_on_spawn、private。后两者为 bool；private 使用 UE 的 BlueprintPrivate 元数据。
- `set_delegate_function`：graph、node、function；在 CreateDelegate 对象和委托上下文连好后设置函数，避免连接时引擎清空此前选择。
- `add_node` 新增 `create_widget`（owner 为具体 Widget 类）、`get_subsystem`（owner 为 GameInstanceSubsystem 子类）、`format_text`（text 字符串）、`parent_call`（function 为父函数名）。`return` 也允许在 void 函数中创建早退节点，不允许事件图。

### 保持 Self 类身份的隔离预览

普通复制预览中，Self 属于复制类；当外部 Widget 的 ExposeOnSpawn 属性精确引用原组件类时，合法的 Self 连线也可能被复制类身份拒绝。此时才使用 `ApplyPatchInIsolatedCommandlet(asset, patch, bWriteBack=false)`：仅 commandlet，直接修改已加载源对象的**内存**，不保存磁盘。必须退出该进程后，在新 commandlet 中对同一源和补丁调用 bWriteBack=true；该写回先备份、编辑、编译成功后保存，再另启进程重载检查。不要在同一进程先预览后重复应用，也不要让外部编辑器同时加载目标资产。写回入口不自动运行另一次预览；编排方负责保存并检查先前预览报告与源未被其他写入者修改的条件。失败进程不得再保存源。普通 ApplyPatch／ApplyPatchToSavedAsset 合同不变。

工具只保证它报告的编辑／编译／保存结果。玩法语义、视觉布局、PIE、设备和正式项目迁移需另外验证。


## 受限读取副本

`add_node` 的 `kind=local_read` 使用同图 `source_node`，另需唯一 `id`，可指定 `x/y`。只复制引擎判定可就近读取的直接成员 getter／Self；所有连接清空，后续显式 connect/disconnect。Inspect 的 `localizable_read` 是规划提示，C++ 执行时会再次校验。不等价于任意节点克隆，不能用于执行节点或任意纯函数。

## 局部默认值、编辑器调用与数组示例

```json
{"operations":[
  {"op":"signature","name":"EditorAction","parameters":[
    {"name":"Enabled","type":"bool","direction":"local","default":"false"}
  ]},
  {"op":"set_local_default","graph":"EditorAction","variable":"Enabled","value":"true"},
  {"op":"set_call_in_editor","graph":"EditorAction","value":true},
  {"op":"add_node","graph":"EditorAction","kind":"local_get","variable":"Enabled","id":"enabled"},
  {"op":"add_node","graph":"EditorAction","kind":"make_array","id":"flags","num_inputs":2},
  {"op":"connect","graph":"EditorAction","from":{"node":"enabled","pin":"Enabled"},"to":{"node":"flags","pin":"[0]"}},
  {"op":"set_default","graph":"EditorAction","node":"flags","pin":"[1]","value":"false"}
]}
```

Inspect 的 FunctionEntry 节点现在包含 `local_variables`（name/type/default）及 `call_in_editor`；CustomEvent 节点包含 `call_in_editor`。这些字段可用于预览与独立重载核对。局部默认值为 UE 字符串格式。Call in Editor 与引擎复选框写入相同数据；Details 面板是否显示可点击按钮仍受引擎的参数签名、实例上下文等规则限制。

## 统一类型（2026-11-25 扩展）

成员、函数参数、局部变量、事件／Dispatcher 参数共用类型描述：

- `type`: bool / byte / int / int64 / float / double / string / name / text / enum / struct / object / class / soft_object / soft_class / interface。
- enum、struct 和所有对象／类／接口类型必须提供准确的 `object` 反射路径；interface 必须是接口类。
- 可选 `container`: array / set / map。map 的键由外层 type/object 描述，值由 `value_type` 对象描述，例如 `{"type":"name","container":"map","value_type":{"type":"int"}}`。旧 `value_object` 结构体写法仍兼容。
- 禁止嵌套容器；Set 元素／Map 键必须满足 UE 可哈希类型规则。不是任何组合都能成为 K2 参数，最终由对应引擎编译器验证。
- 函数／事件参数可带 JSON boolean `reference`、`const`；成员和局部变量不能使用引用／const 参数标记。
- 初值使用字符串；标量例如 `"7"`、`"true"`，数组／Set 例如 `"(1,2,3)"`，Map 例如 `"((A,1),(B,2))"`，Vector 属性例如 `"(X=1,Y=2,Z=3)"`。string/text 用普通文本；硬对象引用必须解析成功，软引用允许尚未加载的资源路径。属性文本与个别 K2 引脚的格式不同，始终以 Inspect 和引擎类型为准。

## 新建资产与引用检查

C++／UE Python 暴露 `CreateBlueprint(kind, parent_class, patch_json, destination_package="")`。kind 为 `normal`、`function_library`、`interface`；只有 normal 使用 parent_class，另两种采用引擎规定的父类。空 destination 只预览；非空只允许新的 /Game 包，不覆盖既有资产。所有新建对象仍使用原有补丁→编译→保存流程。

CLI：

```powershell
# 一次创建函数库并应用补丁；保存后自动启动新进程核对和重新编译。
& Invoke-BlueprintWorkbench.ps1 -ProjectFile 'D:/Game/Game.uproject' -EngineRoot 'C:/Program Files/Epic Games/UE_5.5' -CreateKind function_library -PatchFile 'D:/patch.json' -DestinationPackage '/Game/Tools/BFL_Utility'
# 接口预览：省略 DestinationPackage；普通蓝图另传 -ParentClass '/Script/Engine.Actor'。
& Invoke-BlueprintWorkbench.ps1 -ProjectFile 'D:/Game/Game.uproject' -EngineRoot 'C:/Program Files/Epic Games/UE_5.5' -CreateKind interface -PatchFile 'D:/interface.json'
# 修改公共函数前，查已保存包引用及确认的 K2 调用点。
& Invoke-BlueprintWorkbench.ps1 -ProjectFile 'D:/Game/Game.uproject' -EngineRoot 'C:/Program Files/Epic Games/UE_5.5' -AssetPath '/Game/Tools/BFL_Utility' -MemberName 'Compute'
```

引用 API 为 `FindMemberReferences(asset_path, member_name)`；返回 candidate_packages 与可确认的 function/variable/delegate/event 图引用。基于资产注册表，不覆盖其他编辑器未保存内容、软名称字符串、C++ 或非 Blueprint 资产。候选包不等于确认调用，不自动写回任何调用方。

## 函数、接口与默认值操作

| op | 字段及行为 |
|---|---|
| function_properties | graph；可选 pure(bool)、access(public/protected/private)、category、tooltip。接口 access 仅 public；改变 Pure 后须检查调用点执行引脚 |
| set_member_default | variable、value 字符串。设置生成类 CDO，包括允许编辑的继承属性；先要求当前图可编译。使用完整属性名，不支持点分路径 |
| implement_interface | interface 为接口生成类路径，例如 /Game/BPI_Test.BPI_Test_C；仅普通蓝图，重复实现拒绝。带返回值的方法由引擎生成实现函数图 |
| refresh_interfaces | 显式让当前普通蓝图的所有接口实现与定义重新对齐；不自动保存其他资产 |
| add_dispatcher | name、可选 parameters；创建真实多播委托成员及签名图。之后可用 signature 增加输入、edit_parameter 编辑参数 |
| edit_parameter | graph、name、direction(input/output)、action(add/rename/remove/type/default)。自定义事件另需 node。rename 用 new_name；type/add 用统一类型字段；default 用 value。删除／改类型前必须断开该签名引脚并合并子引脚。事件和 Dispatcher 不支持 default |
| rename_function | name、new_name；仅当前蓝图拥有的函数／Dispatcher 图 |
| rename_variable | name、new_name；默认成员，局部另传 graph。继承成员不允许重命名 |
| refresh_calls | functions 非空名称数组，可选 owner 类路径进一步限定；重构当前资产中匹配的函数／接口消息／Dispatcher 调用节点 |

跨资产修改顺序：先 FindMemberReferences／Inspect → 预览定义修改 → 显式保存定义 → 对受影响实现资产执行 refresh_interfaces、对调用资产执行 refresh_calls 或明确替换调用节点 → 分别编译／保存／独立重载。重命名、删除或改类型可能需要明确的 pin_map 或重新接线；不静默丢线，不承诺跨多个资产原子提交。引用查询不是全工程语义证明。

## 补充节点与引脚编辑

| add_node kind | 字段及行为 |
|---|---|
| interface_event | owner 为已实现接口类、function；仅无返回值的 EventGraph 实现，重复入口拒绝 |
| interface_message | owner 为接口类、function；创建原生 Message 节点，显式连接对象到 self |
| dispatcher_call | name，owner 可省略使用自身类；已有 bind/unbind/delegate 节点仍可配合使用 |
| sequence / select | 原生节点；Select 通过数据和 Index 连线定型 |
| switch_enum | enum 为 UEnum 路径 |
| for_loop / for_loop_break / foreach_break | 对应引擎 StandardMacros；端口名称以 Inspect 为准，包含大小写差异 |
| set_members_struct | struct 路径；可选 fields 为显示的字段名列表。仅支持原生 `UK2Node_MakeStruct::CanBeMade` 接受的结构体；Vector／Rotator 等带 HasNativeMake 的结构体须调用原生 Make 函数并赋值，不支持 Set Members。StructRef 要连接可写结构体变量，按需连接执行线 |

`edit_pins` 使用 graph、node、action：

- add：Make Array／Sequence／可增加选项的 Select 增加一个动态引脚。
- remove：另传 pin；必须未连接、未拆分。Sequence 保留引擎最小出口数；Select 只删除最后一个可删选项。
- split：另传 pin，必须是引擎允许拆分且未连接的结构体引脚。
- recombine：另传父／子 pin；子引脚须先断开，嵌套拆分须从内向外合并。调用引擎原生默认值合并行为。

`replace_node` 使用 graph、node、replacement（add_node 的 kind/id 及补充字段）、可选 pin_map（旧名→新名）。旧节点的每个引脚必须映射到同方向、完全相同类型的唯一新引脚，已有连接和默认值随映射迁移；入口、事件、返回节点和宏不得替换，拆分引脚必须先合并。映射不成立则补丁失败且不保存。外部调用改名可用此入口明确换成新函数调用，不会自动猜测参数对应关系。

## 一次补丁创建并接线的函数库示例

将下面补丁传入 `CreateKind function_library`。即使函数标为 Pure，函数内部仍需要入口到返回节点的执行连接。

```json
{"operations":[
  {"op":"signature","name":"Answer","entry_id":"entry","result_id":"result","parameters":[{"name":"Result","type":"int","direction":"output"}]},
  {"op":"function_properties","graph":"Answer","pure":true,"category":"Utilities","tooltip":"Returns a configured integer"},
  {"op":"connect","graph":"Answer","from":{"node":"entry","pin":"then"},"to":{"node":"result","pin":"execute"}},
  {"op":"set_default","graph":"Answer","node":"result","pin":"Result","value":"42"}
]}
```

Inspect 同时输出 blueprint_type、interfaces、函数 flags/category/tooltip、事件声明身份及局部变量；这些元数据纳入排版语义核对。函数库 World Context 由引擎生成，不能把它当普通 Actor 实例函数处理。测试范围与未验证组合见 COMPATIBILITY.md。
