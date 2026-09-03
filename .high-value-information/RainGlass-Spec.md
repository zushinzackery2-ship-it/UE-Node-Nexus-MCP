# 雨玻璃网格规范

效果迁移只认两棵 shrei-blog。Water-Stains 雨着色器 / `blog-surface` / 风格 lerp **不当公式、不打开**。

## 1. 基准路径

| 档 | 树 | 场 | 着色 | 参数 |
|:-----|:-----|:-----|:-----|:-----|
| **柔和 Soft** | `D:\Projects\shrei-blog` | `src/components/starlake/webgl/glsl/raindropFieldChunk.js` | `.../programs/raindropProgram.js` | `.../surface/raindropParams.js` |
| **写实 Realistic** | `D:\AI\TEST\blog\shrei-blog` | 同相对路径 | 同相对路径 | 同相对路径 |

逐行 diff（忽略 CRLF/LF）：两棵树当前工作区的雨滴模块 **文本相同**。Soft / Realistic 的差不在 DropLayer2，在 `raindropProgram` 的湿区 mip：Soft 网格只做 PNO 透镜；Realistic 还要把 `sampleWallpaperBlurred` 落到半透明折射粗糙度上。禁止用 8fca 粘滞或湿痕 smear 冒充写实档。

**注：那条湿区柔化目前没建。** `MSM_Unlit` 给不出 Roughness 针，Realistic 父材质只接了 EmissiveColor / Opacity / Normal / Refraction。两档现在的唯一差别是折射模型。偏差清单见 `RainGlass-Align.md`。

## 2. 场公式（两档共用）

坐标：传入 uv 为 y 向上。`uvMod.y += t` 使图案下落；`st.y > y` 一侧是雨滴上方湿痕。

**DropLayer2** 返回 `vec2(dropMask, trailMask)`：

- 格 `a=(6,1)`，`grid=(12,2)`；列偏移 `N(id.x)`；`n = N13(id.x*35.2 + id.y*2376.1)`。
- 摆动：`wiggle = sin(y+sin(y))`，`x = (n.x-0.5 + wiggle*(0.5-abs(x))*(n.z-0.5))*0.7`。
- `scaledT = t/0.75`。
- `rhythm = n.y > sawP ? 0.94 : mix(0.62, 0.88, fract(n.x*3.71+n.z*5.13))`。
- `ti = fract(scaledT*rhythm+n.z)`；`y = (Saw(0.85,ti)-0.5)*0.9+0.5`。
- 水平邻格 `id.x±1` 与本格同时算中心。只按水平距锁成一团，y 由正在滑的带着走。见 `RainGlass-Coalesce.md`。
- `mainDrop` 为本格+左右吸引后的圆盘和。大中珠 `inner≈0` 仍是 `S(R,0,d)`；极小珠最多收到 `S(R, 0.28R, d)`。见 `RainGlass-SoftEdge.md`。
- 湿痕带：`r=sqrt(S(1,y,st.y))`，`trail = S(0.23r, 0.15 r², |st.x-x|) * trailFront * r²`，`trailFront=S(-0.02,0.02,st.y-y)`。
- 列珠串：`y2=fract(UV.y*10)+(st.y-0.5)`，`droplets=SoftDisc(length(st-(x,y2)), SoftEdge*0.75)`；`dropMask = mainDrop + droplets*r*trailFront`。

**StaticDrops**：`uv*40`，形状 `SoftDisc(d, 0.3)`，`Saw(0.025, fract(t+n.z))` 明灭。

**Drops**：`c = S(0.3,1, m1.x + m2.x)`（近+远已含邻格合体；水雾另加，不进 3-tap），`trail = max(m1.y*l0, m2.y*l1)`，远层 `uv*1.85`。

**层权**（`raindropProgram`）：`l0=S(-0.5,1,rain)*2`，`l1=S(0.25,0.75,rain)`，`l2=S(0,0.5,rain)`。`dropT = dropTime*0.2`。3-tap `e=0.002` 只对 **Drops.x**。`wetness=sat(max(trail, dropMask))`。折射 `n * refraction * 视口高`。Rim `S(0.3,0.8,|n|*30)*dropMask*0.08`。Spec `pow(sat(N·(-0.5,-0.8)), 20)*dropMask*0.15`。

S 为反向边安全 Hermite：`x=sat((t-a)/(b-a±1e-6))`，`x*x*(3-2x)`。

## 3. 默认参数

Heartfelt 共用：`rainAmount=0.8`，`dropSpeed=0.75`，`refractionStrength=0.3`，`rim=0.08`，`spec=0.15/20`。运输层：`缩放率=1`，`玻璃不透明度=0`，`水雾强度=1`。

`玻璃缩放` **两档不同**：Soft 500，Realistic 800（都不是脚本原来写的 200）。改 `build_mi_presets.py` 前先读一遍线上 MI，别拿一个值覆盖两档。

`spec=0.15` 只是照抄 blog 的数；UE 侧光向没归一化，实际高光是 blog 的 0.312 倍。见 `RainGlass-Align.md §3`。

| | Soft | Realistic |
|:---|:---|:---|
| 父材质 | `M_WS_RainGlass` Unlit PNO | `M_WS_RainGlass_Realistic` Unlit IOR |
| 锯齿 | 0.4 | 0.4 |
| 折射 | PNO 强度 0.3，`N - WO*16` | 滴心 IOR 1.33，`N + WO*法线强度`，干玻璃 IOR=1 |

## 4. 网格只改运输层

- UE **Z-up**。面内线性：±X `(Y,Z)`，±Y `(X,Z)`，±Z `(X,Y)`。V=世界 Z。禁止 `(Z,Y)`。禁止 atan2 圆柱（会在单个面里切开）。
- 覆盖度：`P = WorldPosition(WPT_ExcludeAllShaderOffsets) - Actor`。`W = normalize(max(pow(|N|, 接缝锐度), sat(1-(ext-|P|)/接缝宽度)))`。**RainField 只求值一次**，UV 与切线框按 W 选：`U = dot(W,(Py,Px,Px))`、`V = dot(W,(Pz,Pz,Py))`、`T = (Wy+Wz,Wx,0)`、`B = (0,Wz,Wx+Wy)`。one-hot 下是精确选择而非插值，无 `If` 无分支。**只在硬法线轴对齐盒子上成立**：曲面或大 yaw 下 W 摊开，这就退化成本节禁止的"lerp UV 再采一次"。禁止主轴 `If`。禁止 `cross(N,up)`。禁止 `DDX/DDY(WorldPosition)`。PNO 只用网格法线。详见 `RainGlass-Seams.md`。
- 摆动：格内稳定相位 `n.x*20+n.z`，再 `sin(φ+sin(φ))`。禁止像素 `UV.y*20`。禁止接 Saw 下落 `y*20`。详见 `RainGlass-Jitter.md`。
- Soft 折射只 PNO：Refraction 针=「折射强度」0.3，世界偏移 `*16`。Realistic 是独立父材质，走 IOR：`normalize(Nws + WorldOffset * 法线强度)`，干玻璃 IOR=1，滴心 1.33。**3-tap 吃合体后的 DropMask**（本格+左右主珠+列珠）。湿痕带、水雾不进有限差分。禁止 SceneColor 后 save。禁止 Refraction=1 加大 k。禁止 Realistic 复用 Soft 的 `*16` PNO 图。
- 水雾进 `DropMask` 不进 `PNOMask`：`Drops.RawSum`（未经 S 的近+远）+ 水雾*L0*`水雾强度`，在 RainField 里过 `S(0.3,1,·)`。对齐 blog 的 `c = S(0.3,1, s+m1.x+m2.x)`，同时保住"水雾不进 3-tap"。`水雾强度=0` 回到无水雾。
- 高光光向必须归一化：`(-0.5299989, -0.8479983)`，且 `Normalize(DropNormal + 1e-4)`。不归一化会让高光只有 blog 的 0.312 倍。
- 合成对齐 `raindropProgram`：`col = background.rgb; col += rim; col += spec;` alpha 是背景自己的 alpha，**不吃 dropMask**。半透明 Opacity 只等于 `玻璃不透明度 + rim + spec`（高光预乘），滴心 Opacity=0，靠 PNO 看折射背景。禁止把 DropMask / TrailMask / Mist 写进 Opacity（那是对着黑清漆，水珠和拖尾会变黑）。
- 湿痕是 blog 的湿润柔化，不是脏膜。网格没有 SceneColor mip，湿痕不画成暗条。列珠串在 DropMask 里走完整透镜。
- StaticDrops **禁止进 DropMask 的 3-tap**。水雾也不进 Opacity。
- 时间：`Time * 下落速度 * 0.2` 后进场，对齐 `dropTime*0.2`。
- 接缝默认：`接缝宽度=0.5`（原 8，实测带宽=接缝宽度一比一，8 时距边 4uu 处邻面占 33%），`接缝锐度=4`。硬法线立方体面心是单套线性 UV。`Praw` 那一项在立方体上纯属倒贴，详见 `RainGlass-Seams.md`。
- 写实湿痕是 blog 的 focus mip，不是沿面 V 的 WorldOffset smear。Realistic 用 IOR+Roughness 采背后景；禁止 3-tap 湿痕，禁止湿痕进 Opacity，禁止 SceneColor 节点。

## 5. 资产与重编

```
MF_WS_S / N / N13 / Saw    分母 = (B-A) + 1e-6，没有同号 step 那一套
MF_WS_SoftDisc       S(R, inner, d)；PixelWidth 由调用方传入，函数内不求导
MF_WS_DropPos        单格中心 (x,y)
MF_WS_DropLayer2     三盘相加（左右走邻列自己的行框）+ MergeStrength 门控的互拉
MF_WS_StaticDrops
MF_WS_Drops          DropMask  TrailMask  PNOMask=滴体  RawSum=未经 S 的近+远
MF_WS_RainField      3-tap PNOMask；DropMask=S(0.3,1,RawSum+水雾)；湿痕不进法线
MF_WS_RainFaces      按 W 选单套线性 UV，**只求值一次** RainField，世界偏移
M_WS_RainGlass                 Soft 父：RainFaces + PNO*16 + rim/spec
M_WS_RainGlass_Realistic       Realistic 父：独立图，Unlit IOR 水珠法线
MI_WS_Rain_Soft                Unlit PNO，折射强度 0.3
MI_WS_Rain_Realistic           IOR 1.33，法线强度 3
```

链：SoftDisc / DropPos → DropLayer2 / StaticDrops → Drops → RainField → RainFaces → 两父 → 两 MI。Soft 父改完不要顺手重编 Realistic 父的场参数；Realistic 父改完不要碰 Soft。

`reset_function` / `clear_graph_expressions` 后一次 `graph_build_apply`。禁止 `asset_delete` 被引用 MF。MI `compile_after=false` 再 save。先绑当前 Shadetest pid。

**重建必须顺链走到底。** 重建一个 MF 会作废上游 MF 里指向它的 `MaterialFunctionCall` 节点，即使输出列表没变——只改 RainField 就会让 RainFaces 报 `(Node ComponentMask) Missing ComponentMask input`。改到哪一环，就从那一环一路重建到两张父材质。以父材质 `asset_compile` 的 0 error 为渲染判据。

**禁止 `build_rain_chain.py` 一次跑整链。** 本工程源码管理检出失败，每次 save 弹模态框并走「保存全部脏包」，给一堆材质生成缩略图，撞上缩略图 D3D12 AV 把编辑器打崩（实测 `EXCEPTION_ACCESS_VIOLATION reading 0x0`，栈顶 `UnrealEditor_D3D12RHI`）。逐个脚本跑，每步确认 `UnrealEditor` 进程还在。

图构建脚本只当搭节点工具，脚本里旧公式以本 spec 为准。

## 6. 禁区

SceneColor 后 save；DefaultLit IOR 雨玻璃缩略图（D3D12 AV）；PNO Refraction=1 + 大 k；WorldPosition DDX/DDY；水雾进 DropMask 有限差分；DropMask/TrailMask/Mist 写进 Opacity；`(Z,Y)` UV；`cross(N,up)`；主轴 `If` 选面；lerp UV 再单次采样；`WPT_Default` 让 PNO 改 coverage；对已有图 `graph_build_apply` 而不先清空；用 Water-Stains / 部署物 hoverK 当差异源；Realistic 复用 Soft `*16` PNO 图。

## 7. 验收

Soft 对齐当前 Projects/shrei-blog（已到位）。Realistic 是独立 Unlit IOR 父材质，折射不是 Soft 的 PNO。compile+save 后编辑器仍活。
