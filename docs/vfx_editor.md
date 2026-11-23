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

1. 在视口摆好要使用的视角，点击 `导出场景精灵`。
2. 解压 `.vulkcore-vfx.zip`，将 `atlas.png` 与 `effect.vfx` 放在同一文件夹。
3. 在 `Scene` 标签点击 `Import Effect Sprite...`，选择 `atlas.png`。特效节点出现在相机当前观察的位置。
4. 在 Inspector 调整位置、世界宽度、自动播放和循环。使用 `Replay` 预览，点击 `Save` 保存场景。场景引用图集文件，随场景分发时保留 `atlas.png`。

游戏代码可以在场景加载后调用 `Renderer::playEffectSprite(nodeId, worldPosition)` 或 `Renderer::stopEffectSprite(nodeId)`。省略 `worldPosition` 时使用场景里保存的位置。精灵带透明度，在 Vulkan 场景里作为有深度测试的朝向相机的平面播放，视角和动画由烘焙结果决定。每个精灵最多 256 帧，短效果按 30 fps 采样。精灵只播放烘焙画面。实时点光源、场景深度软交界和屏幕扭曲由下文的共享运行时计算。

## 使用共享运行时

`导出效果` 先进行资源与 GPU Shader 验证并渲染预览，再打包 `.ignis.zip`。解压后使用静态 HTTP 服务打开 `index.html`，可旋转视角、暂停、重播和叠加实例。包内有 `effect.json`、本地 Shader、素材、运行时及类型声明。Three.js 游戏可通过 `VfxRuntime.preload()` 注册，调用 `play()` 放置实例，在帧循环中调用 `update(dt)` 和 `VfxRenderer.render()`。这样粒子、网格、拖尾、灯光与自定义后处理以与编辑器同一套代码求值。导出包的 README 提供完整接入方式。
