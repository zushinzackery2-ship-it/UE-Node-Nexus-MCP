# 文本工作区与协作

同一工程的 agent 使用 ensure 返回的共享镜像根与版本库，每个任务分别 `checkout`。已有 UE 仓库绑定优先；新项目默认根为 `<Project>/Saved/Nexus/Content_Transcoded`。工具返回该工作区的 `id`、`files_root` 和 `file_paths`；编辑时以这些路径为准。回收编辑器后，工作区和历史继续支持离线 lint/stage/commit。

Schema 格式 3 在 `.nexus/schema/.objects/` 按内容共享定义，各环境保留一份当前索引与独立校验绑定；旧格式及历史绑定保持可读。整理镜像时，将共享对象池与引用它的历史目录一并保留。

```
Content_Transcoded/
├── .nexus/schema/<schema_key>/
│   ├── index.md
│   ├── blueprint/  material/  niagara/
│   └── scene/  asset/  common/  contexts/
└── MyProject/.nexus/collaboration/
    ├── index.sqlite
    ├── objects/
    ├── workspaces/<workspace_id>/files/
    ├── sessions/
    ├── transactions/
    └── locks/
```

| 动作 | 行为 |
|:-----|:-----|
| **`checkout / workspaces / close`** | 创建、查询和关闭独立工作区；分支历史继续保留 |
| **`status / diff / lint`** | 查看暂存与未暂存修改、语义差异、会话和恢复状态，校验文本 |
| **`stage / unstage / commit`** | 暂存资产、撤回暂存、建立本地版本；`commit(all=true)` 捕获并提交指定文件 |
| **`fetch / pull`** | 观察当前 UE / 整合 UE 版本并分别重放暂存与未暂存修改 |
| **`merge / resolve / continue / abort`** | 合并分支、逐项解决冲突、继续或取消持久会话 |
| **`push / recover`** | 将已提交版本合并到当前 UE，按条件应用、保存、记录回执；查询或恢复中断执行 |

修改 refs、工作文件、会话或 UE 的动作默认预览，实际执行传 `dry_run=false`。预览提供 `proposal_id` 时，可在执行中携带它以校验固定输入。`push` 默认读取已提交 HEAD；后续文件编辑仍留在工作区。

文件发现默认跳过任意层级的点号目录、点号文件和 `__pycache__`，工具生成的 `.auto-index-mcp`、`.git`、`.cache` 等目录会在扫描时直接剪枝。可在工作区返回的 `files_root` 下创建 `.nexusignore`，按根目录相对路径配置额外忽略规则；普通镜像放在 `<Project>/.nexusignore`。规则作用于新文件发现，已登记、暂存或提交的资产继续跟踪。语法与示例见 [文本镜像指南](text_mirror.md#file-discovery-and-ignore-rules)。

```python
a = ue_sync("checkout", paths=["/Game/Materials"], options=dict(dry_run=False, agent_id="A"))
b = ue_sync("checkout", paths=["/Game/Materials"], options=dict(dry_run=False, agent_id="B"))
# 分别编辑 a/b 返回的 file_paths，然后各自提交。
ue_sync("commit", options=dict(workspace_id="<A-id>", all=True, message="调整颜色", dry_run=False))
ue_sync("push", options=dict(workspace_id="<A-id>", dry_run=False))
ue_sync("commit", options=dict(workspace_id="<B-id>", all=True, message="调整粗糙度", dry_run=False))
ue_sync("push", options=dict(workspace_id="<B-id>", dry_run=False))
```

不同字段的兼容修改会合并；同字段竞争返回 `merge_id` 和包含 base/ours/theirs 的冲突。`ours` 表示当前工作区，`theirs` 表示待整合版本或 UE 状态。

```python
ue_sync("show", options=dict(workspace_id="<id>", merge_id="<merge-id>"))
ue_sync("resolve", options=dict(workspace_id="<id>", merge_id="<merge-id>",
    conflict_id="<conflict-id>", choice="ours", dry_run=False))
ue_sync("continue", options=dict(workspace_id="<id>", merge_id="<merge-id>", dry_run=False))
```

新建材质时，在工作区 `files_root` 下写入 `Materials/M_Example.mat.nexus`，`schema` 使用工作区绑定的 key：

```text
nexus: 1
asset: /Game/Materials/M_Example
class: Material
schema: <current-schema-key>

[graph]
value : Constant(R=0.5) @ 0,0
value -> out.Roughness
```

文本仅记录非默认值。删除属性行表示恢复默认值，删除实例参数行表示清除 override；省略贴图属性表示使用引擎默认贴图。完整语法和类型边界见 [文本镜像指南](text_mirror.md)。

## 历史、回退与恢复

| 动作或机制 | 行为 |
|:-----|:-----|
| **`log / show / diff / blame`** | 查询版本父链、提交内容、语义差异与字段来源 |
| **`branch / switch / tag`** | 创建分支、切换干净工作区、标记里程碑 |
| **`restore / revert`** | 从旧版本选择状态，或撤销某次提交的增量；再次 commit/push 才修改 UE |
| **`reset / amend / rebase`** | 修改未发布的私有历史；safety ref 和 reflog 保留原版本 |
| **`cherry-pick / stash / reflog`** | 挑选提交、保存三层草稿、查找引用移动前的版本 |
| **条件发布** | 检查当前内存 revision、editor epoch 和依赖 read set，按当前 UE 到候选状态的差异执行 |
| **高风险准入** | Niagara变更、材质Custom/WPO等渲染契约与函数接口变更，先在独立UE副本完成apply/compile/save/readback并确认正常退出；验证绑定候选、脏内存基线、引擎/插件与RHI |
| **持久回执** | `apply_id` 绑定执行请求；响应丢失时查询回执，保存结果进入发布历史 |
| **编辑保护** | 发布期间继续修改文件会保留新字节，并返回 `workspace_rebase_required`；用 pull 整合 |

默认 `stop_on_error=true`；设为 false 时按依赖阻断失败项，结果可能部分完成。删除需要 `allow_delete=true`。已发布历史使用 restore/revert 创建新版本；`recover` 返回执行阶段与恢复证据。

## 可用参数目录

模型可以先读 schema 的 `index.md` 和分类索引，再读一个目标 JSON。记录包含类型、默认值、合法枚举、约束、来源与桥接读写支持；动态引脚或条件未解析时标为 `context_required`。

```python
ue_sync("schema", options=dict(category="material", query="TextureSample", details=True))
ue_sync("schema", options=dict(function="KismetSystemLibrary.PrintString"))
ue_sync("schema", options=dict(target="/Game/Materials/MI_Example.MI_Example"))
ue_sync("schema", options=dict(refresh=True))
```

schema 按项目、引擎、插件和模块身份隔离。定向采集扩展索引，历史快照保留使用过的 schema 记录；离线读取注明新鲜度未知。

首次 checkout 会启用协作并迁移旧基线，旧文本原始字节保存在 `imported` 工作区。启用后修改调用携带 `workspace_id`。旧工程在 checkout 前继续使用原 init/pull/push；其 force 参数按旧契约处理。协作调用使用具体冲突的 resolution。

高风险发布要求 checkout 工作区；旧式 push 返回 `safety_workspace_required` 和 checkout 路由。普通材质数值变更沿用正常事务。副本保存当前相关包的内存状态并保留主编辑器脏标记和原磁盘字节；完整验证回执见结果行的 `safety_validation`。内存准入默认启用，用户明确授权后可在本次 push/continue 设置 `safety_allow_low_memory=true`。显式 `safety_validate` 使用保存基线，提供 `allow_low_memory` 同等单次参数。

Niagara 参数读取和已有值写入统一校验数据类别、尺寸、字节范围和对象槽位。四元数与向量通过引擎字节复制读取，支持紧凑非对齐布局及 LWC 转换；存储错误携带参数名、类型和具体原因，沿导出、编译读回和发布事务返回。

隔离回执记录复制字节数及预检、复制、启动、执行、退出、源检查的分项耗时。副本验证共用机器的 GPU；工程原生代码、设备故障和调用方持有的 Python 世界引用仍需各自满足资源与生命周期契约。

## 场景与实例

首次导入明确选择的场景组，之后通过 `Scenes/` 内的文件或目录执行相同的同步动作：

```python
ue_sync("checkout", options=dict(dry_run=False, scene=dict(
    map_path="/Game/Maps/World", name="Block",
    actor_paths=["/Game/Maps/World.World:PersistentLevel.Tiles"],
)))
ue_sync("commit", options=dict(workspace_id="<id>", all=True, message="调整场景", dry_run=False))
ue_sync("push", paths=["Scenes/Maps/World/Block.scene.nexus"], options=dict(workspace_id="<id>"))
```

根 Actor 使用世界变换，挂接 Actor、组件和实例使用相对变换。实例 ID 随删除和重排保留；首次写入才绑定编辑器专用元数据。场景提交先处理镜像资产依赖，再应用场景事务，保存涉及的地图、子关卡和外部 Actor 包。完整语法、只读内容及失败恢复见 [场景镜像指南](scene_mirror.md)。分页及直接批量编辑使用 `component_instances_get/patch`。
