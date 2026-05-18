# 当前任务/需求/待办清单（任务进度）

- 在真实 UE Editor 中打开插件后，用实际 Blueprint/Material 资产跑一次端到端 MCP 调用验证 HTTP bridge 运行态行为。
- 后续可继续扩展专用 Blueprint 节点创建操作，例如函数调用节点、变量 Get/Set 节点、InputAction 节点，避免调用方手填复杂类初始化参数。

# 已解决问题/已完成需求（极简化记录）

- 已设置 code-index 项目路径：`D:\AI\TEST\UE-Node-Nexus-MCP`。
- 已加载 UE 材质图、蓝图工作流、编辑器工具、UE 架构、C++ LSP 相关技能。
- 已查阅 Epic Python API、Blueprint API、MaterialEditingLibrary、EdGraph/UK2Node/FKismetEditorUtilities、现有 UE MCP 项目和 MCP 协议资料。
- 已确认项目定位：节点图连接中枢，排除任意 Python 执行主入口。
- 已创建 Python MCP Server 骨架，暴露 14 个固定工具：资产/Level 枚举、图快照、节点参数、图 patch、材质实例参数、编译/校验/保存/诊断。
- 已创建 UE bridge HTTP 契约文档、架构文档、Python 包配置、基础测试。
- 已明确集成形态：MCP Server 外置；UE 内部使用 Editor 插件 `UeNodeNexusBridge` 实现真实 UE API 桥接。
- 已执行 `git init` 建立本地仓库；当前工作区文件处于未跟踪状态，`.pytest_cache` 和 `__pycache__` 已被 `.gitignore` 忽略。
- 已确认 UE5.5 路径存在：`D:\Program Files\Epic Games\UE_5.5\Engine`。
- 已创建 UE Editor 插件：`Plugins/UeNodeNexusBridge`，使用 HTTPServer 在 `127.0.0.1:8765/mcp` 提供 bridge endpoint。
- 已实现插件首批只读操作：`asset_list`、`asset_get`、`level_current_get`、`level_actors_list`、`diagnostics_get`。
- 已创建清理和安装脚本：`scripts/clean_specialagent.ps1`、`scripts/install_ue55_plugin.bat`、`scripts/clean_specialagent_and_install_ue55.bat`。
- 已找到 opencode SpecialAgent MCP 配置：`C:\Users\Administrator\.config\opencode\opencode.jsonc` 内 `"special-agent"` 指向 `http://localhost:8767/sse`。
- 已找到疑似“教写 SpecialAgent 脚本”的 skill：`C:\Users\Administrator\.config\opencode\skills\write-a-skill`，内容是创建新 skill 并包含 scripts 资源规范。
- 已确认 `ue-material-graph` skill 在 Codex、opencode、Windsurf 中均存在，内容是直接给模型 UE Python / `MaterialEditingLibrary` 代码片段来查询和修改材质节点。
- 已更新清理脚本：会移除 opencode `special-agent` MCP 配置，并报告 `write-a-skill` 作为疑似脚本生成 skill。
- 已更新清理脚本：会精确删除 Codex、opencode、Windsurf、Octonic 下的 `ue-material-graph` skill 目录。
- 已在放开权限后执行清理脚本：已删除 Codex、opencode、Windsurf、Octonic 下四个 `ue-material-graph` skill 目录。
- 已移除 opencode `special-agent` MCP 配置，并精确删除 Octonic `config.yaml` 中的 `special-agent` MCP 条目。
- 已将写操作防呆升级为契约要求：真实写入后必须返回 pin-integrity、compile、dirty-state 和结构化 diagnostics。
- 已实现 UE bridge `asset_compile`、`asset_validate`、`asset_save`：Blueprint 编译返回 `FCompilerResultsLog` 诊断，材质触发重编译，保存返回 dirty state。
- 已实现 UE bridge `graph_snapshot_get`：Blueprint 使用 `UEdGraph/UEdGraphNode/UEdGraphPin` 枚举节点、Pin、连接；Material 使用 `UMaterial::GetExpressions()` 枚举表达式节点和输入连接。
- 已实现 UE bridge `material_instance_params_get/set`：固定 C++ 接口枚举 scalar/vector/texture/static switch，set 使用 `{type,name,value}` 数组并支持 dry-run。
- 已给插件加入 `MaterialEditor` 依赖，以使用 `UMaterialEditingLibrary` 的固定 C++ API。
- 已按 300 行规则拆分插件 C++ 文件；当前插件单文件最大约 205 行。
- 已通过 `python -m pytest`：8 passed。
- 已通过 `python -m compileall src tests`。
- 已修复 UE5.5 插件编译问题：头文件路径、`FHttpServerResponse` 前置声明、材质输入枚举弃用 API。
- 已通过 `scripts\build_plugin_ue55.bat` 完成 UE5.5 插件打包，产物位于 `bin\UeNodeNexusBridge`。
- 已更新 `scripts\install_ue55_plugin.bat` 为安装打包产物，并已安装到 UE5.5 Engine Marketplace 插件目录。
- 已验证 UE5.5 Engine 插件 DLL 存在：`Engine\Plugins\Marketplace\UeNodeNexusBridge\Binaries\Win64\UnrealEditor-UeNodeNexusBridge.dll`。
- 已重新确认当前缺口集中在 UE 插件节点级图读写，Python MCP 外壳已有固定工具契约，不需要引入任意 Python 执行。
- 已实现 UE bridge `graph_patch_apply`：Blueprint/Material 支持连接、断连、位置、创建、删除、参数 patch，并返回 diff、pin-integrity、compile、dirty-state、diagnostics。
- 已实现 UE bridge `node_params_get` / `node_params_set`：Blueprint 读取/写入输入 pin 默认值；Material 读取/写入 editable expression properties。
- 已将材质图 snapshot 的 node id 改为 MaterialExpression GUID，并保留 patch 查找时对旧 path id 的兼容。
- 已更新契约文档与 README，说明图 patch、节点参数、dry-run 与写入返回结构。
- 已重新执行验证：`python -m pytest` 9 passed；`python -m compileall src tests` passed；`scripts\build_plugin_ue55.bat` passed；`scripts\install_ue55_plugin.bat` passed。
- 已验证安装 DLL 存在于 UE5.5 Engine Marketplace 插件目录。

# 任务过程中的经验、教训、高价值发现/信息

- PowerShell 启动时会触发本机 profile 执行策略报错，后续命令输出可能被噪声干扰。
- MCP 协议本身只规定工具/资源/提示等交互层，不替 UE 做权限、安全、事务和回滚，需要 MCP Server 自己设计。
- 现有 UE MCP 项目很多把“摆场景/Actor 操作”做得很重；本项目应反向收敛到图资产编辑、查询、校验、编译和保存。
- Epic `MaterialEditingLibrary` 已覆盖创建/删除/连接表达式、读取输入名/类型、读取材质属性输入、布局、统计、重编译、材质实例参数更新，适合作为材质首版主通道。
- Epic `BlueprintEditorLibrary` 官方 Python 面偏粗粒度，能编译、找 EventGraph/Graph、增删函数图、改变量属性，但不提供完整通用 K2 节点/Pin 图编辑面。
- Blueprint 节点/Pin 级可靠编辑应在 UE Editor C++ 插件中封装 `UEdGraph`、`UEdGraphPin`、`UK2Node`、`FKismetEditorUtilities` 等 API，再由 MCP Server 调用。
- 首版工具应以 declarative graph patch 为核心：查询图快照、规划变更、应用变更、编译/重编译、保存、返回结构化诊断。
- 图/节点/材质实例写操作应默认 `compile_after=true`；编译失败不能只返回泛型错误，必须带 applied diff 和定位到 asset/graph/node/pin 的 diagnostics。
- 工具接口目标不是暴露任意 Python 执行，而是提供固定、类型化、可校验、易读返回的安全接口。
- 推荐分层：只读索引层、图快照层、事务修改层、编译诊断层、非占用保存层。
- 当前实现没有写死未经验证的 UE 内部函数名；MCP 侧只负责固定工具、参数校验和桥接转发。
- 材质实例参数 set 的 MCP 契约已收敛为数组项：`type/name/value`，避免动态对象结构导致桥接层难校验。
- `src/ue_node_nexus_mcp/server.py` 当前 257 行，低于 300 行重构阈值。
- MCP 不应直接以完整协议服务形式嵌入 UE；合理形态是“外部 MCP Server + UE Editor 插件桥”。
- 当前环境阻止 git 创建 `.git/HEAD.lock`，所以 `git branch -M main` 未完成；仓库当前 HEAD 仍是 `master`。
- Codex 主配置里没有 SpecialAgent MCP；opencode 配置中存在 `special-agent` MCP。
- `write-a-skill` 是通用 skill 生成器，不含 SpecialAgent 字符串，但功能上符合“教写 agent/skill 脚本”的描述。
- `ue-material-graph` 对知识参考有价值，但执行形态不符合本项目目标：它依赖模型临场写 Python、直接调用 `ObjectIterator` / `MaterialEditingLibrary` / `save_packages`，缺少固定 MCP 契约、事务、安全校验和结构化返回。
- UE5.5 BuildPlugin 初次放开权限后暴露真实插件编译错误，修复后可正常打包；安装脚本应安装 `bin` 打包产物而不是源码目录。
- UE5.5 `UMaterialExpression::GetMaterialExpressionId()` 是非 const getter，snapshot 里 const 表达式需要显式处理。
- UE5.5 `FProperty::ExportText_InContainer` 需要传入数组索引参数，签名为 `ExportText_InContainer(0, Value, Container, Delta, Parent, Flags)`。
