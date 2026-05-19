# 节点级信息接口设计

## 目标

`graph_snapshot_get` 负责全图导航，`graph_node_info_get` 负责整图复刻包，`node_info_get` 负责单节点编辑视图。AI 不再从全图 JSON 里手动拼节点、pin、参数和连线，而是按用途拿到可读、可写、低上下文占用的信息。

这套接口用于材质和蓝图图编辑，重点支持：

- 读指定节点的基础信息、输入、输出、参数、位置。
- 按 section 和 index 精确读取单个输入、输出或参数。
- 用短别名给 AI 阅读，用真实 ID 给写接口定位。
- 所有未连接 pin 显式返回 `None`。
- 连线方向使用 `<` 和 `>`，参数行保留 `-` 前缀，方便视觉区分。
- 读取接口不触发编译、保存或资产修改。
- 整图默认不返回节点位置和真实 GUID；需要位置时显式调用带位置接口，需要真实 ID 时显式开启。

## 整图复刻包接口

```text
graph_node_info_get(asset_path, graph_kind?, graph_name?, format?, id_mode?, max_nodes?)
graph_node_info_get_w_pos(asset_path, graph_kind?, graph_name?, format?, id_mode?, max_nodes?)
```

`graph_node_info_get` 默认返回高密度字典索引文本，不返回位置。`graph_node_info_get_w_pos` 返回同一份数据，并额外附加位置表。

参数约定：

| 参数 | 默认值 | 说明 |
|:-----|:-----|:-----|
| `format` | `indexed` | `indexed` 为整图复刻包；`grouped` 为按类型聚合的人读压缩包；`text` 保留旧节点块文本 |
| `id_mode` | `alias` | `alias` 不返回真实 GUID；`real` 返回真实 ID 表；`both` 返回别名和真实 ID |
| `max_nodes` | 空 | 限制返回节点数量，用于抽样 |

`indexed` 文本格式按字典拆分，避免重复字段名：

```text
G:/Game/YN/Material/大坝母材质.大坝母材质|material|MaterialGraph|85
T:0=ScalarParameter;1=Multiply;2=Clamp;3=TextureSampleParameter2D
P:0=DefaultValue;1=ParameterName;2=SortPriority;3=Texture;4=SamplerType
N:0:0:红色范围;1:1:Multiply_00;2:2:Clamp_00;3:3:SP遮罩
V:0:0=1.000000;1=红色范围;2=32|3:1=SP遮罩;3=/Game/YN/Texture/透贴.透贴;4=SAMPLERTYPE_Masks
E:3.R>1.A;0.o>1.B;1.o>2.Input
```

更激进的压缩可以追加 `format=grouped`，按节点类型分组节点表，进一步减少类型索引重复。但默认仍使用 `indexed`，因为当前 `E` 段已经使用数字节点索引，连线不会重复 `Clamp_01`、`Multiply_00` 这类长别名；按类型分组的主要收益在 `N` 段，复杂度高于收益，适合作为可选极限格式。

`grouped` 使用输入视角，只返回 `<`，不返回对应的 `>` 反向边，避免同一条连线在整图里出现两次。示例：

```text
G:/Game/YN/Material/大坝母材质.大坝母材质|material|MaterialGraph|85
ScalarParameter:红色范围{p[-ParameterName=红色范围|-DefaultValue=1.000000|-SortPriority=32];i[none]}
Multiply:Multiply_00{p[none];i[A<红色范围.Value|B<TextureSample_00.R]}
Clamp:Clamp_00{p[-MinDefault=0.000000|-MaxDefault=1.000000];i[Input<Multiply_00.Result|Min<None|Max<None]}
```

如果要看某个节点的下游输出，用 `node_info_get(node_id, section="output")`。整图复刻时只需要输入视角即可完整还原连接。

字段含义：

| 段 | 含义 |
|:---|:-----|
| `G` | 图头：资产、图类型、图名、总节点数 |
| `T` | 节点类型字典 |
| `P` | 参数字段名字典 |
| `N` | 节点表：`节点索引:类型索引:显示名` |
| `V` | 参数值表：`节点索引:参数索引=值;参数索引=值` |
| `E` | 连线表：`源节点.输出Pin>目标节点.输入Pin` |
| `X` | 位置表，仅 `graph_node_info_get_w_pos` 返回 |
| `R` | 真实 ID 表，仅 `id_mode=real/both` 返回 |

带 `ParameterName`、`Name`、`Group` 等语义名的参数节点必须保留字段值。字段名可以字典化，但字段值不能丢，因为这些值决定材质实例里显示的参数名称。

整图复刻包只放“这个节点实例当前实际是什么”，不重复解释“这个节点类型理论上有哪些属性”。例如 `ParameterName=红色范围`、`Group=水体`、`DefaultValue=1.0` 属于实例值，必须进入 `V` 段；属性 C++ 类型、是否可编辑、创建时可传字段属于类型模板，交给 `node_class_params_get` 查询。这样能同时满足复刻准确性和上下文压缩。

带位置版本追加：

```text
X:0=-1632,576;1=-1440,560;2=-1296,448;3=-1840,720
```

真实 ID 默认不返回；需要写回已有图时再开启：

```text
R:0=027ED24D-4C03-E1A4-2995-AC8106469909;1=89BDAB16-4723-F692-50E3-5EAD4E9E1562
```

## 类型属性模板接口

```text
node_class_params_get(graph_kind, node_class)
```

用于按节点类型查询可编辑属性模板，不要求图里已经存在该节点。它返回字段名、C++ 类型、是否可编辑，用来让 AI 知道某类节点创建/写参时可以传哪些属性。

示例：

```text
Node.Class = ScalarParameter
param_00.DefaultValue : float
param_01.ParameterName : FName
param_02.Group : FName
param_03.SortPriority : int32
```

整图复刻包依然返回每个节点实际参数值；模板接口用于减少反复查询和解释节点类型，不替代实例值。

使用策略：

- 第一次遇到某类节点时，调用 `node_class_params_get` 了解可写字段。
- 批量复刻时，从 `graph_node_info_get(format="indexed")` 的 `V` 段读取实例值。
- 不在整图输出里重复字段类型说明，避免大材质图上下文膨胀。
- `ParameterName`、`Name`、`Group` 等有显示语义的实例字段不能因为模板接口存在而省略。

## 核心读接口

```text
node_info_get(asset_path, node_id, section?, index?, graph_kind?, graph_name?, format?)
```

参数约定：

| 参数 | 默认值 | 说明 |
|:-----|:-----|:-----|
| `asset_path` | 必填 | UE 资产路径 |
| `node_id` | 必填 | 节点短别名或真实 ID |
| `section` | `all` | `all`、`brief`、`input`、`output`、`param`、`links` |
| `index` | 空 | 非空时只返回对应 section 的单条记录 |
| `graph_kind` | `auto` | `material`、`blueprint`、`auto` |
| `graph_name` | 空 | 蓝图多图时指定图名 |
| `format` | `text` | `text`、`compact_json` |

## 文本返回格式

默认返回一整个节点的信息。单节点视图保留位置，整图默认视图不返回位置：

```text
Node.Name = Clamp_00
Node.Class = Clamp
Node.Id = n18
Node.RealId = 1F8E2A0D4F6B4B0B9B2E1A6D3E55A111
Node.Pos = -1296,448

inpin_00.Input < Multiply_00.outpin_00.Result
inpin_01.Min < None
inpin_02.Max < None

-nodeparam_00.MinDefault = 0.0
-nodeparam_01.MaxDefault = 1.0
-nodeparam_02.ClampMode = CMODE_Clamp

outpin_00.Result > BlendMaterialAttributes_00.inpin_02.Alpha
```

参数行保留 `-` 前缀，用于和 pin 行、节点元信息行快速区分。

无输入或无输出时返回：

```text
none_inpin
outpin_00.Result > None
```

参数对象示例：

```text
Node.Name = 红色范围
Node.Class = ScalarParameter
Node.Id = n04
Node.RealId = 2B7E7BE748F34B5A9F90A50F25B5B1C1
Node.Pos = -1600,320

none_inpin

-nodeparam_00.ParameterName = 红色范围
-nodeparam_01.DefaultValue = 1.0
-nodeparam_02.SortPriority = 0

outpin_00.Value > Multiply_00.inpin_01.A
```

## Section 读取

`section` 用于减少返回体：

```text
node_info_get("/Game/YN/Material/大坝母材质", "Clamp_00", "brief")
```

返回：

```text
Node.Name = Clamp_00
Node.Class = Clamp
Node.Id = n18
Node.RealId = 1F8E2A0D4F6B4B0B9B2E1A6D3E55A111
Node.Pos = -1296,448
```

```text
node_info_get("/Game/YN/Material/大坝母材质", "Clamp_00", "input", 0)
```

返回：

```text
inpin_00.Input < Multiply_00.outpin_00.Result
```

```text
node_info_get("/Game/YN/Material/大坝母材质", "Clamp_00", "param", 1)
```

返回：

```text
-nodeparam_01.MaxDefault = 1.0
```

## 紧凑 JSON 返回

文本用于 AI 直接阅读，`compact_json` 用于程序化处理：

```json
{
  "name": "Clamp_00",
  "class": "Clamp",
  "id": "n18",
  "real_id": "1F8E2A0D4F6B4B0B9B2E1A6D3E55A111",
  "pos": [-1296, 448],
  "input": [
    ["Input", "Multiply_00", "Result"],
    ["Min", null],
    ["Max", null]
  ],
  "param": [
    ["MinDefault", "0.0"],
    ["MaxDefault", "1.0"],
    ["ClampMode", "CMODE_Clamp"]
  ],
  "output": [
    ["Result", "BlendMaterialAttributes_00", "Alpha"]
  ]
}
```

JSON 内不放方向符号，字段名已经表达方向。

## ID 策略

接口同时返回两类 ID：

| 类型 | 用途 |
|:-----|:-----|
| 短别名 | 给 AI 和人阅读，例如 `Clamp_00`、`Multiply_01`、`红色范围` |
| 真实 ID | 给写接口精确定位，例如材质表达式 GUID、蓝图节点 GUID |

短别名必须在单次图快照内稳定，并能被后续节点级接口解析。真实 ID 必须优先用于最终写入，避免同名节点误伤。

## 写接口方向

节点级写入按同一套语义设计，避免 AI 自己拼底层 GUID 和 pin path：

```text
node_create(asset_path, node_class, name?, position?)
node_position_set(asset_path, node_id, x, y)
node_param_set(asset_path, node_id, param, value)
node_pin_connect(asset_path, from_node, from_output, to_node, to_input)
node_pin_disconnect(asset_path, node_id, input)
node_delete(asset_path, node_id)
```

写接口允许传短别名、真实 ID、index 或 pin/param 名称。插件内部负责解析并返回明确诊断：

- 找不到节点。
- 找不到 pin 或参数。
- index 越界。
- 类型不匹配。
- 连接方向错误。
- 写入后 pin-integrity 异常。

## 材质复刻写入边界

复杂材质复刻必须走“读源图数据、在目标图重建”的路径，禁止直接复制源材质里的 UE 节点对象或表达式对象。

允许的加速方式：

- 从源材质读取 `graph_node_info_get_w_pos(format="indexed")` 或 `grouped` 输出，作为复刻蓝图。
- 在目标材质里批量创建节点。
- 在目标材质里批量设置参数。
- 在目标材质里批量写入连线和 `MaterialOutput` 根输出。
- 对目标材质中已经由 MCP 创建出的重复结构，允许在目标材质内部复制/复用，减少重复创建劳动。
- 写入接口可以接收批量操作，但每个操作仍必须等价于显式的节点创建、参数写入、连线写入或位置写入。

禁止的做法：

- 禁止从源材质直接 `DuplicateObject` / `DuplicateMaterialExpression` 到目标材质。
- 禁止把源材质表达式对象、GraphNode、内部指针原样搬到目标材质。
- 禁止用“源对象复制成功”冒充 AI/MCP 手动复刻流程成功。
- 禁止用 UE Python 或外部临时脚本绕过 MCP 固定接口。

复刻验收标准：

- 目标材质节点数量与源图一致，排除 `MaterialOutput` 伪节点时应与源材质真实表达式数一致。
- 目标材质参数值与源图复刻包一致，包含 `ParameterName`、`Name`、`Group`、贴图引用、材质函数引用、枚举值和布尔值。
- 目标材质输入视角连线与源图 `E` 段一致，包含 `MaterialOutput.MaterialAttributes` 等根输出连接。
- Named Reroute 必须显式处理 declaration/usage 关系，不能只创建孤立 usage 节点。
- 最终必须重新读取目标图比对，并显式 compile/save 后再宣称复刻完成。

## 节点创建接口

```text
node_create(asset_path, graph_kind, node_class, name?, position?, params?, graph_name?, dry_run?)
```

参数约定：

| 参数 | 默认值 | 说明 |
|:-----|:-----|:-----|
| `asset_path` | 必填 | 目标资产 |
| `graph_kind` | `auto` | `material`、`blueprint`、`auto` |
| `node_class` | 必填 | 材质表达式类名或蓝图节点类名/别名 |
| `name` | 空 | 可选显示名或参数名 |
| `position` | 空 | `{ "x": -1200, "y": 300 }` |
| `params` | 空 | 创建后立即设置的参数 |
| `graph_name` | 空 | 蓝图目标图 |
| `dry_run` | `true` | 默认只解析和预检 |

文本结果应返回新节点的可编辑视图：

```text
created = true
Node.Name = Clamp_03
Node.Class = Clamp
Node.Id = n42
Node.RealId = 9180A83A4E5F4AE983D6B0F3E2AF1111
Node.Pos = -1200,300

inpin_00.Input < None
inpin_01.Min < None
inpin_02.Max < None

-nodeparam_00.MinDefault = 0.0
-nodeparam_01.MaxDefault = 1.0

outpin_00.Result > None
```

创建失败必须返回明确原因，不允许静默 fallback 到近似节点：

```text
created = false
error = UnknownNodeClass
message = Material node class not found: ClampFloat
suggest = Clamp
```

## 节点位置接口

节点位置单独设计，不混入连线和参数写入。

```text
node_position_get(asset_path, node_id, graph_kind?, graph_name?)
node_position_set(asset_path, node_id, x, y, graph_kind?, graph_name?, dry_run?)
node_position_offset(asset_path, node_id, dx, dy, graph_kind?, graph_name?, dry_run?)
```

读取返回：

```text
Node.Name = Clamp_00
Node.Id = n18
Node.RealId = 1F8E2A0D4F6B4B0B9B2E1A6D3E55A111
Node.Pos = -1296,448
```

写入返回：

```text
moved = true
Node.Name = Clamp_00
Node.Id = n18
Node.Pos.Before = -1296,448
Node.Pos.After = -1180,448
```

批量排布后续可以追加独立接口：

```text
node_layout_apply(asset_path, moves, graph_kind?, graph_name?, dry_run?)
```

`moves` 使用数组承载多节点坐标：

```json
[
  { "node": "Clamp_00", "x": -1180, "y": 448 },
  { "node": "Multiply_01", "x": -1480, "y": 448 }
]
```

## 安全边界

- `node_info_get` 只读，不触发 compile、validate、save。
- `node_info_get` 不遍历不安全的 UE API；材质输入遍历使用安全 iterator。
- 写接口默认 dry-run，实际写入返回 applied diff、pin-integrity、diagnostics、dirty-state。
- 对结构不完整的大材质还原，不自动调用 compile 或 validate。

## 与现有接口关系

| 接口 | 定位 |
|:-----|:-----|
| `graph_snapshot_get(format="wires_tiny")` | 全图导航和找节点 |
| `graph_node_info_get(format="indexed")` | 整图复刻包，不带位置和真实 GUID |
| `graph_node_info_get(format="grouped")` | 按节点类型聚合的人读整图包，只返回输入视角连线 |
| `graph_node_info_get_w_pos(format="indexed")` | 整图复刻包，额外带位置 |
| `node_info_get` | 单节点完整编辑视图 |
| `node_class_params_get` | 按节点类型查询可编辑属性模板 |
| `node_params_get` | 参数专用读取，保留兼容 |
| `graph_patch_apply` | 批量图 patch，保留但不作为复杂母材质复刻主路径 |

结论：全图快照只承担索引和导航，节点级接口承担编辑所需的完整上下文。
