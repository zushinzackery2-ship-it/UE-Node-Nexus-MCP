# Task Status

Last updated: 2026-08-28

## 1. 当前任务

- [x] UnrealClaude 能力全面对齐迁移（仅借鉴设计，自行实现）+ README 重写 + 仓库设为 private

## 2. 已完成

- UnrealClaude 能力对齐迁移（2026-08-28，直接落 main）：
  - 迁移（C++ + Python 全链路）：`asset_dependencies_get`/`asset_referencers_get`（AssetRegistry 硬/软依赖行）、`level_open`（脏地图保护）、`level_actor_spawn`/`level_actor_delete`/`level_actor_transform_set`（窄类型化 Actor 生命周期，默认 dry-run）、MCP 本地 `log_tail_get`（项目日志尾+子串过滤）；`ue_read` 新增 `asset_dependencies`/`asset_referencers`/`log` target
  - operation 总数 102 → 109（58 读/51 写，102 bridge + 7 本地）；新增 tests/test_migrated_operations.py，全量 59 测试通过
  - 边界调整：关卡 Actor 生命周期由"不提供"改为"窄类型化写入提供"；仍不提供任意 Python/控制台执行与泛化反射写入
  - **遗留：6 个新 bridge op 的 C++ handler 未经 UE 实机编译验证（本环境无 UE 构建链），部署前需完整编译插件一次**
  - 未迁移及原因：异步任务队列（需管道协议改造+并发基建，无法编译验证）、视口截图（异步渲染回读不适配同步管道协议）、Enhanced Input 资产创建（大体量资产工厂 C++，无法编译验证，legacy input 已覆盖）、AnimBP 状态机写入（超大图编辑 C++ 面）、任意脚本/控制台执行（安全边界，UnrealClaude 自身也在为其补安全门）、character/character_data（场景化工具，违背通用原语原则）、编辑器内嵌聊天面板（产品形态不同，非 MCP 能力）

- 借鉴 UnrealClaude 增强（2026-08-27，直接落 main）：
  - 借鉴其"按需 UE 文档上下文系统"→ 新增 MCP 本地 operation `workflow_guide_get`：7 类任务级工作流指南（`src/ue_node_nexus_mcp/guides/*.md`），支持分类列表/取正文/关键词检索，Agent 无需安装 skill 也能会话内自取
  - 借鉴其批量/队列工具面 → 新增 MCP 本地 operation `batch_execute`：整批先校验（无效批不执行任何一条）、顺序执行、默认遇错即停、逐项紧凑结果，上限 20 条，禁止嵌套
  - 借鉴其 CLAUDE.md 工具并行分类 → SKILL.md 新增 Concurrency & batching 章节（读并行安全 / 写按资产隔离 / 需串行操作）
  - 未采纳（需 UE 构建环境，无法编译验证）：异步任务队列、asset dependencies/referencers 查询、视口截图
  - operation 总数 100 → 102（55 读 / 47 写，96 bridge + 6 本地）；新增 tests/test_workflow_guides.py、tests/test_batch_execute.py，全量 49 测试通过
- 按架构审查报告修复设计问题：facade 生产路径接通客户端逻辑、operations.json 元数据单源化、hidden 操作生效、ue_diff_get 真分页、vfx 探测缓存修复、真实调用面测试与 C++ 契约对齐测试

- 编写工作区规则：主代理禁止分析与编辑，统一委托单一 fable5xhigh 子代理（`.cursor/rules/subagent-only-delegation.mdc`，`alwaysApply: true`）
- 架构审查修复（分支 `cursor/facade-review-fixes-bcbf`）：
  - `ue_execute` 现按操作分发到客户端逻辑（材质 client_id 展开、diagnostics 日志增强、Niagara 冗长字段裁剪）
  - operation 元数据（risk/summary/default_response/hidden/local）单源于 `src/ue_node_nexus_mcp/operations.json`
  - hidden 高危操作默认不出现在 `ue_capability_get` 索引（`include_hidden=true` 可列出）
  - `ue_diff_get` 支持 `cursor` 真分页；facade 入参错误统一返回结构化 error
  - vfx 探测不可达时不再缓存否定结果；实例绑定通过回调重置 feature 缓存；CLI 参数在 `main()` 启动时解析
  - 新增 facade 真实调用面测试、Python/C++ 操作契约对齐测试；`.inl` 纳入 300 行预算；`.gitignore` 不再忽略 `tests/`

## 3. 高价值信息索引

- 子代理模型 slug：`claude-fable-5-thinking-xhigh`（用户称 fable5xhigh）
- 规则路径：`.cursor/rules/subagent-only-delegation.mdc`
- facade 客户端逻辑分发表：`src/ue_node_nexus_mcp/facade_execute.py` 的 `_client_side_handler`
- 遗留（需 UE 构建环境验证，未动）：AutoIndex `.inl` 单翻译单元拆分为常规 .h/.cpp；VfxBridge 插件内 `UeNodeNexusBridgeNiagara*` 前缀统一
