# 雨玻璃网格规范

效果迁移只认两棵 shrei-blog。Water-Stains 雨着色器 / `blog-surface` / 风格 lerp **不当公式、不打开**。

## 1. 基准路径

| 档 | 树 | 场 | 着色 | 参数 |
|:-----|:-----|:-----|:-----|:-----|
| **柔和 Soft** | `D:\Projects\shrei-blog` | `src/components/starlake/webgl/glsl/raindropFieldChunk.js` | `.../programs/raindropProgram.js` | `.../surface/raindropParams.js` |
| **写实 Realistic** | `D:\AI\TEST\blog\shrei-blog` | 同相对路径 | 同相对路径 | 同相对路径 |

逐行 diff（忽略 CRLF/LF）：两棵树当前工作区的雨滴模块 **文本相同**。Soft / Realistic 的差不在 DropLayer2，在 `raindropProgram` 的湿区 mip：Soft 网格只做 PNO 透镜；Realistic 还要把 `sampleWallpaperBlurred` 落到半透明折射粗糙度上。禁止用 8fca 粘滞或湿痕 smear 冒充写实档。

## 2. 场公式（两档共用）

坐标：传入 uv 为 y 向上。`uvMod.y += t` 使图案下落；`st.y > y` 一侧是雨滴上方湿痕。

**DropLayer2** 返回 `vec2(dropMask, trailMask)`：

- 格 `a=(6,1)`，`grid=(12,2)`；列偏移 `N(id.x)`；`n = N13(id.x*35.2 + id.y*2376.1)`。
- 摆动：`wiggle = sin(y+sin(y))`，`x = (n.x-0.5 + wiggle*(0.5-abs(x))*(n.z-0.5))*0.7`。
- `scaledT = t/0.75`。
- `rhythm = n.y > sawP ? 0.94 : mix(0.62, 0.88, fract(n.x*3.71+n.z*5.13))`。
- `ti = fract(scaledT*rhythm+n.z)`；`y = (Saw(0.85,ti)-0.5)*0.9+0.5`。
- 水平邻格 `id.x±1` 与本格同时算中心，间距小于 `2.125R` 时对称互拉，三颗 SoftDisc 相加。见 `RainGlass-Coalesce.md`。
- `mainDrop` 为本格+左右吸引后的圆盘和。大中珠 `inner≈0` 仍是 `S(R,0,d)`；极小珠最多收到 `S(R, 0.28R, d)`。见 `RainGlass-SoftEdge.md`。
- 湿痕带：`r=sqrt(S(1,y,st.y))`，`trail = S(0.23r, 0.15 r², |st.x-x|) * trailFront * r²`，`trailFront=S(-0.02,0.02,st.y-y)`。
- 列珠串：`y2=fract(UV.y*10)+(st.y-0.5)`，`droplets=SoftDisc(length(st-(x,y2)), SoftEdge*0.75)`；`dropMask = mainDrop + droplets*r*trailFront`。

**StaticDrops**：`uv*40`，形状 `SoftDisc(d, 0.3)`，`Saw(0.025, fract(t+n.z))` 明灭。

**Drops**：`c = S(0.3,1, m1.x + m2.x)`（近+远已含邻格合体；水雾另加，不进 3-tap），`trail = max(m1.y*l0, m2.y*l1)`，远层 `uv*1.85`。

**层权**（`raindropProgram`）：`l0=S(-0.5,1,rain)*2`，`l1=S(0.25,0.75,rain)`，`l2=S(0,0.5,rain)`。`dropT = dropTime*0.2`。3-tap `e=0.002` 只对 **Drops.x**。`wetness=sat(max(trail, dropMask))`。折射 `n * refraction * 视口高`。Rim `S(0.3,0.8,|n|*30)*dropMask*0.08`。Spec `pow(sat(N·(-0.5,-0.8)), 20)*dropMask*0.15`。

S 为反向边安全 Hermite：`x=sat((t-a)/(b-a±1e-6))`，`x*x*(3-2x)`。

## 3. 默认参数

Heartfelt 共用：`rainAmount=0.8`，`dropSpeed=0.75`，`refractionStrength=0.3`，`rim=0.08`，`spec=0.15/20`。运输层：`玻璃缩放=200`，`缩放率=1`，`玻璃不透明度=0`。

| | Soft | Realistic |
|:---|:---|:---|
| 父材质 | `M_WS_RainGlass` Unlit PNO | `M_WS_RainGlass_Realistic` Unlit IOR |
| 锯齿 | 0.4 | 0.4 |
| 折射 | PNO 强度 0.3，`N - WO*16` | 滴心 IOR 1.33，`N + WO*法线强度`，干玻璃 IOR=1 |

## 4. 网格只改运输层

- UE **Z-up**。立方体 UV：±X `(Y,Z)`，±Y `(X,Z)`，±Z `(X,Y)`。V=世界 Z。禁止 `(Z,Y)`。
- 面权重在 **coverage** 空间：`P = WorldPosition(WPT_ExcludeAllShaderOffsets) - Actor`。`W = normalize(max(pow(|N|, 接缝锐度), saturate(1 - (ext-|P|)/接缝宽度)))`，`ext=max(|P|)`。三次采样 RainField 后按 W 混结果。禁止主轴 `If`。禁止先 lerp UV 再采一次。禁止 `cross(N,up)`。禁止 `DDX/DDY(WorldPosition)`。PNO 只写 Pixel Normal，不回头改 UV/权重。详见 `RainGlass-Seams.md`。
- 摆动：格内稳定相位 `n.x*20+n.z`，再 `sin(φ+sin(φ))`。禁止像素 `UV.y*20`。禁止接 Saw 下落 `y*20`。详见 `RainGlass-Jitter.md`。
- Soft 折射只 PNO：Refraction 针=「折射强度」0.3，世界偏移 `*16`。Realistic 是独立父材质，走 IOR：`normalize(Nws + WorldOffset * 法线强度)`，干玻璃 IOR=1，滴心 1.33。**3-tap 吃合体后的 DropMask**（本格+左右主珠+列珠）。湿痕带、水雾不进有限差分。禁止 SceneColor 后 save。禁止 Refraction=1 加大 k。禁止 Realistic 复用 Soft 的 `*16` PNO 图。
- 合成对齐 `raindropProgram`：`col = background.rgb; col += rim; col += spec;` alpha 是背景自己的 alpha，**不吃 dropMask**。半透明 Opacity 只等于 `玻璃不透明度 + rim + spec`（高光预乘），滴心 Opacity=0，靠 PNO 看折射背景。禁止把 DropMask / TrailMask / Mist 写进 Opacity（那是对着黑清漆，水珠和拖尾会变黑）。
- 湿痕是 blog 的湿润柔化，不是脏膜。网格没有 SceneColor mip，湿痕不画成暗条。列珠串在 DropMask 里走完整透镜。
- StaticDrops **禁止进 DropMask 的 3-tap**。水雾也不进 Opacity。
- 时间：`Time * 下落速度 * 0.2` 后进场，对齐 `dropTime*0.2`。
- 接缝默认：`接缝宽度=8`，`接缝锐度=4`。硬法线立方体真正起作用的是宽度。
- 写实湿痕是 blog 的 focus mip，不是沿面 V 的 WorldOffset smear。Realistic 用 IOR+Roughness 采背后景；禁止 3-tap 湿痕，禁止湿痕进 Opacity，禁止 SceneColor 节点。

## 5. 资产与重编

```
MF_WS_S / N / N13 / Saw
MF_WS_SoftDisc       S(R, inner, d)；inner 随 fwidth(d) 相对 R 收紧
MF_WS_DropPos        单格中心 (x,y)
MF_WS_DropLayer2     左右邻格互拉 + 三盘相加
MF_WS_StaticDrops
MF_WS_Drops          DropMask=近+远+邻格合体+珠串  TrailMask  PNOMask=合体滴体
MF_WS_RainField      3-tap PNOMask；中心 Mist；湿痕不进法线
MF_WS_RainFaces      coverage 三面权重 + 三次 RainField + 世界偏移
M_WS_RainGlass                 Soft 父：RainFaces + PNO*16 + rim/spec
M_WS_RainGlass_Realistic       Realistic 父：独立图，Unlit IOR 水珠法线
MI_WS_Rain_Soft                Unlit PNO，折射强度 0.3
MI_WS_Rain_Realistic           IOR 1.33，法线强度 3
```

链：SoftDisc / DropPos → DropLayer2 / StaticDrops → Drops → RainField → RainFaces → 两父 → 两 MI。Soft 父改完不要顺手重编 Realistic 父的场参数；Realistic 父改完不要碰 Soft。

`reset_function` / `clear_graph_expressions` 后一次 `graph_build_apply`。禁止 `asset_delete` 被引用 MF。MI `compile_after=false` 再 save。先绑当前 Shadetest pid。

图构建脚本只当搭节点工具，脚本里旧公式以本 spec 为准。

## 6. 禁区

SceneColor 后 save；DefaultLit IOR 雨玻璃缩略图（D3D12 AV）；PNO Refraction=1 + 大 k；WorldPosition DDX/DDY；水雾进 DropMask 有限差分；DropMask/TrailMask/Mist 写进 Opacity；`(Z,Y)` UV；`cross(N,up)`；主轴 `If` 选面；lerp UV 再单次采样；`WPT_Default` 让 PNO 改 coverage；对已有图 `graph_build_apply` 而不先清空；用 Water-Stains / 部署物 hoverK 当差异源；Realistic 复用 Soft `*16` PNO 图。

## 7. 验收

Soft 对齐当前 Projects/shrei-blog（已到位）。Realistic 是独立 Unlit IOR 父材质，折射不是 Soft 的 PNO。compile+save 后编辑器仍活。
