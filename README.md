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
| **验证范围** | MCP 工具注册、AutoIndex、asset/folder 管理、Project Input mappings、Material/Blueprint graph 读写、Blueprint components、Material Instance 参数、Level material usage、UE 5.5 插件构建 |

> [!IMPORTANT]
> **跨版本使用**
>
> Unreal 插件二进制与引擎版本、编译器和模块 ABI 绑定。跨 UE 版本使用时请优先按源码重新编译；若 UE API 在目标版本发生变化，需要按编译错误调整插件源码。

---

## 功能概览

| 功能 | 说明 |
|:-----|:-----|
| **固定 MCP 工具** | Python MCP Server 默认暴露 59 个类型化工具，向 UE 桥接器转发经过校验的请求载荷 |
| **UE 编辑器桥接** | UE 5.5 编辑器插件通过本地 HTTP 端点 `http://127.0.0.1:8765/mcp` 提供服务 |
| **图快照读取** | `graph_snapshot_get` 支持 `wires_tiny`、`wires_min`、`wires`、`compact`、`full` 五种格式 |
| **高密度整图读取** | `graph_node_info_get` 支持 `indexed` 和 `grouped`，一次返回整张 Material/Blueprint graph 的节点、参数和连线 |
| **图安全写入** | `graph_patch_apply` 编辑 Blueprint pin 或 Material Expression 连线，返回差异、引脚完整性、编译状态和脏标记 |
| **节点参数读写** | `node_params_get` 和 `node_params_set` 支持 alias/真实 ID，稳定导出默认值、枚举、布尔、对象引用和空字符串 |
| **材质节点类枚举** | `material_expression_classes_list` 枚举已加载的 `UMaterialExpression` 子类并返回可编辑属性 schema 统计 |
| **Material Instance 参数** | `material_instance_params_get` 和 `material_instance_params_set` 读写标量、向量、纹理和静态开关参数 |
| **AutoIndex** | `auto_index_*` 在 UE 内维护持久资产/文件夹索引，默认返回 indexed/text/count/cursor |
| **资产管理** | `asset_move`、`asset_rename`、batch、duplicate、delete、folder、redirector 工具覆盖 Content Browser 清理闭环 |
| **Level material usage** | 枚举当前 Level 网格实例、Actor transform、UObject 属性、material slot、Material Instance 参数和材质使用点 |
| **Project Input** | 读写 legacy Project Settings action/axis mappings，用于 Blueprint 输入链路验证 |
| **复杂材质复刻** | 已验证可通过固定 MCP 接口读取整图、创建节点、回放参数、连接根输出、编译并保存 |
| **蓝图摘要/组件** | `blueprint_details_get` 读取类元数据、变量、CDO 默认值和组件模板；`blueprint_components_patch` 写入 Blueprint SCS 组件树 |
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
| **关卡** | `level_actor_get()` | 读取当前 Level 内 Actor 类型、路径、label、组件摘要 |
| **关卡** | `level_actor_transform_get()` / `level_actor_transform_set()` | 读取或写入 Actor 世界 transform |
| **关卡** | `object_properties_get()` / `object_properties_set()` | 读取或写入 UObject 属性，默认只写可编辑属性 |
| **关卡材质** | `level_mesh_instances_list()` | 枚举当前 Level 网格实例、组件路径和材质槽摘要 |
| **关卡材质** | `component_materials_get()` / `component_materials_set()` | 读取或替换网格组件 material slot |
| **关卡材质** | `material_interface_resolve()` | 解析 Material Interface、Material Instance parent chain 和 root material |
| **关卡材质** | `material_usage_find()` | 查找当前 Level 或资产中的 material usage |
| **关卡材质** | `component_material_instance_params_get()` / `component_material_instance_params_set()` | 读写组件 material slot 上的 MID/MI 参数 |
| **Project Input** | `project_input_mappings_get()` | 读取 legacy Project Settings action/axis mappings |
| **Project Input** | `project_input_mappings_patch()` | 批量添加或移除 legacy action/axis mappings，可保存配置 |
| **蓝图** | `blueprint_details_get()` | 读取蓝图元数据、变量、CDO 默认值和组件 |
| **蓝图** | `blueprint_components_patch()` | 批量添加或移除 Blueprint SCS 组件并可编译检查 |
| **蓝图** | `anim_blueprint_summary_get()` | 读取常用 AnimGraph 节点的紧凑语义摘要 |
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
| **诊断** | `asset_compile()` | 编译蓝图或材质资产，返回诊断信息 |
| **诊断** | `asset_validate()` | 校验资产，返回机器可读的结果 |
| **诊断** | `diagnostics_get()` | 读取最近的桥接器诊断信息 |
| **保存** | `asset_save()` | 保存单个资产包，报告脏标记/只读/编辑器冲突状态 |

> [!NOTE]
> **默认工具面**
>
> 代码层保留 65 个固定 operation；默认 MCP 工具面注册 59 个。`auto_index_disable`、`auto_index_flush`、`auto_index_clear`、`auto_index_diff_registry`、`graph_node_info_get_w_pos`、`node_position_offset` 作为高级/兼容入口保留在 Python wrapper 和 UE bridge operation 中，但不进入默认 MCP 工具列表。

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

所有 MCP 响应根对象会附带一个整数 `remaining_errors`，`0` 表示当前没有登记的剩余错误，正整数表示仍有待处理错误数量。

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
| **GameMode 配置** | `Config/DefaultEngine.ini` 中 `GlobalDefaultGameMode` 指向 `BP_MCP_3CGameMode_C` |
| **组件链路** | `Character` native root 下添加 `SpringArmComponent` 和 `CameraComponent` |
| **输入配置** | `MoveForward`、`MoveRight`、`Turn`、`LookUp` axis mappings 和 `Jump` action mapping 已通过 MCP 写入 `Config/DefaultInput.ini` |
| **图链路** | 轴输入驱动移动/视角，空格键驱动 `Jump` / `StopJumping` |
| **整图读回** | `graph_node_info_get(include_position=true)` 返回 16 个节点、12 条关键连线 |
| **编译结果** | Character、PlayerController、GameMode 均为 `0 error / 0 warning` |
| **保存结果** | Blueprint assets 和项目 config 均通过 MCP 保存/落盘 |

---

## 目录结构

```
UE-Node-Nexus-MCP/
├── Plugins/
│   └── UeNodeNexusBridge/
│       ├── Source/
│       ├── Resources/
│       └── UeNodeNexusBridge.uplugin
├── src/
│   └── ue_node_nexus_mcp/
├── pyproject.toml
└── README.md
```

---

## 运行

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

---

## Plugin 编译

把源码插件作为 Project Plugin 放到项目目录，让 Unreal Build Tool 随项目编译：

```
YourProject/
└── Plugins/
    └── UeNodeNexusBridge/
        ├── Source/
        ├── Resources/
        └── UeNodeNexusBridge.uplugin
```

然后右键 `.uproject` 生成项目文件，或直接打开项目触发 UE 的插件编译提示。该方式适合不同 UE 5.x 项目各自编译自己的插件二进制。

也可以把同一目录作为 Engine Plugin 放到目标引擎的 `Engine/Plugins/Marketplace/UeNodeNexusBridge/` 下，再用该引擎重新编译插件。

---

## 参考项目

- **[`bunkerboy258/ue-blueprint-dumper`](https://github.com/bunkerboy258/ue-blueprint-dumper)** — 蓝图 CDO/默认值/组件检查和动画蓝图语义摘要功能的参考

---

<div align="center">

**平台:** Windows x64 | **引擎:** Unreal 5.5 | **许可证:** MIT

</div>
