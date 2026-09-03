# Summary of Issues

不是第一优先级、但确认存在的问题。每条都是 2026-09-03 在 Shadetest 实机上撞到的，
不是读源码猜的。已经修掉的不在这里（见 `Task-Status.md`）。

按「会不会让人做出错误判断」排序，不按修复难度。

---

## 1. `editor_save_all` 没有只读模式，而且桥接静默吞掉未知字段

**现象**：想先列出脏包再决定要不要存，传了 `{"dry_run": true}`，结果 11 个包
**直接被保存了**（`dirty_before_count: 11, dirty_after_count: 0`）。

**根因**：`HandleEditorSaveAll` 不读 `dry_run`（`UeNodeNexusBridgeEditorOps.cpp:111`），
Python wrapper 也没暴露这个参数——所以严格说不算违约。但两件事叠在一起就危险：
(a) 没有任何 op 能**只列出**脏包；(b) C++ 端对写 op 的 payload **不校验未知键**，
拼错 / 多传字段都无声通过。

**建议**：
- 加只读 op `editor_dirty_packages_get`（复用 `CollectDirtyPackageNames`），或让
  `editor_save_all` 认 `dry_run`（这个 op 是 hidden 高危，默认值应保持 false 以兼容
  `editor_request_exit` 内部调用）
- 写 op 的 payload 遇到未知顶层键至少回 `warnings`，理想是 `invalid_request`

## 2. `viewport_capture_status` 只在 MCP 本地存在，直连管道的脚本拿不到

**现象**：`viewport_capture` 的 `note` 让你去问 `viewport_capture_status`，但那是
MCP server 本地 op，用 `UeBridgeClient` 直连命名管道会得到
`ValueError: Unsupported operation`。

**影响**：`tools/toonshade/*.py` 这类直连脚本只能自己轮询文件。今天把
`file_path` 改成绝对路径并加了 `exists` 之后已经不那么疼，但 `note` 文案还是
只对 MCP 客户端成立。

**建议**：`note` 区分「MCP 客户端」和「直连管道」两种调用方；或把文件存在检查
也放进 C++ 端做成桥接 op，两边行为一致。

## 3. `viewport_capture` 在 5.5 上还有一条没验的路径

今天把它改成 `Viewport->Draw(true)` 同步出图并默认截关卡视口（见 `Task-Status.md` §0）。
留一个**待验证**：`FViewport::Draw` 在关卡视口非 realtime、编辑器完全后台时是否
一定服务 `FScreenshotRequest`。代码里保留了 `RedrawAllViewports` 作为兜底并回
`exists=false`，调用方看到 false 仍要轮询。如果实机长期稳定 `exists=true`，
可以把兜底和 Python 侧的轮询文案一起删掉。

## 4. `ue_sync status` 的 `ue-modified` 把「编辑器内脏标记」和「已存文件内容变了」混成一个状态

**现象**：`init` 拉完立刻 `status`，11 个 WaterStains MF 报 `ue-modified`，
文本和 UE 内容其实一致——它们只是在编辑器里被重编译过、**没存盘**。存盘后就 clean。

**影响**：看到 `ue-modified` 的第一反应是「UE 那边改了什么」，实际是「有人没存」。
排查方向完全不同。

**建议**：`transcode_status` 已经单独回 `dirty` 布尔，`compute_status`/`classify`
把它拆成独立状态 `ue-dirty`（或在 row 里加一列），别并进 `ue-modified`。

## 5. `material_instance_params_set` 只增不删，文本镜像那条删除路径没验过

**现象**：MIC 上一旦写了 override（哪怕值等于父默认，桥接也**不会**裁掉），
用 `_params_set` 再怎么写都去不掉。lilToon 那边的 `MI_Toon_Body` 就躺着一个废弃
builder 留下的 `Shadow2ndBorder`。

**待验证**：`.mi.nexus` 里删掉 `[scalar]` 下那一行再 `push`，plan 里有没有
对应的 remove 动词、`transcode_apply` 是否真的清掉 override。如果有，文档里写明
「删 override 走镜像」；如果没有，这就是缺口，需要 `material_instance_params_set`
加 `clear: [names]` 或 `prune_defaults: true`。

## 6. `build-engine-plugins.bat` 不检查编辑器是否在跑

**现象**：脚本第 28 / 42 行 robocopy 把新 DLL 同步进 `Engine/Plugins/Editor/`。
编辑器在跑时 DLL 被锁，robocopy 对单文件失败但整体 errorlevel 可能 <8，脚本继续
往下走并打印 DONE——**看起来成功了，引擎里还是旧 DLL**。

**建议**：开头加 `tasklist | find "UnrealEditor"` 非空即 `exit /b 2`；或把脚本拆成
`build`（只出包到 `F:\UEPluginBuild`，编辑器可以开着）和 `install`（同步进引擎，
要求编辑器关闭）两段。今天的流程是手动先 `editor_request_exit` 再跑整个 bat。

## 7. MCP server 的 venv 被半截 pip 打断后，症状指向错误的地方

**现象**：`.venv\Lib\site-packages` 里只剩 `~e_node_nexus_mcp` 和
`~e_node_nexus_mcp-0.1.0.dist-info`（pip 升级时的临时改名），真包没放回。
`ue-node-nexus-mcp.exe` 一启动就 `ModuleNotFoundError` 退出，客户端只看到
`Connection closed / no callable tools`——第一反应是怀疑刚换的插件。

**建议**：
- README 加一条排障：先手跑 `.venv\Scripts\ue-node-nexus-mcp.exe`，正常表现是
  **挂住等 stdio**，打印 traceback 才是坏
- `pyproject` 加 `ue-node-nexus-mcp --selfcheck`（import 自己 + 打印版本 + 列 pipe），
  给客户端配置一条能快速验证的命令
- 客户端（Cursor）会缓存失败连接，修好后必须手动 reload MCP——写进文档

## 8. 读 op 的返回形状不统一，靠猜

**现象**：`object_properties_get(compact)` 回 `items: [[name,type,value]]`；
`node_params_get` 回 `params: [{name, value, ...}]`；`material_instance_params_get`
回 `items: [{...}]` 或 `[type,name,value]` 三元组（取决于 format）。同一个
「读属性列表」意图三种形状，写脚本时每个都要先打一发看结构。

**建议**：至少在 `ue_capability_get(detail="schema")` 里把**响应**形状也列出来
（现在只有 payload schema）；长期统一为 `items: [{name, type, value}]`。

## 9. `test_cpp_compile_check` 的桩头不覆盖 `UObject/UnrealType.h`

**现象**：新增的 `Private/Level/UeNodeNexusBridgeLevelActorPropertyOps.cpp`
用了 `FProperty` / `FStructProperty` / `FPropertyChangedEvent`，桩里没有，
所以进不了 `CHECKED_SOURCES`，只能靠真 UAT 编译兜底（今天就是这么验的）。

**建议**：补 `UnrealType.h` 桩（`FProperty`、`FStructProperty`、`FObjectPropertyBase`、
`CastField`、`ContainerPtrToValuePtr`、`ImportText_Direct`/`ExportText_Direct`、
`FPropertyChangedEvent`），把这个 TU 和 `Object/UeNodeNexusBridgeObjectPropertyValue.cpp`
一起加进检查列表。

## 10. 文本镜像不区分「贴图参数未设置」和「设置为引擎 DefaultTexture」

**现象**：`M_ToonShade` 四个 `TextureSampleParameter2D` 的 `Texture` 都是引擎
`DefaultTexture`，`.mat.nexus` 里一律不出现 `Texture=`（引擎默认值按规则省略）。
读文本的人无法区分「作者没设」和「作者显式设成 DefaultTexture」——虽然对引擎来说
两者等价，但对「这个采样器有没有被认真配置过」这个问题是有信息量的。

**建议**：只是文档层面的事——在 `text_mirror` guide 里写一句「贴图参数省略 = 引擎
默认贴图（DefaultTexture）」。不建议改导出规则。

## 11. 编辑器刚起来时 `viewport_camera_get` 读到的是过渡机位

**现象**：管道刚出现就读相机得到 `(922,-794,265)`，几秒后再读变成
`(23,-101,442)`——关卡加载完才把地图保存的机位套上去。`orbit_capture.py` 这种
「先存机位、拍完还原」的脚本如果在这个窗口里跑，会把一个过渡机位当成用户机位还原回去。

**建议**：`viewport_camera_get` 回一个 `level_loaded` / `world_ready` 标志（或让
`level_current_get` 带上），脚本据此等待；或者干脆在 `bridge_capabilities_get`
里暴露「编辑器就绪」状态。低优先级，知道就行。

## 12. `viewport_capture` 的 `filename` 只认 `[A-Za-z0-9_-]`

不是 bug，是安全边界（防止逃出 ScreenShotDir），但 `+` 都不给过，拿角度拼文件名
（`yaw+030`）第一次必踩。错误信息是清楚的，只是 schema 里没写这条规则——
建议把字符集写进 `viewport_capture` 的 payload schema 描述里。

---

## 已确认不是问题（免得再查一遍）

- `MaterialOutputProperties()` **有** `WorldPositionOffset`（lilToon 仓库的旧笔记说没有，
  那是对旧插件的，已在 lilToon 侧改正）。`out.WorldPositionOffset` 在镜像里往返过。
- `bridge_capabilities_get` 的 `transcode_root` 编辑器重启后为空是预期：根是会话态，
  下一次 `ue_sync` 调用经 `ensure_schema → ensure_root_registered` 会重新注册。
- 请求一直在 game thread 上跑（`AsyncTask(GameThread)`），lilToon 崩溃笔记里
  「非游戏线程弹模态框」的推断不成立——真正的根因是 SCC 模态框泵消息导致请求嵌套，
  新版已经用 `SavePackageDirect` + `bridge_busy` 守卫堵住。
