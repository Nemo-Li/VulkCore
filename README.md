# VulkCore

一个使用 CMake 构建的最小 Vulkan 开发环境示例，包含：

- 应用层：窗口创建、事件循环和生命周期管理
- Vulkan 核心：实例、调试消息、物理设备、逻辑设备、队列和命令池
- 渲染器：交换链、渲染管线、帧同步以及 vertex/index buffer
- 纹理：`stb_image.h` 图片解码、FBX 材质纹理加载、staging 上传和采样器
- 模型：Assimp FBX 网格导入、节点变换、三角化、法线和 UV 提取，以及四骨骼 CPU 蒙皮动画播放
- UI：Dear ImGui 1.92.9 GLFW/Vulkan backend，主画面控制面板和调试窗口
- 粒子：Unity 风格基础 Particle System，支持发射、生命周期、速度、重力、形状、颜色/尺寸渐变、旋转、混合和 billboard 渲染
- 物理：NVIDIA PhysX 5.6.1、60 Hz 固定步长、地形 BVH/AABB 碰撞、Player 动态胶囊、重力、跳跃和 CCD
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
- [敌人行为编辑与巡逻](docs/10-enemy-behavior.md)

## 项目结构

```text
include/vulkcore/       对外头文件和模块接口
  Core/                  构建配置和通用配置
  Physics/               PhysX 后端隔离与物理世界接口
  Renderer/              Vulkan 上下文、交换链、buffer、渲染器
  Utils/                 文件读取和 Assimp 模型加载
src/                    C++ 实现
  Application.cpp        应用生命周期和 GLFW 窗口
  Renderer/              Vulkan 渲染实现
  Utils/                 工具实现
assets/icons/            应用图标的 SVG 源文件、PNG 预览和多尺寸 Windows ICO
assets/textures/         可选外部纹理图片
assets/models/            模型资源
docs/                     架构、图形管线、差距分析与演进设计
third_party/stb/         stb_image.h 单头文件库
shaders/src/             GLSL 源文件（支持 vert/tesc/tese/geom/frag/comp）
build/shaders/           CMake 自动生成的 SPIR-V 文件，不纳入源码
```

## 依赖

### Windows（Visual Studio 2022 / x64）

需要 Visual Studio 2022 的「使用 C++ 的桌面开发」工作负载（包括 Windows SDK 和
CMake 工具）、Git，以及 [Vulkan SDK](https://vulkan.lunarg.com/sdk/home)。显卡驱动
需要支持 Vulkan；本机 NVIDIA GeForce RTX 4060 Ti 已通过 `vulkaninfo --summary`
检查。

项目提供 Windows 专用预设和 PowerShell 入口，不需要手动配置 MSVC 的环境变量。
GLFW、GLM、Assimp、Dear ImGui 和 PhysX 会优先使用已有依赖，缺少时由 CMake
下载固定版本；首次配置需要网络，后续构建复用缓存。默认构建静态依赖。

在项目根目录的 PowerShell 中执行：

```powershell
# 构建 Release 并执行物理、粒子测试
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/windows.ps1 -Test

# 构建并启动编辑器（默认空场景）
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/windows.ps1 -Run

# 启动游戏场景
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/windows.ps1 -Run -Scene scenes/game.scene

# Debug 构建及测试
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/windows.ps1 -Configuration Debug -Test
```

脚本自动寻找 Visual Studio 2022 附带的 CMake，以及 `VULKAN_SDK`、
`C:/VulkanSDK`、`D:/VulkanSDK` 或项目内 `build/_tools/VulkanSDK` 中的 SDK。
本机 SDK 位于 `build/_tools/VulkanSDK`。也可以在配置了 SDK 的终端手动执行：

```powershell
cmake --preset windows
cmake --build --preset windows-release --parallel 8
ctest --preset windows-release
.\build\windows\Release\vulkcore.exe --scene scenes/game.scene
```

Windows 构建产物在 `build/windows/Release` 或 `build/windows/Debug`，与旧平台的
`build` 缓存分开。可在 Visual Studio 中直接打开项目目录并选择 `windows` 预设。
Debug 版本在验证层可用时启用验证；未安装验证层时输出提示并继续运行。

Windows 构建会把 `assets/icons/vulkcore.ico` 嵌入可执行文件，GLFW 创建窗口时会自动
使用该图标，供任务栏、窗口标题栏、Alt+Tab 和资源管理器显示。ICO 包含 16–256 像素的
九种尺寸，透明背景；可编辑源文件为 `assets/icons/vulkcore.svg`，预览为
[`assets/icons/vulkcore.png`](assets/icons/vulkcore.png)。

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
核心库、GLFW platform backend 和 Vulkan renderer backend。ImGui 使用随项目附带的
思源黑体显示 UTF-8 中文字符，字体和 SIL Open Font License 位于 `assets/fonts/`。

PhysX 不要求单独安装：CMake 会下载固定提交的 PhysX `5.6.1` 源码并构建 CPU 静态库。
由于 NVIDIA 官方 PhysX 5.x 目前只提供 Windows/Linux 构建预设，项目使用 O3DE 维护的
跨平台 PhysX 分支补充 macOS/Apple Silicon 构建文件；PhysX 源码及许可证仍为
BSD-3-Clause。首次构建会比后续增量构建耗时更长。

## 构建与运行

在工程根目录执行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
./build/vulkcore
```

也可以直接加载指定场景。`--hide-ui` 隐藏编辑面板，游戏小地图可按 `M` 切换显示：

```bash
./build/vulkcore --scene scenes/game.scene --hide-ui
```

地形块默认按当前视锥筛选后提交绘制。已上传的地形缓冲保持常驻，转动相机时按当帧视角更新绘制列表。
工具面板中的 `Terrain frustum culling` 可切换此功能并查看提交的地形块数。使用 `--capture-frame`
截图对比时，可加 `--no-terrain-culling` 关闭剔除。

程序启动时默认显示空场景。场景中的模型由场景文件加载，底部 Resource Manager 提供独立资源预览。

`loadModelMesh()` 会将 FBX 中的所有网格合并为一个 32 位索引网格，并应用节点变换、
三角化、平滑法线生成、首个 UV 通道、顶点颜色和材质信息读取；同时保留 FBX 原始节点
层级，供右侧 Model Hierarchy 面板展示。没有 UV 的顶点会使用 `(0, 0)`。每个材质对应
一个绘制分段，材质纹理不可用时使用其 Base Color/Diffuse 颜色。
模型中的骨骼绑定、骨骼层级、动画 Clip、节点通道和 Position/Rotation/Scale 关键帧会
一并解析并缓存。带动画的导入模型默认播放第一个 Clip，选中场景中的模型节点后可切换 Clip、播放/暂停、循环、速度和时间。
渲染器会根据模型包围盒自动居中缩放，并启用深度测试和基于法线的基础光照。

CMake 会优先使用 GLFW/GLM 导出的 CMake package，其次使用 pkg-config，
两者都不可用时自动下载 GLFW `3.4` 和 GLM `1.0.3`。构建时需要 Vulkan SDK 或
系统提供的 `glslc`，CMake 会将
`shaders/src` 下的 GLSL 编译到 `build/shaders`，运行时通过构建配置读取该目录，
不依赖当前工作目录。

### SBFRES 模型查看器

```bash
cmake -S . -B build
cmake --build build --target sbfres_viewer --parallel
./build/sbfres_viewer
```

把 `.sbfres` 或 `.bfres` 模型拖入窗口即可渲染。同目录同名的 `.Tex` 贴图包会自动加载，也可以把模型和贴图包一起拖入。按住鼠标左键拖动可环绕模型，滚轮可缩放，按 **F** 恢复构图。多模型文件可以在左上角切换子模型。也可以用 `./build/sbfres_viewer path/to/model.sbfres` 启动时打开模型。格式支持范围见 [SBFRES 导入文档](docs/09-sbfres-import.md)。

### 导出鸿蒙手机工程

在 Resource Manager 中填写新的工程输出目录，点击 **Export HarmonyOS**，即可保存当前编辑状态并导出独立的 DevEco Studio 工程。也可以导出已保存的场景：

```bash
./build/vulkcore --scene scenes/game.scene \
  --export-ohos exports/HarmonyOSGame \
  --app-name "VulkCore Game" --bundle-name com.vulkcore.game
```

导出工程包含场景、模型、纹理、地形、SPIR-V 和 C++ 运行时源码，面向最低 API 22、目标 API 22 的 arm64 鸿蒙手机。使用 DevEco Studio 6.0.2 或更新版本打开后，同步依赖、确认模板中的签名配置可用后即可运行。手机通过 XComponent/Vulkan 加载导出的场景，提供圆盘摇杆移动、拖动转向、跳跃和 Q/E/R 技能触屏控制。启动时进入全屏横屏，场景以默认编辑器的 16:9 画面为基准等比放大并居中裁切，铺满不同屏幕比例，保持物体比例。

输出目录必须尚不存在。导出工程自带 `arm64-v8a` 的 PhysX 预编译静态库，Native 构建直接链接。首次配置仍可能需要下载 Assimp、GLM 和 Dear ImGui。资源迁移规则、工程结构、验证命令和手机运行边界见 [鸿蒙手机工程导出](docs/08-harmonyos-export.md)。

### 运行时功能

按 `F1` 可将工具栏和所有编辑面板全部隐藏，再按一次恢复显示。

桌面版按 `F5` 切换场景编辑状态。在左侧场景树选中物体后，视口会显示加粗的黄色选中框。`W` / `S` 沿当前镜头朝向在地面平面前后移动，`A` / `D` 向左右移动。鼠标在场景视口内滚动可等比缩放选中物体。
选中 Player 时，移动会同时更新物理胶囊和出生点。地形和程序化草地由场景管理。编辑完成后点击工具栏的 **Save** 保存，再按 `F5` 恢复角色键盘控制。

#### 初始化与渲染流程

程序启动后会创建一个不使用 OpenGL 上下文的 GLFW 窗口，并初始化 Vulkan 实例、交换链、
渲染管线以及 vertex/index buffer。

现有 3D 场景的渲染顺序如下：

1. 场景先绘制到离屏颜色/深度目标。
2. 水面在第二阶段采样离屏场景颜色，完成屏幕空间近似的反射、折射与透明混合。
3. 水面和 Dear ImGui 主界面在同一个 Vulkan render pass 中绘制。
4. 最终结果复制到交换链。

#### 模型与材质加载

`loadModelMesh()` 会将 FBX 中的所有网格合并为一个 32 位索引网格，并处理：

- 节点变换、三角化和平滑法线生成
- 首个 UV 通道、顶点颜色和材质信息读取
- FBX 原始节点层级保留，供右侧 Model Hierarchy 面板展示
- 骨骼、绑定影响、动画 Clip/Channel 和 Position/Rotation/Scale 关键帧读取
- 每个顶点最多保留 4 个骨骼影响；每帧只对动画模型自身的顶点区间执行 CPU 蒙皮和局部缓冲上传，不复制地形等静态场景数据
- 模型实例之间使用独立的骨骼/Clip 索引范围，重复导入同一个动画模型也会分别播放

没有 UV 的顶点会使用 `(0, 0)`。每个材质对应一个绘制分段，材质纹理不可用时使用其
Base Color/Diffuse 颜色。渲染器会根据模型包围盒自动居中缩放，并启用深度测试和基于
法线的基础光照。

模型纹理的加载规则如下：

- 优先使用 FBX 中的 Base Color/Diffuse 纹理。
- 没有嵌入纹理时，按材质名从 `assets/models/tex/<material>-diffuse.png` 加载。
- 默认图片不存在或无法加载时，使用 FBX 中的材质颜色和顶点颜色。
- Normal 和 Specular 贴图沿用 `<material>-diffuse` 的目录与文件名，将 `diffuse` 替换为
  `normal` 或 `specular`。
- Normal 缺失时绑定平坦法线回退贴图；Specular 缺失时绑定白色回退贴图。

#### Model Hierarchy 与节点编辑

右侧面板显示 FBX 内部节点树。节点可以选中，3D 视图会为该节点及其子节点整体显示黄色
AABB 立方体边框；节点本身显示其直接 Mesh 数量，但不会再额外展开 `Mesh 0` 子项。

选中 `rootnode` 或带直接 Mesh 的命名节点后，可以在左侧 Selected Node 区域编辑：

- Transform：节点的位置、绕自身模型坐标系 X/Y/Z 轴的旋转和缩放
- `Render Enabled`：隐藏当前节点及其子节点的全部场景渲染
- 节点旋转动画：设置初始角度和角速度

`rootnode` 的变换作用于整个导入模型；工具栏中的 `Add Sun` 会创建一个独立的 Sun 场景对象，
其变换只作用于 Sun，不会移动其他模型。`MeshPart` 仅是该 Mesh 的材质绘制分段，不保存独立
Transform。

节点的变换参数、渲染开关和动画状态会保存到当前 `.scene` 文件。已启用的节点动画会在
切换选择后继续运行，嵌套节点按层级坐标系叠加。

#### Node Material 与 Resource Manager

Selected Node 区域的 Node Material 可以为当前节点及其子节点绑定已有 Material，或创建
独立 Material。Material 支持：

- 名称、Base Color，以及 Base Color/Normal/Specular 纹理
- Metallic、Roughness、Opacity
- Opaque/Alpha/Additive/Multiply 混合
- 深度写入和双面渲染

新建 Material 与 Node 绑定会保存为 `material`、`material_textures` 和 `node_material`
配置。

页面底部的 Resource Manager 会在桌面可执行文件旁创建 `resource/`，通常是 `build/resource/`。

- 点击 **New Folder** 在当前目录新建文件夹，双击文件夹进入，点击 **Up** 返回上级。
- 将系统文件拖到左侧列表或某个文件夹行，会复制到对应目录。在面板内将文件拖到文件夹行或 **Up**，会移动该文件。同名文件会保留并显示提示。
- 将模型从 Resource Manager 拖入场景视口或左侧场景树，模型会默认开启 **Use Gravity**。在有地形的场景中，模型会自由落到地面。选中模型根节点可在 Inspector 中关闭重力，设置随场景保存。
- 单击 `.sbfres` 或 `.bfres` 模型，在右侧预览。可以切换模型、左键拖动环绕、滚轮缩放。将同名 `.Tex` 贴图包和 `_Animation` 骨骼动画包放在模型同一目录，即可自动加载。
- 单击独立骨骼动画包，可使用同目录的配套模型预览，也可从下拉框选择其他模型。动画支持切换片段、播放、暂停、循环、调速和时间轴定位。

#### Particle System

右侧 Scene Objects 面板的 `Create Particle System` 会创建一个绑定 Particle System
组件的 GameObject。只有选中这个对象时，左侧才会显示粒子调试面板。

每个对象独立维护粒子模拟、纹理和参数：生命周期与运动在 CPU 上维护，粒子在 GPU 上以
批量 camera-facing billboard 绘制。面板支持调整：

- 基础状态：GameObject Transform、Enabled、Play on awake、Looping、最大粒子数、发射速率和持续时间
- 初始状态：初始生命周期/速度及随机度、重力、初始/结束尺寸、颜色和旋转速度
- 发射形状：Point、Sphere、Cone，以及方向、扩散角和半径
- 混合与纹理：Alpha/Additive 混合，以及 PNG/JPG 等 `stb_image` 支持的 RGBA 纹理

纹理颜色及透明度会与粒子的生命周期颜色相乘；未设置或加载失败时使用程序化软圆形回退。
`Play`、`Stop`、`Restart` 控制当前组件，纹理路径和参数会按对象 ID 自动保存到
`vulkcore.ini` 的 `particle_system_object` 记录中。粒子位置使用组件的局部归一化场景坐标，
默认在 GameObject 原点上方生成一束向上的火花。

#### 特效编辑器

macOS 编辑器顶部的 `Effects` 标签支持粒子、网格、拖尾、灯光和后处理，可通过时间轴、曲线和自定义 GLSL 制作特效。可导出供 Three.js 播放的效果包，或导出交给 VulkCore 的 Vulkan 渲染器实时播放的 `.vfx3d` 资源。详见 [特效编辑器](docs/vfx_editor.md)。

#### 地形道路笔刷

打开包含地形的场景后按 `F5` 进入编辑模式，默认启用 `Road Brush`。在右侧面板选择黄土路（`Ochre dirt`）、碎石、石板或橡皮擦，并设置以米为单位的笔刷半径。黄土路的半径沿笔画连续变化，幅度在设定半径的 ±22% 内。`View` 可切换透视和正交俯视，两种视角共用同一份道路数据。

在视口内按住左键拖动绘制，按住右键拖动环绕相机，用 `WASD` 平移，滚轮缩放。选择黄土路时，`Ochre dirt mix` 可实时调整场景内所有黄土路的路心占比（默认 80%），路肩保留更多原地表。点击 `Save` 将道路及占比写入当前 `.scene` 文件。需要移动物体时，从 `Edit Tool` 切换到 `Select / Transform`。

#### PhysX 地形与 Player 物理

加载包含 BOTW Terrain 和 Player 的游戏场景时，每个渲染地形 Chunk 会同时烘焙为 PhysX
静态三角网格。每 Chunk 的 AABB 与网格内部 BVH 用于分级碰撞粗筛，Player 使用 1.58 m 高的
动态胶囊碰撞体。场景可通过 `player_model_resource "path/to/model.glb"` 指定 Player 外观；
FBX/GLB 等导入模型会自动按碰撞体高度缩放、脚底对齐，并让完整节点层级与骨骼动画跟随
Player 移动。未设置该字段时继续显示程序化胶囊，也可以在 Assets 的模型 Inspector 中用
`Set as Player Model` / `Use Player Capsule` 切换并保存到当前场景。
物理场景以 60 Hz 固定步长运行，开启连续碰撞检测（CCD），当前实现包含：

- PhysX 重力、地形接触、脚底高摩擦材质和穿透修正
- `WASD` / 方向键触发 Player 的 `Run1` 循环并平滑转向；移动速度从动画触地脚骨反推，
  每个固定物理步通过落脚相位控制的摩擦冲量推进，松键后由静摩擦减速
- Player 默认保持 bind pose、不自动播放导入动画，`Space` 仍执行 grounded 跳跃
- `Q` 触发高速雷霆剑气、`E` 触发环形爆炎新星、`R` 触发龙卷风；三者分别匹配独立攻击动画
- 游戏视口右下角默认显示小地图，按 `M` 显示或隐藏。地图按地形边界和高度绘制，点击有效地形可将 Player 与 PhysX 胶囊同步传送到该位置。
- 动态刚体位置/速度同步到 Player 渲染节点和相机目标
- Player Inspector 中的落地状态与实时速度信息

当前地形碰撞使用渲染网格精度；物理材质、碰撞层、触发器、接触事件和通用刚体组件仍属于后续扩展。

#### BOTW 风格草地、LOD 与踩踏

地形草地按玩家位置增量更新区块，无玩家时随相机移动。近景用独立弯曲草叶，中景用三张相交的草片，
远景用稀疏单片，最远覆盖约 180 个地形单位。LOD 随相机距离平滑过渡，草叶保留风吹和踩踏。

将解包后的 `.grass.extm` 放在地形根目录的 `terrain_grass/` 下，可按原地形瓦片名
读取草高与 RGB。草高为零的区域保持裸地。缺少瓦片时，坡度和海拔规则继续生成草和少量野花。
目录布局与实现细节见 [BOTW 风格草地技术方案](docs/07-botw-grass.md)。

#### Material Shaders 与水面预设

Material Shaders 区域可以为当前 Material 分别选择 Vertex、Tessellation Control、
Tessellation Evaluation、Geometry、Fragment 和 Compute Shader。

- 未选择的 graphics stage 自动回退到默认 Shader，因此绑定同一 Material 的节点共享 Shader。
- 相同的 Shader/Material 状态会共享 Vulkan graphics pipeline。
- Compute Shader 会为该 Material 创建并校验独立的 compute pipeline，但当前模型 render pass
  不会自动 dispatch；compute 阶段需要额外的 storage buffer/image 资源绑定。

Shader 选择来自 `build/shaders` 中的 SPIR-V 文件。新增 GLSL 文件后重新构建，即可出现在
对应下拉框中。自定义 graphics Shader 需要遵守当前顶点输入、descriptor set `0` 的
UBO/纹理绑定以及 `MaterialPushConstants` 接口。

Material Shaders 提供两个水面预设：

- `Apply water shader`：使用波浪位移、法线扰动和 Fresnel 高光。
- `Apply flowing water`：额外按照参考工程的 flow-map 思路移动 UV，并使用程序化 flow field
  作为当前单纹理布局下的 fallback。

水面 Material 的 Water Flow 配置可以调整 UV 流向（+X/+Y）、流速、流动强度、扰动、波浪
尺度和波高。这些参数随 Material 保存，并由绑定该 Material 的节点共享。

#### Shader Demo Lab

左侧 `Shader Demo Lab` 是参考项目的统一演示入口。

`Screen-space demo` 支持以下效果：

- 纹理与雾效：Render-to-Texture、Fog
- 模糊与光照：Box Blur、Median、Kuwahara、Bloom、SSAO
- 运动与空间效果：Motion Blur、Chromatic Aberration、Screen-space Reflection/Refraction、
  Outlining、Depth of Field
- 风格化与调色：Posterization、Pixelization、Sharpen、Dilation、Film Grain、LUT、Gamma Correction
- 深度缓冲：Deferred Depth/Edge buffer

`Demo strength`、`Demo parameter` 和 Split View 可实时对比效果。所有演示都在现有离屏
颜色/深度目标上使用标准 fullscreen triangle，不改变水面、粒子和 ImGui 的绘制顺序。

选中任意导入网格节点后，`Surface shader preset` 可演示：

- 基础材质：Texturing/Blinn-Phong、Unlit、Diffuse、Fresnel、Rim、Cel、Specular
- 几何与法线：Normal Mapping、Normal/Position Buffer
- 水面效果：Water Reflection/Refraction、Flow Mapping、Foam

水面 shader 继续走现有的第二阶段材质路径；Foam 通过共享的离屏深度采样生成岸边泡沫。
新增 shader 遵守同一 descriptor set `0` 与 `MaterialPushConstants` ABI，CMake 会自动编译
并显示在材质 shader 列表中。

## 常见问题

如果启动时提示 `Vulkan is not supported` 或 `No Vulkan-capable physical device`，说明编译
依赖已找到，但当前系统没有可用的 Vulkan 驱动或 MoltenVK/Metal 运行环境。
