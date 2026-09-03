# 雨玻璃 blog 对齐偏差与成本

基准唯一：两棵 shrei-blog 的 `raindropFieldChunk.js` / `raindropProgram.js` /
`raindropParams.js` 逐字节相同（只差 CRLF）。所谓 Soft / Realistic 之分是 UE 侧的取舍，
blog 只有一套雨滴着色器。

探针：`Water-Stains/Scripts/probe_align_seam.py`（离线复现 blog 与 UE 两套场，96² 采样点，
按线上 MI 数值：玻璃缩放 500、雨量 0.8、软边 0.4、锯齿概率 0.4）。

## 1. 掩膜统计

| 变体 | 均值 | 覆盖率(>0.5) | 对 blog 的 rms |
|:---|:---|:---|:---|
> ⚠ 本节量的是 `DropMask`。**这个数字只对 Realistic 成立。** 那边 `DropMask` 还接了
> `lerp(1, IOR, ·)` 到 Refraction 针，所以掩膜里的水雾真的会折射背景。
> Soft 档的 `DropMask` 只经 rim / 高光出画，而两者都被 `DropNormal` 把关，
> 水雾进不了 `DropNormal`，掩膜里的水雾在 Soft 上一个像素都不落。详见 §2。

| 变体 | 均值 | 覆盖率(>0.5) | 对 blog 的 rms |
|:---|:---|:---|:---|
| blog 基准 | 0.0728 | 0.068 | — |
| 最初（无水雾，合体开） | 0.0492 | 0.046 | 0.1669 |
| 水雾接回，合体仍开 | 0.0705 | 0.066 | 0.1153 |
| **现状（水雾=1，合体=0，邻列修好）** | 0.0729 | **0.068** | **0.0761** |
| 单盘 + blog 摆动（= blog 本身） | 0.0728 | 0.068 | 0.0000 |

读法：

- 水雾接回掩膜后，Realistic 的水覆盖从差 32% 收到完全一致（0.046 → 0.068 = blog 的 0.068）。
- 最后一行 rms 恰好 0.0000，说明探针的 blog 复现和 UE 复现是同一套代码路径，数字可信。
- **现状残余 rms 0.0761 的主要来源是摆动相位**，不是合体也不是三盘：
  换回 blog 的逐像素 `UV.y*20` 会降到 0.0060，那 0.0060 是三盘"不裁水珠"带来的差。
- ⚠ **更正一条早前的结论**：我最初写"格内摆动相位几乎无影响"，那是在合体开着时量的
  （0.1153 vs 0.1144），被合体的大偏差掩盖了。合体关掉后它是最大的单项。
  但 `RainGlass-Jitter.md` 是看着画面否掉 `UV.y*20` 的（"一滴剪成斜条"），
  那是观感判断，不因这个数字翻案。真要重试，`MF_WS_DropPos` 可以加一路逐像素相位输入。

## 2. 水雾曾经整条丢了（已修）

blog：`c = S(0.3, 1, s + m1.x + m2.x)`，`s = StaticDrops*l0`，水雾**进**掩膜，吃 rim 和高光。

修前 UE：`MF_WS_Drops` 是 `S(0.3, 1, near*L1 + far*L2)`，没有 `s`；`MF_WS_RainFaces.Mist`
在两张父材质里都没接线，编译器直接 DCE 掉——不花钱，也完全不出画。
单水雾项均值 0.0400、峰值 1.6245，`S(0.3,1,水雾)` 单独就有 1.5% 覆盖率。

现在的接法（`DropMask` / `PNOMask` 原本接的是同一根线，现在分开了）：

```
MF_WS_Drops   RawSum  = near*L1 + far*L2        （新增输出，未经 S）
              PNOMask = S(0.3, 1, RawSum)       不含水雾 → 3-tap 干净
MF_WS_RainField
              DropMask = S(0.3, 1, RawSum + 水雾*L0*水雾强度)
```

这样同时满足 blog 观感和 `RainGlass-Jitter.md` 那条"水雾不进 3-tap"。

### Soft 档结构上放不了水雾（已核实，别再试）

编码层面追一遍 Soft 父材质：`faces.DropMask` 只有两个去处，`rim0.B` 和 `spec1.B`。

```
rim  = S(0.3, 0.8, |DropNormal| * 30)                  * DropMask * 边缘光
spec = pow(sat(dot(normalize(DropNormal + 1e-4), L)), p) * DropMask * 高光强度
```

两者都只由 `DropNormal` 决定，而 `DropNormal` 是 `PNOMask` 的 3-tap，`PNOMask` 按
`RainGlass-Jitter.md` 那条铁律**不含水雾**。所以在只有水雾的像素上：

- `DropNormal = (0,0)` 精确成立
- `S(0.3, 0.8, 0) = 0` → rim = 0
- `normalize((1e-4,1e-4)) = (0.7071,0.7071)`，与 `L=(-0.53,-0.848)` 点乘 **−0.974**，
  `sat` 后 0，`pow(0,20) = 0` → spec = 0

`light = 0` → `Opacity = 0`、emissive 分子 = 0。**`DropMask` 里的水雾在 Soft 上完全不出画，
无论 `水雾强度` 是多少。** 所以 Soft 的 `faces.MistAmount` 直接接常数 0，参数不再暴露，
整条 StaticDrops 分支被编译器折掉。

Realistic 没这个问题：那边 `DropMask` 还接 `dsat → lerp(1, IOR, ·) → Refraction`，
水雾在那里真的折射背景，所以 `水雾强度=1` 保留在 Realistic 上。

要让 Soft 也有水雾，只有两条路，都堵着：把水雾放进 3-tap（实测导致整面 PNO 爬行），
或者补上 blog 配套的 `focus = maxBlur * wetness` 柔化（要 Roughness 针，`MSM_Unlit` 没有）。

**⚠ 顺带纠一条我自己的误判**：我曾把「高光光向归一化后亮了 3.2 倍」误读成「水雾出现了」，
因为那两条改动在同一次重建里。编辑器视口对比法在这个工程上不可用——
同参数两帧就有 80% 像素差 >2/255（云在动 + 视口不确定），噪声底和信号一样大。
判断这类问题只看图，不看画面。

## 3. 高光曾经只有 blog 的 0.312 倍（已修）

blog：`dot(normalize(n + 1e-4), normalize(vec2(-0.5,-0.8)))`
修前 UE：`dot(Normalize(DropNormal), (-0.5,-0.8))` ——**光向没归一化**。

`|(-0.5,-0.8)| = 0.9434`，过 `pow(.,20)` 后 `0.9434^20 = 0.312`。
两边 `高光强度` 都写 0.15，所以 UE 高光实际只有 blog 的 31%。

已改：两张父材质的光向常数换成 `(-0.5299989, -0.8479983)`，并补回 blog 的 `+1e-4` 保护
（干玻璃上 `DropNormal` 精确等于 `(0,0)`，`Normalize` 在那里是 0/0；DX 上 `saturate(NaN)`
恰好塌成 0 所以以前看不出来，但是 UB）。**高光会比以前明显亮，这是对齐 blog 的结果，
不是变亮了就是错了**；真嫌亮就调 `高光强度`。

## 4. 湿痕层权少乘一档（潜伏）

blog：`max(trail_near*l1*l0, trail_far*l2*l1)`；UE：`max(trail_near*L0, trail_far*L1)`。

雨量 0.8 时 `l1=l2=1`，两边一致；雨量 0.4 时 UE 近层湿痕 **4.63 倍**过强，雨量 0.2 时
blog 是 0 而 UE 是 0.90。湿痕现在没出画所以看不见，一旦接上就是 bug。

## 5. 写实档的招牌功能没建

`RainGlass-Spec.md §1` 说 Realistic 的定义性差别是"把 `sampleWallpaperBlurred` 落到半透明
折射粗糙度上"，对应 blog 的 `focus = maxBlur * wetness`（`maxBlur=1.6`）。

实际 `M_WS_RainGlass_Realistic` 只接了 EmissiveColor / Opacity / Normal / Refraction，
**没有 Roughness**——`MSM_Unlit` 也给不出 Roughness 针。而 `wetness = max(trail, dropMask)`
要的 TrailMask 同样没接。所以两档目前的唯一差别只是折射模型（PNO vs IOR），
blog 那条湿区柔化根本不在。要么换掉 Unlit，要么承认写实档暂时只是"IOR 版柔和档"。

## 6. 每像素求值次数（全部喂活输出，DCE 拿不掉）

```
RainFaces → RainField   x1   （原 x3）
RainField → Drops       x3   （中心 + 2 个有限差分 tap）
Drops     → DropLayer2  x2   （近 + 远）
DropLayer2→ DropPos     x3   （本格 + 左 + 右）
```

| | 优化前 | 现在 | 倍数 |
|:---|:---|:---|:---|
| DropLayer2 | 18 | 6 | 3.0× |
| DropPos | 54 | 18 | 3.0× |
| N13 哈希 | 57 | 19 | 3.0× |
| SoftDisc | 75 | 25 | 3.0× |
| DDX/DDY 对 | 75 | **13** | **5.8×** |

四条都已落地：

1. **三面降到一面。** W 是 one-hot，原先两套 RainField 乘 0 但照算。现在按 W 选：
   `U = dot(W,(Py,Px,Px))`、`V = dot(W,(Pz,Pz,Py))`，切线框同样选：
   `T = (Wy+Wz, Wx, 0)`、`B = (0, Wz, Wx+Wy)`。one-hot 下这是精确选择而非插值，
   无 `If`、无分支，输出与三面混完全相同。RainFaces 节点数 113 → 72。
   **约束**：只在 W 是 one-hot 时成立，即硬法线轴对齐盒子。曲面或大角度 yaw 下 W 会摊开，
   `dot(W,·)` 就退化成 `RainGlass-Spec.md §6` 禁止的"lerp UV 再单次采样"。
   `接缝宽度` 必须保持很小，否则邻近项也会把 W 摊开。
2. **`SoftDisc` 不再自己求导**，`PixelWidth` 由调用方传入。距离场的梯度模在自己度量下恒为 1，
   一个度量算一次footprint 就够。而且改成取 `frac` 之前的网格坐标求导，
   顺手消掉格边那条 `k` 从 0 跳到 1 的硬线。
   注：测试机位实测 `fwidth(D)=0.0183`、`k=0.000`——自适应软边在这个距离本来就不生效，
   原先 75 对导数一分钱没买到，只有远景小珠才激活。
3. **Soft 不再白付不出画的东西。** `湿痕折射` / `粘滞程度` / `水雾强度` 原是 uniform 参数
   （值都让对应特效不出画），乘 0 编译器折不掉。现在三针直接接常数 0，整条 trail smear 链、
   `MF_WS_DropPos` 里三个 sticky lerp、以及整个 StaticDrops 水雾分支全部折掉。
   **代价：这三个参数不再暴露在 Soft 的 MI 上**，要恢复得用 `StaticSwitchParameter`，
   不能用 scalar。Realistic 保留 `水雾强度`（那边水雾真的出画）。
4. **`MF_WS_S` 的同号保护删掉了。** `step/×2/−1/×1e-6/+denom` 五个节点只为让分母不跨零，
   现在是 `denom = (B-A) + 1e-6`。剩 36 处逐像素调用点，每处省 4 条 ≈ 144 条/像素。
   安全性：本链所有调用点的 `|B-A|` 都远大于 2e-6，最接近的是 `S(0.23r, 0.15r², cd)`，
   只在 `r → 0` 时退化，而那里两种符号给出相同的 sat 0/1 极限，且 `trail *= r²` 把它压没。

## 7. 重建会打崩编辑器的那条路

**一次重建整条链（`build_rain_chain.py`）会崩。** 实测：
`EXCEPTION_ACCESS_VIOLATION reading 0x0`，栈顶 `UnrealEditor_D3D12RHI`。

链路：本工程启用了源码管理但检出失败 → 每次 `asset_save` 弹「无法从版本控制检出！」模态框 →
走 `InternalPromptForCheckoutAndSave` 的**保存全部脏包**路径 → 给一堆材质生成缩略图 →
撞上 spec 禁区里那条「雨玻璃缩略图 D3D12 AV」。

**逐个脚本跑就没事**（同时脏的包少，不触发保存全部）。改到哪一环就从那一环
一个一个往下跑到父材质，每跑完确认 `Get-Process UnrealEditor` 还在。

顺带：三面降一面重写之后，`MF_WS_RainFaces` 单独 `asset_compile` 那条
`Missing ComponentMask input` 也消失了，现在全项目 0 error。
