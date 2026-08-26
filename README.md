<div align="center">

# UE Node Nexus MCP

**Unreal Engine 编辑器的 MCP 桥接器：资产、Material/Blueprint 图、Niagara、关卡材质的类型安全读写**

*固定 6 工具 facade + 100 个内部 operation，先查 schema 再调用，无需模型自行编写 Python 脚本*

![C++](https://img.shields.io/badge/C%2B%2B-20-blue?style=flat-square)
![Python](https://img.shields.io/badge/Python-3.11%2B-green?style=flat-square)
![Unreal](https://img.shields.io/badge/Unreal-5.5-black?style=flat-square)
![Platform](https://img.shields.io/badge/Platform-Windows%20x64-lightgrey?style=flat-square)
![License](https://img.shields.io/badge/License-MIT-lightgrey?style=flat-square)

</div>

---

## 它是什么

本项目让 MCP 客户端（Claude Code、Cursor 等）安全地检查和编辑打开中的 Unreal Editor：资产管理、Material/Blueprint 节点图读写、编译诊断、Material Instance 参数、Niagara authoring、关卡材质使用点。不包含场景布局自动化，也不提供任意 Python / 反射写入入口。

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

`ue_read` 支持的 target：`auto`、`artifact`、`asset`、`asset_index`、`graph`、`graph_node_search`、`node`、`blueprint`、`anim_blueprint`、`anim_state_machine`、`anim_montage`、`blend_space`、`material_instance`、`niagara_system`、`niagara_stack`、`cascade_system`、`level`、`diagnostics`、`project_input`、`sound_cue`、`texture`。注意没有 `target="material"`：材质实例参数用 `material_instance`，材质节点图用 `graph`，不确定类型用 `auto`。

---

## 内部 operation registry

100 个 operation 的全部元数据（group、read/write、risk、bridge/local、hidden、默认响应粒度、summary）单源维护在 `src/ue_node_nexus_mcp/operations.json`，Python 注册表和参数 schema 从它派生，并有测试保证与 UE C++ 插件的注册表静态对齐。

| Group | 数量 | 覆盖范围 |
|:------|:----:|:---------|
| `core` | 11 | 桥接诊断、编译/校验/保存、MessageLog 诊断、编辑器实例管理 |
| `asset` | 12 | 创建/删除/移动/重命名/复制、批量操作、文件夹、redirector 修复 |
| `auto_index` | 12 | UE 内持久资产索引：查询、树、概览、路径解析 |
| `graph` | 12 | Material/Blueprint 图快照、整图节点信息、声明式 patch、整图 build、节点/参数读写 |
| `material` | 4 | Material Instance 参数读写、材质表达式类枚举、本地只读 lint |
| `blueprint` | 4 | 蓝图详情/变量/CDO/组件读取、SCS 组件树写入 |
| `level` | 13 | Actor/网格实例枚举、transform、UObject 属性读取、material slot 与 MID 参数读写、Landscape LayerInfo |
| `vfx` | 26 | Niagara System/Emitter/Module Stack/Renderer/User 参数/材质/lint/编译 + Cascade 只读摘要 |
| `animation` | 2 | AnimMontage、BlendSpace 结构化摘要 |
| `audio` | 1 | SoundCue 摘要 |
| `texture` | 1 | Texture 摘要 |
| `project_input` | 2 | legacy Project Settings action/axis mappings 读写 |

其中 54 个读、46 个写；96 个转发到 UE bridge，4 个是 MCP 本地 operation（`bridge_contract_check`、`bridge_instance_list`、`bridge_instance_select`、`material_lint`），在 server 内处理、不进 UE。

**hidden operation**：6 个高危/兼容 operation（如 `editor_save_all`、`editor_request_exit`、`auto_index_clear`）默认不出现在 `ue_capability_get` 索引和 `ue_context_get` 计数中，需 `include_hidden=true` 列出；按名称查 schema 和通过 `ue_execute` 执行不受影响。

**facade 生产路径上的客户端逻辑**：`graph_patch_apply` 对 Material/MaterialFunction 在 `dry_run=false` 时由 Python 端展开同批 `create_node.client_id` 连线引用；`diagnostics_get` 附带 UE log 中的材质编译回退线索（`related_log_items`，标 `stale_possible=true`，不计入全局 `error_count`）；Niagara 读 operation 自动裁剪冗余字段。这些行为都在 `ue_execute` 实际走的路径上生效并有测试覆盖。

---

## 响应与上下文控制

- **写默认 dry-run**：写 operation 默认 `dry_run=true`；实际写入时单个响应返回已应用差异、引脚完整性、编译结果、脏标记和诊断。
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

测试不需要 UE 实例：`tests/conftest.py` 提供假 bridge 注入。覆盖面包括 facade 端到端路径（`ue_execute`/`ue_read`/`ue_diff_get` 分页/capability hidden 过滤/参数校验的结构化错误返回）、`graph_patch_apply` 的 client_id 展开、diagnostics 富化、Niagara 字段裁剪、operation registry 元数据读取、payload schema 派生、响应归一化，以及两个结构性护栏：Python `operations.json` 与 C++ 插件注册表的**契约对齐测试**，和所有源码文件（含 `.py/.h/.cpp/.cs/.inl`）的 **300 行预算检查**。

---

## 兼容性与边界

| 项目 | 说明 |
|:-----|:-----|
| **实测环境** | UE 5.5 Launcher，Windows x64；材质整图复刻（85 节点/110 连线精确一致）、3C Blueprint 工作流、Niagara authoring 均在实机验收通过 |
| **跨版本** | 插件二进制与 UE 版本/编译器/模块 ABI 绑定；换 UE 版本请按源码重新编译，UE API 变化时按编译错误调整 |
| **仓库边界** | 聚焦 asset discovery、graph 检查与编辑、编译诊断、MI 参数、安全 package save；不含场景布局自动化、Actor 实例化、任意 UObject 属性写入、任意 Python 执行 |
| **平台** | 传输层为 Windows 命名管道，server 与 UE 编辑器需在同一台 Windows 机器 |

---

## 参考项目

- [`bunkerboy258/ue-blueprint-dumper`](https://github.com/bunkerboy258/ue-blueprint-dumper) — 蓝图 CDO/默认值/组件检查和动画蓝图语义摘要功能的参考

---

<div align="center">

**平台:** Windows x64 | **引擎:** Unreal 5.5 | **许可证:** MIT

</div>
