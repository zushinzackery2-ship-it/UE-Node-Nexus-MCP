# Task Status

Last updated: 2026-08-28

## 1. 当前任务

- [ ] UE 5.5 真实环境（已搭建，编译验证全通过）：等待完整 UnrealEditor 链接完成后做无头编辑器运行时 E2E 验收

## 2. 已完成

- 用户 PAT 到位 → UE 5.5 真实编译环境搭建 + 剩余能力全部迁移（2026-08-28 第三轮，直接落 main）：
  - **PAT 实测**：主代理在指令中直接转交 PAT。API 实测：`/user` 为 `zushinzackery2-ship-it`；可访问 `EpicGames/UnrealEngine`（Epic 官方私有源码，账户已绑定 Epic）与用户的 UnrealEngine fork；无预编译 Linux 构建产物
  - **环境搭建**（`/ue/UnrealEngine`）：官方 5.5 分支稀疏浅克隆（blob 过滤）→ `Setup.sh` 拉取二进制依赖（约 50GB，含捆绑 clang 18.1.0 工具链与 dotnet）→ `BuildUBT.sh` 编译 UnrealBuildTool → 最小宿主工程 `/ue/HostProject`（启用双插件 + Niagara + EnhancedInput）
  - **真实编译验证**：UBT `-NoLink -Module=UeNodeNexusBridge -Module=UeNodeNexusVfxBridge`，unity 与 `-DisableUnity` 双模式全部 152 个 TU 零错误零警告。真实编译暴露并修复 20+ 处 clang 桩检查测不出的问题：unity 合并 TU 引发的 ODR/重定义冲突（共享化 `ObjectPathOrEmpty`/`ReadPayloadIndex`/`AppendSelectedLines`/`IsExecPin`/`MaterialParamLines`/`ShortRendererClass`/`ReadNiagaraIndexField`/`ResolveNiagaraEmitterHandle`，改名 `RegisterCoreOp`/`RegisterAutoIndexOp` 等）、`MakeError` 的 ADL 歧义（被引擎全局 `TValueOrError` 模板抢走重载，限定命名空间修复）、`ALandscape` 不完整类型、IWYU 首 include 顺序、非 unity 缺 include、`GLevelEditorModeToolsIsValid` 弃用替换、`FKey Key(FName(...))` vexing parse
  - **迁移：Enhanced Input**（C++ `Input/UeNodeNexusBridgeEnhancedInputOps.cpp` + Build.cs/uplugin 依赖 + Python 包装 + operations.json）：`input_action_create`（value_type bool/axis1d/axis2d/axis3d）、`input_mapping_context_create`、`input_mapping_context_entry_add`（键名校验、重复绑定拒绝）、`input_mapping_context_get`（新 `ue_read` target `input_mapping_context`）
  - **迁移：AnimBP 状态机写入**（C++ `Blueprint/UeNodeNexusBridgeAnimStateMachineWriteOps.cpp`）：`anim_state_machine_state_add`（set_as_entry 重连入口）、`anim_state_machine_transition_add`（重复 transition 拒绝）；单状态机可省 machine_name，多状态机必填且错误响应列出可选名
  - operation 总数 115 → 121（62 读/59 写，109 bridge + 12 本地）；新增 tests/test_input_animbp_operations.py（10 项，facade 透传 + wrapper 默认值/校验双路径）；全量 89 测试通过
  - **待办**：完整 UnrealEditor 链接（后台构建中）完成后做无头编辑器运行时 E2E 验收

- 用户规则 PAT 复查（2026-08-28 第二轮）：**该 PAT 未注入本 VM，无法使用**。全盘取证：规则目录只有 `subagent-only-delegation.mdc`（无令牌）；`~/.cursor`、代理存储、全部环境变量、`~/.git-credentials`、`~/.config/git`、`/tmp`、bash 历史、全盘 `ghp_/github_pat_/gh?_` 正则扫描——唯一真实令牌是 `~/.gitconfig` 与 `~/.config/gh/hosts.yml` 中同一个 `ghs_` GitHub App 安装令牌。该令牌 API 实测：`/user`、`/user/repos`、`/user/orgs` 均 403 "Resource not accessible by integration"（证实是安装令牌而非 PAT）；`/installation/repositories` 仅返回本仓库 1 项；`EpicGames/UnrealEngine` 404；账户 23 个可见仓库无 UE 源码/预编译产物。**解锁方式：把 PAT 作为 Cloud Agents secret 注入或在指令中直接给出**。UE 实机编译验证在此之前维持阻塞

- UE 环境搭建探索与剩余能力迁移（2026-08-28，直接落 main）：
  - **UE 环境结论：本 Linux 环境无法搭建**。取证：本机无任何 UE 安装/构建产物/容器（/opt、/usr/local、/home、docker 均查过）；唯一凭据是仅覆盖本仓库的 GitHub App 安装令牌（`/installation/repositories` 只列出 UE-Node-Nexus-MCP）；用户账户 23 个仓库中无 UE 源码仓库；`EpicGames/UnrealEngine` 对本令牌 404（需 Epic 绑定账户）；官方预编译 Linux 二进制（~25GB zip）需 Epic 账户登录下载；非官方镜像违反 Epic EULA 不采用。硬件（4 核/15GB RAM）也不足以在合理时间内完成源码构建
  - **替代验证：clang 桩头文件编译检查**（`tests/test_cpp_compile_check.py` + `tests/compile_check/ue_stubs/`）：按官方文档核对 `SpawnActorFromClass`/`FEditorFileUtils::LoadMap`/`FScreenshotRequest::RequestScreenshot` 等签名后编写最小 UE API 桩，用宿主 clang `-std=c++20 -fsyntax-only` 真实编译 6 个从未过编译器的 TU（资产依赖、Actor 写入、level_open、viewport_capture、operation 名单、核心注册表），全部通过；已验证该检查对故意错误（参数顺序、返回值误用、多余实参）能报错。注意：桩检查只覆盖语法/类型层，不能替代 UE 实机编译
  - **迁移：后台任务队列（纯 Python，可完整实测）**：`task_submit`/`task_status`/`task_result`/`task_cancel`，与 batch_execute 共享前置校验（新模块 `operation_validation.py`），单后台工作线程严格按提交顺序串行执行（不与其他任务交错写桥），任务可包 `batch_execute`、`task_*` 互不嵌套，队列上限 20 活动任务、保留最近 50 条完成记录，状态在内存中
  - **迁移：视口截图（两阶段设计，适配同步管道）**：C++ `viewport_capture` 请求截图并立即返回目标 PNG 路径（下一次视口重绘后异步落盘，文件名白名单校验防路径逃逸）+ MCP 本地 `viewport_capture_status` 检查文件是否落盘（管道传输决定了 server 与 UE 同机，本地文件检查即可）
  - operation 总数 109 → 115（61 读/54 写，103 bridge + 12 本地）；新增 tests/test_task_queue.py（8 项）、tests/test_viewport_capture.py（6 项）、tests/test_cpp_compile_check.py（6 项）；全量 79 测试通过；tools_system.py 超预算拆出 tools_viewport.py
  - 仍未迁移及原因：Enhanced Input 资产创建、AnimBP 状态机写入（均为大体量 C++ 图编辑/资产工厂面，无实机编译迭代调试则风险过高）；任意脚本/控制台执行（安全边界）；character/character_data（场景化工具）
  - **遗留：7 个新 bridge op 的 C++ handler（前 6 个 + viewport_capture）已过 clang 桩编译检查与契约对齐测试，但仍未经 UE 5.5 实机编译与运行验证，部署前需完整编译插件一次**

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
