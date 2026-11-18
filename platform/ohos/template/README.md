# VulkCore HarmonyOS Player

用 DevEco Studio 6.0.2 或更新版本打开当前目录，安装 API 22 SDK 和 Native 工具链。
工程面向 arm64 鸿蒙手机，最低 API 22，使用 XComponent + Vulkan 原生运行导出的场景。

1. 等待 OHPM 同步；首次 Native 配置会下载固定版本 PhysX、Assimp、GLM 和 Dear ImGui，需要可访问 GitHub。
2. 工程沿用模板中的 default 签名配置，包括证书路径、密钥别名和签名密码配置。在 File > Project Structure 中确认该配置适用于当前机器与应用；证书文件仍由本机提供。
3. 选择手机后使用 DevEco Studio 的 Run。左下方向键移动，右侧画面拖动转向，Jump 跳跃，Q/E/R 释放技能。
4. 场景、模型、纹理、地形和 SPIR-V 位于 entry/src/main/resources/rawfile/vulkcore。
   启动时解压到应用 filesDir/vulkcore，再加载 startup.txt 指定的场景。每次启动刷新资源副本。
5. 导出后的 engine 包含运行时源码，可脱离原编辑器目录继续开发。应用启动隐藏编辑面板，保留场景模拟。
6. 场景以默认编辑器的 16:9 比例居中等比显示，屏幕比例不同时留边，不裁切、不拉伸。窗口在页面创建前切到横屏，支持两个横屏方向；场景和触控共用 XComponent 的像素坐标系。

导出工程不包含预编译的第三方库。Native 代码和依赖会由鸿蒙 SDK 为 arm64-v8a 编译。
PhysX 采用 Android CPU 分支（`PX_ANDROID=1`、`PX_LINUX=0`），通过鸿蒙 SDK 编译为 OHOS ABI；日志和 CPU 查询分别适配到 HiLog 与 sysconf，无需 Android NDK。
桌面 geometry/tessellation 自定义材质受手机 Vulkan 特性限制；大型地形和高密度草地也需要按目标设备调整。
需要重新导出时，请在编辑器选择一个新的输出目录，防止覆盖导出工程中的手工修改。
