# Task-Status

## 1. 当前任务 / 需求 / 待办

**Content_Transcoded 文本镜像（`.plan/Transcode-Layer.md`）— 实现完成，实机验收通过（材质/MF/MI/BP/Niagara），结果记在 plan §16**

- 已完成：Python `transcode/` 全套（parser/emitter、raw codec ×5、schema lock、lint、diff→plan、三方 state、pull/push/init/schema 编排、`ue_sync` facade）；C++ core 6 个 op + VFX 2 个 op；129 op 清单、43 个资产形态 op 转 hidden；guide `text_mirror`、SKILL、README；127 pytest 全绿；两个插件 UAT 编译通过。
- 实机验收（Shadetest，headless `UnrealEditor-Cmd -nullrhi`，schema `5.5.4-ced15aa9`）：
  - `init` 拉 30 个资产（3 材质 / 11 MF / 12 MI / 1 BP）→ `status` 全 clean → 每个资产 emit→parse→lint→plan 均为空（`build/live_roundtrip.py`，0 failures）。
  - 从文本新建 `MF_Smoke`/`M_Smoke`/`BP_Smoke`（`/Game/NexusSmoke/`）：一次 push 建资产 + 节点 + 连线 + 变量 + 组件 + 函数，编译 0 错，保存，回读后 status clean。
  - 编辑 push：改常数、插 Saturate 重连、MF 输入 B→C（`interface_changed` → 调用者 `M_Smoke` 自动 refresh，连线保留并显示为 `call.C`）；BP 改变量默认值、加变量、改组件属性、声明 `tick : Event(Actor.ReceiveTick)`（收养模板里的幽灵节点，不重复）。
  - 实机修掉的问题：`ExportText_InContainer` 跳过零值 → 改 `ExportTextItem_InContainer`；MF 调用 pin 名带 ` (S)` 后缀；`PackageSavedHash` 用 AR 哈希会滞后 → 改文件 mtime:size；BP 新建时 `ParentClass` 走创建参数；变量默认值编译后进 CDO（导出读 CDO、push 同时写 CDO）；本地化的 `Default` 分类；`bp_component_set_prop` 参数名冲突；负数被加引号；中文参数名 id；`transcode_apply` 部分失败要回 `error`；`refresh_callers` 对新建资产不能当失败。
- Niagara 实机：Shadetest 无 Niagara 资产，用 `niagara_system_create`+`niagara_emitter_create` 造 `NS_Smoke` 验证通过（改输入/重置/禁用启用/renderer 属性/user 参数，compile 通过，pull 一致）。过程中修掉：模块本地值存在 rapid-iteration 参数里（不是 override pin）→ 导出与 `ns_module_input_set/reset` 走 RI 路径（RI 名 `Constants.<Emitter>.<Function>.<Input>` 自己拼，引擎函数未导出）；`SetVariables_<hash>` 归一为 `SetVariables`，其赋值从 `AssignmentTargets/DefaultValues` 反射读；渲染器 `*Binding`/`TypeDefHandle` 属性不进文本；`/Engine/Transient` 父发射器视为无父；Niagara 类型名用 `float/int/bool/Vector2/Vector/Vector4/Color/Position/Quat`（`NiagaraFloat` 等结构名也接受）；`niagara_modules.json` 用 AssetRegistry 补全 228 个模块名（未加载的无输入表，lint 跳过输入校验）；renderer 属性 lint 现在校验枚举值。
- 实机没验到的：`ns_module_move`（v1 不支持）、换 renderer 材质、`@renamed`（仅单测）、Niagara emitter 资产（只读）。
- 本机 `C:\Users\Administrator\.cursor\skills\ue-node-nexus-mcp\SKILL.md` 已同步为仓库版。

**桥接崩溃加固（依据 `d:\BaiduNetdisk\lilToon\.high-value-information\ue-mcp-bridge-crashes.md` 的 4 次实测崩溃）— 已完成并实机验证**

- 根因归类：(1) `UEditorLoadingAndSavingUtils::SavePackages/SaveDirtyPackages` 在 SCC 工程弹「无法检出」模态框，模态泵消息时下一条桥接请求嵌套执行；(2) 材质图批量改动与在途着色器编译任务取消赛跑；(3) 保存触发 `EditorValidator_Material`。文档里「非游戏线程」的推断不成立——请求一直是 `AsyncTask(GameThread)`。
- 改动：所有保存（`asset_save`、`editor_save_all`、create/move/duplicate/EnhancedInput/Landscape 的 `save=true`、VFX `SaveAssetPackage`）统一走 `Transcode::SavePackageDirect`（`UPackage::SavePackage` + `SAVE_NoError`，只读文件先返回 `save_blocked_read_only`）；`graph_patch_apply`/`ue_sync push` 改材质前 `CancelOutstandingCompilation()`；请求分发加重入守卫返回 `bridge_busy`，Python `call_bridge` 自动重试 8×0.25s；`ResolveMaterialExpressionClass` 裸类名不再先走 `LoadClass`（消除误导性的 `Class None.X` 警告），未知类返回 `unknown_node_class`；`MaterialOutputProperties()` 追加 `WorldPositionOffset / ClearCoat / ClearCoatRoughness / SurfaceThickness / FrontMaterial`（文档里「WPO 连不上」的缺口）。
- 实机：把 `MF_WS_S.uasset` 置只读后 `asset_save` 返回 `save_blocked_read_only`，编辑器存活；`editor_save_all` 无脏包 ok；三种类名拼法 dry-run 均建 3 节点，日志无 `Class None` 警告；从文本建 `M_Wpo` 并连 `out.WorldPositionOffset` 往返一致。
- SKILL.md 与 README 按 skill-authoring / readme-format 规范重写（skill 增加「编辑器安全保证」与错误码表），本机 skill 副本已更新。
- v1 已知边界（文档已写明）：Niagara emitter 资产只读、`ns_module_move` 未实现（删+加）、`@link/@dynamic` 输入只读、继承组件属性只读、BP `ParentClass` 不可改（只能在创建时指定）、Event/Stage 栈与宏/委托/接口只读、`SetVariables` 模块只能编辑输入不能新建、`niagara_modules.json` 只含已加载脚本（lint 对未知模块只 warning）、换 hash 方案后所有资产会一次性显示 `ue-modified`（pull 一次即清）。

三条明确不做，理由已定，别再翻案：

- **合体保持关闭。** 它当初就是为了修「格边裁水珠」，而那件事机制 B 已彻底解决
  （p95 0.0009，比 blog 好 65 倍）；合体额外给的「融团 + 互拖」是 blog 没有的加料，
  spec 第一句只认 blog。要重开得先把位移做成水珠自身的纯函数
  （`DropPos` 18→24 或 30），换一个偏离基准的效果，不值。
- **Soft 不做水雾。** 结构上放不进去，见 `RainGlass-Align.md §2`。
- **写实档不做湿区柔化。** `MSM_Unlit` 给不出 Roughness 针，换 DefaultLit 会撞缩略图 D3D12 AV。

一条留着当已知偏差：摆动相位是对 blog 最大的单项残差（rms 0.076），
换回逐像素 `UV.y*20` 会降到 0.0060，但 `RainGlass-Jitter.md` 是看画面否掉它的，
观感决定不因数字翻案。

## 2. 已完成

**接缝（两种，都定论了）**

- 面边接缝：整条来自 `Praw` 邻面项，带宽 = `接缝宽度` 一比一。立方体上可做到零接缝。
  `接缝宽度` 8 → 0.5。见 `RainGlass-Seams.md`。
- 格边接缝（每颗水珠自己的边界）：真凶是邻列 `colShift` 用错行框（38.6% 幽灵珠）
  和合体 y 的观察者相关性。x 位置不是主因（会望远镜掉）。修好前者后跨界跳变
  p95 **0.1435 → 0.0009**，比 blog 好 65 倍。合体关闭消掉后者。见 `RainGlass-CellSeams.md`。

**blog 对齐**

- 水雾接回 `DropMask`（不进 `PNOMask`）：Realistic 覆盖率 0.046 → 0.068 = blog 的 0.068。
  **Soft 结构上放不了水雾**（`DropMask` 只经 rim/高光出画，两者都被 `DropNormal` 把关，
  水雾进不了 `DropNormal`），已把 Soft 的 `MistAmount` 接常数 0 并撤掉参数。
- 高光光向归一化 + 补回 `+1e-4` 保护（修前只有 blog 的 0.312 倍）。
- 残余 rms 0.0761，主要来源是摆动相位（换回 blog 摆动是 0.0060）。见 `RainGlass-Align.md`。

**性能（每像素）**

| | 前 | 后 | 倍数 |
|:---|:---|:---|:---|
| DropLayer2 | 18 | 6 | 3.0× |
| DropPos | 54 | 18 | 3.0× |
| N13 哈希 | 57 | 19 | 3.0× |
| DDX/DDY 对 | 75 | 13 | 5.8× |

- 三面 RainField 降到一面（按 W 选 UV 和切线框），RainFaces 节点 113 → 72。
- `SoftDisc` 的 `PixelWidth` 由调用方传入，一个度量一次 footprint，且取 `frac` 之前求导。
- `MF_WS_S` 同号保护换成 `+1e-6`（省约 144 条/像素）。
- Soft 的 `湿痕折射` / `粘滞程度` / `水雾强度` 接常数 0，整条 smear 链、三个 sticky lerp、
  整个 StaticDrops 分支被折掉（代价：这三个参数不再暴露在 Soft 上）。

新参数：`合体强度`（两档）、`水雾强度`（只在 Realistic）。
`玻璃缩放` 线上值（Soft 500 / Realistic 800）写回脚本。全项目 `asset_compile` 0 error。

- 80 分检查点：Water-Stains `d0cedda`，本仓 `118f417`。

## 3. 高价值信息索引

- `.plan/Transcode-Layer.md`：文本镜像的完整设计（格式规范、状态矩阵、plan 动词表、C++/Python 改动面、验收闸门）。实现与设计的偏差：材质输出用隐含 `out` 节点而非 `[outputs]` 段；`[graph]` 段内节点行与连线行混排；`entry`/`result` 在函数段里隐含不打印；Niagara 渲染器走 `[renderers Emitter]` 段的声明行；schema key 由 UE 端 `bridge_capabilities_get.schema_key` 给出。
- 文本镜像的不变量测试：`tests/transcode/`（fixtures 即 raw 契约样例；`test_diff_plan.py::test_pull_then_push_is_a_noop_for_every_kind` 是核心闸门）。UE 端 raw 契约以 `transcode/raw_*.py` 的 docstring 为准。
- `.high-value-information/RainGlass-Spec.md` / `RainGlass-Seams.md`（面边）/ `RainGlass-CellSeams.md`（格边）/ `RainGlass-SoftEdge.md` / `RainGlass-Coalesce.md` / `RainGlass-Jitter.md` / `RainGlass-Presets.md` / `RainGlass-Align.md`
- 探针：`Water-Stains/Scripts/probe_align_seam.py`（面边带宽 + blog/UE 场对比 + 求值计数 + 格边跳变）
- **两种接缝要分清**：面边接缝是沿面边一条固定带；格边接缝在面正中间也有、跟着水珠走、时隐时现。
- **禁止 `build_rain_chain.py` 一次跑整链**：源码管理检出失败会弹模态框，
  走保存全部脏包路径 → 缩略图 → D3D12 AV 崩编辑器。逐个脚本跑，每步确认进程还在。
  这材质在编辑器里反复重编/截图也会撞同一个 AV（渲染线程），本次一共崩了三次。
- **别用视口截图判断观感差异**：同参数两帧就有 80% 像素差 >2/255（云在动 + 视口不确定），
  噪声底和信号一样大。`probe_capture_diff.py` 量过。判断走图和探针，不走画面。
- **重建必须顺链走到底**：改一个 MF 会作废上游指向它的函数调用节点，即使输出列表没变。
- 三面降一面只在 W 是 one-hot 时成立（硬法线轴对齐盒子）。`接缝宽度` 要保持小，
  yaw 45° 时 W 会摊开 20%，那时这条优化就不成立了。
- `colShift = N(id.x)` 是逐列的，**任何跨列取水珠都必须用邻列自己的行框**，否则画出幽灵珠。
- 面内必须线性 UV。互拉不能吃竖直距，否则滑的会把团拆开。
- 禁：FunctionInput PreviewValue；SceneColor 后 save；mask 进 Opacity；DDX(WorldPosition)
