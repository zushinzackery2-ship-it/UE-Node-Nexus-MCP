# 雨玻璃 Soft / Realistic

两档折射模型不同，必须两张父材质。禁止用 Soft 的 PNO 图改针冒充 Realistic。

## Soft（已到位，勿改）

父：`M_WS_RainGlass`。源：`D:\Projects\shrei-blog`。

- Unlit + `RM_PixelNormalOffset`
- 折射针 = 折射强度 0.3
- 像素法线 = `normalize(Nws - WorldOffset * 16)`（屏幕空间透镜）
- 锯齿 0.4；**软边** 默认 0.4（外半径；边宽随水珠屏幕尺度收，见 `RainGlass-SoftEdge.md`）；粘滞 / 湿痕折射 针存在但为 0

## Realistic（独立父材质）

父：`M_WS_RainGlass_Realistic`。折射模型与 Soft 不同，所以单独一张父材质。

- Unlit + `RM_IndexOfRefraction`（与 Soft 的 PNO 不是同一套折射；不用 DefaultLit，缩略图会打崩 D3D12）
- 干玻璃 IOR = 1（立方体不是玻璃砖）
- 滴心 IOR → 1.33；像素法线 = `normalize(Nws + WorldOffset * 法线强度)`，默认法线强度 3，**没有** Soft 的 `*16` PNO
- 不暴露粘滞、湿痕折射、折射强度；RainFaces 那两针接常数 0
- Opacity = 玻璃不透明度 + rim + spec，不进 DropMask/Trail/Mist
