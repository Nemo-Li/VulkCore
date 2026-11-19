# Switch SBFRES 模型导入与渲染

`scenes/tree_apple_sbfres.scene` 从 `/Users/nemo/Downloads/Model/Obj_TreeApple_A.sbfres` 读取模型，并自动加载同目录的 `Obj_TreeApple_A.Tex.sbfres`。场景选择 `Obj_TreeApple_A_L_01` 的 LOD0，用 `node_render` 隐藏其他 FMDL 变体和两处伐木切面。相机采用原模型坐标和 Y-up。

```sh
cd /Users/nemo/Study/VulkCore
cmake --build build --parallel
./build/vulkcore --scene scenes/tree_apple_sbfres.scene
```

使用下面的命令抓取一帧。两份源文件通过场景路径和同目录命名约定加载。迁移到其他机器时，把两份文件放在同一目录，再更新场景中的 `model_resource_path`。解析 API 也接受显式贴图包路径 `loadSbfresMesh(modelPath, texturePath)`。

```sh
./build/vulkcore --scene scenes/tree_apple_sbfres.scene \
  --capture-frame 1 0 1280 960 build/tree_apple_sbfres.png
```

## 解包和偏移规则

- SBFRES 以 `Yaz0` 开头。`+0x04` 是大端解压大小，数据从 `+0x10` 开始。控制字节从最高位向最低位读取，1 表示一个字面字节，0 表示回溯引用。引用的低 12 位加 1 得到距离，高 4 位非零时加 2 得到长度，为零时读取额外字节并加 18。重叠的引用逐字节展开。解析器也接受已经解压的 `.bfres`。
- BFRES 检查 `FRES`、小端 BOM `0xFEFF` 和版本主编号 5。样本版本字为 `0x00050003`。常规指针为小端 64 位绝对偏移，以解压后的 FRES 起点为基准。字符串由 `u16` 长度和后续字节组成。ResDict 记录有一个根节点和按值数组顺序排列的键，键记录步长为 `0x10`。
- FRES 的 `+0x1C` 是文件大小，`+0x28` 和 `+0xBC` 分别指向 FMDL 数组和记录其数量。`+0x90` 指向 BufferInfo，其中 `+0x04` 是 GPU 数据区大小，`+0x08` 是起点。顶点流和索引流使用相对于这个 GPU 数据区的偏移。
- `+0x98` 指向外部文件表，`+0xC8` 是条目数。每个 16 字节条目记录 payload 指针和长度。通过 payload 的 `BNTX` 标记识别贴图包。同名纹理优先使用伴随包。缺少伴随包时，仍可使用模型内嵌 BNTX 和现有材质的默认贴图。
- 读取前检查区间、表长度、顶点流 stride 和索引范围。输入及解压大小各限 256 MiB。单次导入另有几何与贴图预算，格式或布局不匹配时抛出包含文件名的错误。

## 几何结构与 VulkCore 映射

下表的偏移均相对当前记录起点。

| 记录 | 用到的字段 | 映射 |
| --- | --- | --- |
| `FMDL`，步长 `0x78` | `+0x10` 名称，`+0x20` FSKL，`+0x28` FVTX，`+0x30` FSHP，`+0x40` FMAT，`+0x68` 三类数组计数 | 每个 FMDL 是根下的 `ModelNode`，其中的 FSHP 成为子节点。节点 ID 按文件顺序分配。 |
| `FVTX`，步长 `0x60` | `+0x10` 属性数组，`+0x38` 流大小数组，`+0x40` stride 数组，`+0x50` GPU 相对偏移，`+0x54` 属性数和流数，`+0x58` 顶点数 | 各顶点流按 8 字节对齐。属性记录用 16 字节步长，格式 ID 在 `+0x08` 且按大端读取，流内偏移和流号在 `+0x0C`、`+0x0E`。实际顶点分量按小端读取。 |
| `FSHP`，步长 `0x70` | `+0x20` 指向 LOD 数组，`+0x5E` 材质索引，`+0x60` 骨骼索引，`+0x62` FVTX 索引，`+0x66` 每顶点骨骼影响数，`+0x67` LOD 数 | 取 LOD0。LOD 记录步长 `0x38`，索引缓冲大小记录、GPU 相对偏移、拓扑、索引格式、索引数、`FirstVertex` 依次取 `+0x18`、`+0x20`、`+0x24`、`+0x28`、`+0x2C`、`+0x30`。拓扑 3 是三角列表。索引格式 0、1、2 分别对应 8、16、32 位无符号索引。 |
| `FSKL` | `+0x18` 指向骨骼表，`+0x3C` 是计数。骨骼记录步长 `0x50`，包含父节点和 TRS | 将刚性绑定变换应用到顶点，用逆转置矩阵变换法线。 |

`_p0`、`_n0`、`_u0`、`_c0` 分别写入 `Vertex` 的位置、法线、首组 UV 和顶点色。样本用 `0x0515` 的 FP16×4 位置、`0x020E` 的 10:10:10:2 打包法线、FP16 或归一化 16 位 UV，以及 `0x010B` 的 UNORM8×4 顶点色。导入器也支持 `0x0109` 和 `0x0209` 的 8 位归一化双分量属性。对索引依次加 `FirstVertex` 和 VulkCore 合并缓冲的起始顶点，得到最终的 `uint32_t` 索引。每个形状生成一个 `MeshPart`、材质索引和包围盒。顶点、索引、材质与节点通过 `MeshData` 进入现有的上传与绘制流程。

两份样本验证了六个 FMDL、20 个 LOD0 网格、7,380 个顶点和 6,367 个三角形。默认场景显示其中四个网格，共 2,923 个三角形。

## 贴图与材质

- `FMAT` 的步长为 `0xB8`。`+0x38` 是贴图名指针数组，`+0xA8` 是贴图数，`+0x50` 指向采样器名字典。同一索引的采样器名与贴图名配对，再用贴图名匹配 BNTX。`_a0` 和 `_n0` 分别映射基础色和法线。启用 alpha test 的材质把 `_ms0` 绑定到独立遮罩。
- BNTX 内部指针以 BNTX payload 起点为基准。从 `NX` 头的 `+0x24`、`+0x28` 读取数量和 BRTI 指针表。BRTI 记录提供平铺方式、宽高、格式、组件选择、GOB 高度参数和 mip 指针。图像以像素或 BC 压缩块为单位重新排序。Pitch-linear 使用 32 字节行对齐，Tegra block-linear 按 64 字节宽、8 行的 GOB 及其垂直分组计算地址。
- CPU 解码 R8、RGBA8、BC1/2/3、BC4 UNORM 和 BC5 UNORM 的 mip 0，按 BRTI 的通道选择转为 RGBA8。Vulkan 上传时保留颜色贴图的 sRGB 或线性语义，法线与遮罩使用线性格式。支持线性 blit 的设备会重新生成 mip 链。
- 从 `FMAT` 的 `+0x18` 和 `+0x20` 读取 RenderInfo 值与键。`gsys_render_state_display_face=both` 复用现有双面渲染。树叶的 `gsys_alpha_test_enable=true` 和 `gsys_alpha_test_value=0.6` 启用裁切。树叶的 BC1 基础色 mip 0 完全不透明，独立 BC4 遮罩的红通道决定叶片轮廓。两张遮罩分别为 200×200 和 1024×1024。

## 现有渲染管线与改动

现有管线已满足索引三角网格、基础色纹理、法线纹理、双面材质和深度测试的要求。对于本样本，额外需要独立的透明度遮罩、材质裁切阈值，以及线性原始像素上传。

| 位置 | 改动 |
| --- | --- |
| 模型导入 | `.sbfres` 和 `.bfres` 经同一个模型入口分发到原生解析器。资源面板将 `.Tex.sbfres` 和 `.Tex.bfres` 识别为伴随文件。 |
| 材质数据与上传 | `EmbeddedTextureData` 记录色彩空间。`MaterialData` 增加 `embeddedAlphaMask` 和 `alphaCutoff`。材质更新时保留模型导入的裁切数据。 |
| Descriptor 与片元着色器 | 材质布局增加 binding 8，默认绑定白色遮罩。`shader_base.frag` 将基础色纹理 alpha、材质 alpha 和遮罩红通道相乘，丢弃低于阈值的片元。树叶在不透明队列中写入深度。阈值使用基础 shader 原先闲置的 `waterFlow.x`，Push Constant 布局仍为 128 字节。 |

当前实现面向 Switch 5.x 静态三角网格，渲染使用 VulkCore 的基础光照。Wii U 和其他 Switch 布局、带权重蒙皮与动画、多组 UV、源 shader、交互换 LOD、原始 mip 链和完整采样器状态属于后续扩展范围。叶片风动、透射和游戏的复合材质也在此范围内。

## 参考实现

- Switch Toolbox 的 BFRES 导入、顶点属性映射和材质关联。[BfresSwitch.cs](https://github.com/KillzXGaming/Switch-Toolbox/blob/be66ed6ee713626a45cd4927e0fc61ceac1bb49f/File_Format_Library/FileFormats/BFRES/BfresSwitch.cs)
- Switch BFRES 的文件头、模型块和流表字段。[BfresLibrary](https://github.com/KillzXGaming/BfresLibrary/tree/e2c5debca60e9b95e0fd220d32886102d65166bf/BfresLibrary/Switch)
- BNTX 的 BRTI 字段、表面格式和 Tegra block-linear 地址规则。[BNTX-Extractor](https://github.com/aboood40091/BNTX-Extractor/tree/2c08e8e55fcc666ba6568a102f36dabd6c584e25)
