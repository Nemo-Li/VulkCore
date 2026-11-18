# 实体到线条车资产

- `su7_body.glb` 是从 [SU7 体验页](https://gamemcu.com/su7/) 下载的实体车。站点资源版本为 `1.0.5`，原始模型地址为 [sm_car.bin](https://gamemcu.com/su7/1.0.5/mesh/sm_car.bin)。
- `solid_sedan.glb` 提供备用的低面数轿跑网格。
- `line_ribbons.glb` 来自用户提供的 `solid-to-wireframe/assets/models/sm_linecar_uncompressed.glb`。转换为核心 glTF 的浮点顶点属性，保留两套网格的索引、节点变换、位置、法线、UV0 和顶点色。参考资产最初由 SU7 体验页提供。
- `stage.glb` 提供用于展示的地面和网格平面。


### 实体车导入

恢复下载包的 GLB 标识和数据块，解码 Meshopt 网格，将量化顶点属性转换为浮点，保留节点变换、索引、材质槽和 UV。内嵌的 8 张 WebP 贴图转换为 PNG。Assimp 验证了 40 个网格、170,676 个三角形和 9 种材质。

| 文件 | SHA-256 |
|---|---|
| 原始 `sm_car.bin` | `960d608c6323cf2fb8faaf6780fc0a9355969be8c8260cd0c5b5ccce839e91c1` |
| 可导入的 `su7_body.glb` | `1dc06fcd5e521a082c63f9f7a39fb5d2d1f451001c307ee6117fab842c67d5a0` |

### 生成线条和地面资产

```bash
python3 build_assets.py /path/to/solid-to-wireframe/assets/models/sm_linecar_uncompressed.glb
```
