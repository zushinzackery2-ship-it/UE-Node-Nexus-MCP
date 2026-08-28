<div align="center">

# UE Node Nexus MCP

**Unreal Engine 编辑器的 MCP 桥接器：资产、Material/Blueprint 图、Niagara、关卡材质的类型安全读写**

*固定 6 工具 facade + 115 个内部 operation，先查 schema 再调用，无需模型自行编写 Python 脚本*

![C++](https://img.shields.io/badge/C%2B%2B-20-blue?style=flat-square)
![Python](https://img.shields.io/badge/Python-3.11%2B-green?style=flat-square)
![Unreal](https://img.shields.io/badge/Unreal-5.5-black?style=flat-square)
![Platform](https://img.shields.io/badge/Platform-Windows%20x64-lightgrey?style=flat-square)
![License](https://img.shields.io/badge/License-MIT-lightgrey?style=flat-square)

</div>

---

## 它是什么

本项目让 MCP 客户端（Claude Code、Cursor 等）安全地检查和编辑打开中的 Unreal Editor：资产管理与依赖图、Material/Blueprint 节点图读写、编译诊断与日志、Material Instance 参数、Niagara authoring、关卡材质使用点、关卡 Actor 生命周期（spawn/delete/transform，窄类型化写入）。不提供任意 Python/控制台命令执行，也不提供泛化的 UObject 反射写入入口。

```
MCP Client (stdio)
    │
    ▼
Python MCP Server  (ue-node-nexus-mcp, 6 个 facade 工具)
    │  本地命名管道 \\.\pipe\UeNodeNexusBridge.<pid>
    ▼
UE Editor 插件
    ├── UeNodeNexusBridge      核心：资产 / 图 / 蓝图 / 关卡 / 诊断
    └── UeNodeNexusVfxBridge   VFX：Niagara authoring + Cascade 只读摘要
```

- **Python 端**：纯标准库 + `mcp` SDK 的 MCP server。只注册 6 个 facade 工具，完整能力放在内部 operation registry 中按需查询和执行，最大限度压低 `list_tools` 和历史 tool result 的上下文占用。
- **UE 端**：两个编辑器插件通过每实例一条、按 pid 命名的命名管道提供服务，无端口占用；所有 UE 操作在 game thread 上安全执行。

---

## 快速开始

**1. 编译并启用 UE 插件**

本仓库不发布预编译二进制。把 `Plugins/UeNodeNexusBridge` 和 `Plugins/UeNodeNexusVfxBridge`（只要核心能力可不装后者）复制到项目 `Plugins/` 目录，打开项目触发 UE 编译提示，或右键 `.uproject` 生成项目文件后编译。也可以作为 Engine Plugin 放到 `Engine/Plugins/` 下随引擎编译。

**2. 安装 Python server**

```bash
pip install .
```

**3. 配置 MCP client（stdio）**

```json
{
  "mcpServers": {
    "ue-node-nexus": {
      "command": "ue-node-nexus-mcp",
      "args": ["--response-mode", "minimal"]
    }
  }
}
```

没有 console entry 时可用 `"command": "python", "args": ["-m", "ue_node_nexus_mcp.server"]`。

**4. 安装 Agent skill（强烈建议）**

把 `skill/ue-node-nexus-mcp/SKILL.md` 装进 Agent 的 skill 目录（Claude Code：`~/.claude/skills/ue-node-nexus-mcp/` 或项目 `.claude/skills/`）。它教 Agent **先查 schema 再调用**、正确区分读/写 operation。不装的话模型容易凭工具名猜参数导致调用失败。

**5. 验收连接**

UE 项目打开且插件启用后，先 `ue_execute("project_context_get", {})` 确认 `.uproject` 和 `/Game` mount，再 `ue_execute("bridge_contract_check", {})` 确认 Python 合同与 UE 端 operation 一致、模块加载正常。

---

## 公开工具面：6 个 facade 工具

MCP `list_tools` 固定只暴露这 6 个入口：

| 工具 | 说明 |
|:-----|:-----|
| **`ue_context_get()`** | 返回启用的 operation group、facade 工具清单、当前绑定的 UE 实例和推荐下一跳 |
| **`ue_capability_get()`** | 按 group 查 operation 索引，或按 operation 名查参数 schema |
| **`ue_execute(operation, payload)`** | 执行内部 operation，默认返回 delta summary + diff token |
| **`ue_read(target, ...)`** | 统一读取 asset、graph、diagnostics、Niagara 等常见状态，默认 summary |
| **`ue_diff_get(since_token, ...)`** | 按 diff token 读取 compact changes，支持 cursor 分页 |
| **`ue_plan_validate(operations)`** | 批量验证 operation 的存在性、风险等级和预计变更，不写 UE 状态 |

推荐工作流：

```text
ue_context_get()
  -> ue_capability_get(group="graph", detail="index")
  -> ue_capability_get(operation="node_params_set", detail="schema")
  -> ue_execute(operation="node_params_set", payload={...})
  -> ue_diff_get(since_token="diff_...")
```

常见资产读取可以走自动路由，短名即可：

```text
ue_read(target="auto", asset_path="terrain_demo", format="detail")
  -> auto_index_resolve_path -> asset_get -> 按资产类型路由到
     graph_snapshot_get / material_interface_resolve / texture_summary_get / ...
```

`ue_read` 支持的 target：`auto`、`artifact`、`asset`、`asset_dependencies`、`asset_referencers`、`asset_index`、`graph`、`graph_node_search`、`node`、`blueprint`、`anim_blueprint`、`anim_state_machine`、`anim_montage`、`blend_space`、`material_instance`、`niagara_system`、`niagara_stack`、`cascade_system`、`level`、`log`、`diagnostics`、`project_input`、`input_mapping_context`、`sound_cue`、`texture`。注意没有 `target="material"`：材质实例参数用 `material_instance`，材质节点图用 `graph`，不确定类型用 `auto`。

---

## 内部 operation registry

121 个 operation 的全部元数据（group、read/write、risk、bridge/local、hidden、默认响应粒度、summary）单源维护在 `src/ue_node_nexus_mcp/operations.json`，Python 注册表和参数 schema 从它派生，并有测试保证与 UE C++ 插件的注册表静态对齐。

| Group | 数量 | 覆盖范围 |
|:------|:----:|:---------|
| `core` | 20 | 桥接诊断、编译/校验/保存、MessageLog 诊断、UE 日志尾读取、编辑器实例管理、工作流指南、批量执行、后台任务队列、视口截图 |
| `asset` | 14 | 创建/删除/移动/重命名/复制、批量操作、文件夹、redirector 修复、依赖/引用图查询 |
| `auto_index` | 12 | UE 内持久资产索引：查询、树、概览、路径解析 |
| `graph` | 12 | Material/Blueprint 图快照、整图节点信息、声明式 patch、整图 build、节点/参数读写 |
| `material` | 4 | Material Instance 参数读写、材质表达式类枚举、本地只读 lint |
| `blueprint` | 6 | 蓝图详情/变量/CDO/组件读取、SCS 组件树写入、AnimBP 状态机 state/transition 写入 |
| `level` | 17 | Actor 枚举/spawn/delete、transform 读写、地图切换、UObject 属性读取、material slot 与 MID 参数读写、Landscape LayerInfo |
| `vfx` | 26 | Niagara System/Emitter/Module Stack/Renderer/User 参数/材质/lint/编译 + Cascade 只读摘要 |
| `animation` | 2 | AnimMontage、BlendSpace 结构化摘要 |
| `audio` | 1 | SoundCue 摘要 |
| `texture` | 1 | Texture 摘要 |
| `project_input` | 6 | legacy Project Settings action/axis mappings 读写、Enhanced Input 资产创建与映射读写 |

其中 62 个读、59 个写；109 个转发到 UE bridge，12 个是 MCP 本地 operation（`bridge_contract_check`、`bridge_instance_list`、`bridge_instance_select`、`material_lint`、`workflow_guide_get`、`batch_execute`、`log_tail_get`、`task_submit`/`task_status`/`task_result`/`task_cancel`、`viewport_capture_status`），在 server 内处理、不进 UE。

**关卡 Actor 生命周期（窄类型化写入）**：`level_actor_spawn`（`class_path` 接受引擎类短名、`/Script/` 路径或蓝图资产路径，附带 location/rotation/scale/label）、`level_actor_delete`、`level_actor_transform_set`（至少给 location/rotation/scale 之一，返回前后 transform）。全部默认 `dry_run=true`，走编辑器 `UEditorActorSubsystem`，不开放泛化反射写入。`level_open` 切换编辑器地图：当前地图有未保存修改时拒绝执行，需显式 `discard_changes=true`。

**资产依赖图**：`asset_dependencies_get` / `asset_referencers_get` 基于 AssetRegistry 返回 `[package_name, hard|soft]` 行（默认过滤 `/Script/`、`/Engine/` 包，`include_engine=true` 可包含），支持 cursor 分页，用于重命名/删除前的影响面分析。

**Enhanced Input**：`input_action_create`（`value_type` 支持 `bool/axis1d/axis2d/axis3d`）与 `input_mapping_context_create` 创建 UInputAction / UInputMappingContext 资产；`input_mapping_context_entry_add` 把已有 InputAction 绑定到按键（键名如 `SpaceBar`、`W`、`Gamepad_FaceButton_Bottom`，无效键名和重复绑定会被拒绝）；`input_mapping_context_get` 列出 context 内全部映射（也可走 `ue_read(target="input_mapping_context")`）。写 operation 默认 `dry_run=true`，`save=true` 时落盘保存。

**AnimBP 状态机写入**：`anim_state_machine_state_add` 向状态机添加命名状态（`set_as_entry=true` 时把入口节点重连到新状态），`anim_state_machine_transition_add` 在两个命名状态之间建立 transition（重复的 from→to 会被拒绝，新 transition 的条件图为空、需后续补充规则）。蓝图只有一个状态机时 `machine_name` 可省略，多个时必填、错误响应会列出可选名称。写入后自动标记蓝图结构性修改。读取侧配套 `anim_state_machine_summary_get`（或 `ue_read(target="anim_state_machine")`）。

**UE 日志尾读取**：`log_tail_get` 是 MCP 本地 operation，读取最新项目日志尾部（`tail_kb`、`match` 子串过滤、`max_lines`），补足 `diagnostics_get.related_log_items` 之外的原始日志排查。

**hidden operation**：6 个高危/兼容 operation（如 `editor_save_all`、`editor_request_exit`、`auto_index_clear`）默认不出现在 `ue_capability_get` 索引和 `ue_context_get` 计数中，需 `include_hidden=true` 列出；按名称查 schema 和通过 `ue_execute` 执行不受影响。

**按需工作流指南**：`ue_execute("workflow_guide_get", {})` 列出 7 类任务级指南（入门、图编辑、材质、蓝图、Niagara、诊断修复、并发批量），`{"category": "..."}` 取指南正文，`{"query": "connect pins"}` 按关键词路由到最匹配的指南。指南正文维护在 `src/ue_node_nexus_mcp/guides/*.md`，Agent 不装 skill 文件也能在会话内自取工作流知识。

**批量执行**：`batch_execute` 是 MCP 本地 operation，一次调用顺序执行一小批 registry operation（上限 20 条）：全批先校验（未知 operation、组未启用、缺必填字段时整批拒绝、不执行任何一条），执行时默认遇错即停并把其余标记为 skipped（`continue_on_error=true` 可继续），桥接连接失败则中止剩余项。逐项返回 `ok`、紧凑摘要和诊断计数。它是省往返的工具，不是事务——已执行项不会回滚。

**后台任务队列**：`task_submit` 先做与 `batch_execute` 相同的前置校验，然后把单个 operation 交给会话内唯一的后台工作线程排队执行并立即返回 `task_id`，适合大编译等长耗时调用（配合调高 `UE_NEXUS_TIMEOUT_SECONDS`）。任务严格按提交顺序串行执行，不会与其他任务交错写桥。`task_status` 查单个任务或列出全部任务，`task_result` 取已完成任务存储的完整响应，`task_cancel` 只能取消仍在排队的任务。任务里可以套 `batch_execute`（后台跑整批），但 `task_*` 之间不可互相嵌套。任务状态在内存中，server 重启即失效。

**视口截图（两阶段）**：`viewport_capture` 请求编辑器主视口截图并立即返回目标 PNG 路径（写入发生在下一次视口重绘之后，异步完成）；`viewport_capture_status` 是 MCP 本地 operation，按返回的 `file_path` 检查文件是否已落盘及其大小。文件名限定为字母/数字/下划线/连字符的裸名，固定写入项目 `Saved/Screenshots/` 目录，不能指向任意路径。

**facade 生产路径上的客户端逻辑**：`graph_patch_apply` 对 Material/MaterialFunction 在 `dry_run=false` 时由 Python 端展开同批 `create_node.client_id` 连线引用；`diagnostics_get` 附带 UE log 中的材质编译回退线索（`related_log_items`，标 `stale_possible=true`，不计入全局 `error_count`）；Niagara 读 operation 自动裁剪冗余字段。这些行为都在 `ue_execute` 实际走的路径上生效并有测试覆盖。

---

## 响应与上下文控制

- **写默认 dry-run**：改动 UE 资产/关卡状态的写 operation 默认 `dry_run=true`；实际写入时单个响应返回已应用差异、引脚完整性、编译结果、脏标记和诊断。唯一例外是 `viewport_capture`（只向 `Saved/Screenshots/` 写 PNG，不触资产，默认直接请求）。
- **默认压缩**：写 operation 默认响应 `delta`，读 operation 默认 `summary`。完整 bridge envelope 需要显式 `response={"mode": "full"}` 或 `"debug"`。`ue_execute.response` 只接受 `mode` 和 `allow_heavy` 两个字段；`response.format="full"` 是无效写法，会被拒绝并提示改用 `response.mode`。读取粒度放在 operation payload 的 `format` 或 `ue_read(format="detail")`。
- **artifact**：超过 inline 阈值的大响应不直接进入 tool result，而是返回 `artifact.id`、payload 字节数和摘要，用 `ue_read(target="artifact", query={"artifact_id": "..."})` 取回完整内容。
- **diff 分页**：`ue_diff_get` 的 change 列表超过 `limit` 时返回 `next_cursor`，把它作为 `cursor` 传回即可翻页。
- **重读拦截**：`graph_snapshot_get(format="full", include_node_params=true, node_params_format="full")` 这类重负载读取会被前置拦截，需显式 `response.allow_heavy=true`。
- **图快照格式**：`graph_snapshot_get` 支持 `wires_tiny`（默认）/`wires_min`/`wires`/`compact`/`full`；`graph_node_info_get` 支持 `indexed`（默认，`T/P/N/V/E/X/R` 字典行压缩整图）和 `grouped`；Niagara 读 operation 默认 `indexed`。真实 pin GUID、完整 issue 对象等重数据只在显式 `format="full"` 时返回。
- **诊断计数语义**：带 `asset_path` 的具体资产写响应可在根级附带 `remaining_errors`（该资产写后检查剩余错误数）；全项目诊断走 `diagnostics_get`，以 `data.error_count` / `data.warning_count` / `data.items` 表达，两者不混用。

---

## 多 UE 实例

每个 UE 编辑器实例各自暴露一条 `\\.\pipe\UeNodeNexusBridge.<pid>` 管道，互不冲突。MCP 会话默认自动连接唯一在线实例；有多个实例时：

```text
ue_execute("bridge_instance_list", {})            # 查看在线实例
ue_execute("bridge_instance_select", {"pid": 1234})   # 或 {"project": "工程名"}
```

`ue_context_get()` 显示当前 `active_instance` 和 `available_instances`。切换绑定实例会自动重置 VFX 等 feature 探测缓存。

---

## 配置参考

| 环境变量 | 默认值 | 说明 |
|:---------|:-------|:-----|
| `UE_NEXUS_TIMEOUT_SECONDS` | `30` | 桥接请求超时（秒） |
| `UE_NEXUS_RESPONSE_MODE` | `minimal` | facade 响应模式：`minimal` 或 `full` |
| `UE_NEXUS_FEATURES` | 全部 group | 显式指定启用的 operation group，如 `core,asset,material` |
| `UE_NEXUS_ENABLE_FEATURES` | — | 在当前 group 集合上追加 |
| `UE_NEXUS_DISABLE_FEATURES` | — | 从当前 group 集合中移除 |
| `UE_NEXUS_VFX_SUPPORT` | — | `true`/`false`，VFX group 本地意图开关 |

CLI 支持同样的开关，server 启动时一次性解析：`--response-mode`、`--features`、`--enable-feature`、`--disable-feature`、`--vfx-support`。

可用 group：`core,asset,auto_index,graph,material,blueprint,animation,audio,level,project_input,texture,vfx`。关闭某 group 后对应 operation 不进入 capability 索引也不可执行。**VFX 由 UE 插件状态最终裁决**：只有 `UeNodeNexusVfxBridge` 已加载且 UE Niagara 插件启用时才可用（以 `bridge_capabilities_get().data.modules.vfx_available` 为准）；本地配置只能关闭或表达启用意图，不能强行开启。探测结果不确定时（如 UE 未连接）不会缓存否定结论，下次调用会重新探测。

---

## 目录结构

```
UE-Node-Nexus-MCP/
├── Plugins/
│   ├── UeNodeNexusBridge/        核心 UE 编辑器插件（C++）
│   └── UeNodeNexusVfxBridge/     Niagara/Cascade UE 插件（C++）
├── src/ue_node_nexus_mcp/        Python MCP server
│   ├── operations.json           operation 元数据单一事实源
│   ├── guides/                   按需工作流指南正文（workflow_guide_get 服务）
│   ├── tools_facade.py           6 个 facade 入口
│   ├── facade_*.py               capability / execute / read / plan / response / state
│   └── tools_*.py                各 group 的 payload 构造与客户端逻辑
├── tests/                        pytest 测试（facade 真实调用面 + 契约对齐）
├── skill/ue-node-nexus-mcp/      Agent skill 文档
└── pyproject.toml
```

---

## 开发与测试

```bash
pip install -e . && python -m pytest tests -q
```

测试不需要 UE 实例：`tests/conftest.py` 提供假 bridge 注入。需要在真实编辑器进程内验证 C++ handler 时（CI 或管道服务不可用的非 Windows 主机），用无头 smoke commandlet 回放请求：`UnrealEditor-Cmd Host.uproject -run=UeNodeNexusBridgeSmoke -RequestFile=req.jsonl -ResponseFile=resp.jsonl`（JSONL 每行一个 `{operation, request_id, payload}` envelope，走与命名管道完全相同的分发路径）。覆盖面包括 facade 端到端路径（`ue_execute`/`ue_read`/`ue_diff_get` 分页/capability hidden 过滤/参数校验的结构化错误返回）、`graph_patch_apply` 的 client_id 展开、diagnostics 富化、Niagara 字段裁剪、`workflow_guide_get` 分类/检索、`batch_execute` 校验先行与遇错即停语义、后台任务队列（提交/失败上报/取消/并发上限/批量嵌套）、视口截图两阶段流程、`log_tail_get` 日志定位/过滤、关卡 Actor 与依赖图 operation 的载荷与路由、operation registry 元数据读取、payload schema 派生、响应归一化，以及三个结构性护栏：Python `operations.json` 与 C++ 插件注册表的**契约对齐测试**、所有源码文件（含 `.py/.h/.cpp/.cs/.inl`）的 **300 行预算检查**、以及对从未经过 UE 实机编译的新增 C++ TU 的 **clang 桩头文件编译检查**（`tests/test_cpp_compile_check.py` + `tests/compile_check/ue_stubs/`，用宿主机 clang 按文档化的 UE 5.5 API 形状做语法/类型检查，机器上没有可用编译器时自动跳过）。

---

## 兼容性与边界

| 项目 | 说明 |
|:-----|:-----|
| **实测环境** | UE 5.5 Launcher，Windows x64；材质整图复刻（85 节点/110 连线精确一致）、3C Blueprint 工作流、Niagara authoring 均在实机验收通过 |
| **编译验证** | 两个插件的全部 C++ TU 已在 Linux 上对照 UE 5.5 官方源码用 UnrealBuildTool 完整编译+链接通过（unity 与非 unity 双模式，clang 18，零错误零警告）；期间修复的 ODR/unity 合并冲突、ADL 重载与弃用 API 问题均已进主干 |
| **实机运行验证（Linux 无头编辑器）** | 通过 smoke commandlet 在真实 UnrealEditor-Cmd 进程内 E2E 验收：Enhanced Input 全周期（创建/保存/映射/重复与无效键拒绝/跨会话持久化读回）、资产依赖图双向 hard 依赖、关卡 Actor 全生命周期（spawn→transform 写读→delete）、蓝图创建与图快照、诊断读取，以及各错误路径（`asset_not_found`/`asset_already_exists`/`mapping_already_exists`/`invalid_key`/`viewport_unavailable`） |
| **仍待有资产环境验证** | AnimBP 状态机写入的成功路径（需含骨骼/AnimBP 的项目；注册、分发与错误路径已实机验证）、`viewport_capture` 成功路径（需真实视口，无头 `-nullrhi` 下正确返回 `viewport_unavailable`）、命名管道传输本身（Windows 专属，Linux 上为空实现；分发层已由 commandlet 按字节一致路径验证） |
| **跨版本** | 插件二进制与 UE 版本/编译器/模块 ABI 绑定；换 UE 版本请按源码重新编译，UE API 变化时按编译错误调整 |
| **仓库边界** | 聚焦 asset discovery 与依赖图、graph 检查与编辑、编译诊断与日志、MI 参数、窄类型化关卡 Actor 生命周期、安全 package save；不含任意 Python/控制台命令执行、泛化 UObject 反射写入、场景模板类工具 |
| **平台** | 传输层为 Windows 命名管道，server 与 UE 编辑器需在同一台 Windows 机器 |

---

## 参考项目

- [`bunkerboy258/ue-blueprint-dumper`](https://github.com/bunkerboy258/ue-blueprint-dumper) — 蓝图 CDO/默认值/组件检查和动画蓝图语义摘要功能的参考

---

<div align="center">

**平台:** Windows x64 | **引擎:** Unreal 5.5 | **许可证:** MIT

</div>
