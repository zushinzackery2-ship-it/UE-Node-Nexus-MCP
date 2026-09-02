# 雨玻璃接缝：blog 不变量在立方体上的对应

公式源只认两棵 shrei-blog。接缝不变量写在：

- `D:\Projects\shrei-blog\.high-value-information\Starlake-Surface-Invariants.md` §二 §三
- 同文：`D:\AI\TEST\blog\shrei-blog\.high-value-information\Starlake-Surface-Invariants.md`
- 成文：`src/content/posts/20-starlake-wallpaper-refraction-seam.md`（softRange / 过渡带）
- 成文：`src/content/posts/15-ue-triplanar-material.md`（三面：三次采样 → `abs(N)^Sharpness` → 归一化混合）

壁纸矩形和立方体面不是同一套几何，但翻车的结构相同。

## 1. 覆盖度 vs 采样（§二）

折射偏移可以很大。用 **位移后的坐标** 做分区 / alpha，会在边界打出洞或一条暗线。

正确切法：

| 坐标 | 职责 |
|:---|:---|
| **coveragePixel**（未位移） | 分区归属、透明度、接缝权重 |
| **samplePixel**（位移后） | 只决定取到的颜色 / 折射看到什么 |

接缝采样行必须钉在 `coveragePixel.y`，只有允许扰动的那一轴跟位移走。玻璃扰动的是「看见的内容」，不是「页面自己的边界」。

网格对应：

- coverage：`WorldPosition(WPT_ExcludeAllShaderOffsets) - ActorPosition`，以及由它和 `VertexNormalWS` 算出的面权重。禁止让 PNO 偏移回头改 UV / 改权重。
- sample：PNO 只写在 Pixel Normal 上，Refraction 针仍是「折射强度」0.3。

`WPT_Default` 会把 shader offset 喂回位置，等于用 samplePixel 去判 coverage。禁止。

## 2. soft mask 不能退化成布尔（§三）

`if (insideWallpaper > 0.5)` 把 0..1 覆盖度砍成 1px 硬切。接缝层在 0.5 那一行盖不住的色差，就是用户看到的截断线。

网格上同一结构是父材质的主轴 `If`：

```
|Nx| > |Ny| ? UV_YZ : UV_XZ
max(|Nx|,|Ny|) > |Nz| ? 那条 : UV_XY
```

立方体棱上 UV 空间跳变，雨滴场不对齐，就是那条接缝。`If` 是布尔面选择，不是 0..1 覆盖度。

## 3. 三面要混结果，不要混 UV

`15-ue-triplanar-material.md`：空间 P → 三套二维 UV → **三次采样** → `W = normalize(pow(abs(N), Sharpness))` → `Σ Sample_i * W_i`。

禁止 `UV = Σ UV_i * W_i` 再采一次：棱上会把两套格子剪成一条拉伸带。

Color / Mask 用同一套权重线性混。法线必须先变到世界再混：每面 `(ndx, ndy)` 乘该面的 T/B，得到世界偏移后再 `Σ off_i * W_i`。

## 4. 硬法线立方体：只靠 `abs(N)^k` 不够

默认立方体面法线是轴对齐的。面上 `N=(1,0,0)`，`pow(abs(N), k)` 仍是 `(1,0,0)`，棱两侧各属于不同顶点，权重还是布尔。

所以权重取 **coverage 空间的棱距离** 与法线权重的分量 max，再归一化：

```
P = coverage 位置（未位移）
ext = max(|P.x|, |P.y|, |P.z|)
inward = ext - abs(P)          // 该轴的面为 0，往里为正
Praw = saturate(1 - inward / 接缝宽度)
Nraw = pow(abs(N), 接缝锐度)
W = normalize(max(Nraw, Praw))
```

面上内部 `W` 仍是单轴；棱上两侧都走到约 0.5/0.5，雨滴场软过渡。`接缝宽度` 控制立方体棱羽化（硬 N 时真正起作用的量）。`接缝锐度` 给圆角/斜面，太大又变硬切，太小斜面糊成三套场叠影。

禁止 `DDX/DDY(WorldPosition)` 做融合。禁止再加主轴 `If`。
