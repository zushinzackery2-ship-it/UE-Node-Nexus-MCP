# MCP Thin Facade Design

本文档设计一层薄 MCP 工具面：默认只向 Agent 暴露少量稳定工具，完整 Unreal 能力作为内部 operation 指令集按需查询和执行。

## 目标

- 将默认 `list_tools` 上下文从几十个工具 schema 降到 4 到 8 个工具 schema。
- 保留现有 UE bridge operation 能力，不把能力删除当成上下文优化。
- 让 Agent 按需拉取分组指令集，而不是每轮都看到完整工具面。
- 让大 readback 和写操作默认返回 diff 或 summary，避免一次返回完整资产状态。
- 保持现有 full/legacy 工具面可回退，迁移期不破坏旧脚本和现有合同测试。

## 非目标

- 不新增任意 Python 执行入口。
- 不把所有 operation 文档一次性塞进一个大 schema。
- 不绕过 UE bridge 现有白名单、dry-run、编译、校验和诊断合同。
- 不允许隐藏 operation 被 MCP 客户端直接 `call_tool` 调用。

## 核心判断

MCP 客户端只能直接调用 `list_tools` 暴露出来的工具。因此“深层 MCP 指令集不主动暴露但可调用”的正确实现不是隐藏工具仍可直接调用，而是通过少量公开工具间接调用：

```text
Agent
  -> ue_capability_get(group="niagara")
  -> ue_execute(operation="niagara_module_inputs_set", payload={...})
  -> ue_diff_get(scope="asset", since_token="...")
```

深层 operation 不再是 MCP tool，它们是 MCP tool 内部的 operation 指令。公开 MCP 工具只负责查询能力、执行 operation、读取状态和读取 diff。

另一个根因是历史 tool result 会持续累积在对话上下文中。只压缩单次返回不够，必须让读写工具默认返回 summary、delta、ids 和 token，把完整数据留在服务端状态中，后续按需精读。

## 推荐公开工具

默认 thin facade 暴露 5 个工具。第 6 个 `ue_plan_validate` 可选，但建议实现，因为它能把复杂写入前的错误提前集中暴露出来。

| 工具 | 作用 | 默认返回 |
|:-----|:-----|:-----|
| `ue_context_get` | 获取 bridge 状态、项目上下文、启用分组、推荐入口 | 极简状态和可用 group |
| `ue_capability_get` | 按 group 或 operation 查询深层指令集 | operation 索引或单个 operation schema |
| `ue_execute` | 执行一个内部 operation | delta summary、诊断计数、diff token |
| `ue_read` | 统一读取资产、graph、diagnostics、Niagara 状态 | summary/index，显式 detail 才展开 |
| `ue_diff_get` | 按 token 获取变更 | 变更列表、诊断摘要、next token |
| `ue_plan_validate` | 可选：验证一批 operation，不实际写入 | 风险、错误、预计 diff |

## 工具合同

### `ue_context_get`

用途：Agent 进入 UE 工作流后的第一跳。它不返回完整工具说明，只返回当前可用能力入口。

请求：

```json
{
  "include_counts": true
}
```

响应：

```json
{
  "ok": true,
  "data": {
    "profile": "thin",
    "bridge": "ready",
    "project": {
      "name": "MyProject",
      "engine": "5.5"
    },
    "groups": [
      ["asset", 12],
      ["graph", 11],
      ["material", 3],
      ["blueprint", 3],
      ["level", 12],
      ["niagara", 25],
      ["project_input", 2]
    ],
    "recommended_next": "ue_capability_get"
  },
  "remaining_errors": 0
}
```

### `ue_capability_get`

用途：按需返回内部 operation 指令集。它必须支持分级返回，避免把工具上下文从 `list_tools` 搬到 capability 响应里。

请求：

```json
{
  "group": "graph",
  "detail": "index"
}
```

`detail` 取值：

- `index`：只返回 operation 名、读写类型、风险等级、短描述。
- `schema`：返回单个 operation 的参数 schema。
- `examples`：返回单个 operation 的最小示例。
- `full`：返回单个 operation 的完整 schema、返回合同、错误码和示例。

索引响应：

```json
{
  "ok": true,
  "data": {
    "group": "graph",
    "detail": "index",
    "operations": [
      ["graph_snapshot_get", "read", "low", "Read graph wire/index snapshot."],
      ["graph_patch_apply", "write", "medium", "Apply connect/disconnect/delete patch."],
      ["graph_build_apply", "write", "medium", "Build graph from declarative spec."],
      ["node_create", "write", "medium", "Create one graph node."],
      ["node_params_set", "write", "medium", "Set typed node parameters."]
    ],
    "next_read": {
      "tool": "ue_capability_get",
      "args": {
        "operation": "graph_patch_apply",
        "detail": "schema"
      }
    }
  },
  "remaining_errors": 0
}
```

单 operation schema 响应：

```json
{
  "ok": true,
  "data": {
    "operation": "graph_patch_apply",
    "kind": "write",
    "risk": "medium",
    "payload_schema": {
      "asset_path": "string",
      "graph_kind": "material|material_function|blueprint|auto",
      "operations": "array<object>",
      "dry_run": "boolean default true",
      "compile_after": "boolean default true"
    },
    "returns": "delta_summary",
    "next_read": "ue_diff_get"
  },
  "remaining_errors": 0
}
```

### `ue_execute`

用途：执行内部 operation。所有现有 bridge operation 保留为内部 operation，通过 allowlist 校验后转发。

请求：

```json
{
  "operation": "node_params_set",
  "payload": {
    "asset_path": "/Game/Materials/M_Example.M_Example",
    "node_id": "n3",
    "params": {
      "ParameterName": "Roughness"
    },
    "dry_run": true
  },
  "response": {
    "mode": "delta",
    "diagnostics_limit": 5
  }
}
```

`response.mode` 统一控制返回粒度：

| Mode | 行为 |
|:-----|:-----|
| `silent` | 只返回成功/失败、错误计数和 token |
| `brief` | 返回一句摘要、错误计数和 token |
| `ids_only` | 返回新建/影响对象 ID，不返回对象详情 |
| `delta` | 返回变更摘要、影响范围和 diff token |
| `summary` | 返回当前状态摘要，不返回完整字段 |
| `full` | 返回完整 data 负载 |
| `debug` | 返回完整 bridge envelope、request id、diagnostics、warnings |

写 operation 默认 `delta`。批量写 operation 默认 `ids_only` 或 `delta`，不默认返回完整节点、完整 graph 或完整诊断数组。

默认响应：

```json
{
  "ok": true,
  "data": {
    "operation": "node_params_set",
    "changed": false,
    "dry_run": true,
    "affected": {
      "assets": ["/Game/Materials/M_Example.M_Example"],
      "nodes": ["n3"]
    },
    "diagnostics": {
      "errors": 0,
      "warnings": 0
    },
    "state_token": "asset_456_after",
    "diff_token": "req_0189_after",
    "next_read": {
      "tool": "ue_diff_get",
      "args": {
        "scope": "asset",
        "asset_path": "/Game/Materials/M_Example.M_Example",
        "since_token": "req_0189_before"
      }
    }
  },
  "remaining_errors": 0
}
```

`ue_execute` 不应默认返回完整 bridge envelope。`operation`、`request_id`、空 `warnings`、空 `diagnostics` 只在 `response.mode="debug"` 或 `response.mode="full"` 时返回。

### `ue_read`

用途：统一读取常见状态，避免 Agent 在多个 get 工具之间选择。它不是万能数据库查询，而是少量 target 的规范入口。

请求：

```json
{
  "target": "graph",
  "asset_path": "/Game/Materials/M_Example.M_Example",
  "query": {
    "format": "index",
    "include_links": true,
    "limit": 80
  }
}
```

`target` 建议取值：

- `asset`
- `asset_index`
- `graph`
- `node`
- `diagnostics`
- `level`
- `material_instance`
- `niagara_system`
- `niagara_stack`
- `project_input`

默认响应必须是 index 或 summary。需要完整字段时显式 `format="detail"` 或 `format="debug"`。

大对象读取应默认返回服务端 token：

```json
{
  "ok": true,
  "data": {
    "target": "graph",
    "asset_path": "/Game/Materials/M_Example.M_Example",
    "format": "index",
    "summary": "42 nodes, 51 links, 0 errors.",
    "snapshot_token": "graph_snap_123",
    "state_token": "asset_rev_456",
    "next_read": {
      "tool": "ue_read",
      "args": {
        "target": "graph",
        "snapshot_token": "graph_snap_123",
        "format": "detail"
      }
    }
  },
  "remaining_errors": 0
}
```

完整 snapshot 留在 MCP server 或 UE bridge 状态中。Agent 上下文只保留摘要和 token。

### `ue_diff_get`

用途：读取某次操作或某个 token 后的变化。整体状态不再默认回传，只回传变化。

请求：

```json
{
  "scope": "asset",
  "asset_path": "/Game/Materials/M_Example.M_Example",
  "since_token": "req_0189_before",
  "limit": 50
}
```

响应：

```json
{
  "ok": true,
  "data": {
    "scope": "asset",
    "asset_path": "/Game/Materials/M_Example.M_Example",
    "since_token": "req_0189_before",
    "current_token": "req_0189_after",
    "changes": [
      ["node_param", "n3", "ParameterName", "OldName", "Roughness"],
      ["compile", "material", "success", 0, 0]
    ],
    "diagnostics": {
      "errors": 0,
      "warnings": 0
    },
    "truncated": false,
    "next_cursor": null
  },
  "remaining_errors": 0
}
```

Diff token 可以先实现为一次 request 生命周期内的 token，后续再持久化到 bridge 状态目录。第一阶段不需要做复杂历史数据库。

如果资产被用户、Editor、脚本或其他请求在 token 之外修改，`ue_diff_get` 必须返回失效错误，而不是伪造 diff：

```json
{
  "ok": false,
  "error": {
    "code": "state_invalidated",
    "message": "Asset changed outside the tracked snapshot.",
    "details": {
      "asset_path": "/Game/Materials/M_Example.M_Example",
      "expected_state_token": "asset_rev_456",
      "current_state_token": "asset_rev_489"
    }
  },
  "remaining_errors": 1
}
```

失效后 Agent 应重新调用 `ue_read(format="summary")` 或 `ue_read(format="index")`。

### `ue_plan_validate`

用途：在执行一批复杂写入前做统一验证，避免 Agent 一步一步试错导致上下文和 UE 状态都膨胀。

请求：

```json
{
  "operations": [
    {
      "operation": "node_create",
      "payload": {
        "asset_path": "/Game/M.M",
        "node_class": "MaterialExpressionMultiply"
      }
    },
    {
      "operation": "graph_patch_apply",
      "payload": {
        "asset_path": "/Game/M.M",
        "operations": []
      }
    }
  ],
  "mode": "dry_run"
}
```

响应：

```json
{
  "ok": true,
  "data": {
    "valid": true,
    "operation_count": 2,
    "risk": "medium",
    "estimated_changes": [
      ["create_node", "MaterialExpressionMultiply"],
      ["patch_graph", 0]
    ],
    "errors": [],
    "warnings": []
  },
  "remaining_errors": 0
}
```

## 内部 Operation Registry

Thin facade 需要一个内部 operation registry。它替代当前多处重复维护的工具面清单，但不要求第一阶段删除旧结构。

建议 registry 字段：

```json
{
  "name": "node_params_set",
  "group": "graph",
  "kind": "write",
  "risk": "medium",
  "bridge_operation": "node_params_set",
  "hidden_from_mcp": true,
  "default_response": "delta",
  "payload_schema": {},
  "summary": "Set typed node parameters.",
  "examples": []
}
```

Registry 是 facade 的核心 Module：

- `ue_capability_get` 从 registry 生成指令集。
- `ue_execute` 根据 registry 做 allowlist、风险等级、读写分类和默认响应模式。
- 合同测试从 registry 派生 operation 数量、分组数量和暴露策略。
- 旧 83 工具可继续作为 registry 的 Adapter 存在。

## Diff 合同

Diff 是减少上下文的关键，不应只是文本 patch。建议统一为行式数组，优先保证可读且短：

```json
["node_param", "node_id", "field", "before", "after"]
["link_add", "from_pin", "to_pin"]
["link_remove", "from_pin", "to_pin"]
["asset_create", "asset_path", "class"]
["asset_move", "old_path", "new_path"]
["compile", "asset_kind", "status", "error_count", "warning_count"]
["diagnostic", "severity", "code", "message"]
```

复杂对象不要放进 diff 行。需要详情时给 `next_read`：

```json
{
  "next_read": {
    "tool": "ue_read",
    "args": {
      "target": "node",
      "asset_path": "/Game/M.M",
      "node_id": "n3",
      "format": "detail"
    }
  }
}
```

批量写操作默认只返回 counts、ids 和 token：

```json
{
  "created_count": 5,
  "updated_count": 3,
  "deleted_count": 0,
  "created_ids": ["n10", "n11", "n12", "n13", "n14"],
  "affected_ids": ["n3", "n4", "n10", "n11", "n12", "n13", "n14"],
  "diagnostics": {
    "errors": 0,
    "warnings": 1
  },
  "diff_token": "batch_021_after"
}
```

完整对象详情只能通过后续 `ue_read` 按 ID 精读。

## Server-side State

Thin facade 需要服务端状态缓存，避免把完整 snapshot 放进模型上下文。

建议缓存对象：

```text
snapshot_token -> graph/asset/diagnostics snapshot
state_token -> asset revision/hash/dirty generation
diff_token -> before/after summary and compact diff
artifact_id -> large debug payload or full bridge response
```

缓存规则：

- token 必须有 TTL。
- token 必须绑定 asset path 和 state token。
- token 失效时返回 `state_invalidated` 或 `token_expired`。
- debug/full 大负载默认存为 artifact，只返回 handle。
- artifact 通过 `ue_read(target="artifact", artifact_id="...")` 显式读取。

不要把完整数据 base64 放进 tool result。只要进入 tool result，很多 MCP client 仍可能把它保留在历史上下文里。正确做法是返回 handle：

```json
{
  "summary": "Full graph snapshot stored.",
  "artifact": {
    "id": "artifact_778",
    "kind": "graph_snapshot_full",
    "expires_in_seconds": 600,
    "fetch_with": "ue_read"
  }
}
```

## 响应压缩规则

Thin facade 默认响应使用 minimal envelope：

- 保留：`ok`、`data`、`error`、`remaining_errors`。
- 省略：空 `warnings`、空 `diagnostics`、重复 `operation`、默认 `request_id`。
- 诊断默认只返回计数和前 N 条。
- 完整 bridge envelope 只在 `response.mode="full"` 或 `response.mode="debug"` 返回。
- 大列表统一返回 `limit`、`truncated`、`next_cursor`。
- `isEphemeral`、TTL 或客户端缓存提示只能作为可选增强，不能作为核心正确性依赖。核心压缩必须依赖 facade 自身的 summary、token、diff 和 artifact handle。

## 兼容策略

第一阶段不直接删除现有 MCP tools。新增 profile：

| Profile | MCP 暴露面 | 用途 |
|:-----|:-----|:-----|
| `legacy` | 当前默认 83 工具 | 旧客户端、测试、回归 |
| `thin` | 5 到 6 个 facade 工具 | 新 Agent 默认推荐 |
| `debug` | facade 工具加维护工具 | 开发和排障 |

环境变量建议：

```text
UE_NEXUS_MCP_PROFILE=thin
UE_NEXUS_RESPONSE_MODE=minimal
```

CLI 建议：

```text
ue-node-nexus-mcp --mcp-profile thin --response-mode minimal
```

迁移期间，`legacy` 必须与当前工具面合同保持一致。`thin` 是新增能力，不改变旧脚本行为。

## 安全策略

- `ue_execute` 必须拒绝 registry 外 operation。
- 写 operation 默认继承现有 `dry_run=true` 策略。
- 高风险 operation 需要在 capability 中标注 `risk`，并在 execute 响应中返回风险。
- 维护类 operation 默认不进入 thin profile。
- 关卡实例写入和任意 UObject 属性写入继续排除，不通过 facade 回流。
- `ue_execute` 不能提供任意路径调用、任意 Python、任意 reflection 写入。

## 测试计划

必须新增两层合同测试。

外层 MCP facade 测试：

- `thin` profile 的 `list_tools` 只包含 5 到 6 个公开工具。
- `legacy` profile 的 `list_tools` 与当前 83 工具面一致。
- `ue_capability_get(detail="index")` 不返回完整 payload schema。
- `ue_capability_get(operation=..., detail="schema")` 只返回单个 operation schema。
- `ue_execute` 拒绝未知 operation。
- `ue_execute` 默认返回 delta summary，不返回完整 bridge envelope。
- `ue_read` 默认返回 index/summary，`detail/debug` 必须显式请求。
- `ue_diff_get` 支持 `limit`、`truncated`、`next_cursor`。
- 写 operation 默认返回 `diff_token` 或 `state_token`，不返回完整对象详情。
- token 对应资产外部变更时返回 `state_invalidated`。
- full/debug 大负载可存为 artifact handle，不进入默认响应。

内部 operation 测试：

- Registry 覆盖所有现有 bridge operation。
- Registry 的 group/kind/risk 与现有 READ/WRITE/FEATURE 合同一致。
- 旧工具 wrapper 和 `ue_execute` 调用同一个 bridge operation。
- 禁止回归 operation 不在 registry 中。
- Niagara 不可用时，thin capability 不返回 Niagara operation。
- `bridge_contract_check` 能按 profile 校验 exposed/enabled/registry 三种视角。

## 落地顺序

1. 建立 operation registry，但不改变现有工具注册。
2. 新增 `ue_capability_get` 和 registry 合同测试。
3. 新增 `ue_execute`，让它调用现有 bridge client 和同一 allowlist。
4. 新增 minimal envelope 和 delta summary，默认只用于 thin profile。
5. 新增 `ue_read`，先覆盖 asset index、graph、diagnostics 三类高频读取。
6. 新增 `ue_diff_get`，先支持 request 生命周期内的 diff token。
7. 新增 server-side state cache，支持 snapshot token、state token、artifact handle 和 token invalidation。
8. 增加 `UE_NEXUS_MCP_PROFILE=thin|legacy|debug`。
9. README 推荐 thin profile，但保留 legacy 作为兼容默认或可回退模式。

## 风险与控制

| 风险 | 控制 |
|:-----|:-----|
| Agent 不知道先查 capability | `ue_context_get` 返回 `recommended_next`，README 给固定工作流 |
| capability 响应过大 | 强制 `index/schema/examples/full` 分级 |
| `ue_execute` 变成过强入口 | registry allowlist、risk、dry-run、禁止任意 reflection |
| 旧脚本找不到工具 | 迁移期保留 `legacy` profile |
| diff token 实现复杂 | 第一阶段只做 request 生命周期 token |
| token 与外部编辑冲突 | state token 校验，冲突时返回 `state_invalidated` |
| 大 payload handle 调试麻烦 | `response.mode="debug"` 显式读取 artifact |
| 调试信息不够 | `response.mode="debug"` 保留完整 bridge envelope |

## 结论

Thin facade 是比单纯压缩单个工具返回更根本的 MCP 上下文优化。它把默认上下文压力从工具 schema 层移走，同时保留完整 UE 能力作为内部 operation 指令集。正确实现方式是“少量公开 MCP 工具 + 内部 operation registry + 按需 capability + execute + diff”，而不是隐藏 MCP tools 后继续直接调用。
