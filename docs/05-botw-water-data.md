# BOTW water data

`assets/terrain/terrain_water` contains the extracted `.water.extm` tiles from
`botw-tools-master/terrain_water`. The directory grouping follows the source
SARC layout: a group such as `54000000D8.water.extm` contains the four possible
tiles `54000000D8` through `54000000DB`.

Each file is a native 64×64 little-endian grid. Every sample is 8 bytes:

| Offset | Type | Meaning |
| --- | --- | --- |
| `0x00` | `uint16` | Water height sample |
| `0x02` | `uint16` | Flow X, decoded as `value - 0x8000` |
| `0x04` | `uint16` | Flow Z, decoded as `value - 0x8000` |
| `0x06` | `uint8` | Reserved |
| `0x07` | `uint8` | Water material index, 0–7 |

`MainField.tscb` supplies the matching tile coordinate and the shared maximum
height. VulkCore converts a sample to world Y with
`sample / 65535.0 * maximumHeight`; values 0 and 1 are the BOTW no-surface
sentinels. Flow and material values are carried in the generated water mesh
(vertex color RG/B, with alpha `0.5` marking extm data) and consumed by the
water shader.
