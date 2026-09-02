# 雨玻璃软边（按水珠屏幕尺度）

固定 `S(R, 0, d)` 会让整颗圆盘都是过渡。大珠看起来正常；极小珠在屏幕上只有两三个像素时，这条边相对珠身会偏大。

## 公式

`MF_WS_SoftDisc(D, Radius)`：

```
w = length(float2(DDX(D), DDY(D)))
k = sat((w / max(R, 1e-6)) * 1.2 - 0.25)
inner = R * k * 0.28
return S(R, inner, D)
```

- 大中珠：`w/R < 0.2` → `k=0` → `inner=0` → Heartfelt `S(R,0,d)`，整盘软肩
- 极小珠：`k→1` → `inner=0.28R` → 仍留约 72% 软肩，禁止收到只剩外圈 25%

「软边」参数仍是外半径 R，默认 0.4。列珠半径 `R*0.75`（默认 0.3）。水雾半径 0.3。

导数只对标量距离 D。禁止 `DDX/DDY(WorldPosition)`。
