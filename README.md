<div align="center">

# UE Node Nexus MCP

**Unreal Editor 的 MCP 桥接：7 个 facade 工具读写运行中的编辑器，`.nexus` 文本镜像把材质、蓝图、Niagara 变成可 grep、可 diff、可回滚的源文件**

*UE 只做编译器；Agent 用普通文件工具改资产，`ue_sync` 负责三方同步*

![C++](https://img.shields.io/badge/C%2B%2B-20-blue?style=flat-square)
![Python](https://img.shields.io/badge/Python-3.11%2B-green?style=flat-square)
![Unreal](https://img.shields.io/badge/Unreal-5.5-black?style=flat-square)
![Platform](https://img.shields.io/badge/Platform-Windows%20x64-lightgrey?style=flat-square)
![License](https://img.shields.io/badge/License-MIT-lightgrey?style=flat-square)

</div>

---

> [!NOTE]
> **仓库边界**  
> 资产发现与依赖图、Material / Blueprint / Niagara 图的检查与编辑、编译诊断与日志、Material Instance 参数、窄类型化的关卡 Actor 生命周期（含按点路径写关卡 Actor / 组件的可编辑属性）、文本镜像同步。不提供任意 Python / 控制台命令执行、对资产或 CDO 的泛化 UObject 反射写入、场景模板类工具。

## 功能概览

| 功能 | 说明 |
|:-----|:-----|
| **文本镜像（Content_Transcoded）** | Material / MaterialFunction / MaterialInstance / Blueprint / Niagara / DataAsset 导出为行式 `.nexus` 文本；`ue_sync` 做 `status / pull / lint / push`，push 是按稳定 id 的增量 patch，每资产一个编辑器事务 |
| **7 工具 facade** | `list_tools` 固定 7 个入口，132 个内部 operation 先查 schema 再执行，压低上下文占用 |
| **图读写** | Material / MaterialFunction / Blueprint 图快照、节点信息、声明式 patch、整图 build |
| **诊断** | MessageLog、资产编译诊断、UE 日志尾读取、离线材质 lint |
| **Niagara / Cascade** | Niagara System / Emitter / Module Stack / Renderer / User 参数读写与编译，Cascade 只读摘要 |
| **关卡** | Actor 枚举、spawn / delete / transform、地图切换、组件材质槽与 MID 参数、Landscape LayerInfo |
| **资产管理** | 创建 / 删除 / 移动 / 重命名 / 复制、redirector 修复、依赖与引用图、AutoIndex 持久索引 |
| **编辑器安全** | 所有保存直写 `UPackage::SavePackage`，不弹源码管理对话框；材质写入前取消在途着色器编译；拒绝嵌套请求 |

## 架构

```text
MCP Client (stdio)
    │
    ▼
Python MCP Server  ue-node-nexus-mcp（7 个 facade 工具，132 个 operation）
    │  命名管道 \\.\pipe\UeNodeNexusBridge.<pid>        Content_Transcoded/<Project>/**.nexus
    ▼                                                   ▲ 磁盘到磁盘导出 / 事务化 apply
UE Editor 插件（所有请求在 game thread 串行执行）        │
    ├── UeNodeNexusBridge      核心：资产 / 图 / 蓝图 / 关卡 / 诊断 / transcode
    └── UeNodeNexusVfxBridge   VFX：Niagara authoring、Cascade 摘要、vfx_transcode
```

---

## 核心 API

| 分类 | API | 说明 |
|:-----|:----|:-----|
| **上下文** | `ue_context_get(include_counts)` | 启用的 group、绑定的编辑器实例、推荐下一跳 |
| **能力** | `ue_capability_get(group, operation, detail)` | operation 索引（`index`）、单个 operation 的 `schema` / `examples` |
| **执行** | `ue_execute(operation, payload, response)` | 执行任一 operation；写默认 `delta`，读默认 `summary` |
| **读取** | `ue_read(target, asset_path, format, query)` | 类型化读取，大响应以 artifact 句柄返回 |
| **变更** | `ue_diff_get(since_token, cursor, limit)` | 按 diff token 读取变更，`next_cursor` 分页 |
| **校验** | `ue_plan_validate(operations)` | 批量校验存在性、风险等级、预计变更，不写 UE |
| **同步** | `ue_sync(action, paths, options)` | 文本镜像：`init` / `status` / `pull` / `lint` / `push` / `schema` |

`ue_read` 的 target：`auto` `artifact` `asset` `asset_dependencies` `asset_referencers` `asset_index` `graph` `graph_node_search` `node` `blueprint` `anim_blueprint` `anim_state_machine` `anim_montage` `blend_space` `material_instance` `niagara_system` `niagara_stack` `cascade_system` `level` `log` `diagnostics` `project_input` `input_mapping_context` `sound_cue` `texture`。没有 `target="material"`：材质图用 `graph`，实例参数用 `material_instance`，类型未知用 `auto`。

---

## 文本镜像（`ue_sync`）

镜像根目录默认为 MCP server cwd 下的 `Content_Transcoded/`（`UE_NEXUS_TRANSCODE_DIR` 覆盖），每个 UE 工程一个子目录：

```
Content_Transcoded/
├── .nexus/schema/<engine>-<plugins_hash>/   schema lock：类 → 可编辑属性 / 默认值 / 枚举、MF 签名、Niagara 模块索引
└── <Project>/
    ├── Materials/M_Glass.mat.nexus          .mf 材质函数  .mi 材质实例  .bp 蓝图  .ns/.ne Niagara  .asset 属性包  .stub 只读桩
    ├── .nexus/base/                         上次同步的全保真导出（id ↔ GUID、opaque 节点原文）= 三方合并基准
    └── .nexus/state.json                    每资产的 base / 文本 / UE 三方哈希
```

| 动作 | 作用 | 常用 options |
|:-----|:-----|:-------------|
| **`init`** | 绑定根目录、导出 schema、全量 pull | `include_stubs` `refresh_schema` |
| **`status`** | 三方状态：`clean` `local-modified` `ue-modified` `both-modified` `local-new` `ue-new` `local-deleted` `ue-deleted` | `discover` `include_clean` |
| **`pull`** | UE → 文本；冲突需 `force="ue"` | `force` `discover` |
| **`lint`** | 离线校验：未知类 / 属性 / 枚举 / 引脚 / 类型、悬空连线、opaque 编辑 | — |
| **`push`** | 文本 → UE：默认 dry run 回 plan；`dry_run=false` 时一资产一事务、编译、只保存被碰的包、重导出规范文本 | `dry_run` `compile` `save` `force` `allow_delete` `stop_on_error` |
| **`schema`** | 重导 schema lock（引擎或插件集变化后） | — |

文本一行一个事实，只写非默认值，值文法直接用 UE `ExportText`，文件里不出现 GUID：

```
[asset]
BlendMode = BLEND_Translucent

[graph]
c_eps : Constant(R=0.000001) @ -1000,220
call  : MaterialFunctionCall(MaterialFunction=/Game/F/MF_A.MF_A)
old   : @opaque(/Script/Engine.MaterialExpressionCustom) @ 0,0      # 只能移动 / 删除 / 连线

c_eps -> call.A
call -> out.BaseColor                                                # 材质的隐含 out 节点
c_eps -> out.WorldPositionOffset
```

- 新建资产：直接写 `.nexus` 文件再 push，材质 / 材质函数 / 材质实例 / 蓝图（含 `ParentClass`）/ DataAsset 会被创建。
- MaterialFunction 输入输出接口变化时自动刷新全部调用者并重新 pull。
- 蓝图未识别节点类、Niagara 的 Event / Stage 栈、`@link` / `@dynamic` 输入、继承组件属性以 opaque / 只读形式保真往返。
- 完整格式：`ue_execute("workflow_guide_get", {"category": "text_mirror"})`，设计文档 `.plan/Transcode-Layer.md`。

---

## 内部 operation registry

132 个 operation 的元数据（group、读写、风险、bridge / local、hidden、默认响应粒度）单源维护在 `src/ue_node_nexus_mcp/operations.json`，Python 注册表与 payload schema 从它派生，测试保证与 C++ 注册表对齐。

| Group | 数量 | 覆盖范围 |
|:------|:----:|:---------|
| `core` | 22 | 桥接诊断、编译 / 校验 / 保存、MessageLog、日志尾、实例管理、工作流指南、批量执行、后台任务、关卡视口截图（同步出图）与视口相机读写 |
| `transcode` | 6 | 文本镜像 UE 端：`transcode_root_set` `transcode_status` `transcode_export` `transcode_apply` `schema_export` `transcode_watch_set` |
| `asset` | 14 | 创建 / 删除 / 移动 / 重命名 / 复制、批量、文件夹、redirector、依赖与引用图 |
| `auto_index` | 12 | UE 内持久资产索引：查询、树、概览、路径解析 |
| `graph` | 12 | 图快照、整图节点信息、声明式 patch、整图 build、节点参数读写（hidden，镜像覆盖） |
| `material` | 4 | Material Instance 参数、表达式类枚举、离线 lint（hidden，镜像覆盖） |
| `blueprint` | 6 | 蓝图详情 / 组件树（hidden，镜像覆盖）、AnimBP 状态机写入 |
| `level` | 18 | Actor 枚举 / spawn / delete / transform、地图切换、UObject 属性读取、**Actor / 组件属性按点路径写入**（`level_actor_properties_set`，如 PostProcessVolume 的 `Settings.AutoExposureMethod`）、材质槽与 MID 参数、Landscape LayerInfo |
| `vfx` | 28 | Niagara System / Emitter / Stack / Renderer / User 参数 / 材质（hidden，镜像覆盖）、lint、编译、Cascade 摘要、`vfx_transcode_*` |
| `animation` | 2 | AnimMontage、BlendSpace 摘要 |
| `audio` | 1 | SoundCue 摘要 |
| `texture` | 1 | Texture 摘要 |
| `project_input` | 6 | 传统 action / axis mappings、Enhanced Input 资产创建与映射 |

- 120 个转发到 UE，12 个为 MCP 本地 operation：`bridge_contract_check` `bridge_instance_list` `bridge_instance_select` `workflow_guide_get` `batch_execute` `task_submit` `task_status` `task_result` `task_cancel` `log_tail_get` `viewport_capture_status` `material_lint`。
- 49 个 `hidden`：6 个高危 / 兼容 op（`editor_save_all` `editor_request_exit` `auto_index_clear` 等）和 43 个被文本镜像取代的资产形态读写 op。仍可按名查 schema 与执行，只是不进默认索引；`include_hidden=true` 列出。
- 8 类任务级指南由 `workflow_guide_get` 在会话内提供：`getting_started` `text_mirror` `graph_editing` `material_authoring` `blueprint_authoring` `niagara_authoring` `diagnostics_repair` `concurrency`，正文在 `src/ue_node_nexus_mcp/guides/`。

---

## 响应与上下文控制

| 机制 | 说明 |
|:-----|:-----|
| **写默认 dry-run** | 改 UE 状态的写 operation 默认 `dry_run=true`；实际写入返回差异、引脚完整性、编译结果、脏标记与诊断 |
| **响应模式** | `ue_execute.response.mode` ∈ `silent` `brief` `ids_only` `delta` `summary` `full` `debug`；`response` 只接受 `mode` 与 `allow_heavy`，读取粒度放在 payload 的 `format` |
| **artifact** | 超阈值的大响应返回 `artifact.id` 与摘要，`ue_read(target="artifact", query={"artifact_id": ...})` 取回 |
| **diff 分页** | `ue_diff_get` 超过 `limit` 时返回 `next_cursor` |
| **重读拦截** | 整图 `node_params_format="full"` 等重负载读取需显式 `response.allow_heavy=true` |
| **批量与后台** | `batch_execute` 顺序执行 ≤ 20 条（先整批校验，遇错即停，非事务）；`task_submit` 把长调用排到后台工作线程，`task_status` / `task_result` / `task_cancel` 跟踪 |

## 编辑器安全保证

| 保证 | 实现 |
|:-----|:-----|
| **不弹对话框** | 所有保存（`asset_save`、`editor_save_all`、写 op 的 `save=true`、`ue_sync push`）直接走 `UPackage::SavePackage`；文件只读（源码管理未检出）时返回 `save_blocked_read_only` 而不是弹「无法检出」模态框 |
| **不撞着色器编译** | 材质图写入前调用 `CancelOutstandingCompilation()`，整批只在末尾编译一次 |
| **不嵌套请求** | 一个请求执行中到达的请求返回 `bridge_busy`，MCP server 自动重试约 2 秒 |
| **不 SaveAll** | 只保存被写入的包；`editor_save_all` 逐包直写并列出失败项 |
| **类名不含糊** | 材质节点类接受 `/Script/Engine.MaterialExpressionX` / `MaterialExpressionX` / `X`，解析失败返回 `unknown_node_class` |

---

## 快速开始

**1. 编译并安装 UE 插件**

仓库不发布二进制。最简单的方式是把 `Plugins/UeNodeNexusBridge`（必需）和 `Plugins/UeNodeNexusVfxBridge`（Niagara 需要）复制到工程 `Plugins/`，打开工程触发编译。作为引擎插件安装时用 UAT 打包，先核心再 VFX（VFX 依赖核心）：

```bat
"<UE_5.5>\Engine\Build\BatchFiles\RunUAT.bat" BuildPlugin -Plugin="<repo>\Plugins\UeNodeNexusBridge\UeNodeNexusBridge.uplugin" -Package="<out>\UeNodeNexusBridge" -TargetPlatforms=Win64 -Rocket
```

> [!IMPORTANT]
> **BuildPlugin 会改写 `.uplugin`**  
> 打包产物里的 `.uplugin` 丢失 `EnabledByDefault`，装进 `Engine/Plugins/Editor/` 后要把仓库里的 `.uplugin` 拷回去，否则插件不会随工程加载。

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

没有 console entry 时用 `"command": "python", "args": ["-m", "ue_node_nexus_mcp.server"]`。

**4. 安装 Agent skill**

把 `skill/ue-node-nexus-mcp/` 复制到 Agent 的 skill 目录（Cursor：`~/.cursor/skills/`；Claude Code：`~/.claude/skills/`）。它约定「先查 schema 再调用」、读写 operation 的区分和文本镜像工作流。

**5. 验收连接**

```text
ue_execute("project_context_get", {})      # .uproject 与 /Game mount
ue_execute("bridge_contract_check", {})    # Python 合同与 UE 端 operation 一致
ue_sync("init")                            # 拉取整个工程到文本镜像
```

---

## 配置

| 环境变量 | 默认值 | 说明 |
|:---------|:-------|:-----|
| `UE_NEXUS_TIMEOUT_SECONDS` | `30` | 桥接请求超时（秒），大编译请调高 |
| `UE_NEXUS_RESPONSE_MODE` | `minimal` | facade 响应模式：`minimal` / `full` |
| `UE_NEXUS_FEATURES` | 全部 group | 显式启用的 operation group，逗号分隔 |
| `UE_NEXUS_ENABLE_FEATURES` | — | 在当前集合上追加 group |
| `UE_NEXUS_DISABLE_FEATURES` | — | 从当前集合移除 group |
| `UE_NEXUS_VFX_SUPPORT` | — | `true` / `false`，VFX group 本地意图开关 |
| `UE_NEXUS_TRANSCODE_DIR` | `<cwd>/Content_Transcoded` | 文本镜像根目录 |

CLI 同名开关在 server 启动时解析：`--response-mode` `--features` `--enable-feature` `--disable-feature` `--vfx-support`。可用 group：`core asset auto_index graph material blueprint animation audio level project_input texture vfx`。VFX 由 UE 端最终裁决（`bridge_capabilities_get().data.modules.vfx_available`），本地配置只能关闭或表达意图。

多个编辑器同时在线时每个实例各有一条 `\\.\pipe\UeNodeNexusBridge.<pid>` 管道：`ue_execute("bridge_instance_list", {})` 查看，`ue_execute("bridge_instance_select", {"pid": 1234})` 或 `{"project": "工程名"}` 绑定。

---

## 目录结构

```
UE-Node-Nexus-MCP/
├── Plugins/
│   ├── UeNodeNexusBridge/            核心 UE 编辑器插件（C++）
│   │   └── Source/.../Private/Transcode/   文本镜像的导出 / apply / schema
│   └── UeNodeNexusVfxBridge/         Niagara / Cascade 插件（C++）
├── src/ue_node_nexus_mcp/
│   ├── operations.json               operation 元数据单一事实源
│   ├── tools_facade.py               ue_context_get / ue_capability_get / ue_execute / ue_read / ue_diff_get / ue_plan_validate
│   ├── tools_sync.py                 ue_sync
│   ├── transcode/                    .nexus 解析 / 生成、raw 编解码、lint、diff → plan、三方 state、pull / push 编排
│   ├── guides/                       workflow_guide_get 的指南正文
│   ├── facade_*.py                   capability / execute / read / plan / response / state
│   └── tools_*.py                    各 group 的 payload 构造与客户端逻辑
├── tests/                            pytest（假 bridge 注入，不需要 UE）
│   ├── transcode/                    文本格式与同步引擎的不变量测试
│   └── compile_check/                新增 C++ TU 的 clang 桩编译检查
├── skill/ue-node-nexus-mcp/          Agent skill
├── .plan/Transcode-Layer.md          文本镜像设计与验收记录
└── pyproject.toml
```

## 开发与测试

```bash
pip install -e . && python -m pytest tests -q
```

| 护栏 | 说明 |
|:-----|:-----|
| **契约对齐** | `operations.json` 与 C++ 注册表逐名对齐 |
| **300 行预算** | 所有源码文件（`.py` `.h` `.cpp` `.cs` `.inl`）不超过 300 行 |
| **clang 桩编译** | 新增 C++ TU 用宿主机 clang 对照 UE 5.5 桩头做语法 / 类型检查，无 clang 时跳过 |
| **镜像不变量** | 每种资产 raw → 文本 → 解析 → 再生成逐字节一致，pull 后立即 push 的 plan 为空 |

实机验收用无头编辑器：`UnrealEditor-Cmd <Project>.uproject -nullrhi -unattended -nosplash -NoSound`，约 80 秒后命名管道出现即可连接。Launcher 版引擎在 commandlet 阶段尚未加载插件模块，不要走 `-run=` commandlet。

---

## 兼容性

| 项目 | 说明 |
|:-----|:-----|
| **实测环境** | UE 5.5 Launcher，Windows x64。文本镜像在真实工程上验收：30 个资产 init → status 全 clean → pull 后 push 空 plan；从文本新建 MF / 材质 / 蓝图 / Niagara 并往返编辑，编译 0 错误 |
| **跨版本** | 插件二进制与 UE 版本 / 编译器 / 模块 ABI 绑定，换版本请按源码重编；schema lock 以 `<引擎版本>-<插件集哈希>` 为 key，变化后 `ue_sync("schema")` |
| **平台** | 传输层为 Windows 命名管道，server 与编辑器需在同一台 Windows 机器 |
| **v1 边界** | Niagara emitter 资产只读、模块重排序不支持（删 + 加）、`SetVariables` 模块可编辑不可新建、蓝图 `ParentClass` 只在创建时生效、宏 / 委托 / 接口与 Event / Stage 栈只读 |

## 参考项目

- [`bunkerboy258/ue-blueprint-dumper`](https://github.com/bunkerboy258/ue-blueprint-dumper) — 蓝图 CDO / 组件检查与动画蓝图语义摘要的参考

---

<div align="center">

**平台:** Windows x64 | **引擎:** Unreal 5.5 | **许可证:** MIT

</div>
