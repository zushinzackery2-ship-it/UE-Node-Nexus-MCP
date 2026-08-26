# Task Status

Last updated: 2026-08-26

## 1. 当前任务

- [x] 按架构审查报告修复设计问题：facade 生产路径接通客户端逻辑、operations.json 元数据单源化、hidden 操作生效、ue_diff_get 真分页、vfx 探测缓存修复、真实调用面测试与 C++ 契约对齐测试

## 2. 已完成

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
