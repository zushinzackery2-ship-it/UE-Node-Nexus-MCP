# 验证与交付矩阵

以下路径以仓库根目录为基准。按变更影响选择组合，已有同一源码的有效验收可以复用；文档修改不触发原生重建。

| 变更 | 必要证据 | 常见漏测 |
| --- | --- | --- |
| 文本、schema、比较 | 对应 tests/transcode 与 contracts；生产端 schema 契约 | fixture手写了native实际没提供的字段 |
| Blueprint接口/引脚 | 同ID增删改名改型、本地/跨资产/继承；native compile及实际执行 | 改节点ID绕过旧绑定；只有未连线空节点 |
| Niagara输入/栈 | 原生Stack模式/值、真实RI字节、Link/Local/reset及顺序控制；涉及GPU用真实RHI | 只看导出字符串或CPU字节码；忽略静态类型 |
| 迁移/删除/恢复 | 真实包与硬/软依赖、部分失败回执、保存后冷加载 | 内存正确但磁盘旧依赖残留 |
| 实例与任务 | 生命周期专项；缓存过期、进程退出、响应丢失、取消和scope交接 | fresh fixture没有catalog TTL或跨任务状态 |
| 构建与部署 | 源码指纹、模块清单、DLL内嵌身份、实际导入/载入路径 | 项目构建成功但目标模块沿用预编译DLL |
| 性能 | 匹配工作负载、预热、重复样本与方差，工作量和等待分开 | 模拟桥测量被写成UE实机提速 |

## 现成入口

源码 Python 测试必须设置 PYTHONPATH 为本仓库 src 的**绝对路径**，避免子进程换工作目录后导入安装版。使用 .venv/Scripts/python.exe 运行 pytest，选择对应领域测试；并行 pytest 分配不同 --basetemp。

- 静态：python -m tests.compile_check.check_sources。
- 原生：设置 UE_NEXUS_ENGINE_DIR，执行 tests/compile_check/build_plugins.bat；NEXUS_NON_UNITY=1 逐源文件检查 include，普通模式检查Unity构建。
- 连续实机：python -m tests.stress --seeds 2711,2712,2713 --steps 12。
- 安装版：清除 PYTHONPATH，用目标已安装解释器运行同一入口并加 --installed。
- 失败重放：python -m tests.stress --replay <trace.json>。
- 同故障缩减：python -m tests.stress.shrink <trace.json> <report.json> --attempts 12。

上述 seed 是可复现起点，新增状态机缺陷应添加它的具体序列。增加随机次数不能替代缺失的 oracle。

tests/stress 当前随机跨层场景覆盖 Material 和 Blueprint；Niagara 由固定真实原生案例覆盖。报告实际执行的领域、动作、种子和重启次数。NullRHI不能证明GPU、RHI或截图正确。

完整原生Automation与持续编辑工程分开，恢复fixtures会绑定自己的仓库。cold oracle直接读取原生值并执行Blueprint函数；应检查输入生成器确实写入预期动作，尤其默认值被省略之后。

## 证据与成本

每次失败记录引擎/RHI、Python来源、模块指纹、输入、动作序列、操作/任务/apply身份、产生端错误、日志与退出。缩减只接受相同故障特征，并保持原测试条件。

性能分别报告枚举数量、导出数量、计划数量、锁持有、复制字节、启动/执行/退出时间。清晰区分冷启动、预热后样本、模拟桥和实际编辑器。

确认测试进程退出后，清理重复插件副本、schema缓存及临时工作区，保留复现必需资产与原始证据，记录实际释放量。

## 发布与安装

先核对源码、安装目录和当前进程身份，再决定是否需要构建与部署。使用仓库现有构建/发行流程；本机有 scripts/local_update 时复用其wheel、模块身份和实载验证，正式安装说明见 release/INSTALL.md。

引擎插件当前安装在 Engine/Plugins/Marketplace。工程级插件可能覆盖它，因此“文件已复制”必须用实际 module_path 验证。

原生修复需要新Editor加载新DLL；Python更新需要新服务进程导入新包。旧stdio连接在服务替换后由客户端重建，不能宣称已断开的stdin能够自我恢复。

界定验收：成功回执只证明对应源码、输入与环境；保存成功不替代冷加载，冷加载不替代玩法/渲染运行验收。
