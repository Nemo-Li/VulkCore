# 鸿蒙手机工程导出

VulkCore 可将编辑好的 `.scene` 导出成独立的 HarmonyOS Stage 工程，用 DevEco Studio 打开后，由鸿蒙 Native 工具链编译 C++ 运行时。手机通过 XComponent 和 `VK_OHOS_surface` 运行同一套 Vulkan 渲染、模型动画、PhysX、地形和粒子逻辑。

## 使用

先构建桌面编辑器，使 GLSL 对应的 SPIR-V 更新：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

在编辑器底部 Resource Manager 中填写 Scene path，在新的 HarmonyOS project directory 输入框中填写一个尚不存在的输出目录，点击 **Export HarmonyOS**。导出前会保存当前场景，包含尚未手动保存的节点、材质、动画和粒子设置。

也可以从命令行导出已保存的场景，不需要创建桌面窗口或初始化 Vulkan：

```bash
./build/vulkcore \
  --scene scenes/game.scene \
  --export-ohos exports/HarmonyOSGame \
  --app-name "VulkCore Game" \
  --bundle-name com.vulkcore.game
```

`--app-name` 默认是 `VulkCore Player`，`--bundle-name` 默认是 `com.vulkcore.player`。导出目录须不存在；重新导出时使用新的目录，以保护已在 DevEco 中修改的工程。`exports/` 已加入 Git 忽略规则。

使用 DevEco Studio 6.0.2 或更新版本打开输出目录，安装 API 22 和 Native SDK，同步 OHPM。工程最低兼容 API 22，目标为 `arm64-v8a` 手机，也声明支持平板。首次配置 Native 工程会通过 GitHub 下载固定版本的 Assimp、GLM 和 Dear ImGui，可使用标准 `FETCHCONTENT_SOURCE_DIR_<依赖名>` CMake 参数指定本地源码缓存。PhysX 由模板提供，构建时直接链接。

根目录的 `build-profile.json5` 按 `platform/ohos/template/build-profile.json5` 原样导出，采用展开的 products、buildModeSet、signingConfigs 和 modules 结构。目标 SDK 与最低兼容 SDK 均为 `6.0.2(22)`，strictMode 同时启用 `caseSensitiveCheck` 和 `useNormalizedOHMUrl`，default 产品关联 default 签名配置。

模板沿用当前 `exports/HarmonyOSGame/build-profile.json5` 的完整配置，包含证书路径、密钥别名和签名密码配置；导出不会复制证书文件。在 DevEco 中确认签名配置适用于当前机器与应用后，选择设备运行。

## 工程与资源

```text
HarmonyOSGame/
  AppScope/                 应用名称、包名和图标
  build-profile.json5       API 与产品配置
  entry/src/main/
    ets/                    UIAbility、XComponent 页面与触屏控制
    cpp/                    Native 构建入口和 NAPI 类型声明
    resources/rawfile/vulkcore/
      assets/               项目资源及外部资源副本
      shaders/              编译好的 SPIR-V
      scenes/<原场景名>.scene
      startup.txt           启动场景路径
      manifest.txt          运行时文件清单
  engine/                   独立的 C++ 运行时源码、CMake 和 stb
    third_party/physx/      PhysX 头文件、许可证与 arm64-v8a 静态库
```

导出包含整个 `assets/`，以保留天空盒、草地、技能特效、模型纹理等运行时隐式依赖。编辑器允许的外部模型会复制其所在文件夹，保留 glTF buffer、OBJ/MTL 和同目录纹理；外部地形与水面目录递归复制，单独引用的外部纹理也会复制。外部模型应放在独立资源文件夹中，相关 sidecar 使用相对路径；不支持依赖资源文件夹之外的嵌入绝对路径。

导出器改写以下场景字段中的资源路径，保留节点 ID、材质名称、变换、动画与其他设置：

- `model_resource_path`、`player_model_resource`
- `terrain_resource`、`water_resource`
- `material` 的颜色纹理、`material_textures` 的法线/高光纹理
- `particle_system_object` 的粒子纹理

场景引用的模型和地形相对于场景目录，纹理相对于运行时资源根目录。保留原始场景文件名，使 `tornado.scene` 的程序化特效初始化行为继续生效。模型或地形等必需资源不存在、SPIR-V 缺失或输出目录已存在时，导出会返回具体错误，并清理临时工程；原场景不做路径改写。缺失的可选纹理会清空对应路径，继续使用与编辑器相同的材质颜色、平坦法线或软圆粒子回退。

手机启动时，NAPI `prepare()` 按清单把 rawfile 内容复制到应用沙箱 `filesDir/vulkcore`，以供现有文件系统、Assimp 和图片解码接口使用。每次页面启动刷新资源副本，然后加载 `startup.txt` 指定的场景。

## 手机运行与操作

启动时显示横屏加载页，进度条跟随资源解包、图形引擎和场景初始化更新，首帧提交成功后自动进入游戏。加载页沿用导出时指定的应用名称，底图位于 `entry/src/main/resources/base/media/game_loading.jpg`。

手机入口默认隐藏编辑面板，保留场景模拟和游戏小地图。页面加载前先设置全屏横屏并隐藏状态栏与导航栏，允许在两个横屏方向间随设备旋转。场景以编辑器默认的 1920×1080（16:9）画面为基准，等比放大并居中裁切以铺满手机屏幕：超宽手机裁切部分上下画面，较窄屏幕裁切部分左右画面，物体比例保持一致。XComponent 使用实际全屏尺寸，并延伸到系统栏与刘海区域；圆盘摇杆、技能按钮及小地图按全屏坐标独立布局。

ArkUI 布局使用 vp；Native Surface、Vulkan viewport/scissor、ImGui 与 XComponent 触控使用组件内部的像素坐标，原点位于左上角，X 向右、Y 向下。创建或重建交换链前，将 NativeWindow 缓冲区设为 XComponent 回报的像素尺寸，避免沿用竖屏缓冲区。运行时不执行预旋转，因此交换链声明 `VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR`，交由系统完成屏幕旋转；不能直接把 `currentTransform` 当作已执行的预旋转。交换链创建后启用等比 crop 呈现，并在下一帧绘制前处理尺寸变化。`SceneProjection.hpp` 按实际视口宽高比调整相机投影，实现参考画面的 cover 效果；直接按屏幕分辨率渲染，避免扩大 Surface 导致小地图裁切或触摸坐标偏移。

| 操作 | 控制 |
|---|---|
| Player 移动 | 左下圆盘摇杆，按住后拖动 |
| Player 相机转向 | 在画面右半部分拖动 |
| 跳跃 | Jump |
| 雷霆落击 / 棱光护盾 / 炽焰爆破 / 奥术迸发 | Q / W / E / R |

左下采用固定底盘的圆盘摇杆，支持 360° 连续方向，中心设有死区，拖动超出底盘时摇杆帽保持在最大半径内。摇杆通过 `setMovement(right, forward)` 传入二维方向，角色按现有移动速度沿该方向行走；松手、触摸取消、页面离开或应用进入后台时，摇杆回中并清零移动输入。左右手指分别跟踪，可同时移动、转向和释放技能。

移动、转向和技能取决于场景中是否有已激活的 Player；纯模型或特效场景使用保存的相机设置并播放场景动画。转向手势从右半场景区域按下后持续跟踪同一手指，左右拖动调整水平视角，上下拖动调整俯仰；松手或取消时结束。支持多点触控下移动与转向组合，其他手指按下或松开不会抢占转向手指。XComponent 的移动事件可能将 `isPressed` 设为 false，因此转向按事件类型和手指 ID 更新位置，不以该标志过滤 MOVE。后台暂停绘制并释放按键与转向手势；前台恢复。Surface 销毁会先结束渲染线程、释放 Vulkan 资源，再交还 NativeWindow；Surface 尺寸变化触发交换链重建。初始化或渲染失败会显示错误文字并写入 HiLog。

手机 GPU 的可用 Vulkan 特性和性能仍决定自定义材质与大型场景的运行效果。鸿蒙端 LOD0 草叶按相机到草根的三维距离绘制，在 30–40 地形单位内淡出。鸿蒙端关闭 LOD1 草片和水面绘制，并隐藏 `spawn_woodland.glb`。水位数据仍用于过滤水下的草叶和野花。

鸿蒙端使用 SDR，关闭 HDR 合成、Bloom 和依赖 HDR 的技能全屏后处理。技能的网格、粒子、拖尾和场景光源继续绘制。常规帧直接渲染到交换链，需要读取场景颜色或深度的效果使用离屏缓冲。地形网格以 4 为高度图采样步长，碰撞和植被落点使用同一地表。地形优先采用预烘焙底色与草色覆盖，保留距离雾。为降低片元开销，地表草色覆盖跳过逐像素道路遮罩。太阳光照继续生效，级联实时阴影关闭。

自定义 geometry/tessellation shader 也仍受现有设备特性启用逻辑限制。

## 验证

桌面构建和现有物理、粒子 smoke test 保持可用。`vulkcore_export_smoke` 检查路径迁移、节点 ID/材质名称保留、外部模型 sidecar、启动场景名、JSON 转义、文件清单、失败清理与覆盖保护。`vulkcore_surface_smoke` 覆盖 0/90/180/270 度 Surface 旋转、横竖屏像素尺寸、可变尺寸与未初始化缓冲区回退，并检查桌面的呈现策略和相机投影保持一致。全屏投影测试覆盖 16:9、20:9、19.5:9、16:10、4:3 等屏幕比例，验证等比放大、居中裁切、屏幕覆盖及物体不变形。`vulkcore_movement_smoke` 检查连续移动方向、归一化和非法输入回退。`vulkcore_touch_smoke` 回放 XComponent 触摸事件，覆盖 `isPressed=false` 的移动事件、多个手指、跨越屏幕中线、松手、取消及后台重置。

```bash
ctest --test-dir build --output-on-failure
```

摇杆手势测试直接回放 `JoystickState.ets` 的实际实现，覆盖任意角度、死区、拖出范围、多指、松手与重置。使用本机 DevEco 附带的 TypeScript 编译器运行，无需新增 npm 依赖：

```bash
node tests/JoystickStateSmoke.cjs /path/to/typescript/lib/typescript.js
```

可以单独配置和编译鸿蒙 Native 目标，不需要打包或签名：

```bash
cmake -S exports/HarmonyOSGame/entry/src/main/cpp -B build/ohos-native \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/openharmony/native/build/cmake/ohos.toolchain.cmake \
  -DOHOS_ARCH=arm64-v8a -DCMAKE_BUILD_TYPE=Release
cmake --build build/ohos-native --target VulkCore --parallel
```

ArkTS/资源检查使用模块的 `default@CompileArkTS` 任务：

```bash
cd exports/HarmonyOSGame
/path/to/hvigorw --mode module -p module=entry@default -p product=default default@CompileArkTS
```

实现过程中已完成桌面构建、导出测试、API 22 SDK 下的 arm64 共享库编译/链接与 ArkTS/资源编译。未执行 HAP 打包、签名、安装或手机运行验证；设备上的画面、交互和性能需在 DevEco Studio 中继续确认。

## 架构决策：复用场景与运行时

采用轻量 `Platform/Window.hpp` 接口隔离窗口、按键、指针、Surface 和 ImGui 平台帧。桌面实现转发 GLFW；鸿蒙实现使用 XComponent NativeWindow 和线程安全的触控状态。`Application` 仍是桌面入口，鸿蒙由 NAPI 与 Surface 回调管理生命周期，渲染器只保留共享的场景执行路径。

使用 rawfile + 沙箱文件副本，避免为现有 Assimp、地形解析器与 stb 单独实现虚拟文件系统。代价是首次页面加载需要复制资源，占用额外沙箱空间；后续可演进为带版本的资源缓存或资源包文件系统。

模板里的 PhysX SDK 来自 `exports/HarmonyOSGame` 的 API 22、`arm64-v8a` Release Native 构建，对应 O3DE PhysX 提交 `d5c964f23c3ba5553a305bbc7af3b50a5fa47a12`（5.6.1）。六个静态库位于 `platform/ohos/template/engine/third_party/physx/lib/arm64-v8a`，入库前通过 `llvm-strip --strip-debug` 去掉调试信息。更新静态库时要同时更新同一版本的头文件和许可证。

源码构建时，`cmake/OhosPhysX.cmake` 用鸿蒙 Native SDK 编译 PhysX 的 Android CPU 分支，日志接到 HiLog，CPU 数量通过 `sysconf` 获取。模板沿用该构建生成的 `PxPreprocessor.h`，使运行时物理封装与静态库使用相同的 `PX_ANDROID=1`、`PX_LINUX=0` 平台配置。导出工程的 CMake 只导入这些静态库。
