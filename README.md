<div align="center">

# UE Node Nexus MCP

**Unreal Engine 材质与蓝图图编辑工作流的 MCP 桥接工具**

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
> 本项目是 Unreal Editor 的节点图 MCP 桥接器，聚焦于资产发现、材质/蓝图图结构检查与编辑、编译诊断、材质实例参数读写以及安全的包保存流程。不包含场景布局自动化和任意 Python 执行功能。

---

## 功能概览

| 功能 | 说明 |
|:-----|:-----|
| **固定 MCP 工具** | Python MCP Server 暴露类型化操作，向 UE 桥接器转发经过校验的请求载荷 |
| **UE 编辑器桥接** | UE 5.5 编辑器插件通过本地 HTTP 端点 `http://127.0.0.1:8765/mcp` 提供服务 |
| **图快照读取** | `graph_snapshot_get` 支持 `wires_tiny`、`wires_min`、`wires`、`compact`、`full` 五种格式 |
| **图安全写入** | `graph_patch_apply` 编辑蓝图引脚或材质表达式连线，返回差异、引脚完整性、编译状态和脏标记 |
| **节点参数读写** | `node_params_get` 和 `node_params_set` 暴露蓝图输入引脚默认值和材质表达式可编辑属性 |
| **材质实例参数** | `material_instance_params_get` 和 `material_instance_params_set` 读写标量、向量、纹理和静态开关参数 |
| **蓝图摘要** | `blueprint_details_get` 读取类元数据、变量、CDO 默认值和组件模板 |
| **动画蓝图摘要** | `anim_blueprint_summary_get` 提取常用 AnimGraph 节点的紧凑语义摘要 |
| **资产创建** | `asset_create` 支持材质、材质实例常量和蓝图资产的固定创建流程 |
| **编译与保存** | `asset_compile`、`asset_validate`、`asset_save` 返回结构化诊断和包状态 |

---

## 核心 API

| 分类 | API | 说明 |
|:-----|:----|:-----|
| **资产** | `asset_list()` | 列出 Unreal 资产，支持紧凑和完整格式 |
| **资产** | `asset_get()` | 读取单个资产的元数据 |
| **资产** | `asset_create()` | 创建材质、材质实例或蓝图资产，支持试运行 |
| **关卡** | `level_current_get()` | 读取当前编辑器关卡标识和脏标记状态 |
| **关卡** | `level_actors_list()` | 列出当前关卡的 Actor，可选包含组件行 |
| **蓝图** | `blueprint_details_get()` | 读取蓝图元数据、变量、CDO 默认值和组件 |
| **蓝图** | `anim_blueprint_summary_get()` | 读取常用 AnimGraph 节点的紧凑语义摘要 |
| **图** | `graph_snapshot_get()` | 读取材质或蓝图图拓扑，默认格式为 `wires_tiny` |
| **图** | `graph_patch_apply()` | 应用声明式图编辑，附带写后检查 |
| **节点参数** | `node_params_get()` | 读取单个图节点的可编辑参数 |
| **节点参数** | `node_params_set()` | 写入节点参数，附带编译诊断 |
| **材质实例** | `material_instance_params_get()` | 读取材质实例参数值 |
| **材质实例** | `material_instance_params_set()` | 写入材质实例参数，附带类型校验 |
| **诊断** | `asset_compile()` | 编译蓝图或材质资产，返回诊断信息 |
| **诊断** | `asset_validate()` | 校验资产，返回机器可读的结果 |
| **诊断** | `diagnostics_get()` | 读取最近的桥接器诊断信息 |
| **保存** | `asset_save()` | 保存单个资产包，报告脏标记/只读/编辑器冲突状态 |

---

## 返回格式

| 格式 | 用途 |
|:-----|:-----|
| **`wires_tiny`** | 默认图快照格式。最小节点字典、边表和类型统计，低上下文占用 |
| **`wires_min`** | 精简的连线文本，含节点字典和边行 |
| **`wires`** | 人类可读的水平连线表 |
| **`compact`** | 列+行数组，带别名映射回真实 UE 节点和引脚标识符 |
| **`full`** | 详细的一节点一对象格式，用于调试 |

> [!IMPORTANT]
> **写入安全**
>
> 写入工具默认 `dry_run=true`。实际写入时，单个响应中返回已应用的差异、引脚完整性、编译结果、脏标记、诊断和警告信息。

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
| **`UE_NEXUS_BRIDGE_URL`** | `http://127.0.0.1:8765` | UE 桥接器端点 |
| **`UE_NEXUS_TIMEOUT_SECONDS`** | `30` | 桥接器 HTTP 超时时间（秒） |

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
| **`scripts/build_plugin_ue55.bat`** | 使用 RunUAT 打包 UE 5.5 桥接插件 |
| **`scripts/install_ue55_plugin.bat`** | 将打包好的插件复制到 `UE_5.5\Engine\Plugins\Marketplace` |
| **`scripts/package_plugin_ue55.ps1`** | 创建 `bin\dist\UeNodeNexusBridge-UE5.5-Win64.zip` 并镜像到 `G:\vdio\UEPlugins\MCP` |

---

## 参考项目

- **[`bunkerboy258/ue-blueprint-dumper`](https://github.com/bunkerboy258/ue-blueprint-dumper)** — 蓝图 CDO/默认值/组件检查和动画蓝图语义摘要功能的参考

---

<div align="center">

**平台:** Windows x64 | **引擎:** Unreal 5.5 | **许可证:** MIT

</div>
