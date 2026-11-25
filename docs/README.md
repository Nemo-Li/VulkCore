# VulkCore 设计与演进文档

本目录描述的是 VulkCore 当前源码的真实状态，以及从教学/原型级 Vulkan 渲染器演进到可扩展通用引擎的建议方案。分析基线为 2026-08-20 的工作区源码；代码继续变化时，应同步更新文档中的“已实现、部分实现、未实现”判断。

## 阅读顺序

1. [整体代码框架与现有架构](01-codebase-architecture.md)：理解模块边界、对象生命周期、场景与资源数据如何组织。
2. [Vulkan 图形管线与帧流程](02-rendering-pipeline.md)：理解从设备创建到每帧提交，以及网格、水面、透明物体、粒子和 UI 的实际渲染顺序。
3. [能力差距、问题分析与演进路线](03-gap-analysis-and-roadmap.md)：识别与通用完善引擎的差距、优先级、依赖关系和可验证的阶段目标。
4. [高扩展性引擎目标架构](04-scalable-engine-architecture.md)：给出模块化、数据驱动、Render Graph、资源系统、ECS、插件与工具链的目标设计和渐进迁移方法。
5. [BOTW 风格草地技术方案与实现](07-botw-grass.md)：说明三层草地 LOD、程序化种植、风场、Player 踩踏和当前性能边界。

6. [鸿蒙手机工程导出](08-harmonyos-export.md)：说明 DevEco 工程导出、资源路径迁移、XComponent 原生运行与手机触屏控制。
7. [Switch SBFRES 模型导入](09-sbfres-import.md) 记录模型与贴图格式、管线改动和苹果树场景的运行方法。
8. 了解敌人行为的常见编辑机制、引擎支持以及 VulkCore 的巡逻配置。[敌人行为编辑与巡逻](10-enemy-behavior.md)
9. 地形纹理数组、分距离渲染以及 Switch 样本中的 `MaterialAlb` 与 `MaterialCmb`。[地形材质与分距离渲染](06-botw-terrain-textures.md)
10. 仓库所附资源的 88 个地形材质 ID、实际纹理层和资源名。[地形材质 ID 与纹理层对照表](11-botw-terrain-material-mapping.md)

## 状态约定

| 标记 | 含义 |
|---|---|
| 已实现 | 当前源码中存在可运行路径，不代表已达到生产质量 |
| 部分实现 | 主路径存在，但扩展性、正确性边界或工程保障不完整 |
| 未实现 | 当前源码中没有对应系统，或只有配置/管线占位而没有运行语义 |
| 目标设计 | 建议的未来形态，不应理解为当前能力 |

## 文档维护原则

- 先写运行时事实，再写评价与建议；不把路线图描述成现成功能。
- 架构结论尽量指向具体目录、类型或函数，便于从文档回到源码。
- 新增跨模块功能时，同时更新能力矩阵、目标架构偏差和迁移阶段。
- 涉及 GPU 同步、资源生命周期或序列化格式的变更，应增加 Architecture Decision Record（ADR）并记录兼容策略。

## 关键源码入口

- 应用入口：[`src/main.cpp`](../src/main.cpp)、[`src/Application.cpp`](../src/Application.cpp)
- 渲染调度：[`include/vulkcore/Renderer/Renderer.hpp`](../include/vulkcore/Renderer/Renderer.hpp)、[`src/Renderer/Renderer.cpp`](../src/Renderer/Renderer.cpp)
- Vulkan 基础设施：[`src/Renderer/VulkanContext.cpp`](../src/Renderer/VulkanContext.cpp)、[`src/Renderer/SwapChain.cpp`](../src/Renderer/SwapChain.cpp)
- 资源封装：[`src/Renderer/Buffer.cpp`](../src/Renderer/Buffer.cpp)、[`src/Renderer/Texture.cpp`](../src/Renderer/Texture.cpp)
- 场景与材质数据：[`include/vulkcore/Renderer/Mesh.hpp`](../include/vulkcore/Renderer/Mesh.hpp)
- 模型导入：[`src/Utils/ModelLoader.cpp`](../src/Utils/ModelLoader.cpp)
- 粒子模拟：[`include/vulkcore/Renderer/ParticleSystem.hpp`](../include/vulkcore/Renderer/ParticleSystem.hpp)、[`src/Renderer/ParticleSystem.cpp`](../src/Renderer/ParticleSystem.cpp)
- Shader：[`shaders/src`](../shaders/src)
