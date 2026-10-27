# 目录与开发

```
UE-Node-Nexus-MCP/
  assets/branding/         项目标识、透明标记和单色 SVG
  Plugins/
    UeNodeNexusBridge/       Guard 早期门禁及核心编辑器插件
    UeNodeNexusVfxBridge/    VFX 插件
  src/ue_node_nexus_mcp/
    server.py              MCP 入口
    runtime.py             FastMCP 应用、桥接单例与工具注册
    errors.py              桥接错误类型
    bridge/                命名管道客户端、帧传输与响应规范化
    settings/              命令行与环境变量：能力组、响应模式
    registry/              操作清单、操作规格与调用校验
      operations.json      操作定义索引
      operations/          按能力组划分的操作定义
      payloads/            由包装签名推导的载荷 schema 与示例
    facade/                七个薄门面、操作路由、批量执行与响应存储
      cache/               门面证据缓存的容量与保留
    tools/                 内部操作的类型化包装，即 schema 来源
      niagara/             Niagara 系统、模块与属性包装
    tasks/                 后台任务队列、执行与完成
    guides/                客户端可查询的工作流指南
    build_info/            三模块构建身份与契约校验
    coordination/          通用 OS 锁与控制管道
    instances/             身份、Broker、租约、作用域、回收与仓库绑定
    diagnostics/           会话日志、诊断计数、运行时日志解析与总量保留
    geometry/              网格几何检查与派生资产契约
    safety/                操作准入、隔离验证、截图回执与运行时验收
    transcode/             文本解析、schema、diff 和同步
      collaboration/       store、history、workspace、semantic、merge、apply、report
      text/                语法、值与语义规范化
      raw/                 原始图与文本转换
      diff/                图差异与操作计划
      lint/                按资产类型校验
      material/            材质表达式语义：函数调用、引脚与自定义输入
      sync/                项目同步与 schema 编排
      storage/             文件与路径
      blueprint/           签名、引脚与类身份
      schema/              反射目录、名称索引与上下文查询
      push/                计划、提交、恢复和调用者刷新
      scene/               场景编解码、预检、提交和恢复
      transaction/         镜像事务所有权
      progress/            作用域内的进度观察回调
  tests/
    contracts/             操作契约、导入架构与源码预算
    facade/                对外入口与响应行为
    diagnostics/           日志与保留策略
    instances/             单元、跨进程竞争、故障与真实 UE 长测
    collaboration/         版本库、工作区、历史、合并与发布回归
    transcode/             blueprint/material/schema/semantic/text/raw/diff/sync
    safety/                准入、隔离验证、截图目标与模块清单
    geometry/              网格几何 schema 与示例
    release/               发行包成员与陈旧文件拒绝
    performance/           性能回归
    support/               跨领域测试夹具
    scene/                 场景、身份、并发与原生回归源码
    compile_check/         编译桩与真实 UE 构建入口
    live/                  隔离编辑器集成验证
  skill/ue-node-nexus-mcp/  Agent skill
  TestProjects/            本地 UE 验收工程（不随仓库）
  scripts/                 本机维护、更新与 schema 工具（不随仓库）
  release/                发行包组装、构建身份与安装说明
  pyproject.toml
  LICENSE
```

```bat
.venv\Scripts\python.exe -m pip install -e . pytest
.venv\Scripts\python.exe -m pytest -q
set "UE_NEXUS_ENGINE_DIR=D:\Unreal\UE_5.5"
tests\compile_check\build_plugins.bat
tests\compile_check\build_startup_fixture.bat
tests\compile_check\compile_scene_tests.bat
.venv\Scripts\python.exe -m tests.live.automation
.venv\Scripts\python.exe -m tests.live.issues4.runner
.venv\Scripts\python.exe -m tests.live.native_runner
.venv\Scripts\python.exe tests\live\sync_smoke.py
.venv\Scripts\python.exe -m tests.live.collaboration_runner
```

测试按领域及单元、集成、实机层次组织，覆盖操作契约、导入无环、文本往返、失败恢复、并发与 300 行源码预算；共享夹具集中在 support。pytest 临时目录默认为项目内 `build/pytest-temp`，可用 `--basetemp` 指定。真实 UE 构建验证引擎 API 和链接，并生成与引擎 BuildId 匹配的模块清单。

`automation` 运行全部 `Nexus.` 原生自动化；`issues4.runner` 验证四种启动状态及 sparse/MF 发布。启动提示由独立测试插件在隔离工程中通过真实模态对话框触发；`--engine-plugins` 验证正式安装路径。

原生测试在 `build/scene-tests/` 内编译和运行实例身份及 Undo/Redo 回归；生产插件在 `build/validation/` 内构建。同步 smoke 使用 DX12 验证 shader/RHI 就绪、保存失败恢复与回读，runner 绑定自建编辑器并正常退出。场景与捕获批次的重放入口位于 `tests/live/`，每次运行记录构建身份、请求响应和验收结果。
