# Task Status

Last updated: 2026-08-27

## 1. 当前任务

- [x] 对照参考仓库 Natfii/UnrealClaude 借鉴设计并增强本项目（仅借鉴思路，未搬代码）

## 2. 已完成

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
