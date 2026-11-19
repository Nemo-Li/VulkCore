# VulkCore 编辑器改版

桌面设计基准为 1440 × 900。全屏 Vulkan 视口作为底层，工具栏、层级、属性和抽屉都悬浮在画面上。参考 Unity 的 Hierarchy / Inspector、Unreal 的 Content Drawer，以及图形调试器的 Pass / Pipeline 视图，将常用操作放在与当前任务对应的位置。

[打开 FigCraft 源稿](VulkCore-Editor.figcraft)

## 工作区

| 工作区 | 默认布局 | 主要操作 |
| --- | --- | --- |
| Scene | 左侧 Hierarchy，右侧 Inspector，底部抽屉默认收起 | 搜索、选中、创建和隐藏对象。Inspector 随选择显示 Transform、材质、动画、粒子或 Player 状态。 |
| Demos | 底部 Demo 库，左上显示当前示例 | 按 Rendering、Gameplay、Assets 浏览。加载示例、打开控制面板，或进入演示模式。 |
| Shader | 左侧选择目标或效果，右侧查看 Pipeline 或 Pass，底部可展开 Diagnostics | 调整材质预设和阶段绑定，查看状态与报错，或拖动分割线比较屏幕后处理。 |

| 现有入口 | 新位置 |
| --- | --- |
| 新建、打开、保存场景，导出 HarmonyOS | 顶栏场景选择器和 Project 菜单 |
| Create Particle System、Add Sun、移除对象 | Hierarchy 新建菜单与对象菜单 |
| 相机、环境灯光、网格和视口方向 | 视口上方工具条 |
| Resource Manager | 底部 Assets 抽屉，资源详情复用右侧 Inspector。选中场景对象后恢复对象属性。 |
| 帧率、帧时间、截图反馈、地形提交与剔除 | 状态栏和 Stats 抽屉。F12 保留为截图入口。 |
| Player 调试与小地图 | Gameplay 的演示 HUD，F2 切换 Player 调试。 |

## 操作流程

- **场景编辑。** 左侧选中对象，右侧编辑属性。通过 `Inspect in Shader` 带着当前节点的材质进入 Shader。面板可拖动、贴边、缩放、锁定位置和折叠。窄窗口优先收起底部抽屉，右侧 Inspector 改为按需呼出。仅悬浮面板和正在拖动的视口控件接管鼠标，面板之间的区域直接用于视口操作。
- **Demo 演示。** `Load demo` 加载示例，切换前处理当前场景的未保存编辑。`Present demo` 收起编辑面板，保留当前 Demo 的 HUD。Solid to Wireframe 提供进度、播放、反向、循环和 Flow。Terrain & Player 使用控制提示和小地图。SBFRES Tree 使用环绕预览，并沿用当前机器上的模型资源路径。退出演示恢复原面板布局。F1 切换编辑工具显隐。
- **Shader 材质。** 显示当前对象、有效材质和共享范围。按需选择 Surface 预设，或检查 Vertex、Tessellation、Geometry、Fragment、Compute 阶段。区分引擎默认与自定义 SPIR-V。阶段选择沿用现有 CMake 构建产物，Compute 作为单独的管线配置入口。
- **Shader 屏幕后处理。** 显示效果名和当前效果实际使用的参数。A/B 左侧为原图，右侧为效果，拖动分割线可改变比例。HDR 场景显示当前合成路径，并给出 `Open A/B sample scene` 入口。采用单独的 LDR 场景做对比，保留原 HDR 场景的构图和参数。
- **调试反馈。** Diagnostics 关联材质、Shader 阶段或 Pass。管线构建失败时显示具体目标和错误，并标明视口当前使用的有效管线。

## 画板

| 画板 | 预览 |
| --- | --- |
| 01 Scene 对象编辑 | [01-scene.png](01-scene.png) |
| 02 Demo 库 | [02-demo-library.png](02-demo-library.png) |
| 03 演示播放 | [03-presentation.png](03-presentation.png) |
| 04 Shader 材质管线 | [04-shader-material.png](04-shader-material.png) |
| 05 Shader 屏幕 A/B | [05-shader-ab.png](05-shader-ab.png) |
| 06 Shader HDR 兼容提示 | [06-shader-hdr.png](06-shader-hdr.png) |
| 07 Assets 抽屉 | [07-assets.png](07-assets.png) |

FigCraft 原型中可从 Scene 打开 Assets，从 Demos 进入和退出演示，从 Inspector 进入材质管线，并从 HDR 兼容提示切到 A/B 样例。车辆与树模型等视口素材采用 VulkCore 渲染截图。A/B 右侧基于实渲染帧制作，用于示意像素化效果和分割线交互。诊断与帧率使用示例数据。
