# BOTW 风格草地技术方案与实现

## 1. 目标与结论

本实现服务于 `scenes/game.scene` 的 BOTW 地形，目标不是导入一张透明草贴图，而是复现原作草原最重要的视觉语言：近处能读出独立弯曲草叶，中景仍保持成片体积，远景逐渐并入地表颜色；整片草随风形成大尺度明暗波动，Player 穿过时局部草叶沿移动方向倒伏。

最终采用三层几何表示：

| 层级 | 表示 | 距离（世界单位） | 作用 |
|---|---|---:|---|
| LOD0 | 独立弯曲三角 Mesh，每叶 4 个弯曲段 | 0–22 | 草毯级近景轮廓、连续弯曲和踩踏细节 |
| LOD1 | 每簇 3 张相交的 Alpha Cutout 草簇卡片 | 15–34 | 从任意方向维持浓密草丛体积 |
| LOD2 | 稀疏单片 Alpha Cutout 草簇卡片 | 29–40 | 远景色块和轮廓，随后并入 terrain grass layer |
| 野花 | 2 张相交的透明花簇卡片 | 0–27 | 确定性随机散布白、黄、蓝色野花 |

LOD 区间有重叠。Shader 在交界处同时将旧表示下沉到地表并按草叶稳定哈希整叶溶解，避免硬切、透明排序和屏幕门噪点。

## 2. 调研依据

- [NVIDIA GPU Gems：Rendering Countless Blades of Waving Grass](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-7-rendering-countless-blades-waving-grass)：建议用相交草片保持各观察方向的密度，以对象根坐标驱动顶点风动画，并在距离边界渐入渐出。
- [NVIDIA GPU Gems 2：Toward Photorealism in Virtual Botany](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-1-toward-photorealism-virtual-botany)：根部权重为 0、顶部权重为 1；多频正弦风只应做克制的顶点偏移，远处还应通过亮度变化表现风。
- [WalkingFat：还原《塞尔达：旷野之息》的草地](https://walkingfat.com/%E8%BF%98%E5%8E%9F%E3%80%8A%E5%A1%9E%E5%B0%94%E8%BE%BE%E3%80%8B%E7%9A%84%E8%8D%89%E5%9C%B0/)：画面对比后拆出“近处单根密集、中处三片一组、远处稀疏单片”的三层结构，并用顶点下沉跨越 LOD。
- [Daniel Ilett：BOTW Stylised Grass](https://danielilett.com/2021-08-24-tut5-17-stylised-grass/)：用草叶高度 UV 做根到梢渐变，并由几何阶段产生独立草叶。VulkCore 没有照搬 Unity tessellation/geometry 路线，而是在 CPU 生成确定性 Mesh，以兼容 MoltenVK 和现有共享 Vertex ABI。
- [《厄灾的默示录》开发者访谈](https://dengekionline.com/articles/84309/)：开发团队明确把草原的“观感和延伸感”视为 BOTW 世界辨识度的核心。由此，本方案优先保证草地整体节奏，而不是追求写实 PBR 草材质。

这些资料只能证明通用方法和公开复现经验，不代表 Nintendo 原始 Shader 或资源格式；本实现因此称为 BOTW-inspired，而不宣称还原原作内部代码。

## 3. CPU 种植与 Mesh

入口为 `buildBotwGrassPatch()`：

1. 以 Player 出生点为中心，在 40 m 半径内建立 0.52 m 抖动网格。
2. 对每个候选点采样当前渲染地形高度，并用四个邻域高度估算法线。
3. 依据坡度、相对高度和低频区域噪声决定密度；陡坡和雪线以上不种草。
4. 哈希由网格坐标和固定 seed 生成，因此每次加载结果一致，没有帧间闪烁。
5. 中央 22 m 为每个根点按黄金角圆盘补 17–23 根错位草叶，并提高近玩家区域的有效种植覆盖率；可见近景达到约 75–90 根 Mesh 草叶/㎡，基本形成连续草毯。中远层按哈希概率随机降采样，避免规则网格。
6. 约 1.4% 的合格种植点生成两张相交的花簇卡片，使用相同 seed 保证重载后位置不跳变。

每个顶点复用引擎 `Vertex`：

- `texCoord.y`：根到梢的弯曲权重；
- `color`：色相和明度变化；
- `terrainMapping.xy`：草根 XZ 世界坐标；
- `terrainMapping.z`：风相位；
- `terrainMapping.w`：LOD 编号。

三层草和花簇索引连续存入共享 scene buffer，但保留四个 `MeshPart` 和四个场景子节点，便于 Inspector 独立隐藏与分析。

中远草与野花使用 `assets/textures/botw_grass_clump.png` 和
`assets/textures/botw_wildflowers.png`。两张纹理由内置 ImageGen 生成，保留真实 Alpha；Shader
以 0.54 阈值执行 Alpha Cutout，从而清理半透明边缘并继续使用 Opaque depth-write 路径。

## 4. GPU 动画与着色

`shader_grass.vert` 执行两类弯曲：

- 风：两个方向和频率不同的正弦场相加，位移乘以 `heightWeight²`，根部严格固定；
- 踩踏：Uniform 传入 Player 位置和速度，1.25 m 半径内用径向与移动方向混合得到倒伏方向，顶部最大水平偏移约 0.58 m，同时轻微下压。

`shader_grass.frag` 使用深绿根部、偏黄绿尖端的双调色板，并加入：

- 三档离散漫反射，保持 BOTW 的大色块可读性；
- 克制的逆光透射和边缘光；
- 每叶稳定的颜色变化，避免规则网格感；
- Opaque + depth write + double sided；贴图层使用 Alpha Cutout，没有逐草片透明排序成本。

## 5. 运行路径

加载含 Terrain 和 Player 的场景时：

```text
TSCB/.hght -> TerrainScene -> reset Player -> build grass patch
                                      |              |
                                      v              v
                              player UBO data   3 LOD MeshParts
                                      \              /
                                       shader_grass
```

可直接启动验收场景：

```bash
./build/vulkcore --scene scenes/game.scene --hide-ui
```

不传 `--hide-ui` 时保留编辑器；运行中仍可按 `F1` 切换工具 UI。

## 6. 当前边界与下一阶段

- 当前草 patch 固定在 Player 出生点周围，覆盖 40 m 半径，满足 `game.scene` 演示和交互验证。开放世界长距离移动应升级为相机跟随的环形 patch/clipmap，并在跨 cell 时增量重种植。
- 现阶段 LOD 在 Shader 中按每叶距离过渡，三个 MeshPart 都会提交。下一阶段可按 8–16 m vegetation chunk 做 CPU frustum/distance culling 或 indirect draw，减少远处顶点吞吐。
- 密度来自 terrain 高度/坡度的近似 biome 规则；若后续恢复 BOTW vegetation density map，应替换这一步而不改草叶 Mesh/Shader ABI。
- 踩踏是即时 Player 场，离开后草叶恢复。需要持久脚印时可增加低分辨率 interaction render target，并让倒伏量随时间衰减。
