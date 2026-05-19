# 大坝母材质 MCP 复刻执行记录

## 边界

本次测试目标是把源材质 `/Game/YN/Material/大坝母材质.大坝母材质` 通过固定 MCP 接口复刻到独立目标材质。

允许：

- 使用 `graph_node_info_get_w_pos(format="indexed")` 读取源材质结构、参数、连线和位置。
- 使用 `asset_create` 创建目标材质。
- 使用 `graph_patch_apply` 批量创建目标材质节点、写入参数、写入连线和连接 `MaterialOutput`。
- 在目标材质内部复用已经由 MCP 创建的重复结构。

禁止：

- 禁止从源材质直接复制 `UMaterialExpression`、GraphNode 或 UObject 指针到目标材质。
- 禁止使用 UE Python 或临时脚本直接操作 UE 资产。
- 禁止把源对象复制称为 MCP 手动复刻。

## 当前源图

- 源材质节点包：`graph_node_info_get_w_pos(format="indexed", id_mode="alias")`
- 源图返回：86 个节点，其中 `MaterialOutput` 是伪节点，真实表达式节点为 85 个。
- 最新运行时验证：`NamedRerouteUsage` 已返回 `DeclarationName`，可复刻 declaration/usage 关系。
- 根输出：`MaterialFunctionCall_00.Material Attributes > MaterialOutput.MaterialAttributes`

## 本轮实测结果

目标材质：

```text
/Game/YN/Material/大坝母材质MCPtest_full_1779212723.大坝母材质MCPtest_full_1779212723
```

执行路径：

- `asset_create` 新建目标材质并保存。
- `graph_node_info_get_w_pos(format="indexed")` 读取源图结构、参数、连线、位置。
- `graph_patch_apply(create_node)` 批量创建 85 个真实表达式节点。
- `node_params_set` 回放 78 组节点参数。
- `graph_patch_apply(connect_pins)` 回放 110 条连线，包含 `MaterialOutput.MaterialAttributes` 根输出。
- `asset_compile` 编译目标材质。
- `asset_save` 显式保存目标材质。
- `graph_node_info_get_w_pos(format="indexed")` 回读目标材质做数量比对。

验收数据：

| 项目 | 源材质 | 目标材质 |
|:-----|:------:|:--------:|
| 真实表达式节点 | 85 | 85 |
| 连线 | 110 | 110 |
| 带参数节点 | 78 | 78 |
| 编译错误 | 0 | 0 |
| 保存 | 已存在 | 成功 |

本轮暴露并修复的问题：

- 参数节点创建后的初始 alias 与源图显示名不同，复刻执行时必须使用 `create_node` 返回的真实节点 ID，而不是直接拿源 alias 写目标图。
- `SetMaterialAttributes.AttributeSetTypes` 写入后需要同步 `Inputs`，否则后续连接 `材质属性`、`环境光遮挡`、`粗糙度` 等动态 pin 会失败。

## 验收

- 目标材质真实表达式节点数应为 85。
- 目标材质整图读取应返回 86 个节点，包含 `MaterialOutput`。
- 参数值应包含 `ParameterName`、`Name`、`Group`、贴图引用、材质函数引用、枚举值、布尔值和 `DeclarationName`。
- 连线应覆盖源图 `E` 段，包括 `MaterialOutput.MaterialAttributes`。
- 最终执行编译和显式保存，再重新读取目标材质比对。
