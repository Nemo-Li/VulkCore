# BOTW 风格草地技术方案与实现

## 1. 目标与结论

`scenes/game.scene` 以相机为中心更新活动草地区块。草叶随风摆动，玩家经过时沿移动方向倒伏。两级草地与野花分别绘制。

| 层级 | 表示 | 距离（地形单位） |
|---|---|---:|
| LOD0 | 四段索引网格草叶，沿叶片 UV 采样 `GrassAlb` | 0–40 |
| LOD1 | 每株两张相交的 `GrassCrossAlb` 卡片，使用两层贴图和 Alpha Cutout | 30–112 |
| 彩色野花、黄色小花 | 各自独立的交叉卡片 | 0–27 |

两级按相机到草根的三维距离切换。LOD0 在 30–40 单位内淡出，LOD1 在同一区间淡入，并在 83–112 单位间淡出。根位置的固定哈希决定整株显示，两张卡片同步切换。更远处继续绘制草地下方的地表材质。

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

每块为 64 × 64 个四字节样本，按行存储 `height, R, G, B`。引擎按 TSCB 瓦片定位，重叠时优先选最细瓦片。在草根位置做双线性采样，颜色按草高加权，避免裸地像素给邻近草叶染色。只有存在 `.grass.extm` 且草高大于零的位置才生成植被。水面以下也剔除。

两个 LOD 都用瓦片 RGB 作为草叶底色，按线性归一化值参与光照。草叶和草片纹理提供亮度细节，草片保留原有透明轮廓。近景草根按各自位置贴合地形，极矮草叶缩窄并减小踩踏偏移。瓦片颜色随实例传入着色器，卡片共用同一草根和颜色。

两级草地共享按世界 XZ 划分的 12 × 12 遮罩区块。每块用坐标确定三个随机角度，分别旋转并双线性采样 `GrassCoverAlb` 的线性 R 通道。任意一层达到 191/255 即通过遮罩，重叠处只通过一次。LOD0 在 8 × 8 的生成区块内采样 20 × 20 个草丛位置，对每片草叶的根坐标检查遮罩。LOD1 在 16 × 16 的生成区块内采样 24 × 24 个草丛位置，对交叉卡片共用的根坐标检查遮罩。

`GrassCoverage` 以 4 个地形单位为相机锚点网格，每次跨格复用交叠区块。世界坐标和固定种子决定草根，往返或原地转向时位置保持稳定。实例数据包含根位置、草高、法线、宽度和动画参数。渲染器仅在覆盖变化时更新逐帧缓冲，绘制时按视锥筛选区块。

彩色野花继续使用 `assets/textures/botw_wildflowers.png` 和 0.54 的 Alpha Cutout 阈值。小花由 `shader_grass.frag` 绘制花瓣、花茎和小叶，花头宽度约 0.09 m。两层都只在草高大于零的位置散布，尺寸随草冠高度变化。在场景树的 **World Grass** 下可以分别控制它们的可见性。

## 4. GPU 动画与着色

`shader_grass.vert` 执行两类弯曲：

- 风将整丛的缓慢摆动与单叶的小幅颤动叠加，位移乘以 `heightWeight²`，根部固定。单叶带有方向不同的静态弯曲，产生适度交叉。
- 踩踏在角色周围 1.25 m 内混合径向与移动方向，草梢最大水平偏移约 0.30 m，同时轻微下压。

`shader_grass.frag` 从草叶或草片贴图读取底色，以线性 `GrassSpm` 调制高光。两级都采用双面绘制和深度写入，卡片用 Alpha Cutout 保留草叶轮廓。

地表由独立的 `.mate` 材质渲染路径处理，参见[地形材质说明](06-botw-terrain-textures.md)。

## 5. 运行路径

把 `Terrain.sbfres` 和 `Terrain.Tex.sbfres` 放在地形根目录的 `source/` 下。场景加载时只解码渲染草地所需的纹理层。地形和草叶使用 VulkCore 自己的网格。缺少档案时仍可加载地形，草叶与草片层需要档案中的贴图。

可直接启动验收场景：

```bash
./build/vulkcore --scene scenes/game.scene --hide-ui
```

不传 `--hide-ui` 时保留编辑器。窗口模式下可按 `F3` 切换工具 UI，按 `F1` 切换全屏。

## 6. 当前验证范围

地形测试使用合成瓦片检查两个 LOD 的草高和 RGB、黑色和最小正高度、裸地和缺失瓦片，以及三层随机旋转遮罩和草根位置的稳定性。
