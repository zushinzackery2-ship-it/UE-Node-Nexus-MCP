<div align="center">

# UE Node Nexus MCP

**Fixed MCP bridge for Unreal Engine Material and Blueprint graph workflows**

*Typed, compact, compile-aware tools without arbitrary model-authored Python execution*

![C++](https://img.shields.io/badge/C%2B%2B-20-blue?style=flat-square)
![Python](https://img.shields.io/badge/Python-3.11%2B-green?style=flat-square)
![Unreal](https://img.shields.io/badge/Unreal-5.5-black?style=flat-square)
![Platform](https://img.shields.io/badge/Platform-Windows%20x64-lightgrey?style=flat-square)
![License](https://img.shields.io/badge/License-MIT-lightgrey?style=flat-square)

</div>

---

> [!NOTE]
> **Repository Boundary**
>
> This project is a node-graph MCP bridge for Unreal Editor. It focuses on asset discovery, Material/Blueprint graph inspection and edits, compile diagnostics, material instance parameters, and safe package save flows. It intentionally excludes broad scene layout automation and arbitrary Python execution.

---

## 功能概览

| 功能 | 说明 |
|:-----|:-----|
| **固定 MCP 工具** | Python MCP server exposes typed operations and forwards validated payloads to the UE bridge. |
| **UE Editor Bridge** | UE 5.5 Editor plugin serves `http://127.0.0.1:8765/mcp` through a local HTTP endpoint. |
| **图快照读取** | `graph_snapshot_get` supports `wires_tiny`, `wires_min`, `wires`, `compact`, and `full` formats. |
| **图安全写入** | `graph_patch_apply` edits Blueprint pins or Material expression links and returns diff, pin integrity, compile state, and dirty state. |
| **节点参数读写** | `node_params_get` and `node_params_set` expose Blueprint input pin defaults and Material expression editable properties. |
| **材质实例参数** | `material_instance_params_get` and `material_instance_params_set` read/write scalar, vector, texture, and static switch parameters. |
| **Blueprint 摘要** | `blueprint_details_get` reads class metadata, variables, selected CDO defaults, and component templates. |
| **AnimBlueprint 摘要** | `anim_blueprint_summary_get` extracts compact semantic summaries for common AnimGraph nodes. |
| **资产创建** | `asset_create` supports fixed creation for Material, Material Instance Constant, and Blueprint assets. |
| **编译与保存** | `asset_compile`, `asset_validate`, and `asset_save` return structured diagnostics and package state. |

---

## 核心 API

| 分类 | API | 说明 |
|:-----|:----|:-----|
| **Assets** | `asset_list()` | List Unreal assets in compact or full format. |
| **Assets** | `asset_get()` | Read metadata for one asset. |
| **Assets** | `asset_create()` | Create Material, Material Instance, or Blueprint assets with dry-run support. |
| **Level** | `level_current_get()` | Read current editor level identity and dirty state. |
| **Level** | `level_actors_list()` | List current level actors with optional component rows. |
| **Blueprint** | `blueprint_details_get()` | Read Blueprint metadata, variables, CDO defaults, and components. |
| **Blueprint** | `anim_blueprint_summary_get()` | Read compact semantic summaries for common AnimGraph nodes. |
| **Graph** | `graph_snapshot_get()` | Read Material or Blueprint graph topology. Default format is `wires_tiny`. |
| **Graph** | `graph_patch_apply()` | Apply declarative graph edits with post-write checks. |
| **Node Params** | `node_params_get()` | Read editable parameters for one graph node. |
| **Node Params** | `node_params_set()` | Write node parameters with compile diagnostics. |
| **Material Instance** | `material_instance_params_get()` | Read material instance parameter values. |
| **Material Instance** | `material_instance_params_set()` | Write material instance parameters with typed validation. |
| **Diagnostics** | `asset_compile()` | Compile Blueprint or Material assets and return diagnostics. |
| **Diagnostics** | `asset_validate()` | Validate an asset and return machine-readable results. |
| **Diagnostics** | `diagnostics_get()` | Read recent bridge diagnostics. |
| **Save** | `asset_save()` | Save one asset package with dirty/read-only/editor conflict reporting. |

---

## 返回格式

| 格式 | 用途 |
|:-----|:-----|
| **`wires_tiny`** | Default graph snapshot format. Minimal node dictionary, edge table, and type stats for low context usage. |
| **`wires_min`** | Less abbreviated wire text with node dictionary and edge rows. |
| **`wires`** | Human-readable horizontal wire table. |
| **`compact`** | Column + row arrays with alias maps back to real UE node and pin identifiers. |
| **`full`** | Verbose object-per-node shape for debugging. |

> [!IMPORTANT]
> **Write Safety**
>
> Write tools default to `dry_run=true`. Real writes return applied diff, pin integrity, compile result, dirty state, diagnostics, and warnings in one response.

---

## 目录结构

```
UE-Node-Nexus-MCP/
├── Plugins/
│   └── UeNodeNexusBridge/
│       ├── Source/
│       └── UeNodeNexusBridge.uplugin
├── docs/
│   ├── ARCHITECTURE.md
│   └── UE_BRIDGE_CONTRACT.md
├── scripts/
│   ├── build_plugin_ue55.bat
│   ├── install_ue55_plugin.bat
│   ├── package_plugin_ue55.ps1
│   └── run_mcp_server.py
├── src/
│   └── ue_node_nexus_mcp/
├── tests/
├── pyproject.toml
└── README.md
```

---

## 运行

```bash
python -m ue_node_nexus_mcp.server
```

| 环境变量 | 默认值 | 说明 |
|:-----|:-----|:-----|
| **`UE_NEXUS_BRIDGE_URL`** | `http://127.0.0.1:8765` | UE bridge endpoint. |
| **`UE_NEXUS_TIMEOUT_SECONDS`** | `30` | Bridge HTTP timeout in seconds. |

---

## 构建

```bat
.\scripts\build_plugin_ue55.bat
```

```bat
.\scripts\install_ue55_plugin.bat
```

```bash
powershell -ExecutionPolicy Bypass -File .\scripts\package_plugin_ue55.ps1
```

| 脚本 | 说明 |
|:-----|:-----|
| **`scripts/build_plugin_ue55.bat`** | Package the UE 5.5 bridge plugin with RunUAT. |
| **`scripts/install_ue55_plugin.bat`** | Copy packaged plugin into `UE_5.5\Engine\Plugins\Marketplace`. |
| **`scripts/package_plugin_ue55.ps1`** | Create `bin\dist\UeNodeNexusBridge-UE5.5-Win64.zip` and mirror it to `G:\vdio\UEPlugins\MCP`. |

---

## 来源说明

| 来源 | 影响范围 |
|:-----|:-----|
| **[`bunkerboy258/ue-blueprint-dumper`](https://github.com/bunkerboy258/ue-blueprint-dumper)** | Blueprint CDO/default/component inspection and AnimBlueprint semantic summary feature ideas. |

UE Node Nexus MCP keeps a separate runtime shape: fixed typed MCP tools plus a UE C++ Editor bridge, without arbitrary model-authored Python execution.

---

<div align="center">

**Platform:** Windows x64 | **Unreal:** 5.5 | **License:** MIT

</div>
