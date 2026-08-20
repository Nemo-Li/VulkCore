# VulkCore

一个使用 CMake 构建的最小 Vulkan 开发环境示例，包含：

- 应用层：窗口创建、事件循环和生命周期管理
- Vulkan 核心：实例、调试消息、物理设备、逻辑设备、队列和命令池
- 渲染器：交换链、渲染管线、帧同步以及 vertex/index buffer
- 纹理：`stb_image.h` 图片解码、FBX 材质纹理加载、staging 上传和采样器
- 模型：Assimp FBX 网格导入、节点变换、三角化、法线和 UV 提取
- UI：Dear ImGui 1.92.9 GLFW/Vulkan backend，主画面控制面板和调试窗口
- 粒子：Unity 风格基础 Particle System，支持发射、生命周期、速度、重力、形状、颜色/尺寸渐变、旋转、混合和 billboard 渲染
- Shader Demo Lab：对照 `3d-game-shaders-for-beginners` 提供可切换的材质 shader 与全屏后处理演示
- 资源工具：GPU buffer 封装、staging buffer 上传和二进制文件读取
- Shader 管线：GLSL 源码与构建生成的 SPIR-V 分离

## 架构与演进文档

项目的实现边界、图形管线、能力差距和长期架构方案已按主题拆分：

- [设计文档索引](docs/README.md)
- [整体代码框架与现有架构](docs/01-codebase-architecture.md)
- [Vulkan 图形管线与帧流程](docs/02-rendering-pipeline.md)
- [能力差距、问题分析与演进路线](docs/03-gap-analysis-and-roadmap.md)
- [高扩展性引擎架构设计方案](docs/04-scalable-engine-architecture.md)

## 项目结构

```text
include/vulkcore/       对外头文件和模块接口
  Core/                  构建配置和通用配置
  Renderer/              Vulkan 上下文、交换链、buffer、渲染器
  Utils/                 文件读取和 Assimp 模型加载
src/                    C++ 实现
  Application.cpp        应用生命周期和 GLFW 窗口
  Renderer/              Vulkan 渲染实现
  Utils/                 工具实现
assets/textures/         可选外部纹理图片
assets/models/            默认 FBX 模型
docs/                     架构、图形管线、差距分析与演进设计
third_party/stb/         stb_image.h 单头文件库
shaders/src/             GLSL 源文件（支持 vert/tesc/tese/geom/frag/comp）
build/shaders/           CMake 自动生成的 SPIR-V 文件，不纳入源码
```

## 依赖

### macOS（Homebrew）

```bash
brew install cmake vulkan-loader vulkan-headers molten-vk glfw glm shaderc
```

设备需要有可用的 Vulkan 驱动；macOS 通常通过 MoltenVK 映射到 Metal。

### Ubuntu/Debian

```bash
sudo apt install cmake build-essential libvulkan-dev vulkan-tools \
    libglfw3-dev libglm-dev glslc
```

同时需要显卡对应的 Vulkan 驱动。`vulkan-tools` 不是编译必需项，但可以用来运行
`vulkaninfo` 检查驱动。

Assimp 不要求单独安装：CMake 会优先使用系统中的 Assimp package；如果找不到，
会在配置阶段从官方仓库下载并构建固定版本 `v6.0.5`。

Dear ImGui 不要求单独安装：CMake 会从官方仓库下载固定版本 `v1.92.9`，并只编译
核心库、GLFW platform backend 和 Vulkan renderer backend。

## 构建与运行

在工程根目录执行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
./build/vulkcore
```

默认会加载并显示 `assets/models/mill-scene.fbx`。如果要替换模型，在配置时传入新的路径：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
    -DVULKCORE_MODEL_PATH=/absolute/path/to/model.fbx
cmake --build build --parallel
./build/vulkcore
```

`loadModelMesh()` 会将 FBX 中的所有网格合并为一个 32 位索引网格，并应用节点变换、
三角化、平滑法线生成、首个 UV 通道、顶点颜色和材质信息读取；同时保留 FBX 原始节点
层级，供右侧 Model Hierarchy 面板展示。没有 UV 的顶点会使用 `(0, 0)`。每个材质对应
一个绘制分段，材质纹理不可用时使用其 Base Color/Diffuse 颜色。
渲染器会根据模型包围盒自动居中缩放，并启用深度测试和基于法线的基础光照。

CMake 会优先使用 GLFW/GLM 导出的 CMake package；如果系统只提供 pkg-config
元数据，则会自动使用 pkg-config 查找它们。构建时需要 `glslc`，CMake 会将
`shaders/src` 下的 GLSL 编译到 `build/shaders`，运行时通过构建配置读取该目录，
不依赖当前工作目录。

程序启动后会创建一个不使用 OpenGL 上下文的 GLFW 窗口，初始化 Vulkan 实例、交换链、
渲染管线以及 vertex/index buffer。模型材质优先使用 FBX 中的 Base Color/Diffuse
纹理；没有嵌入纹理时，按材质名从 `assets/models/tex/<material>-diffuse.png` 加载。
默认图片不存在或无法加载时使用 FBX 中的材质颜色和顶点颜色；Normal 和 Specular
贴图默认沿用 `<material>-diffuse` 的目录与文件名，将 `diffuse` 替换为 `normal` 或
`specular`。Normal 缺失时绑定平坦法线回退贴图，Specular 缺失时绑定白色回退贴图。
现有 3D 场景会先绘制到离屏颜色/深度目标，再复制到交换链；水面在第二阶段采样离屏场景颜色完成屏幕空间近似的反射、折射与透明混合，随后在同一个 Vulkan
render pass 中绘制水面和 Dear ImGui 主界面；右侧面板显示 FBX 内部节点树。右侧层级中的节点可以选中，3D 视图会为该节点及其子节点整体显示黄色 AABB 立方体边框；节点本身显示其直接 Mesh 数量，但不会再额外展开 `Mesh 0` 子项。
选中带直接 Mesh 的命名节点后，左侧 Selected Node 区域的 Transform 会直接编辑该 Mesh 节点的位置、绕自身模型坐标系 X/Y/Z 轴的旋转和缩放；例如选中生成的 Sun 时，变换只作用于 Sun，不会移动其他模型。`MeshPart` 仅是该 Mesh 的材质绘制分段，不保存独立 Transform。各节点的变换参数会自动保存到项目根目录的 `vulkcore.ini`，下次启动时自动恢复。该区域也可以启用节点旋转动画，分别设置初始角度和角速度；已启用的节点动画会在切换选择后继续运行，嵌套节点按层级坐标系叠加。
同一区域的 Node Material 可以为当前节点及其子节点绑定已有 Material，或创建独立 Material；Material 支持名称、Base Color、Base Color/Normal/Specular 纹理、Metallic、Roughness、Opacity、Opaque/Alpha/Additive/Multiply 混合、深度写入和双面渲染。新建 Material 与 Node 绑定会保存为 `material`、`material_textures` 和 `node_material` 配置。
页面底部的 Resource Manager 是统一的资源导入与检查工作区：输入模型路径即可在运行时替换当前模型，成功导入后会重建网格、材质纹理和 Vulkan descriptor 资源，并持久化当前模型路径。资源列表按 Model、Material、Texture 分类；选择 Model 后可沿原始层级检查节点的导入材质与实际生效材质，选择 Material 或 Texture 后可查看三类纹理槽的加载/回退状态并重新指定资源。`Apply engine-safe defaults` 可恢复当前引擎默认 Shader ABI、Opaque 混合和深度写入要求。
右侧 Scene Objects 面板的 `Create Particle System` 会创建一个绑定 Particle System 组件的 GameObject；只有选中这个对象时，左侧才会显示其粒子调试面板。每个对象独立维护粒子模拟、纹理和参数，在 CPU 上维护生命周期和运动，在 GPU 上以批量 camera-facing billboard 绘制。面板可以调整 GameObject Transform、Enabled、Play on awake、Looping、最大粒子数、发射速率、持续时间、初始生命周期/速度及随机度、重力、Point/Sphere/Cone 发射形状、方向/扩散角、半径、初始/结束尺寸、颜色、旋转速度和 Alpha/Additive 混合。Renderer 区域可以为当前粒子对象加载 PNG/JPG 等 `stb_image` 支持的 RGBA 纹理，纹理颜色及透明度会与粒子的生命周期颜色相乘；未设置或加载失败时使用程序化软圆形回退。`Play`、`Stop`、`Restart` 控制当前组件，纹理路径和参数会按对象 ID 自动保存到 `vulkcore.ini` 的 `particle_system_object` 记录中。粒子位置使用组件的局部归一化场景坐标，默认在 GameObject 原点上方生成一束向上的火花。
Material Shaders 区域可以为当前 Material 分别选择 Vertex、Tessellation Control、Tessellation Evaluation、Geometry、Fragment 和 Compute Shader。未选择的 graphics stage 自动回退到默认 Shader，因此绑定同一 Material 的节点共享 Shader；相同的 Shader/Material 状态会共享 Vulkan graphics pipeline。Compute Shader 会为该 Material 创建并校验独立的 compute pipeline，但当前模型 render pass 不会自动 dispatch 它，因为 compute 阶段需要额外的 storage buffer/image 资源绑定。
Shader 选择来自 `build/shaders` 中的 SPIR-V 文件；新增 GLSL 文件后重新构建即可出现在对应下拉框中。自定义 graphics Shader 需要遵守当前顶点输入、descriptor set `0` 的 UBO/纹理绑定以及 `MaterialPushConstants` 接口。
Material Shaders 还提供 `Apply water shader` 和 `Apply flowing water` 两个预设：前者使用波浪位移、法线扰动和 Fresnel 高光，后者额外按照参考工程的 flow-map 思路移动 UV，并使用程序化 flow field 作为当前单纹理布局下的 fallback。水面 Material 的 Water Flow 配置可以调整 UV 流向（+X/+Y）、流速、流动强度、扰动、波浪尺度和波高；这些参数随 Material 保存并由绑定该 Material 的节点共享。
左侧 `Shader Demo Lab` 是参考项目的统一演示入口。`Screen-space demo` 覆盖 Render-to-Texture、Fog、Box Blur、Median、Kuwahara、Bloom、SSAO、Motion Blur、Chromatic Aberration、Screen-space Reflection/Refraction、Outlining、Depth of Field、Posterization、Pixelization、Sharpen、Dilation、Film Grain、LUT、Gamma Correction 以及 Deferred Depth/Edge buffer；`Demo strength`、`Demo parameter` 和 Split View 可实时对比效果。它们都在现有离屏颜色/深度目标上使用一个标准 fullscreen triangle，不改变水面、粒子和 ImGui 的绘制顺序。
选中任意导入网格节点后，`Surface shader preset` 可演示 Texturing/Blinn-Phong、Unlit、Diffuse、Fresnel、Rim、Cel、Normal Mapping、Normal/Position Buffer、Specular，以及 Water Reflection/Refraction、Flow Mapping 和 Foam。水面 shader 继续走现有的第二阶段材质路径；Foam 通过共享的离屏深度采样生成岸边泡沫。新增 shader 遵守同一 descriptor set `0` 与 `MaterialPushConstants` ABI，CMake 会自动编译并显示在材质 shader 列表中。

如果启动时提示 `Vulkan is not supported` 或 `No Vulkan-capable physical device`，
说明编译依赖已找到，但当前系统没有可用的 Vulkan 驱动或 MoltenVK/Metal 运行环境。
