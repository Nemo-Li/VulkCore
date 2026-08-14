# VulkCore

一个使用 CMake 构建的最小 Vulkan 开发环境示例，包含：

- Vulkan：创建 `VkInstance`、GLFW 窗口表面，并选择一个物理设备
- GLFW：创建 Vulkan 窗口并处理窗口事件
- GLM：验证头文件库已正确接入，并提供后续矩阵/向量计算基础

## 依赖

### macOS（Homebrew）

```bash
brew install cmake vulkan-loader vulkan-headers molten-vk glfw glm
```

设备需要有可用的 Vulkan 驱动；macOS 通常通过 MoltenVK 映射到 Metal。

### Ubuntu/Debian

```bash
sudo apt install cmake build-essential libvulkan-dev vulkan-tools \
    libglfw3-dev libglm-dev
```

同时需要显卡对应的 Vulkan 驱动。`vulkan-tools` 不是编译必需项，但可以用来运行
`vulkaninfo` 检查驱动。

## 构建与运行

在工程根目录执行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
./build/vulkcore
```

CMake 会优先使用 GLFW/GLM 导出的 CMake package；如果系统只提供 pkg-config
元数据，则会自动使用 pkg-config 查找它们。

程序启动后会创建一个不使用 OpenGL 上下文的 GLFW 窗口，初始化 Vulkan 实例、窗口
表面和物理设备，然后进入事件循环。当前示例还没有交换链、渲染管线和帧同步，适合
作为后续 Vulkan 开发的起点。

如果启动时提示 `Vulkan is not supported` 或 `No Vulkan-capable physical device`，
说明编译依赖已找到，但当前系统没有可用的 Vulkan 驱动或 MoltenVK/Metal 运行环境。
