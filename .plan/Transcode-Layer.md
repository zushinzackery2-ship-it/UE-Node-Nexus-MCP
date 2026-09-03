# Content_Transcoded 转码层 + L2U / U2L 实施计划

状态：草案 v1，待开工。一次交付，不分期发布，但内部按工作包依赖顺序推进（§15）。

## 0. 一句话

把 Material / MaterialFunction / MaterialInstance / 通用属性包资产 / Niagara / Blueprint 在 **MCP 工作区**镜像成行式文本 `.nexus`，agent 只用 Read / StrReplace / grep 编辑文本；`ue_sync` 负责 UE ↔ 文本的三方同步；UE 只在同步时刻参与，角色是"编译器"，文本是"源码"。

## 1. 已定决策

| 项 | 决定 |
|---|---|
| 镜像根目录 | MCP 工作区。`UE_NEXUS_TRANSCODE_DIR`，默认 server cwd 下 `Content_Transcoded/`。每个绑定过的 UE 工程一个子目录，工作区是不是 UE 工程本身都用同一条规则。 |
| 交付范围 | 一次做完：材质三件、通用属性包、Niagara System/Emitter、Blueprint 子集（不认识的节点 `@opaque`，永不丢）。 |
| 入口 | `ue_sync` 为第 7 个 facade。资产形态的读写 op 43 个转 `hidden`（清单见 §12），仍可执行。 |
| 分工 | C++ 只做"全保真导出"和"事务化应用"；格式、lint、diff、编排全在 Python。 |
| 数据通道 | bulk 数据磁盘到磁盘（UE 插件直接写镜像目录），tool result 只回摘要和 `file:line` 诊断。 |

## 2. 目标 / 非目标

目标
- 图编辑不再逐步走 JSON 往返；一次 push 完成整资产变更，返回编译诊断。
- 镜像只写非默认值、不出现 GUID，token 密度比现有 `wires_tiny` 快照再低一档。
- 离线 lint（不连 UE）能报出类名/属性/枚举/pin/签名错误。
- push 是增量 patch（按稳定 id 原地改），不是清空重建；MF 接口变更自动刷新上游调用者直到父材质。
- 每资产一个编辑器事务（Ctrl+Z 一次撤回整次 push）；只保存被碰的包，逐包保存，绝不 SaveAll。
- 镜像可进 git，蓝图/材质 diff 可 review。

非目标
- 离线写 `.uasset`。
- 关卡（`.umap`）、贴图/网格/动画的二进制载荷（只出只读 stub）。
- 文件变化自动 push（只允许 UE 保存时自动导出，见 §8.6）。
- 任意 UObject 反射写入：所有写入仍限白名单类的 `CPF_Edit` 属性 + plan 动词表（§9）。

## 3. 术语

| 词 | 含义 |
|---|---|
| raw | UE 端导出的全保真 JSON（GUID、pin GUID、opaque 原文、全部可编辑属性 ExportText）。存 `.nexus/base/`。 |
| text | `.nexus` 行式文本。agent 唯一编辑面。 |
| model | Python 内存语义模型；raw 和 text 都能转成它，diff 在 model 层做。 |
| base | 上次同步时双方一致的 raw。三方合并基准。 |
| plan | model diff 产出的动词序列（IR，§9），`transcode_apply` 的输入。 |
| schema lock | 反射导出的类/属性/默认值/枚举/签名快照，按 key 缓存（§7）。 |
| opaque | codec 不认识的节点/子对象。以 T3D（`FEdGraphUtilities::ExportNodesToText`）或 ExportText 原文进 raw，文本里只留一行占位。 |

## 4. 架构

```
Agent ──Read/StrReplace/grep──► <root>/<Project>/**/*.nexus         文本，唯一编辑面
  │                                        ▲           │
  │ ue_sync(status|pull|lint|push|...)     │ U2L       │ L2U
  ▼                                        │           ▼
Python MCP server                          │           │
  transcode/ : paths · lexer/parser/emitter · model · raw codecs · ids · schema_lock
               lint · diff → plan · state(3-way) · sync(status/pull/push/deps) · layout · stubs
  │
  │ 命名管道（只走控制面和 plan，不走 bulk）
  ▼
UE 插件
  UeNodeNexusBridge     : transcode_root_set · schema_export · transcode_export · transcode_apply · transcode_watch_set
  UeNodeNexusVfxBridge  : vfx_transcode_export · vfx_transcode_apply
  导出直接写 <root>/<Project>/.nexus/base/**.json；apply 一事务 → compile → 逐包 save
```

## 5. 目录布局与路径映射

```
<root>/                                    UE_NEXUS_TRANSCODE_DIR，默认 <cwd>/Content_Transcoded
  .nexus/
    schema/<schema_key>/                   跨工程共享：同引擎同插件集就是同一份
      classes.material_expression.json     类 → 可编辑属性(类型/枚举值/CDO 默认/Clamp) + inputs/outputs 名
      classes.k2node.json                  K2 节点类 → 可编辑属性 + 静态 pin 模板
      classes.asset.json                   UMaterial/UMaterialFunction/UMaterialInstanceConstant/UNiagaraSystem/UNiagaraEmitter/通用属性包类
      classes.component.json               UActorComponent 子类（SCS 模板）
      classes.niagara_renderer.json
      enums.json
      material_functions.json              工程 + 引擎 MF 签名（inputs/outputs 名、类型、SortPriority）
      niagara_modules.json                 工程 + /Niagara 模块脚本输入签名
      functions.cache.json                 BlueprintCallable 函数签名，按需累积
      key.json                             engine_version / plugins_hash / 生成时间
  <ProjectName>/                           每个绑定过的 UE 工程一个
    project.json                           uproject 路径、Content 目录、上次绑定 pid、auto_export 开关
    WaterStains/Functions/MF_WS_S.mf.nexus
    Materials/M_Glass.mat.nexus
    Materials/MI_Glass_Soft.mi.nexus
    Blueprints/BP_Door.bp.nexus
    VFX/NS_Rain.ns.nexus
    VFX/Emitters/E_Spark.ne.nexus
    Input/IMC_Default.asset.nexus
    Textures/T_Rock.stub.nexus             只读桩：类 + AssetRegistry tags
    .nexus/
      base/<同路径>.json                    raw
      state.json                           每资产 base_hash / local_hash / ue_saved_hash / ue_dirty / 时间
```

路径映射：`/Game/A/B/M_X.M_X` → `<Project>/A/B/M_X.<kind>.nexus`。后缀由类决定：

| 类 | 后缀 |
|---|---|
| Material | `.mat.nexus` |
| MaterialFunction（子类 MaterialFunctionMaterialLayer / MaterialLayerBlend v1 只出 stub） | `.mf.nexus` |
| MaterialInstanceConstant | `.mi.nexus` |
| Blueprint（AnimBlueprint / WidgetBlueprint v1 只出 stub） | `.bp.nexus` |
| NiagaraSystem / NiagaraEmitter | `.ns.nexus` / `.ne.nexus` |
| 通用属性包白名单类（§6.4） | `.asset.nexus` |
| 其它一切 | `.stub.nexus` |

v1 只镜像 `/Game` mount；插件 content 不镜像（MF/模块签名进 schema lock 即可）。

## 6. 文本格式规范

### 6.1 通用语法

- UTF-8，LF，`#` 到行尾是注释。前四行固定头：`nexus: 1` / `asset: /Game/...` / `class: <UE 类短名>` / `schema: <key>`。
- 段头 `[name]` 或 `[name 参数...]`。段内三种行：
  - 属性行 `Key = Value`
  - 节点行 `id : Class(k=v, k2=v2) @ x,y`（`@ x,y` 可省，省则 push 时自动排版；`(...)` 可省）
  - 连线行 `src[.pin] -> dst[.pin]`（省 pin = 唯一输入 / 默认输出）
- `id` 是 `[A-Za-z_][A-Za-z0-9_]*`，段内唯一。文件里**永远不出现 GUID**；id ↔ GUID 映射在 base。
- `Class` 用短名（去 `MaterialExpression` / `K2Node_` 前缀），短名有歧义时用完整 `/Script/Module.Class`。
- 值文法直接用 UE `ExportText`：数字、`true/false`、`"带引号字符串"`（反斜杠转义）、结构 `(R=1,G=0,B=0,A=1)`、对象 `/Game/T.T` 或 `/Script/Engine.Texture2D`、枚举短名 `BLEND_Translucent`、数组 `(1,2,3)`。我们不发明值文法；tokenizer 只负责括号/引号配平。
- 属性行的 Key 是原始名（允许中文、空格），到 ` = ` 为止；以 `#`/`[` 开头或含 ` = ` 时加双引号。
- 只写非 CDO 默认值的属性；删掉一行 = 恢复默认（MI 里 = 清除 override）。
- 注释框节点：`id : Comment("文字", w,h) @ x,y`。
- 不认识的节点：`id : @opaque(/Script/BlueprintGraph.K2Node_Timeline) @ x,y`。可移动、删除、连线；不可改参数、不可新建。

### 6.2 Material / MaterialFunction

```
nexus: 1
asset: /Game/WaterStains/Functions/MF_WS_S
class: MaterialFunction
schema: 5.5.4-a1b2c3

[asset]
Description = "smoothstep, denom = (B-A)+1e-6"
bExposeToLibrary = true

[graph]
in_a    : FunctionInput(InputName=A, InputType=FunctionInput_Scalar, SortPriority=0)  @ -1400,0
in_b    : FunctionInput(InputName=B, InputType=FunctionInput_Scalar, SortPriority=1)  @ -1400,120
in_x    : FunctionInput(InputName=X, InputType=FunctionInput_Scalar, SortPriority=2)  @ -1400,240
sub_ba  : Subtract(Desc="denom = B - A")                                               @ -1000,80
c_eps   : Constant(R=1e-6)                                                             @ -1000,220
add_eps : Add                                                                          @ -800,120
sub_xa  : Subtract                                                                     @ -800,-40
div     : Divide                                                                       @ -600,40
sat     : Saturate                                                                     @ -400,40
out     : FunctionOutput(OutputName=Result)                                            @ -200,40

in_b    -> sub_ba.A
in_a    -> sub_ba.B
sub_ba  -> add_eps.A
c_eps   -> add_eps.B
in_x    -> sub_xa.A
in_a    -> sub_xa.B
sub_xa  -> div.A
add_eps -> div.B
div     -> sat
sat     -> out
```

- Material 的 `[asset]` 是 `UMaterial` 的 `CPF_Edit` 非默认属性（BlendMode / MaterialDomain / ShadingModel / TwoSided / bUsedWith* ...）。
- Material 有隐含节点 `out`（材质输出）：`final.RGB -> out.BaseColor`、`n.R -> out.Opacity`。
- MF 调用：`s : MaterialFunctionCall(MaterialFunction=/Game/WaterStains/Functions/MF_WS_S)`；pin 名 = MF 的 InputName/OutputName，来自 schema lock 的 `material_functions.json`。
- 向量输出别名 `RGB/RGBA/R/G/B/A` 沿用现有 pin 解析。
- 参数节点的 `ParameterName`、`Group`、`SortPriority` 都是普通属性；`NamedRerouteUsage(DeclarationName=X)` 沿用现有合成属性。

### 6.3 MaterialInstance

```
class: MaterialInstanceConstant
[asset]
Parent = /Game/Materials/M_Glass
BasePropertyOverrides = (bOverride_BlendMode=True,BlendMode=BLEND_Translucent)

[scalar]
玻璃缩放 = 500
[vector]
Tint = (R=1,G=0.9,B=0.8,A=1)
[texture]
Normal = /Game/T/T_Glass_N
[switch]
UseMist = true
[component_mask]
Channels = (R=true,G=false,B=false,A=false)
```

只列 override 的参数；出现即 override，删行即清除。

### 6.4 通用属性包 `.asset.nexus`

`class:` + `[asset]` 段，通用反射 codec，零逐类代码。v1 白名单：`UDataAsset`/`UPrimaryDataAsset` 子类、`UInputAction`、`UInputMappingContext`、`UPhysicalMaterial`、`UMaterialParameterCollection`、`UCurveFloat/Vector/LinearColor`。

已知边界：**instanced 子对象属性**（IMC 的 Triggers / Modifiers、DataAsset 里 `Instanced` UObject 指针）v1 只保真保留（raw 存 ExportText/T3D，文本里显示为 `@opaque`），不可在文本里编辑。

### 6.5 Niagara

```
class: NiagaraSystem
[asset]
bFixedBounds = true
WarmupTime = 0.5

[user]
Spawn Rate : float = 100
Color      : LinearColor = (R=1,G=0.5,B=0,A=1)

[emitter Sparks]
Enabled = true
Parent = /Game/VFX/Emitters/E_Base          # 继承的父 emitter，没有就是独立 emitter
SimTarget = CPUSim

[stack Sparks/EmitterUpdate]
rate  : SpawnRate(SpawnRate=@link(User.Spawn Rate))

[stack Sparks/ParticleSpawn]
init  : InitializeParticle(Lifetime=(Min=1,Max=2), Color=(R=1,G=0.5,B=0,A=1))

[stack Sparks/ParticleUpdate]
grav  : GravityForce(Gravity=(X=0,Y=0,Z=-980))
drag  : Drag(Drag=0.5)   !disabled

[renderer Sparks/sprite : NiagaraSpriteRendererProperties]
Material = /Game/VFX/M_Spark
Alignment = VelocityAligned
```

- 栈组：`EmitterSpawn / EmitterUpdate / ParticleSpawn / ParticleUpdate`；`Event:<Name>` 与 `Stage:<Name>` v1 整组 `@opaque`（保真保留、不可编辑）。
- 模块行 `id : Script(input=value, ...) [!disabled]`。Script 短名经 `niagara_modules.json` 解析，歧义时写完整资产路径。只列有 override 的输入；`@link(Namespace.Name)` 表示链接到参数（v1 可读可保留，新建链接 push 报 unsupported）；动态输入（输入上挂函数调用）`@dynamic` 保真保留。
- 模块顺序有意义：文件顺序 = 栈顺序，diff 出 `ns_module_move`。
- `NiagaraEmitter` 资产（`.ne.nexus`）用同一套栏目，没有 `[user]`/`[emitter]` 段头，直接 `[stack ParticleSpawn]`。

### 6.6 Blueprint

```
class: Blueprint
[asset]
ParentClass = /Script/Engine.Actor
BlueprintDescription = "门"

[variables]
Health   : float = 100                          {Category=Stats, InstanceEditable, ExposeOnSpawn}
Target   : Object(/Script/Engine.Actor)         {Replicated}
Tags2    : Array<Name>                          {BlueprintReadOnly}
Hits     : int = 0                              @renamed(HitCount)

[components]
Root     : SceneComponent
Mesh     : StaticMeshComponent(parent=Root)     { StaticMesh=/Game/Meshes/SM_Door, RelativeLocation=(X=0,Y=0,Z=50) }
AimVFX   : NiagaraComponent(parent=Mesh, socket=Muzzle)
Movement : @inherited(CharacterMovementComponent) { MaxWalkSpeed=450 }

[defaults]
bReplicates = true
InitialLifeSpan = 0

[dispatchers]
OnOpened(By: Object(/Script/Engine.Actor))

[interfaces]
/Game/Interfaces/BPI_Interactable

[graph EventGraph]
begin  : Event(Actor.ReceiveBeginPlay)                                     @ 0,0
hp     : VariableGet(Health)                                               @ 300,120
print  : CallFunction(KismetSystemLibrary.PrintString, InString="Hello")   @ 600,0
seq    : Sequence(pins=3)                                                  @ 900,0
tl     : @opaque(/Script/BlueprintGraph.K2Node_Timeline)                   @ 1200,0

begin.then -> print.execute
hp -> print.Duration
print.then -> seq.execute

[function TakeDamage(Amount: float) -> (Dead: bool) {Category=Combat, Public}]
local Remaining : float = 0
sub    : CallFunction(KismetMathLibrary.Subtract_FloatFloat)
cmp    : CallFunction(KismetMathLibrary.LessEqual_FloatFloat, B=0)
entry.Amount -> sub.B
sub -> cmp.A
cmp -> result.Dead
```

规则：
- 变量类型文法：`float | double | int | int64 | bool | byte | string | name | text | Object(路径) | Class(路径) | SoftObject(路径) | SoftClass(路径) | Struct(路径) | Enum(路径) | Interface(路径) | Array<T> | Set<T> | Map<K,V>`；委托类型 `@opaque`。
- 变量元数据：`Category=, Tooltip=, InstanceEditable, BlueprintReadOnly, ExposeOnSpawn, Private, Replicated | RepNotify(Func), Transient, SaveGame, Config, Multiline, ExposeToCinematics`。`= 默认值` 是 `FBPVariableDescription::DefaultValue`。
- `@renamed(Old)`：变量/组件/函数改名的显式标注，diff 出 rename 动词（`FBlueprintEditorUtils::RenameMemberVariable` 会修引用）。无标注时"消失 + 新出现同类型"按删+建处理并给 warning。
- 组件：SCS 节点 `Name : Class(parent=, socket=) { 模板非默认属性 }`；原生继承组件 `@inherited(Class)`，只能改属性。
- `[defaults]`：CDO 上继承自父类的 `CPF_Edit` 非默认属性（不含组件、不含 `[variables]` 已列的）。
- `[dispatchers]` / `[interfaces]`：v1 只读保真（改动 push 报 unsupported）。
- 图段：`[graph Name]`（ubergraph 页）、`[function 签名 {flags}]`（隐含 `entry`/`result` 节点，pin 由签名派生）、`[macro Name]` 整段 `@opaque`。折叠图 / Composite / Tunnel `@opaque`。
- **pin 不在文件里声明**，由节点参数派生（`AllocateDefaultPins` + `ReconstructNode`）。节点参数里的键先当 pin 默认值解析，再当节点属性；冲突时用 `pin:X=` / `prop:X=` 前缀。exec pin 固定 `execute` / `then`；目标 `self`；返回 `ReturnValue`。
- 动态 pin 数：`Sequence(pins=3)`、`MakeArray(pins=4)`、`CommutativeAssociativeBinaryOperator`/`PromotableOperator` 同理。
- v1 可编辑节点类：`Event(Class.Func)`、`CustomEvent(Name, A: T, ...)`、`CallFunction(Class.Func | self.Func | /Game/BP.BP_C.Func)`、`CallParentFunction`、`Message`（接口调用）、`VariableGet/Set(Name)`（含组件变量）、`Branch`/`IfThenElse`、`Sequence`、`Knot`、`Comment`、`Select`、`Switch*`、`DynamicCast(Class)`、`SpawnActorFromClass(Class)`、`MakeStruct/BreakStruct(Struct)`、`MakeArray`、`GetArrayItem`、`Self`、`Literal`、`MacroInstance(StandardMacros.ForEachLoop)`、`InputKey(Key)`、`InputAction/InputAxisEvent(Name)`、`EnhancedInputAction(/Game/Input/IA_X)`、`PromotableOperator`。其余 `@opaque`。

### 6.7 stub

```
nexus: 1
asset: /Game/Textures/T_Rock
class: Texture2D
[tags]
Dimensions = 2048x2048
Format = BC7
SRGB = true
LODGroup = World
```

来源 AssetRegistry tags，只读；push 时 stub 文件的任何改动都是 lint 错误。目的：让镜像成为整个工程可 `grep` 的地图，替掉大部分 `asset_list` / AutoIndex 用法。

### 6.8 规范化规则（emitter 的确定性）

- 段顺序固定（头 → asset → variables → components → defaults → dispatchers → interfaces → user → emitter/stack/renderer → graph/function）。
- 节点顺序：**保留 base 中既有顺序**（手写顺序有意义），新节点按拓扑深度、再按 x、再按 y 追加；首次 pull 全拓扑排序。连线按目标节点顺序、再按目标 pin 顺序。
- 属性按 schema 中的声明顺序。
- 数字：整数不带小数点；浮点用 UE ExportText 原文再去掉尾零。
- id 生成：优先 `Desc` / `ParameterName` / `InputName` / 函数名 / 变量名 的 ASCII 化，冲突加 `_2`；否则 `<class_lower>_<n>`。一旦进 base 就固定，后续 pull 不改 id。
- push 后 UE 端重导出 → 覆盖文本为规范形，响应里标 `normalized_files`。

## 7. schema lock

- `schema_export {out_dir, families?}`：反射导出上表全部文件。属性过滤沿用 `CPF_Edit && !CPF_DisableEditOnInstance`；每个属性带 `type/kind/enum_values/default_value(CDO ExportText)/ClampMin/ClampMax/object_class/struct_type`——即现有 `BuildMaterialExpressionClassParams` 的输出整批落盘。
- 材质表达式类另导 `inputs`（`GetInputsView()` 名字）与 `outputs`（`GetOutputs()` 名字）；K2 类另导静态 pin 模板（Branch/Sequence/Knot/Select/...），CallFunction/Event 的 pin 由函数签名派生，签名按需查 UE 并写入 `functions.cache.json`。
- `key = <EngineVersion>-<sha1(排序后已启用插件名列表)[:8]>`。`bridge_capabilities_get` 增加 `schema_key`；不一致时 `ue_sync` 的任何动作先报 `schema_stale`，`ue_sync schema` 重拉。
- 用途：离线 lint（§8.2）与 emitter 的"只写非默认值"。

## 8. 同步语义

### 8.1 状态矩阵

对每个资产比较三个哈希：`base_hash`（base raw）、`local_hash`（文本规范化后哈希）、`ue`（`FAssetPackageData` 的 PackageSavedHash + `UPackage::IsDirty()`；取不到时退回 `.uasset` mtime+size）。

| 状态 | 含义 | pull | push |
|---|---|---|---|
| `clean` | 三方一致 | 跳过 | 跳过 |
| `local-modified` | 只改了文本 | 跳过 | 执行 |
| `ue-modified` | 只改了 UE（已保存或 dirty） | 执行 | 跳过 |
| `both-modified` | 冲突 | 拒绝，落 `X.ue.nexus` 旁文件 | 拒绝，需 `force` |
| `local-new` | 文本有、UE 无 | 跳过 | `asset_create` + 全量 plan |
| `ue-new` | UE 有、文本无 | 执行（生成文本） | 跳过 |
| `local-deleted` | 文本被删 | 报 orphan | 报 orphan，`allow_delete` 才删 |
| `schema-stale` | key 不一致 | 拒绝 | 拒绝 |

UE 未连接时 `ue` 为 unknown：status 照常报本地两方，pull/push 报 `bridge_unavailable`，lint 正常。

### 8.2 动作

`ue_sync(action, paths=None, options=None)`；`paths` 接受 `/Game/...`、镜像文件/目录路径，空 = 全部已镜像资产。

- `init`：解析 root、写 `project.json`、确保 schema lock（缺则导出）、可选全量首次 pull（`options.pull_all`，含 stub）。
- `status`：状态矩阵 + root/project/schema_key + 待处理冲突。
- `pull`：对 `ue-modified/ue-new` 调 `transcode_export`（UE 直接写 base），Python 由 raw 生成文本、更新 state。`options.include_stubs`。
- `lint`：离线。解析错误（行/列）→ schema 校验（类存在、属性存在且可编辑、枚举值、Clamp 范围、对象类兼容、pin 存在、MF/模块签名、重复 id、悬空连线、变量类型文法、stub 被改）→ 返回 `file:line:col: message`。
- `push`：`lint` 通过 → 与 base 的 model diff → plan → 默认 `dry_run=true` 只回 plan 摘要（每资产动词计数 + 前 N 条）；`dry_run=false` 时按 §8.3 顺序对每资产调 `transcode_apply`（事务 → compile → save）→ 重导出更新 base → 文本重写规范形 → 返回每资产 `applied/failed/remaining_errors/normalized`，诊断映射到 `file:line`（node id → 行号表）。`options`: `dry_run`(默认 true) / `compile`(true) / `save`(true) / `force: "local"|"ue"` / `allow_delete`(false) / `stop_on_error`(true)。
- `schema`：强制重拉 schema lock。

### 8.3 依赖序与调用者刷新

- 多资产 push 按引用拓扑排序：MF → 引用它的 MF → 材质 → MI；Niagara emitter → system；BP 父类 → 子类。用 AssetRegistry `GetReferencers` 建图。
- 某个 MF 的 FunctionInput/FunctionOutput 集合变了（新增/删除/改名/改类型）：push 该 MF 后自动对**所有**引用者（包括这次没改的）执行 `refresh_function_callers`（`UMaterialExpressionMaterialFunctionCall::UpdateFromFunctionResource` + 按 pin 名重连），沿引用链到父材质，逐个 compile，逐个确认编辑器进程仍在。被刷新的调用者自动 pull 一次更新 base 与文本。
- Niagara 父 emitter 改动后子 system 同理触发重编。

### 8.4 保存策略

- 只保存 plan 触碰的包和被刷新的调用者包，一次一个，`UPackage::SavePackage` 直接落盘，不走 `PromptForCheckoutAndSave`、不 SaveAll、不弹源码管理框。
- 每保存一个包后向 Python 回报；Python 每资产结束检查编辑器实例仍在（`bridge_instance_list`），不在则中止并把剩余标 `skipped`。
- 这是对 `RainGlass-Align.md §7` 那条崩溃链的机器化规避。

### 8.5 冲突与保守默认

- `both-modified` 默认拒绝；`force="local"` 以文本为准覆盖 UE，`force="ue"` 以 UE 为准覆盖文本。
- 文本删除 ≠ 删资产；文件移动 ≠ 重命名（报出来，让人显式 `asset_move`）。
- `@opaque` 节点只允许移动/删除/连线；其属性改动 lint 报错。
- push 写入前把被覆盖的文本副本放 `.nexus/undo/<时间戳>/`，保留最近 10 次。

### 8.6 auto export（可选，默认关）

`transcode_watch_set {enabled}` 挂 `UPackage::PackageSavedWithContextEvent`：镜像范围内的资产在编辑器里保存时自动导出 raw 到 base 旁的 `pending/`。下一次任何 `ue_sync` 调用先把 pending 转成文本。文件变化不触发 push。

## 9. plan IR（动词表）

Python diff 产出、`transcode_apply` 消费。每条 `{"op": ..., ...}`，按资产成组，组内有序。

| 域 | 动词 | 参数 |
|---|---|---|
| 通用 | `set_asset_prop` | `name, value_text` |
| 图（mat/mf/bp） | `create_node` | `id, class, params{}, x, y, graph?` |
| | `delete_node` | `id` |
| | `set_node_param` | `id, name, value_text`（bp 侧带 `pin:`/`prop:` 前缀已由 Python 解析） |
| | `set_node_pins` | `id, count`（动态 pin 数） |
| | `set_node_position` | `id, x, y` |
| | `set_comment` | `id, text, w, h` |
| | `connect_pins` / `disconnect_pins` | `from_id, from_pin, to_id, to_pin` |
| 材质 | `connect_output` / `disconnect_output` | `from_id, from_pin, property` |
| | `refresh_function_callers` | （post-step，作用于引用者） |
| | `mi_set_param` / `mi_clear_param` | `kind, name, value_text` |
| 蓝图 | `bp_variable_add / remove / set / rename` | `name, type, default, meta` / `old, new` |
| | `bp_component_add / remove / set_prop / reparent / rename` | `name, class, parent, socket` / `prop, value_text` |
| | `bp_default_set` | `prop, value_text` |
| | `bp_function_add / remove / signature_set / rename` | `name, inputs[], outputs[], flags` |
| | `bp_local_variable_add / remove / set` | `function, name, type, default` |
| | `bp_custom_event_signature_set` | `id, params[]` |
| Niagara | `ns_system_prop_set` / `ns_emitter_prop_set` | `name, value_text` / `emitter, name, value_text` |
| | `ns_user_param_add / remove / set` | `name, type, value_text` |
| | `ns_emitter_add / remove / rename` | `name, parent` |
| | `ns_module_add / remove / move / set_enabled` | `emitter, stack, id, script, index` |
| | `ns_module_input_set / reset` | `emitter, stack, id, input, value_text` |
| | `ns_renderer_add / remove / set_prop` | `emitter, id, class` / `prop, value_text` |

`compile` / `save` 不是动词，是 `transcode_apply` 的选项。

## 10. UE 端改动

### 10.1 新 op

| op | 插件 | 类型 | 说明 |
|---|---|---|---|
| `transcode_root_set` | core | write | 会话级注册允许写入的绝对根目录；必须存在、不在 Engine 目录、不在任何工程的 `Content/` 下。export/apply 拒绝根外路径。 |
| `schema_export` | core（VFX 部分由 VFX 模块通过注册的 provider 追加） | read | §7。 |
| `transcode_export` | core | read | `{asset_paths[], out_dir, include_opaque}` → 逐资产写 raw，返回行 `[asset_path, class, file, sha256, saved_hash, dirty]`。Niagara 类由 Python 路由到 `vfx_transcode_export`。 |
| `transcode_apply` | core | write | `{asset_path, plan[], dry_run, compile, save}`。一个 `FScopedTransaction`；内部复用现有 patch handler（JSON 再入，嵌套事务由外层收拢）；compile；按 §8.4 save；返回逐动词结果 + 编译诊断（带 node GUID）+ `remaining_errors` + 重导出后的 raw 文件。 |
| `vfx_transcode_export` / `vfx_transcode_apply` | vfx | read / write | Niagara 同上。 |
| `transcode_watch_set` | core | write | §8.6。 |

`bridge_capabilities_get` 增加 `schema_key`、`transcode_root`。

### 10.2 导出保真度要求（在现有 snapshot 之上补的）

- 材质/MF：每个表达式的全部可编辑属性 ExportText、`MaterialExpressionGuid`、注释框、`EditorX/Y`、MaterialFunctionCall 的函数路径与当前 pin 列表、材质属性输入连接（含 `OutputIndex`/mask）、`UMaterial`/`UMaterialFunction` 可编辑属性。
- MI：`Parent`、`BasePropertyOverrides`、四类参数的 override 集合与值。
- 蓝图：`NewVariables` 全字段、SCS 树 + 模板非默认属性、CDO 非默认属性、事件分发器签名、接口列表、每个图的节点（`NodeGuid`、类、可编辑属性、pin 列表含 GUID/方向/类型/默认值/连接、`bAdvancedView`、注释、`EnabledState`）、函数入口/出口签名与 flags、局部变量；不识别类的节点附 T3D。
- Niagara：system/emitter 可编辑属性、emitter handle（名、Enabled、父）、每组栈的模块序列（`NodeGuid`、脚本路径与版本、Enabled、输入 override 值/链接/动态输入 T3D）、renderer（类 + 可编辑属性）、user 参数、Event/Stage 组整块 T3D。

### 10.3 模块拆分与文件预算

按现有目录习惯新增 `Private/Transcode/`（core）与 `Private/Niagara/Transcode/`（vfx），每个 TU 不超过 300 行（`test_source_structure`）。新增 TU 需要在 `tests/compile_check/ue_stubs/` 补 `FScopedTransaction`、`FFileHelper`、`IFileManager`、`FSavePackageArgs`、`UPackage::SavePackage`、`FEdGraphUtilities`、`FBlueprintEditorUtils`、`FKismetEditorUtilities` 的桩。

## 11. Python 端改动

新增包 `src/ue_node_nexus_mcp/transcode/`，每文件 ≤ 300 行：

| 模块 | 职责 |
|---|---|
| `paths.py` | root 解析（env → cwd）、工程子目录、资产路径 ↔ 文件路径、后缀表 |
| `lexer.py` / `parser.py` | 行式语法 → 带行号的 AST；ExportText 括号/引号配平 |
| `emitter.py` | 规范形写出（§6.8） |
| `model.py` / `model_blueprint.py` / `model_niagara.py` | 语义模型 dataclass |
| `raw_material.py` / `raw_blueprint.py` / `raw_niagara.py` / `raw_generic.py` | raw JSON ↔ model |
| `ids.py` | id 生成、GUID 映射、`@renamed` 解析 |
| `schema_lock.py` | 读取/校验 key/触发导出；类与签名查找 |
| `lint_common.py` / `lint_material.py` / `lint_blueprint.py` / `lint_niagara.py` | §8.2 规则 |
| `diff_graph.py` / `diff_props.py` / `diff_blueprint.py` / `diff_niagara.py` | model 对 → plan |
| `plan.py` | 动词 dataclass、序列化、风险摘要 |
| `state.py` | `state.json`、哈希、状态矩阵 |
| `sync_status.py` / `sync_pull.py` / `sync_push.py` / `sync_deps.py` | 编排、拓扑序、调用者刷新、进程存活检查、undo 副本 |
| `layout.py` | 省略坐标时的分层排版 |
| `stubs.py` | stub 生成 |
| `errors.py` | `file:line:col` 诊断类型 |

包外：`tools_sync.py`（facade `ue_sync`）、`payload_schema_definitions.py` 补新 op 的 schema、`facade_capabilities.py` 的 `facade_tools` 列表、`contracts.THIN_MCP_OPERATIONS`。

## 12. facade / operations.json / hidden 清单

- 新 facade `ue_sync(action: Literal["init","status","pull","lint","push","schema"], paths: list[str] | None = None, options: dict | None = None)`。响应固定紧凑：`root/project/schema_key` + 每资产一行 `[asset, state, action, applied, errors, warnings]` + 诊断列表（超阈值走 artifact）。
- `operations.json` 新增 7 个 op（§10.1），新 group `transcode`（`FEATURE_GROUPS` 从 manifest 派生，自动生效；`UE_NEXUS_DISABLE_FEATURES=transcode` 可整体关闭）。
- 置 `hidden: true`（仍可执行，不进索引与计数）：
  - graph 全组 12 个
  - material：`material_expression_classes_list`、`material_lint`、`material_instance_params_get`、`material_instance_params_set`
  - blueprint：`blueprint_details_get`、`blueprint_components_patch`
  - vfx：Niagara 读写 23 个（保留可见：`niagara_compile`、`niagara_asset_lint`、`cascade_system_summary_get`）
  - project_input：`input_mapping_context_get`、`input_mapping_context_entry_add`
  - 合计 43 个。`asset_*`、`auto_index_*`、`level_*`、`core` 全部保持可见。
- `ue_read` 现有 target 不删（它们路由到仍存在的 op），skill 改为优先引导到文件。

## 13. 文档与 skill

- `guides/transcode_sync.md`，`workflow_guide_get` 新类别 `sync`，`getting_started.md` 首段改为镜像工作流。
- `skill/ue-node-nexus-mcp/SKILL.md` 顶部新增"优先走文本镜像"：`ue_sync status` → 改文件 → `ue_sync lint` → `ue_sync push`（dry_run）→ `push(dry_run=false)` → 读 `file:line` 诊断修；什么时候才回落到 `ue_execute` 细粒度 op（opaque 节点、关卡、未镜像类型）。
- README：新章节、op 计数更新、目录结构更新、兼容性表补"镜像格式版本 `nexus: 1`"。
- 本文件开工后转入 `.high-value-information/Transcode-Layer.md` 作为长期规范，`Task-Status.md` 挂工作包。

## 14. 测试

- 单元：`tests/transcode/test_lexer_parser.py`、`test_emitter.py`、`test_ids.py`、`test_lint_*.py`、`test_diff_*.py`、`test_state_matrix.py`、`test_layout.py`。
- 不变量（核心）：`tests/transcode/golden/` 放真实编辑器导出的 raw 与对应规范文本，覆盖：RainGlass 的 `MF_WS_S`、一张带 `out` 连接的材质、一个 MI、一个含变量/组件/EventGraph/函数/opaque 节点的 BP、一个双 emitter 的 Niagara system、一个 IMC。断言：
  1. `emit(parse(text)) == text`
  2. `model(raw) == model(parse(emit(model(raw))))`
  3. `diff(model(raw), model(raw)) == []`（pull 后立即 push 是空 plan）
- 编排：`test_sync_push_fake_bridge.py` 用 conftest 假 bridge 记录 `transcode_apply` 载荷，断言拓扑顺序、冲突拒绝、`allow_delete` 语义、进程消失时 skipped、undo 副本落盘。
- facade：`test_sync_facade.py` 参数校验、artifact 溢出、`schema_stale` 路径；`test_server_registration.py` 的 facade 数 6 → 7。
- 契约：`test_bridge_contract_parity.py` 自动覆盖新 op；`test_source_structure.py` 预算；`test_cpp_compile_check.py` 新桩。
- 无头 E2E：smoke commandlet 回放 `schema_export → transcode_export → transcode_apply(空 plan) → transcode_export`，两次 raw 相等。
- 实机验收：见 §16。

## 15. 工作包与顺序

一次交付，内部按依赖推进。规模：S < 1 天、M 2–3 天、L 一周量级（单人）。

| WP | 内容 | 依赖 | 规模 |
|---|---|---|---|
| 0 | 契约脚手架：`operations.json` 新 op 与 hidden、`ue_sync` 空壳、`paths/state/errors`、`transcode_root_set` | — | S |
| 1 | schema lock：C++ `schema_export`（含 inputs/outputs、K2 静态 pin 模板、MF/模块签名）、Python `schema_lock.py`、key 校验 | 0 | M |
| 2 | 格式核心：lexer/parser/emitter/model/ids/layout，先用手写 golden 跑不变量 1 | 0 | M |
| 3 | 材质族：C++ 导出保真 + `transcode_apply` 骨架（事务/compile/逐包 save）+ 动词 `set_asset_prop`/`connect_output`/`refresh_function_callers`/`mi_*`；Python raw/lint/diff | 1, 2 | L |
| 4 | 通用属性包 + stub | 3 | S |
| 5 | Niagara：VFX 插件导出/应用 + `ns_module_move`；Python raw/lint/diff | 1, 2 | L |
| 6 | Blueprint：导出保真（含 T3D opaque）、变量/组件/默认/函数/局部变量动词、编译错误 → GUID 映射；Python raw/lint/diff/类型文法 | 1, 2 | L+ |
| 7 | 同步引擎：状态矩阵、pull/push 编排、拓扑序与调用者刷新、进程存活检查、undo、watch | 3 | M |
| 8 | facade 收口、hidden 生效、guides/skill/README | 7 | S |
| 9 | 测试补齐、无头 E2E、实机验收（§16） | 全部 | M |

WP3 / 5 / 6 可并行；WP7 在 WP3 之后即可开始（先只对材质族联调），5/6 完成后接入。

## 16. 验收（单一闸门）

1. golden 集三条不变量全绿；实机对 Shadetest 全量 pull 后立即 push，所有资产 plan 为空。
2. RainGlass 链：改 `MF_WS_S.mf.nexus` 一个常数 → push → `MF_WS_S` 及全部上游调用者到两张父材质 compile 0 error，编辑器进程存活，日志无 SaveAll、无 checkout 弹框；再 pull，文本与提交时的规范形逐字节一致。
3. 改 MF 接口（新增一个 FunctionInput）→ push → 上游调用者被自动刷新并重连，文本随之更新。
4. 测试 BP：改变量默认、`@renamed` 改名、加组件并设属性、加 CallFunction 节点并连线、加带参函数 → push → 编译通过 → pull 一致；含 `@opaque` 节点的图往返节点数不变。
5. 测试 Niagara：改模块输入、禁用模块、移动模块顺序、换 renderer 材质、加 user 参数 → push → `niagara_compile` 通过 → pull 一致。
6. `ue_context_get` 列出 `ue_sync`；`ue_capability_get(detail="index")` 默认索引比现在少 43 个 op。
7. 全部 pytest 通过（含 parity、300 行预算、clang 桩编译）。

**验收结果（2026-09-03，Shadetest，UE 5.5.4，headless `-nullrhi`）**

- 1 ✓ 30 个资产 init → status 全 clean → 每个资产 pull→push 空 plan（`build/live_roundtrip.py` 0 failures）。
- 2/3 ✓ 未直接改用户的 RainGlass 链（避免重存用户资产），改在 `/Game/NexusSmoke/` 用文本新建 `MF_Smoke`+`M_Smoke` 验证：改常数、插节点重连、MF 输入改名 → 调用者自动 refresh、连线保留（`call.C`），compile 0 error，无 SaveAll。
- 4 ✓ `BP_Smoke` 从文本新建（变量/组件/事件图/带参函数），再改默认值、加变量、改组件属性、声明 Tick 事件（收养幽灵节点）。`@renamed` 只有单测覆盖。
- 5 ✓/△ `NS_Smoke`（`niagara_system_create` 造）：改模块输入（rapid-iteration 路径）、重置输入、禁用/启用模块、改 renderer 属性、加 user 参数 → compile 通过 → pull 一致。**未验**：移动模块顺序（`ns_module_move` v1 不支持）、换 renderer 材质。
- 6/7 ✓ 127 pytest。
- 与设计的偏差：模块行打印 Stack 面板显示的全部 rapid-iteration 值（不是"仅覆盖值"），因为脚本元数据默认值不可靠；`SetVariables` 模块可编辑不可新建；渲染器 `*Binding` 属性不进文本；BP 幽灵事件节点隐藏；`PackageSavedHash` 用文件 mtime:size 而非 AR 哈希。

## 17. 风险与对策

| 风险 | 对策 |
|---|---|
| 蓝图保真不足导致 pull 后信息丢失 | 任何不认识的节点/子对象走 opaque 保真保留；验收 4 里"节点数不变"是硬指标。 |
| 保存触发缩略图 D3D12 AV | §8.4：逐包 `SavePackage`，不走 prompt，不 SaveAll；每包后存活检查。 |
| MF 接口变更连锁作废上游 | §8.3 自动刷新到父材质并自动 pull 调用者。 |
| 引擎/插件升级导致 schema 漂移 | key 不一致直接拒绝同步，`ue_sync schema` 重拉；值文法两端都由 UE 解释，不受影响。 |
| 编辑器与 agent 同时改同一资产 | 三方状态矩阵；冲突默认拒绝并落旁文件；push 前做 undo 副本。 |
| MCP server cwd 不是工作区 | `ue_sync status/init` 明示解析出的 root；`UE_NEXUS_TRANSCODE_DIR` 覆盖。 |
| 大蓝图导出/文本过大 | bulk 走磁盘；文本可 grep/分段 Read；agent 不需要整读。 |
| `transcode_apply` 内部 JSON 再入现有 handler 的事务嵌套 | UE 嵌套 `FScopedTransaction` 收拢到最外层，dry_run 语义由外层统一控制；现有 handler 各自的 dry_run 一律传 false。 |
| 中文/空格参数名 | 属性行 Key 为原始名，仅在歧义时引号；id 强制 ASCII 标识符。 |

## 18. 明确不做

- 离线写 `.uasset`（UAssetAPI 路线）。
- 用 UE 实验性 `.utxt` TextAssetFormat 当人读格式；T3D 只做 opaque 载体。
- 文件 watcher 自动 push。
- YAML/JSON 作主格式。
- 关卡纳入镜像。
- Niagara Event/Stage、蓝图委托/接口/宏/折叠图/Timeline 的文本编辑（v1 只读保真）。

## 19. 开工时再定的小项

- `@link` 新建链接是否在 v1 放开（取决于 `niagara_module_inputs_set` 现有能力）。
- `functions.cache.json` 的按需拉取是在 lint 阶段触发一次 UE 查询，还是 lint 只 warning、push 时解析。
- stub 是否也覆盖 `/Engine` 与插件 content（默认不）。
- undo 副本保留次数（默认 10）。
