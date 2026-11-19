# JSON 补丁与能力边界

根对象只接受 `operations` 数组，按顺序执行。图用 Inspect 输出的 `path` 或唯一 `name`；同名图须使用路径。节点用已导出的 GUID 或本次请求创建的非 GUID 别名。引脚用 `pin` 名或 `pin_id`。别名仅在本请求有效，预览副本不在下一进程保留。

## 核心操作

| op | 字段与行为 |
|---|---|
| add_variable | name、type；type 为 bool/int/float/double/string/name/struct/object。struct/object 的 object 为准确反射路径，例如 /Script/CoreUObject.Guid。成员初值为类型默认值；不接受自定义 default。可选 editable。只额外支持 container=map 且 value_object 为结构体路径，其他容器拒绝 |
| add_override | name；事件另传 graph。按父类真实签名创建覆盖，带返回值创建函数图和 Entry/Result；已有覆盖拒绝 |
| add_node | graph、id、kind，位置 x/y 可选；节点种类见下表 |
| set_default | graph、node、pin 或 pin_id、value。value 必须为字符串；只支持未连线数据输入，bool 严格 true/false；其余支持 int/real/string/name/struct/byte。对象与本地化文本默认值暂不支持 |
| connect / disconnect | graph、from、to；两端含 node 与 pin 或 pin_id。from 为输出、to 为输入；只操作该连线，不自动断开其他线、插入转换节点 |
| move_node | graph、node、x、y；有限坐标，绝对值不大于 1,000,000，不改逻辑 |
| remove_unused_nodes | graph、nodes；仅显式候选的保守删除。禁止入口／事件、潜伏节点、宏、时间轴与未知类型；不能跨删除集合边界切断消费者或外部执行／委托输入 |

| add_node kind | 补充字段与边界 |
|---|---|
| branch | 无额外字段 |
| call | function；owner 为可选类对象路径，缺省使用本蓝图类。普通 BlueprintCallable/Pure；自定义参数／内部专用函数需专门节点 |
| get / set | variable；owner 缺省为本对象，外部属性访问需准确类路径及显式 self 连线。不能写 BlueprintReadOnly 属性 |
| custom_event | name；仅 EventGraph，不带自定义参数 |
| break_struct | struct 为 Blueprint 可用结构体路径；具有 HasNativeBreak 的结构体必须调用对应原生函数 |
| enum_to_string | 连枚举到 Enumerator，从 ReturnValue 取字符串；枚举引脚未定型时不编译 |
| comment | text、details、width、height，可选 members 节点 ID 列表。尺寸范围 100–100000。创建 GroupMovement 块注释，不包含 Slate 实测布局 |

## 原有通用扩展入口

提取源码还保留以下已有通用操作；它们不等于支持任意引擎专用节点。兼容矩阵的测试数量不表示下表每种组合、类型和生命周期都已验收，复杂委托／容器图须先预览并核对引脚。

| 操作或节点 | 格式／限制 |
|---|---|
| signature | op、name、parameters。创建或扩展函数；每项 name、type、direction，object/struct 另有 object 路径；type 目前为 object/struct/bool/float，direction 为 input/output/local |
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

不包含 Parkour hud_buttons、StageB 批处理或游戏类型。Widget Designer 控件树、Binding、动画可检查／复制，不提供通用编辑。Material、Niagara、Anim Blueprint、接口／宏库／函数库的图修改暂不支持；不支持引脚拆分或任意节点强制删除。

工具只保证它报告的编辑／编译／保存结果。玩法语义、视觉布局、PIE、设备和正式项目迁移需另外验证。
