# 特效编辑器

在 macOS 构建中打开编辑器，选择顶部 `Effects` 标签。此工作区使用 `vfx-studio` 的效果文档协议和 Three.js 0.180.0 渲染器。CMake 会使用 Node.js 22 和 npm 构建页面，放在构建目录的 `vfx-studio/site/`。在其他桌面系统，可从 `tools/vfx-studio/` 使用同一编辑器的浏览器入口。

```bash
cd tools/vfx-studio
npm ci
npm run dev
```

## 制作

从八套可编辑预设开始，或新建项目。图层支持粒子、网格、拖尾、点光源和后处理。图层可以复制、删除、排序、启停和单独预览。右侧资源库可导入 PNG、JPEG、WebP 和静态 GLB，GLB 使用默认场景的静态几何。顶部的打开按钮可载入 `.ignis.json` 或 `.ignis.zip`。可在 Shader 工作台导入或编写 GLSL ES 3.00，编译成功后应用到粒子、网格和后处理图层。

左侧检查器管理图层属性、材质与全局渲染参数。点击参数旁的菱形按钮打开曲线编辑器，双击增加关键帧，拖动调整，右键删除。支持线性、平滑和阶梯插值。时间轴支持逐帧、循环、倍速、拖动定位与裁切。`W`、`E`、`R` 切换变换工具，`F` 重置视角，空格暂停或继续。项目库自动保存，`保存` 另存自包含的 `.ignis.json`。长度以米计，时间以秒计，Y 轴向上。

## 在 VulkCore 场景中播放

1. 在 `Effects` 点击 `导出 Vulkan 特效`，得到一个 `.vfx3d` 文件。
2. 在 `Scene` 点击 `Import Realtime Effect...` 并选择该文件。场景会按照导出时 Effects 视口的视角和画面占比取景，并把预览中心对准 Scene 视口中央。未记录预览相机的早期 `.vfx3d` 使用 Effects 的默认视角。
3. 在 Inspector 调整位置、旋转、缩放、自动播放和循环。已导入的特效可用 `Frame Effect` 重新取景，也可用 `Return to Scene Camera` 返回先前视角。拖动视口可环绕观察，在普通预览模式下滚轮可缩放相机。使用 `Replay` 和 `Stop` 验证播放。点击 `Save` 可保存对象设置和自由相机的构图；使用玩家或过场相机时，特效取景只在编辑时生效。发布时将 `.vfx3d` 和引用它的 `.scene` 一起携带。

运行时可调用 `Renderer::playRealtimeEffect(nodeId, worldPosition)` 和 `Renderer::stopRealtimeEffect(nodeId)`。省略 `worldPosition` 时使用场景保存的位置。每次播放从时间轴起点开始，位置覆写仅作用于这次播放。引擎将编辑器的 Y 向上坐标转换到场景的向上方向。

`game.scene` 直接引用 `assets/effects/player/` 下的四个 Effects 预设。为 `realtime_effect` 记录追加 `skill Q 2.15 0.08 0` 可绑定技能键，依次指定按键、向前偏移（米）、向上偏移（米）和是否跟随角色（`0` 或 `1`）。技能特效在按下时从头播放一次。

文件内含源贴图转换出的 PNG、程序和 GLB 网格、粒子初始数据、生命周期曲线、属性关键帧及 SPIR-V。GPU 每帧求值粒子，拖尾随场景相机更新。场景深度用于遮挡与软交界，点光源影响默认材质、地形、草和水。扭曲、色差、闪光和自定义后处理在场景 HDR 颜色上运行。自定义后处理可在 Bloom 前或后执行。

场景的 HDR 配置决定全局 Bloom、曝光、抗锯齿和色彩输出。没有 HDR 配置的场景使用活跃特效包的 Bloom 强度、阈值和半径，由 VulkCore 的滤镜合成。编辑器的全局暗角与色差用于预览，运行时的局部色差可用后处理图层导出。自定义 GLSL 的 Vulkan 转换支持工作台约定的属性和 Uniform，每层最多四张自定义贴图和十四个自定义数值 Uniform。每帧最多处理八盏特效点光源，Bloom 前后各最多八个特效后处理图层。自定义后处理的 `texture(uEffectMask, uv)` 读取当前相机下实时计算的环形遮罩，可使用其 Alpha 通道。

打开含有旧版图集特效的场景时，引擎会跳过这些对象并在 Inspector 的 `Skipped resources` 中列出。要在场景中播放对应效果，可将原始 Effects 项目重新导出为 `.vfx3d`。

## 使用共享运行时

`导出效果` 先进行资源与 GPU Shader 验证并渲染预览，再打包 `.ignis.zip`。解压后使用静态 HTTP 服务打开 `index.html`，可旋转视角、暂停、重播和叠加实例。包内有 `effect.json`、本地 Shader、素材、运行时及类型声明。Three.js 游戏可通过 `VfxRuntime.preload()` 注册，调用 `play()` 放置实例，在帧循环中调用 `update(dt)` 和 `VfxRenderer.render()`。这样粒子、网格、拖尾、灯光与自定义后处理以与编辑器同一套代码求值。导出包的 README 提供完整接入方式。
