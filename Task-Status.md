# Task-Status

## 1. 当前任务 / 需求 / 待办

- 检查点：当前雨玻璃约 80 分。竖棱仍是三面混两套场；圆柱运输层未动。

## 2. 已完成

- Soft Unlit PNO；Realistic 独立 Unlit IOR。
- 软边按屏幕尺度自适应；相近水珠水平邻格互拉合体。
- coverage 三面混结果（硬法线立方体靠接缝宽度羽化）。

## 3. 高价值信息索引

- `.high-value-information/RainGlass-Spec.md` / `RainGlass-Presets.md` / `RainGlass-SoftEdge.md` / `RainGlass-Coalesce.md` / `RainGlass-Seams.md` / `RainGlass-Jitter.md`
- SoftDisc：`k = sat(w/R * 1.2 - 0.25)`，`inner = R * k * 0.28`
- 合体：`w = S(2.125R, 0.375R, d)`，对称拉到中点
- blog HVI 接缝是 2D 壁纸覆盖度，消不掉立方体 UV 不连续
- 禁：FunctionInput PreviewValue；SceneColor 后 save；mask 进 Opacity；DDX(WorldPosition)
