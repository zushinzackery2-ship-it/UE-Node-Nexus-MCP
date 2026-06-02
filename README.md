<div align="center">

# UE Node Nexus MCP

**Unreal Engine Material 与 Blueprint Graph 工作流的 MCP 桥接工具**

*类型安全、紧凑、编译感知的固定工具集，无需模型自行编写 Python 脚本*

![C++](https://img.shields.io/badge/C%2B%2B-20-blue?style=flat-square)
![Python](https://img.shields.io/badge/Python-3.11%2B-green?style=flat-square)
![Unreal](https://img.shields.io/badge/Unreal-5.5-black?style=flat-square)
![Platform](https://img.shields.io/badge/Platform-Windows%20x64-lightgrey?style=flat-square)
![License](https://img.shields.io/badge/License-MIT-lightgrey?style=flat-square)

</div>

---

> [!NOTE]
> **仓库边界**
>
> 本项目是 Unreal Editor 的 node graph MCP 桥接器，聚焦于 asset discovery、Material/Blueprint graph 检查与编辑、compile diagnostics、Material Instance 参数读写以及安全的 package save 流程。不包含场景布局自动化和任意 Python 执行功能。

---

## 测试边界

| 项目 | 说明 |
|:-----|:-----|
| **实测引擎** | UE 5.5 Launcher Windows x64 |
| **源码兼容预期** | UE 5.x 同类编辑器环境通常只需要放入目标引擎/项目后重新编译 |
| **二进制边界** | 预编译插件不承诺跨 UE 小版本通用；更换 UE 版本后应重新编译插件 |
| **验证范围** | MCP 工具注册、AutoIndex、asset/folder 管理、Project Input mappings、Material/Blueprint graph 读写、Blueprint components、Material Instance 参数、Level material usage、Niagara 通用 System/Emitter/Renderer/Module Stack authoring、UE 5.5 插件构建 |
| **最终 UE5.5 验收** | `bridge_contract_check` 合同一致、六面映射大坝材质函数接入、Niagara 炉火资产、3C Blueprint 工作流均已在 UE 5.5 Launcher 实机通过 |

> [!IMPORTANT]
> **跨版本使用**
>
> Unreal 插件二进制与引擎版本、编译器和模块 ABI 绑定。跨 UE 版本使用时请优先按源码重新编译；若 UE API 在目标版本发生变化，需要按编译错误调整插件源码。

---

## 功能概览

| 功能 | 说明 |
|:-----|:-----|
| **固定 MCP 工具** | Python MCP Server 只暴露 6 个 facade 工具，向 UE 桥接器转发经过校验的请求载荷 |
| **MCP Facade** | 完整 UE 能力作为内部 operation registry 按需查询和执行；Niagara operation 只在 UE 端实际可用时进入 capability |
| **UE 编辑器桥接** | UE 5.5 编辑器插件通过本地 HTTP 端点 `http://127.0.0.1:8765/mcp` 提供服务 |
| **图快照读取** | `graph_snapshot_get` 支持 `wires_tiny`、`wires_min`、`wires`、`compact`、`full` 五种格式 |
| **高密度整图读取** | `graph_node_info_get` 支持 `indexed` 和 `grouped`，一次返回整张 Material/Blueprint graph 的节点、参数和连线 |
| **图安全写入** | `graph_patch_apply` 编辑 Blueprint pin 或 Material Expression 连线，返回差异、引脚完整性、编译状态和脏标记 |
| **节点参数读写** | `node_params_get` 和 `node_params_set` 支持 alias/真实 ID，稳定导出默认值、枚举、布尔、对象引用和空字符串 |
| **材质节点类枚举** | `material_expression_classes_list` 枚举已加载的 `UMaterialExpression` 子类并返回可编辑属性 schema 统计 |
| **Material Instance 参数** | `material_instance_params_get` 和 `material_instance_params_set` 读写标量、向量、纹理和静态开关参数 |
| **Niagara 工具组** | 由独立 `UeNodeNexusNiagaraBridge` 插件承载；仅当 UE 端启用 Niagara 插件并加载 Niagara bridge 模块时暴露 System、Emitter、Module Stack、Renderer、User 参数、材质、lint 和编译工具 |
| **AutoIndex** | `auto_index_*` 在 UE 内维护持久资产/文件夹索引，默认返回 indexed/text/count/cursor |
| **资产管理** | `asset_move`、`asset_rename`、batch、duplicate、delete、folder、redirector 工具覆盖 Content Browser 清理闭环 |
| **Level material usage** | 枚举当前 Level 网格实例、Actor transform、UObject 属性、material slot、Material Instance 参数和材质使用点；不提供关卡 Actor 实例化或 Actor transform/任意 UObject 属性写入 |
| **Project Input** | 读写 legacy Project Settings action/axis mappings，用于 Blueprint 输入链路验证 |
| **复杂材质复刻** | 已验证可通过固定 MCP 接口读取整图、创建节点、回放参数、连接根输出、编译并保存 |
| **蓝图摘要/组件** | `blueprint_details_get` 默认读取类元数据和变量，可显式包含 CDO 默认值和组件模板；`blueprint_components_patch` 写入 Blueprint SCS 组件树 |
| **动画蓝图摘要** | `anim_blueprint_summary_get` 提取常用 AnimGraph 节点的紧凑语义摘要 |
| **资产创建** | `asset_create` 支持 Material、Material Instance Constant 和 Blueprint 资产的固定创建流程 |
| **编译与保存** | `asset_compile`、`asset_validate`、`asset_save` 返回结构化诊断和包状态 |

---

## 核心 API

| 分类 | API | 说明 |
|:-----|:----|:-----|
| **资产** | `asset_list()` | 列出 Unreal 资产，支持紧凑和完整格式 |
| **资产** | `asset_get()` | 读取单个资产的元数据 |
| **资产** | `asset_create()` | 创建 Material、Material Instance 或 Blueprint 资产，支持试运行 |
| **资产** | `asset_delete()` | 删除资产并可清理删除后的 registry/disk 状态 |
| **资产** | `asset_move()` / `asset_rename()` | 移动或重命名单个资产，可保存并修复 redirector |
| **资产** | `asset_move_batch()` / `asset_rename_batch()` | 批量移动或重命名，支持逐项结果和 `continue_on_error` |
| **资产** | `asset_duplicate()` | 复制资产到目标 object path |
| **资产** | `folder_create()` / `folder_delete()` | 创建或删除 Content Browser 文件夹 |
| **资产** | `asset_redirectors_fixup()` | 修复指定文件夹下 redirector |
| **AutoIndex** | `auto_index_enable()` | 开启 UE 内持久资产索引监听 |
| **AutoIndex** | `auto_index_status()` / `auto_index_rebuild()` | 查看索引状态或按 root 重建索引 |
| **AutoIndex** | `auto_index_overview()` / `auto_index_tree_get()` | 低上下文查看资产分类统计和文件夹树 |
| **AutoIndex** | `auto_index_query()` / `auto_index_get()` | 按文本、class、路径查询资产或读取单个索引记录 |
| **AutoIndex** | `auto_index_resolve_path()` | 用短名、包路径或 object path 解析资产 |
| **关卡** | `level_current_get()` | 读取当前编辑器关卡标识和脏标记状态 |
| **关卡** | `level_actors_list()` | 列出当前关卡的 Actor，可选包含组件行 |
| **关卡** | `level_actor_get()` | 读取当前 Level 内 Actor 类型、路径、label，可显式包含组件摘要 |
| **关卡** | `level_actor_transform_get()` | 读取 Actor 世界 transform |
| **关卡** | `object_properties_get()` | 读取 UObject 属性，用于诊断和定位，不提供任意属性写入 |
| **关卡材质** | `level_mesh_instances_list()` | 枚举当前 Level 网格实例、组件路径和材质槽摘要 |
| **关卡材质** | `component_materials_get()` / `component_materials_set()` | 读取或替换网格组件 material slot |
| **关卡材质** | `material_interface_resolve()` | 解析 Material Interface、Material Instance parent chain 和 root material |
| **关卡材质** | `material_usage_find()` | 查找当前 Level 或资产中的 material usage |
| **关卡材质** | `component_material_instance_params_get()` / `component_material_instance_params_set()` | 读写组件 material slot 上的 MID/MI 参数 |
| **Project Input** | `project_input_mappings_get()` | 读取 legacy Project Settings action/axis mappings |
| **Project Input** | `project_input_mappings_patch()` | 批量添加或移除 legacy action/axis mappings，可保存配置 |
| **项目诊断** | `project_context_get()` | 读取当前 UE 项目路径、命令行、Content 目录和 `/Game` mount 检查 |
| **蓝图** | `blueprint_details_get()` | 读取蓝图元数据和变量，可显式包含 CDO 默认值和组件；`include_inherited_components` 沿父蓝图链回填继承 SCS 组件并标 `origin` |
| **蓝图** | `blueprint_components_patch()` | 批量添加或移除 Blueprint SCS 组件并可编译检查 |
| **蓝图** | `anim_blueprint_summary_get()` | 读取常用 AnimGraph 节点的紧凑语义摘要 |
| **动画** | `anim_montage_summary_get()` | 读取 AnimMontage 的 Section/Slot/Segment/Notify 结构化时间数据 |
| **动画** | `blend_space_summary_get()` | 读取 BlendSpace 轴范围（名/Min/Max/Grid）和动画采样点 |
| **Cascade** | `cascade_system_summary_get()` | 只读 Cascade 粒子系统的 Emitter、TypeData 和模块栈（旧粒子迁移读取入口）|
| **图** | `graph_snapshot_get()` | 读取材质或蓝图图拓扑，默认格式为 `wires_tiny` |
| **图** | `graph_node_info_get()` | 读取整张图的高密度节点信息，默认 `indexed`，可用 `include_position=true` 附带坐标表 |
| **图** | `graph_patch_apply()` | 应用声明式图编辑，附带写后检查 |
| **图** | `graph_build_apply()` | 用 `nodes`、`links`、`material_outputs` 一次创建节点、写参数、连线并编译 |
| **节点** | `node_info_get()` | 读取单个节点的紧凑编辑视图，可按 section/index 精确截取 |
| **节点** | `node_create()` | 创建材质节点并返回完整节点编辑视图 |
| **节点** | `node_position_get()` | 读取节点坐标 |
| **节点** | `node_position_set()` | 设置节点坐标 |
| **节点参数** | `node_class_params_get()` | 按节点类型读取可编辑参数模板 |
| **节点参数** | `node_params_get()` | 读取单个图节点的可编辑参数 |
| **节点参数** | `node_params_set()` | 写入节点参数，附带编译诊断 |
| **材质节点类** | `material_expression_classes_list()` | 枚举材质表达式节点类及其可编辑属性 schema 数量 |
| **Material Instance** | `material_instance_params_get()` | 读取 Material Instance 参数值 |
| **Material Instance** | `material_instance_params_set()` | 写入 Material Instance 参数，附带类型校验 |
| **Niagara** | `niagara_system_create()` / `niagara_system_duplicate()` | 创建空 Niagara System 或复制已有 System |
| **Niagara** | `niagara_system_summary_get()` / `niagara_asset_lint()` | 读取 System 摘要，或执行不写资产的通用 Niagara authoring 风险检查 |
| **Niagara** | `niagara_system_properties_get()` / `niagara_system_properties_set()` | 读写可编辑 System 属性 |
| **Niagara** | `niagara_emitter_create()` / `niagara_emitters_list()` | 创建 default/empty/from_asset Emitter 并枚举 Emitter |
| **Niagara** | `niagara_emitter_properties_get()` / `niagara_emitter_properties_set()` | 读写 Emitter 名称、启用状态、local space、determinism、random seed 等紧凑属性 |
| **Niagara** | `niagara_modules_list()` | 按 Emitter 和 Usage 枚举 module stack，默认返回 compact columns/items |
| **Niagara** | `niagara_module_add()` / `niagara_module_remove()` / `niagara_module_set_enabled()` | 添加已有 Niagara Module Script，删除模块并保持参数图链路，启用或禁用单个模块 |
| **Niagara** | `niagara_module_inputs_get()` / `niagara_module_inputs_set()` | 读取或写入指定 Module 的可编辑输入参数 |
| **Niagara** | `niagara_renderer_create()` / `niagara_renderers_list()` | 为 Emitter 创建 Sprite/Ribbon/Mesh/Light/Component/Decal/Volume Renderer 并枚举 |
| **Niagara** | `niagara_renderer_properties_get()` / `niagara_renderer_properties_set()` | 读写 Renderer UObject 可编辑属性 |
| **Niagara** | `niagara_user_params_get()` / `niagara_user_params_set()` | 读取或写入 Niagara User 参数，支持 float/int/bool/vector/color/material |
| **Niagara** | `niagara_materials_get()` / `niagara_materials_set()` | 读取或替换 Sprite、Ribbon、Mesh Renderer 材质 |
| **Niagara** | `niagara_compile()` | 请求 Niagara System 编译并返回 ready/needs_compile/readiness_issue_count |
| **桥接诊断** | `bridge_capabilities_get()` | 读取已加载 UE bridge 模块实际支持的 operation、插件模块和版本信息 |
| **桥接诊断** | `bridge_contract_check()` | MCP 本地诊断包装器；用当前 Python 合同调用 `bridge_capabilities_get` 并检查 UE bridge operation 缺失、额外和模块加载状态 |
| **诊断** | `asset_compile()` | 编译蓝图或材质资产，返回诊断信息 |
| **诊断** | `asset_validate()` | 校验资产，返回机器可读的结果 |
| **诊断** | `diagnostics_get()` | 读取最近的桥接器诊断信息 |
| **保存** | `asset_save()` | 保存单个资产包，报告脏标记/只读/编辑器冲突状态 |

---

## MCP Facade

MCP 公开面固定为 6 个 facade 工具，用少量入口承载完整 UE operation 能力，降低 `list_tools` 和历史 tool result 对上下文窗口的占用。底层 asset、graph、material、Niagara、level、project operation 保留为内部 registry 能力，通过 facade 查询和执行。

| 工具 | 说明 |
|:-----|:-----|
| **`ue_context_get()`** | 返回启用 group、facade 工具清单和推荐下一跳 |
| **`ue_capability_get()`** | 按 group 或 operation 查询内部 operation 索引/schema |
| **`ue_execute()`** | 通过 operation registry 执行内部 UE operation，默认返回 delta summary |
| **`ue_read()`** | 统一读取 asset、graph、node、diagnostics、Niagara 等常见状态，默认 summary/index |
| **`ue_diff_get()`** | 按 diff token 读取 compact changes 和诊断计数 |
| **`ue_plan_validate()`** | 验证一批 operation 的风险、错误和预计变更，不写 UE 状态 |

Facade 不删除现有能力，也不新增任意 Python 或反射写入入口。内部 operation registry 覆盖现有 92 个 operation，并保留 group、read/write、risk、bridge/local、hidden、默认响应粒度等元数据。写 operation 默认 `delta`，读 operation 默认 `summary`；完整 bridge envelope 需要显式 `response.mode="full"` 或 `debug`。

推荐 thin 工作流：

```text
ue_context_get()
  -> ue_capability_get(group="graph", detail="index")
  -> ue_capability_get(operation="node_params_set", detail="schema")
  -> ue_execute(operation="node_params_set", payload={...})
  -> ue_diff_get(since_token="diff_...")
```

启动 MCP server：

```bash
ue-node-nexus-mcp --response-mode minimal
```

MCP client 配置：

```json
{
  "mcpServers": {
    "ue-node-nexus": {
      "command": "ue-node-nexus-mcp",
      "args": ["--response-mode", "minimal"],
      "env": {
        "UE_NEXUS_BRIDGE_URL": "http://127.0.0.1:8765"
      }
    }
  }
}
```

也可以通过环境变量配置响应模式：

```json
{
  "env": {
    "UE_NEXUS_RESPONSE_MODE": "minimal"
  }
}
```

完整设计见 `docs/MCP_THIN_FACADE_DESIGN.md`。

> [!NOTE]
> **Niagara 通用边界**
>
> Niagara MCP 能力由独立 UE 插件 `UeNodeNexusNiagaraBridge` 提供，是否可用以 UE 端 `bridge_capabilities_get().data.modules.niagara_available` 为准。当前内部 operation 覆盖创建/复制 System、创建 Emitter、枚举/添加/删除/启停 Module Stack、读写 Module Input、创建 Renderer、读写 System/Emitter/Renderer 属性、读写 User 参数、替换 Renderer 材质、lint、编译和保存。Niagara 能力保持通用 authoring 原语，不提供 `create_fire_effect` 这类按具体效果命名的模板工具。

> [!NOTE]
> **默认工具面**
>
> MCP 公开面固定且只注册 6 个 facade 工具：`ue_context_get`、`ue_capability_get`、`ue_execute`、`ue_read`、`ue_diff_get`、`ue_plan_validate`。代码层保留固定 operation registry；当前 Python 合同为 92 个内部 operation，其中 91 个转发到 UE bridge，`bridge_contract_check` 是 MCP 本地诊断包装器，不是 UE bridge HTTP operation。底层 asset、graph、material、Niagara、level、project operation 不进入 MCP `list_tools`，只能通过 facade 查询和执行。

---

## 返回格式

| 格式 | 用途 |
|:-----|:-----|
| **`wires_tiny`** | 默认图快照格式。最小节点字典、边表和类型统计，低上下文占用 |
| **`wires_min`** | 精简的连线文本，含节点字典和边行 |
| **`wires`** | 人类可读的水平连线表 |
| **`compact`** | 列+行数组，带别名映射回真实 UE 节点和引脚标识符 |
| **`full`** | 详细的一节点一对象格式，用于调试 |
| **`indexed`** | 整图复刻包，使用 `T/P/N/V/E/X/R` 字典行压缩类型、参数名、节点、值、边、位置和真实 ID |
| **`grouped`** | 人读整图包，按节点类型聚合；每个节点包含 `p[...]` 参数和 `i[...]` 输入连线 |
| **`node_info_text`** | 单节点编辑视图，包含节点名、类、短 ID、真实 ID、位置、输入、参数和输出 |

Niagara 读取工具默认使用低上下文格式：`niagara_system_summary_get`、`niagara_emitters_list`、`niagara_renderers_list`、`niagara_user_params_get`、`niagara_materials_get` 默认 `indexed`，可切到 `tiny` 或 `full`。`niagara_asset_lint(format="indexed")` 返回 `G/S/C/I` 文本：`S` 是严重级别计数，`C` 是 issue code 字典，`I` 是 `严重级别:code索引:位置` 行；`format="full"` 才返回完整 issue 对象。

默认响应按有效信息压缩：图快照默认 `wires_tiny`，整图信息默认 `indexed`，Niagara 查询默认 `indexed` 或 `summary`，详细数组、真实 pin GUID 和完整 issue 对象仅在显式 `format="full"` 或对应 include flag 打开时返回。验收脚本需要精确连线时只做局部 `full` snapshot，不改变 MCP 工具的默认低上下文输出。

所有 MCP 响应根对象末端会附带一个整数 `remaining_errors`，默认 `0`。该值由当前响应实时计算：结构化 `error`、`error/fatal` 级 diagnostics、嵌套 `error_count` 会计入总数。`remaining_errors` 是根级保留字段，嵌套同名字段会被 wrapper 移除，避免业务 data 与全局错误汇总混用。

> [!NOTE]
> **节点参数语义**
>
> 材质节点参数读取按 UE 可编辑属性表导出，保留默认值、枚举、布尔、FName、对象引用和空字符串。`node_info_get(..., section="param", index=N)` 的索引与 `node_class_params_get()` 的模板顺序一致。当前运行时 all-class schema 验证覆盖 UE 5.5 已加载的 `329/329` 个非抽象、非废弃 `UMaterialExpression` 子类。

> [!NOTE]
> **graph_build_apply 紧凑写图**
>
> `graph_build_apply` 支持紧凑 spec：`nodes` 里用 `id`、`class_path`/`node_class`、`x/y`、`params` 声明节点；`links` 里可用 `from/to` 的 `node.pin` 简写，例如 `base_color.RGB -> MaterialOutput.BaseColor`；`params` 可直接传结构化 JSON 值。Vector 输出支持 `RGB/RGBA/Color/Vector/R/G/B/A/X/Y/Z/W` 别名，标量输出支持 `Value/Out/Output/Result` 别名；无效 pin 诊断会返回可用输出候选。

> [!IMPORTANT]
> **写入安全**
>
> 写入工具默认 `dry_run=true`。实际写入时，单个响应中返回已应用的差异、引脚完整性、编译结果、脏标记、诊断和警告信息。

---

## 复刻验证

| 验证项 | 结果 |
|:-----|:-----|
| **源材质** | `/Game/YN/Material/大坝母材质.大坝母材质` |
| **目标材质** | `/Game/YN/Material/大坝母材质MCPtest_full_1779212723.大坝母材质MCPtest_full_1779212723` |
| **执行路径** | `asset_create`、`graph_node_info_get(include_position=true)`、`graph_patch_apply(create_node)`、`node_params_set`、`graph_patch_apply(connect_pins)`、`asset_compile`、`asset_save` |
| **真实表达式节点** | 源图 `85`，目标图 `85` |
| **连线** | 源图 `110`，目标图 `110` |
| **带参数节点** | 源图 `78`，目标图 `78` |
| **根输出** | `MaterialOutput.MaterialAttributes` 已连接 |
| **精确比对** | 节点类型、参数值、连线、位置按索引一致 |
| **编译结果** | `0 error / 0 warning` |
| **保存结果** | 目标材质显式保存成功 |

> [!IMPORTANT]
> **复刻边界**
>
> 该验证只使用固定 MCP 工具读取源图数据并在目标材质中重建节点、参数和连线；没有从源材质直接复制 `UMaterialExpression`、GraphNode 或 UObject 指针。

---

## Blueprint 3C 验证

| 验证项 | 结果 |
|:-----|:-----|
| **Character** | `/Game/MCP_3C/BP_MCP_3CCharacter.BP_MCP_3CCharacter` |
| **PlayerController** | `/Game/MCP_3C/BP_MCP_3CPlayerController.BP_MCP_3CPlayerController` |
| **GameMode** | `/Game/MCP_3C/BP_MCP_3CGameMode.BP_MCP_3CGameMode` |
| **组件链路** | `Character` 组件树添加 `CameraBoom` SpringArm 和 `FollowCamera` Camera，并通过 `blueprint_details_get` 读回验证 |
| **输入配置** | `MoveForward`、`MoveRight`、`Turn`、`LookUp` axis mappings 和 `Jump` action mapping 已通过 MCP 写入 `Config/DefaultInput.ini` |
| **图链路** | `SpaceBar` 输入节点的 `Pressed` / `Released` exec pin 分别连接 `Jump` / `StopJumping` |
| **精确连线** | `node_create` 默认返回压缩文本；验收脚本显式读取一次局部 `graph_snapshot_get(format="full")` 获取真实 pin GUID 后连线 |
| **编译结果** | Character、PlayerController、GameMode 均为 `0 error / 0 warning` |
| **保存结果** | Blueprint assets 和 Project Input config 均通过 MCP 保存/落盘 |

---

## Widget Blueprint 一致性验证

| 验证项 | 结果 |
|:-----|:-----|
| **UUserWidget 子类蓝图** | 创建、详情读取、空编译、EventGraph 节点编辑、再次编译、删除链路已走通 |
| **节点短名解析** | `node_class_params_get(K2Node_Event)` 与 `node_create(node_class="K2Node_Event")` 使用一致解析路径 |
| **Event 节点语义** | 泛型 `K2Node_Event` 缺少 `function_name/function_owner` 时返回 `node_config_required`，不再创建无语义 `事件None` |
| **Widget Blueprint SCS** | Widget Blueprint 走 SCS component patch 时返回 `blueprint_scs_unavailable`，不再误报 `blueprint_not_found` |
| **未保存资产状态** | `asset_get` 可返回已加载但 AssetRegistry 不可见的资产，并标记 `asset_registry_visible=false` 与 `package_dirty` |

---

## 目录结构

```
UE-Node-Nexus-MCP/
├── Plugins/
│   └── UeNodeNexusBridge/
│       ├── Source/
│       ├── Resources/
│       └── UeNodeNexusBridge.uplugin
│   └── UeNodeNexusNiagaraBridge/
│       ├── Source/
│       └── UeNodeNexusNiagaraBridge.uplugin
├── src/
│   └── ue_node_nexus_mcp/
├── pyproject.toml
└── README.md
```

---

## 运行

先安装 Python server：

```bash
pip install .
```

```bash
python -m ue_node_nexus_mcp.server
```

安装为 Python 包后也可以使用 console entry：

```bash
ue-node-nexus-mcp
```

| 环境变量 | 默认值 | 说明 |
|:-----|:-----|:-----|
| **`UE_NEXUS_BRIDGE_URL`** | `http://127.0.0.1:8765` | UE 桥接器端点 |
| **`UE_NEXUS_TIMEOUT_SECONDS`** | `30` | 桥接器 HTTP 超时时间（秒） |
| **`UE_NEXUS_RESPONSE_MODE`** | `minimal` | facade 响应模式，支持 `minimal` 或 `full` |

---

## MCP Client 配置

启动 UE 项目并启用 `UeNodeNexusBridge` 插件后，把 MCP client 配成 stdio 启动 Python server：

```json
{
  "mcpServers": {
    "ue-node-nexus": {
      "command": "ue-node-nexus-mcp",
      "env": {
        "UE_NEXUS_BRIDGE_URL": "http://127.0.0.1:8765",
        "UE_NEXUS_TIMEOUT_SECONDS": "30"
      }
    }
  }
}
```

如果没有安装 console entry，也可以直接用模块入口：

```json
{
  "mcpServers": {
    "ue-node-nexus": {
      "command": "python",
      "args": ["-m", "ue_node_nexus_mcp.server"],
      "env": {
        "UE_NEXUS_BRIDGE_URL": "http://127.0.0.1:8765"
      }
    }
  }
}
```

连接链路为 `MCP Client -> Python MCP Server -> UE Editor Plugin`。UE 未打开、插件未启用或端口不通时，工具会返回桥接器连接错误。

### 部署验收

连接 UE 后建议先运行 `project_context_get()` 确认 `.uproject`、Content 目录和 `/Game` mount，再运行 `bridge_capabilities_get()` 或 MCP 本地工具 `bridge_contract_check()` 确认 Python MCP、核心 bridge 插件和 Niagara bridge 插件的 operation 合同一致。脚本验收会直接调用 UE 端 `bridge_capabilities_get`，不会把 MCP 本地 wrapper 当成 UE operation。

`scripts/` 验收脚本仅存在于本地开发树、不随仓库分发；一般用户直接通过 MCP 工具执行 `project_context_get()`、`bridge_contract_check()` 和需要的 `niagara_asset_lint()` 即可完成同等验收。

最终 UE5.5 工作流验收入口会依次检查 bridge 合同、六面映射大坝材质函数接入、Niagara 炉火资产和 3C Blueprint 工作流：

```bash
python scripts/run_final_ue_acceptance.py --wait-timeout 300 --step-timeout 120 --asset-timeout 600
```

```bash
python scripts/verify_bridge_contract.py --mode enabled --json
```

源码验收脚本使用与 MCP server 相同的 feature 开关解析逻辑，支持 `UE_NEXUS_FEATURES`、`UE_NEXUS_ENABLE_FEATURES`、`UE_NEXUS_DISABLE_FEATURES`、`UE_NEXUS_NIAGARA_SUPPORT`，也支持 CLI 的 `--features`、`--enable-feature`、`--disable-feature`、`--niagara-support`。例如只验核心和资产工具：

```bash
python scripts/verify_bridge_contract.py --mode enabled --features core,asset --json
```

需要把 Niagara 资产纳入验收时：

```bash
python scripts/verify_bridge_contract.py --mode enabled --lint-niagara-asset /Game/FX/NS_Test.NS_Test --json
```

### Tool Feature 开关

默认候选工具组为 `core,asset,auto_index,graph,material,blueprint,animation,cascade,level,project_input,niagara`。关闭某组时，对应工具不会进入 MCP tool list。Niagara 由 UE 插件状态最终裁决：只有 `UeNodeNexusNiagaraBridge` 已加载且 UE Niagara 插件启用时才注册；本地配置只能关闭或表达启用意图，不能绕过 UE 插件状态强行开启。

| 配置 | 说明 |
|:-----|:-----|
| **`UE_NEXUS_FEATURES`** | 显式指定工具组，例如 `core,asset,material` |
| **`UE_NEXUS_ENABLE_FEATURES`** | 在默认或显式工具组上追加工具组 |
| **`UE_NEXUS_DISABLE_FEATURES`** | 从当前工具组中移除工具组 |
| **`UE_NEXUS_NIAGARA_SUPPORT`** | `true`/`false`，本地 Niagara 工具组意图开关；`true` 不会覆盖 UE 端 `niagara_available=false` |

示例：只暴露资产、材质和核心诊断工具：

```json
{
  "mcpServers": {
    "ue-node-nexus": {
      "command": "ue-node-nexus-mcp",
      "env": {
        "UE_NEXUS_FEATURES": "core,asset,material"
      }
    }
  }
}
```

CLI 也支持同样的开关：

```bash
ue-node-nexus-mcp --features core,asset,material
```

关闭 Niagara 工具时，在 MCP 配置中加入：

```json
{
  "env": {
    "UE_NEXUS_NIAGARA_SUPPORT": "false"
  }
}
```

---

## 安装位置

本仓库不发布预编译二进制，请从源码构建（见下方「Plugin 编译」）。构建产物部署到目标引擎或项目的 `Plugins/` 下，目录形如：

```
UE_5.5/
└── Engine/
    └── Plugins/
        ├── UeNodeNexusBridge/
            ├── Binaries/
            ├── Source/
            ├── Config/
            ├── Resources/
            └── UeNodeNexusBridge.uplugin
        └── UeNodeNexusNiagaraBridge/
            ├── Binaries/
            ├── Source/
            └── UeNodeNexusNiagaraBridge.uplugin
```

也可以放到项目目录：

```
YourProject/
└── Plugins/
    ├── UeNodeNexusBridge/
        └── UeNodeNexusBridge.uplugin
    └── UeNodeNexusNiagaraBridge/
        └── UeNodeNexusNiagaraBridge.uplugin
```

二进制随目标引擎/项目编译产生；不同 UE 5.x 版本各自保留 `Source` 重新编译即可。

---

## Plugin 编译

把源码插件作为 Project Plugin 放到项目目录，让 Unreal Build Tool 随项目编译：

```
YourProject/
└── Plugins/
    ├── UeNodeNexusBridge/
        ├── Source/
        ├── Resources/
        └── UeNodeNexusBridge.uplugin
    └── UeNodeNexusNiagaraBridge/
        ├── Source/
        └── UeNodeNexusNiagaraBridge.uplugin
```

然后右键 `.uproject` 生成项目文件，或直接打开项目触发 UE 的插件编译提示。该方式适合不同 UE 5.x 项目各自编译自己的插件二进制。

也可以把两个插件目录作为 Engine Plugin 放到目标引擎的 `Engine/Plugins/` 下，再用该引擎重新编译插件。只需要核心能力时，可以不启用 `UeNodeNexusNiagaraBridge`。

---

## 参考项目

- **[`bunkerboy258/ue-blueprint-dumper`](https://github.com/bunkerboy258/ue-blueprint-dumper)** — 蓝图 CDO/默认值/组件检查和动画蓝图语义摘要功能的参考

---

<div align="center">

**平台:** Windows x64 | **引擎:** Unreal 5.5 | **许可证:** MIT

</div>
