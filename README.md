# VulkCore

一个使用 CMake 构建的最小 Vulkan 开发环境示例，包含：

- 应用层：窗口创建、事件循环和生命周期管理
- Vulkan 核心：实例、调试消息、物理设备、逻辑设备、队列和命令池
- 渲染器：交换链、渲染管线、帧同步以及 vertex/index buffer
- 纹理：`stb_image.h` 图片解码、FBX 材质纹理加载、staging 上传和采样器
- 模型：Assimp FBX 网格导入、节点变换、三角化、法线和 UV 提取
- UI：Dear ImGui 1.92.9 GLFW/Vulkan backend，主画面控制面板和调试窗口
- 资源工具：GPU buffer 封装、staging buffer 上传和二进制文件读取
- Shader 管线：GLSL 源码与构建生成的 SPIR-V 分离

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
third_party/stb/         stb_image.h 单头文件库
shaders/src/             GLSL 源文件
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
三角化、平滑法线生成、首个 UV 通道、顶点颜色和材质信息读取；没有 UV 的顶点会使用
`(0, 0)`。每个材质对应一个绘制分段，材质纹理不可用时使用其 Base Color/Diffuse
颜色。
渲染器会根据模型包围盒自动居中缩放，并启用深度测试和基于法线的基础光照。

CMake 会优先使用 GLFW/GLM 导出的 CMake package；如果系统只提供 pkg-config
元数据，则会自动使用 pkg-config 查找它们。构建时需要 `glslc`，CMake 会将
`shaders/src` 下的 GLSL 编译到 `build/shaders`，运行时通过构建配置读取该目录，
不依赖当前工作目录。

程序启动后会创建一个不使用 OpenGL 上下文的 GLFW 窗口，初始化 Vulkan 实例、交换链、
渲染管线以及 vertex/index buffer。模型材质优先使用 FBX 中的 Base Color/Diffuse
纹理；没有嵌入纹理时，按材质名从 `assets/models/tex/<material>-diffuse.png` 加载。
默认图片不存在或无法加载时使用 FBX 中的材质颜色和顶点颜色，不再绑定固定的示例纹理。
现有 3D 场景会先绘制，随后在同一个 Vulkan
render pass 中绘制 Dear ImGui 主界面；左侧控制面板可以调整模型旋转速度并打开
ImGui demo 窗口。Transform Debug 区域支持拖动修改模型的 X/Y/Z 位置和绕 X/Y/Z 轴
的旋转角度，也可以关闭 Z 轴自动旋转或恢复默认变换。

如果启动时提示 `Vulkan is not supported` 或 `No Vulkan-capable physical device`，
说明编译依赖已找到，但当前系统没有可用的 Vulkan 驱动或 MoltenVK/Metal 运行环境。
