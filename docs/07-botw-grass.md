# BOTW 风格草地技术方案与实现

## 1. 目标与结论

`scenes/game.scene` 的草地覆盖随玩家移动。近处可见达到角色胸口的独立弯曲草叶，中远景由地表草地材质呈现。草地会随风摆动，玩家经过时草叶沿移动方向倒伏。

草地采用两档 LOD，野花独立绘制。

| 层级 | 表示 | 距离（世界单位） | 作用 |
|---|---|---:|---|
| LOD0 | 独立弯曲三角 Mesh，每叶 4 个弯曲段 | 0–40 | 草毯级近景轮廓、连续弯曲和踩踏细节 |
| LOD1 | 地形网格上的草地纹理层 | 30 起 | 使用与近景草叶相同的底色，随后随距离逐渐褪绿 |
| 野花 | 2 张相交的透明花簇卡片 | 0–27 | 确定性随机散布白、黄、蓝色野花 |

LOD0 在 30–40 单位间逐渐露出同色的地表纹理。Shader 将草叶下沉到地表，并按草根位置的稳定哈希整株溶解。

## 2. 调研依据

- [NVIDIA GPU Gems：Rendering Countless Blades of Waving Grass](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-7-rendering-countless-blades-waving-grass)：建议用相交草片保持各观察方向的密度，以对象根坐标驱动顶点风动画，并在距离边界渐入渐出。
- [NVIDIA GPU Gems 2：Toward Photorealism in Virtual Botany](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-1-toward-photorealism-virtual-botany)：根部权重为 0、顶部权重为 1；多频正弦风只应做克制的顶点偏移，远处还应通过亮度变化表现风。
- [WalkingFat：还原《塞尔达：旷野之息》的草地](https://walkingfat.com/%E8%BF%98%E5%8E%9F%E3%80%8A%E5%A1%9E%E5%B0%94%E8%BE%BE%E3%80%8B%E7%9A%84%E8%8D%89%E5%9C%B0/)：画面对比后拆出“近处单根密集、中处三片一组、远处稀疏单片”的三层结构，并用顶点下沉跨越 LOD。
- [Daniel Ilett：BOTW Stylised Grass](https://danielilett.com/2021-08-24-tut5-17-stylised-grass/)：用草叶高度 UV 做根到梢渐变，并由几何阶段产生独立草叶。VulkCore 在 CPU 上确定草根位置，通过 Vulkan 实例化和顶点着色器展开草叶与草片。
- [《厄灾的默示录》开发者访谈](https://dengekionline.com/articles/84309/)：开发团队明确把草原的“观感和延伸感”视为 BOTW 世界辨识度的核心。由此，本方案优先保证草地整体节奏，而不是追求写实 PBR 草材质。

草片、风场和渐隐参考通用植被渲染技术与公开复现经验。`.grass.extm` 的字段和网格尺寸参考 [ZeldaMods 格式说明](https://zeldamods.org/wiki/Grass.extm)。

## 3. 草图读取与区块更新

将已解包的瓦片放到地形根目录的 `terrain_grass/`。例如，瓦片 `5400000001` 可用以下两种布局：

```text
terrain_grass/5400000001.grass.extm
terrain_grass/5400000000.grass.extm/5400000001.grass.extm
```

每块为 64 × 64 个四字节样本，按行存储 `height, R, G, B`，共 16 KiB。引擎根据 TSCB 瓦片边界定位，覆盖重叠时优先选最细瓦片，并双线性采样草高与 RGB。草高为零的区域保持裸地。草高映射到约 0–1.36 个地形单位，再加上单叶变化。水面高于地形的候选点会被过滤。缺少草图的区域用地形坡度、海拔和区域噪声补草，可散布野花。

`GrassCoverage` 按玩家 XZ 坐标维护各层活动区块。无玩家时采用相机 XZ 坐标。每跨过 4 个地形单位的网格边界，复用交叠区块，只生成新进入的区块。候选点由世界网格和固定种子确定，离开后再次进入时草根保持原位。覆盖范围包含第三人称相机的绕行距离，原地转向时沿用已有草根布局。

实例记录包含根部、草高、法线、宽度、朝向、风相位和颜色。草叶与野花共用索引模板，由顶点着色器展开成四段弯曲草叶或四顶点贴图花片。渲染器使用独立的逐帧实例缓冲，在覆盖变化后上传，并按当帧视锥筛选区块。

野花使用 `assets/textures/botw_wildflowers.png`，以 0.54 阈值执行 Alpha Cutout，继续使用 Opaque depth-write 路径。

## 4. GPU 动画与着色

`shader_grass.vert` 执行两类弯曲：

- 风：两个方向和频率不同的正弦场相加，位移乘以 `heightWeight²`，根部严格固定；
- 踩踏：Uniform 传入 Player 位置和速度，1.25 m 半径内用径向与移动方向混合得到倒伏方向，顶部最大水平偏移约 0.58 m，同时轻微下压。

`shader_grass.frag` 使用和地表草地纹理一致的绿底色及漫反射曲线，以较暗的根部和较亮的尖端表现草叶形状。地图 RGB 在网格草淡出前逐渐融入地表配色。
- 克制的逆光透射和边缘光；
- 每叶稳定的颜色变化，避免规则网格感；
- Opaque + depth write + double sided；贴图层使用 Alpha Cutout，没有逐草片透明排序成本。

## 5. 运行路径

加载地形场景后，`TerrainScene` 按世界坐标提供高度、草图与水面采样。
`GrassCoverage` 更新当前实例范围，`shader_grass` 按实际相机距离过渡 LOD，保留 Player 的踩踏输入。

可直接启动验收场景：

```bash
./build/vulkcore --scene scenes/game.scene --hide-ui
```

不传 `--hide-ui` 时保留编辑器；运行中仍可按 `F1` 切换工具 UI。

## 6. 当前验证范围

地形测试使用合成瓦片检查草高、RGB、裸地和移动后的新覆盖。当前 Game 资源缺少原版草图，直接启动时使用地形补草。
