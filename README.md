<div align="center">

<img src="assets/branding/nexus-512.png" width="112" height="112" alt="UE Node Nexus MCP icon">

# UE Node Nexus MCP

**通过 MCP 查询、编辑和验证 Unreal Engine 项目。**

*类型化操作接口，文本资产镜像，编辑器内编译与回读。*

![Unreal Engine](https://img.shields.io/badge/Unreal_Engine-5.5-313131?style=flat-square)
![Python](https://img.shields.io/badge/Python-3.11%2B-3776AB?style=flat-square)
![Platform](https://img.shields.io/badge/Platform-Windows_x64-0078D4?style=flat-square)
![License](https://img.shields.io/badge/License-MIT-2E8B57?style=flat-square)

</div>

---

## 简述

UE 材质、蓝图等资产包含大量节点、引脚和属性。传统的 MCP 逐节点读写模式会让这些结构反复以 JSON 进入模型上下文，一次编辑也可能拆成多次工具调用；图越大，上下文占用、调用开销和维护中间状态的成本越高。

因此，项目增加了 **本地解码层**：由 UE 导出真实资产结构，Python 在本地将其转换为紧凑的 `.nexus` 文本，并负责校验、计算差异和生成执行计划。AI 编辑文本，UE 应用差异、编译并回读结果，完整导出与节点映射保留在本地。MCP 仍承担调度和诊断反馈，资产编辑则通过文本差异批量提交，减少重复传输与逐项调用，也让修改可以审阅、失败可以恢复。

> [!NOTE]
> **运行环境**
>
> 当前验证平台为 Windows x64、Unreal Engine 5.5 和 Python 3.11+。MCP 服务与编辑器通过本机命名管道通信，插件需要按目标引擎版本编译。

## 功能

| 功能 | 内容 |
|:-----|:-----|
| **文本资产镜像** | Material、MaterialFunction、MaterialInstance、Blueprint、Niagara System 和属性型资产的导出、校验、差异计划与提交 |
| **资产查询与管理** | 资产索引、元数据、依赖与引用关系，以及创建、复制、移动、重命名、删除和 redirector 修复 |
| **图与蓝图** | 节点、引脚、连接、变量、组件、函数，以及动画蓝图与状态机摘要 |
| **关卡操作** | Actor、变换、组件属性、材质槽、Landscape LayerInfo、关卡切换、视口相机与截图 |
| **VFX** | Niagara 发射器、模块栈、渲染器和用户参数；Cascade 系统摘要 |
| **诊断与批处理** | 编译诊断、MessageLog、日志尾、离线材质检查、批量执行和后台任务 |

当前操作注册表包含 **132 个 operation**。其中 49 个兼容或底层操作从默认能力索引隐藏，仍可按名称查询与调用。完整清单由 [operations.json](src/ue_node_nexus_mcp/operations.json) 维护。

---

## 安装

### 使用发行包

[最新 Release](https://github.com/zushinzackery2-ship-it/UE-Node-Nexus-MCP/releases/latest) 提供 UE 5.5 Windows x64 双插件 ZIP、Python Wheel 和 SHA256 校验文件。关闭编辑器，将 ZIP 中的 `Plugins/` 合并到工程根目录，再安装下载的 Wheel；配置 MCP 客户端时参照下方第 3 步。

```bat
py -3 -m venv .venv
.venv\Scripts\python.exe -m pip install ue_node_nexus_mcp-0.3.0-py3-none-any.whl
```

源码安装与构建流程如下。

### 1. 获取源码和 Python 服务

```bat
git clone https://github.com/zushinzackery2-ship-it/UE-Node-Nexus-MCP.git
cd UE-Node-Nexus-MCP
py -3 -m venv .venv
.venv\Scripts\python.exe -m pip install .
```

### 2. 编译 UE 插件

安装 Visual Studio 2022 的 C++ 游戏开发工作负载、MSVC 工具链和 Windows SDK。将 `UE_NEXUS_ENGINE_DIR` 指向本机 UE 5.5 安装目录：

```bat
set "UE_NEXUS_ENGINE_DIR=D:\Unreal\UE_5.5"
tests\compile_check\build_plugins.bat
```

脚本使用 `vswhere` 查找 Visual Studio，在 `build/validation/` 内创建独立工程，并编译两个插件。输出位于 `build/validation/Plugins/`。

关闭目标编辑器，将编译后的插件复制到 UE 工程的 `Plugins/` 目录：

```bat
set "UE_PROJECT_DIR=D:\UEProjects\MyProject"
robocopy "build\validation\Plugins\UeNodeNexusBridge" "%UE_PROJECT_DIR%\Plugins\UeNodeNexusBridge" /E /XD Intermediate
robocopy "build\validation\Plugins\UeNodeNexusVfxBridge" "%UE_PROJECT_DIR%\Plugins\UeNodeNexusVfxBridge" /E /XD Intermediate
```

`UeNodeNexusBridge` 是必需插件；`UeNodeNexusVfxBridge` 用于 Niagara 和 Cascade。重新打开工程，在 Plugins 面板确认所需插件启用。

已有 C++ 工程也可以直接复制仓库中的 `Plugins/` 源码，通过工程自己的构建流程编译。

### 3. 配置 MCP 客户端

将下面路径替换为实际仓库与镜像目录，加入客户端的 MCP 配置：

```json
{
  "mcpServers":
  {
    "ue-node-nexus":
    {
      "command": "D:/Tools/UE-Node-Nexus-MCP/.venv/Scripts/ue-node-nexus-mcp.exe",
      "args": ["--response-mode", "minimal"],
      "env":
      {
        "UE_NEXUS_TRANSCODE_DIR": "D:/UEProjects/AssetMirror"
      }
    }
  }
}
```

启动加载了桥接插件的 UE 工程，再重连 MCP 客户端。可将 [随附 Agent skill](skill/ue-node-nexus-mcp/SKILL.md) 安装到客户端的技能目录，提供操作发现和文本镜像工作流。

---

## 核心 API

| 工具 | 用途 |
|:-----|:-----|
| **`ue_context_get`** | 查看已启用能力、当前编辑器和可用实例 |
| **`ue_capability_get`** | 查询操作索引、请求 schema 或 payload 示例 |
| **`ue_execute`** | 按名称执行类型化 operation |
| **`ue_read`** | 读取资产、图、关卡、诊断等数据；大结果使用 artifact 句柄 |
| **`ue_diff_get`** | 通过 diff token 分页读取变更 |
| **`ue_plan_validate`** | 验证批量操作的参数与执行计划 |
| **`ue_sync`** | 初始化、查询、拉取、校验、推送和刷新文本镜像 schema |

连接后先检查项目与桥接契约，再查询所需操作的 schema：

```text
ue_context_get(include_counts=true)
ue_execute("project_context_get", {}, response={"mode": "full"})
ue_execute("bridge_contract_check", {})
ue_capability_get(operation="level_actors_list", detail="schema")
```

多个编辑器同时运行时，调用 `bridge_instance_list` 查看实例，再通过 `bridge_instance_select` 的 `pid` 或 `project` 参数明确绑定。

`ue_read(target="graph")` 读取材质图，`target="material_instance"` 读取实例参数，`target="auto"` 解析未知资产类型。操作的 `format` 控制数据形状，`response.mode` 控制响应详细程度。

---

## 文本镜像

每个工程使用独立目录，资产路径映射为可审阅的文本文件：

```
Content_Transcoded/
  .nexus/schema/<key>/
  MyProject/
    Materials/M_Example.mat.nexus
    Materials/MI_Example.mi.nexus
    Blueprints/BP_Example.bp.nexus
    .nexus/base/
    .nexus/pending/
    .nexus/state.json
```

| 动作 | 行为 |
|:-----|:-----|
| **`init`** | 绑定镜像根目录、导出 schema、拉取资产 |
| **`status`** | 比较文本、同步基线与 UE 状态，另列 `ue_dirty` 和 `ue_saved_changed` |
| **`pull`** | 将 UE 状态导出为文本；冲突版本生成相邻 `.ue.nexus` 文件 |
| **`lint`** | 离线校验类、属性、枚举、引脚、类型和连线 |
| **`push`** | 默认返回 dry-run 计划；显式应用时按依赖执行、编译、保存和回读 |
| **`schema`** | 引擎版本或插件集合变化后更新反射 schema |

创建材质时，可以写入 `MyProject/Materials/M_Example.mat.nexus`。`schema` 填写初始化返回的 key：

```text
nexus: 1
asset: /Game/Materials/M_Example
class: Material
schema: <current-schema-key>

[graph]
value : Constant(R=0.5) @ 0,0
value -> out.Roughness
```

```text
ue_sync("init")
ue_sync("lint", paths=["/Game/Materials/M_Example"])
ue_sync("push", paths=["/Game/Materials/M_Example"])
ue_sync("push", paths=["/Game/Materials/M_Example"], options={"dry_run": false})
ue_sync("status")
```

文本仅记录非默认值。删除属性行表示恢复默认值，删除实例参数行表示清除 override；省略贴图属性表示使用引擎默认贴图。完整语法和类型边界见 [文本镜像指南](src/ue_node_nexus_mcp/guides/text_mirror.md)。

### 提交与恢复

| 机制 | 行为 |
|:-----|:-----|
| **依赖排序** | 新资产和材质依赖先于引用者提交；识别组件属性、默认值和嵌套数组引用 |
| **失败保护** | 应用、编译、保存或导出失败时保留本地编辑和旧基线，恢复记录写入 `.nexus/pending/*.push.json` |
| **冲突处理** | `both-modified` 要求明确选择；`force="local"` 从实时 UE 状态重新计算推送差异，`force="ue"` 用于拉取 UE 版本 |
| **部分成功重试** | 保存已创建节点的 ID 映射，重新计算剩余修改，避免重复建节点 |
| **调用者刷新** | MF 接口变更后刷新 UE 调用者；调用者本地编辑保持，同批提交前重新计算差异 |
| **文件提交** | 成功回读后更新文本、基线与状态；文件替换错误会恢复已替换文件 |

默认 `stop_on_error=true`。设为 false 时独立资产可以继续，失败资产的依赖方保持阻断。dry-run 保持文本、同步基线和 state 不变，schema 与原始导出仍可写入暂存目录。

---

## 配置与排障

| 环境变量 | 默认值 | 用途 |
|:-----|:-----|:-----|
| **`UE_NEXUS_TRANSCODE_DIR`** | `<cwd>/Content_Transcoded` | 文本镜像根目录 |
| **`UE_NEXUS_TIMEOUT_SECONDS`** | `30` | 桥接请求超时秒数 |
| **`UE_NEXUS_RESPONSE_MODE`** | `minimal` | MCP facade 返回 `minimal` 或 `full` |
| **`UE_NEXUS_FEATURES`** | 全部能力组 | 显式启用的组，逗号分隔 |
| **`UE_NEXUS_ENABLE_FEATURES`** | 空 | 追加启用的组 |
| **`UE_NEXUS_DISABLE_FEATURES`** | 空 | 禁用的组 |
| **`UE_NEXUS_VFX_SUPPORT`** | 自动探测 | `true` / `false`，VFX 能力意图 |

| 现象或错误码 | 处理 |
|:-----|:-----|
| **连接立即关闭** | 直接运行 `.venv\Scripts\ue-node-nexus-mcp.exe`；正常情况下等待 stdio 输入。出现导入错误时重新安装包，再重连客户端 |
| **`mcp_bridge_error`** | 检查编辑器进程、插件加载和实例绑定 |
| **`schema_stale`** | 执行 `ue_sync("schema")`，按新 schema 更新文本 |
| **`save_blocked_read_only`** | 先检出资产或恢复文件可写，再重试 |
| **`dependency_not_selected`** | 将所引用的本地新资产加入同一批选择 |
| **`dependency_cycle`** | 检查新资产之间的循环依赖 |
| **操作与 schema 对不上** | 同步更新 Python 服务与 UE 插件，重启编辑器并重连客户端 |

桥接保存使用 `UPackage::SavePackage`，只读检查失败直接返回诊断；重入请求返回 `bridge_busy`。材质图写入前取消该材质的在途编译，批量修改结束后统一编译。

---

## 目录与开发

```
UE-Node-Nexus-MCP/
  Plugins/
    UeNodeNexusBridge/       核心编辑器插件
    UeNodeNexusVfxBridge/    VFX 插件
  src/ue_node_nexus_mcp/
    operations.json        操作注册表
    guides/                客户端可查询的工作流指南
    transcode/             文本解析、schema、diff 和同步
      push/                计划、提交、恢复和调用者刷新
  tests/
    transcode/             同步行为与故障恢复回归
    compile_check/         编译桩与真实 UE 构建入口
    live/                  隔离编辑器集成验证
  skill/ue-node-nexus-mcp/  Agent skill
  release/                发行包组装与发布说明
  pyproject.toml
  LICENSE
```

```bat
.venv\Scripts\python.exe -m pip install -e . pytest
.venv\Scripts\python.exe -m pytest -q
set "UE_NEXUS_ENGINE_DIR=D:\Unreal\UE_5.5"
tests\compile_check\build_plugins.bat
.venv\Scripts\python.exe tests\live\sync_smoke.py
```

测试覆盖注册表契约、参数 schema、文本往返、依赖排序、失败保留、恢复重试和 300 行源码预算。clang 桩检查在缺少工具链时跳过；真实 UE 构建用于验证引擎 API 和链接。实机 runner 绑定自己启动的隔离编辑器，并在结束后关闭该测试进程。

## 支持边界

| 项目 | 当前范围 |
|:-----|:-----|
| **引擎版本** | 已验证 UE 5.5；其他版本需重新构建和验证 API |
| **Niagara** | emitter 资产、动态或链接输入、Event / Stage 栈按只读内容保留；模块重排序尚未实现 |
| **Blueprint** | ParentClass 仅用于新建；继承组件属性、宏、委托与接口存在只读边界 |
| **不透明节点** | `@opaque` 内容支持保留、移动、删除与连接，不能任意改写内部数据 |
| **批次原子性** | 每个资产单独事务；多资产批次可能部分完成，以返回诊断与同步状态为准 |

源码按 [MIT License](LICENSE) 发布。Unreal Engine 及其工具链遵循 Epic Games 的许可条款。

---

<div align="center">

**平台:** Windows x64 | **引擎:** Unreal Engine 5.5 | **许可证:** MIT

</div>
