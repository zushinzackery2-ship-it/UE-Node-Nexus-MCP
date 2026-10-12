# Nexus 存储与整理

默认共享镜像位于 `<Project>/Saved/Nexus/Content_Transcoded`。工程已有绑定时以
ensure、checkout 返回的位置为准；顶层 `Content_Transcoded` 可能是此前使用的
镜像。镜像中的 `.nexus` 保存目录、历史和恢复信息。

## 目录用途

| 目录 | 写入来源与用途 | 保留规则 |
| --- | --- | --- |
| `Content_Transcoded/.nexus/schema` | UE 反射、函数、引脚和属性定义，供离线编辑、校验及历史解释 | 当前目录、共享对象池及历史绑定一并保留 |
| `Content_Transcoded/<project>/.nexus/collaboration` | 本地版本库、分支、工作区、未提交文本及发布记录 | 按正式版本库备份和保留 |
| `Saved/Nexus/Collaboration` | 原生事务的包检查点、磁盘副本和恢复回执 | 依据事务状态保留恢复证据 |
| `Saved/Nexus/SafetyBaseline` | 隔离验证导出的相关包内存状态，保留主编辑器未保存修改 | 消费结束后归档到工程 `.del/SafetyBaseline` |
| `Saved/Nexus/Runtime` | 项目管理器状态、实例身份、仓库绑定、工具及可选验证根 | 运行中的管理器和工具保留 |
| `Saved/Nexus/Runtime/Validation` | 独立编辑器使用的完整工程副本及结果 | 确认退出后将 Project 移至验证根 `.del` |
| `Saved/Nexus/Jobs` | 后台请求、结果及阶段信息 | 查询和排查使用相应任务回执 |
| `Saved/Nexus/Logs` | 原生阶段日志和错误证据 | 日志遵循各记录器的轮转预算 |
| `Saved/Nexus/PluginBackups` | 旧安装器保存的完整插件备份 | 依据安装回执归档，保留回滚版本 |

`Runtime/Tools/RenderDoc` 是 GPU 捕获工具。目录链接或 junction 的目标位于
其他卷时，容量审计应记录目标位置，避免把同一批文件重复计为本地副本。

## Schema 增长与共享

旧格式 2 会将环境身份写进每个定义。插件或配置身份变化后，相同定义在新
schema 目录重复出现。旧发布流程还会将整个索引写入 `indexes/<hash>.json`，
每次刷新都可能增加一份大索引；当前读取路径使用 `key.json`。

格式 3 将中性定义和属性页按内容保存到 `schema/.objects`，环境目录仅保存
当前 manifest 与 Markdown 索引。读取时重建环境身份，核验原逻辑记录 hash，
使原有工作区的 schema 绑定继续成立。刷新只写变化的对象，并原子替换 manifest。

旧格式 2 保持可读，更新程序之后的新刷新使用共享存储。历史目录中的定义
仍可能被提交、工作区和恢复记录引用，整理时应先核对这些绑定。版本库备份
需要覆盖环境目录及 `.objects`。

## 内存基线与隔离副本

自动高风险发布先建立基线 lease 和 OS 锁，再请求 UE 序列化相关包的当前
内存状态。副本验证读取并校验这些字节；主编辑器的原磁盘字节和脏标记保留。
导出中途失败、容量拒绝及正常验证结束都会归档该 lease 的输出目录。

`SafetyBaseline/.leases/<id>.json` 记录消费进程创建身份、原文件 hash、验证
回执路径、错误及最终 `holding`。消费进程中断后，下次发布会按创建身份
确认其退出并恢复归档；正在使用的快照继续受锁保护。

隔离工程也按进程创建身份确认退出，再集中到验证根的 `.del`。验证根可以
通过 `UE_NEXUS_VALIDATION_DIR` 选择；目标卷实际分配、内存基线和工作余量
进入复制前的容量预算。

`.del` 是供用户手动删除的归档区。移动只整理路径，实际释放空间发生在删除
归档内容时。整理回执、正式历史及处于恢复状态的事务应保留在原目录。
