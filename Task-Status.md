# 当前任务/需求/待办清单（任务进度）

- 远端仓库已创建并推送：`https://github.com/zushinzackery2-ship-it/UE-Node-Nexus-MCP`。
- 远端仓库为 private，默认分支为 `main`。
- 已确认本地参考项目 `refApic/`、构建产物 `bin/`、`obj/`、缓存目录不会入库。

# 已解决问题/已完成需求（极简化记录）

- 已设置 code-index 项目路径：`D:\AI\TEST\UE-Node-Nexus-MCP`。
- 已确定架构：外置 Python MCP Server + UE 5.5 Editor C++ 插件 bridge，不让模型临场写 UE Python 脚本操作资产。
- 已实现固定 MCP 工具：资产枚举/读取/创建、当前 Level 读取、Level Actor 读取、Blueprint 详情、AnimBlueprint 摘要、图快照、图 patch、节点参数读写、材质实例参数读写、编译、校验、保存、诊断。
- 已实现 UE 插件 `UeNodeNexusBridge`，HTTP endpoint 为 `127.0.0.1:8765/mcp`。
- 已将图快照默认收敛到 `wires_tiny` 高密度文本返回，保留 `wires_min`、`wires`、`compact`、`full` 用于不同场景。
- 已新增 `asset_create`，支持 `material`、`material_instance`、`blueprint`。
- 已新增 `blueprint_details_get` 和 `anim_blueprint_summary_get`，其中 CDO/组件读取、AnimBlueprint 语义摘要的部分功能思路来源已在 README 声明。
- 已把 Python MCP 工具拆分到 `tools_assets`、`tools_blueprints`、`tools_graphs`、`tools_materials`、`tools_system`，避免主入口膨胀。
- 已拆分 UE 插件大文件，当前核心 C++ 文件均控制在 300 行以内。
- 已清理 SpecialAgent 相关 MCP 配置与 `ue-material-graph` skill，避免继续使用任意脚本式材质操作方案。
- 已安装 `ue-node-nexus-mcp` 到 Codex、opencode、Windsurf 三个 agent MCP 客户端配置。
- 已完成 README 重排，遵循 `$readme-format`：居中标题、徽章、提示块、功能/API 表格、目录结构、构建命令、来源说明和页脚。
- 已执行验证：`python -m pytest` 14 passed；`python -m compileall scripts src tests` passed；UE5.5 `BuildPlugin` passed。
- 已生成可投放插件包并覆盖到 `G:\vdio\UEPlugins\MCP\UeNodeNexusBridge-UE5.5-Win64.zip`。
- 已创建 GitHub remote repository 并推送 `main`：首个完整工具扩展提交为 `06f2708`。

# 任务过程中的经验、教训、高价值发现/信息

- MCP 协议只提供工具交互层，UE 资产安全、事务、编译诊断、保存策略必须由 MCP Server 和 UE Editor 插件共同约束。
- 本项目核心边界是材质/蓝图节点图资产工作流，不扩展到摆 3D 场景、Actor 布局或任意 UE Python 执行。
- 大图快照不能默认携带完整节点参数和重复字段；默认应给高密度拓扑摘要，写入前再按需获取具体 node/pin id 和参数。
- 对大材质/蓝图，最省上下文的读取路径是 `wires_tiny`/`wires_min`；需要写入时使用 `compact`，需要完整调试才使用 `full`。
- 写操作必须返回 applied diff、pin-integrity、compile、dirty-state、diagnostics，避免连线改坏后只给泛型错误。
- 参考项目 `ue-blueprint-dumper` 的可取部分集中在 CDO/组件读取、AnimBP 语义摘要、RigVM/ControlRig 专用方向；未验证 UE5.5 API 前不硬搬 ControlRig 写接口。
- `refApic/` 仅为本地参考 clone，已加入 `.gitignore`，不进入远端仓库。
