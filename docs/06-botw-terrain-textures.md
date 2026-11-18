# BOTW terrain texture pass

`assets/terrain/MainField.tscb` contains the terrain material information
array, including each material's U/V texture scale and two additional tuning
values. The extracted project does not include the source `.mate` files or
`Terrain.Tex1.sbfres`, so the renderer cannot reproduce the original material
index map verbatim.

The project now keeps the TSCB material records and uses the area height band
plus the area flag to choose a stable TSCB tuning record for each tile. The
terrain shader derives a hand-painted landform blend from height, slope and
that per-tile tuning. This gives the extracted heightfield a BOTW-inspired
palette without claiming to be the original Nintendo material data.

Generated base-color textures live in `assets/terrain/textures/`:

- grass, dirt, rock, sand, snow, mud, gravel and volcanic ash
- `terrain_atlas.png` packs them into a 4×2 atlas used by
  `shaders/src/shader_terrain.frag`

The atlas layout is row-major: grass/dirt/rock/sand on the first row and
snow/mud/gravel/ash on the second row. The material U/V scale from TSCB is
carried through the terrain vertex color and controls atlas repeat frequency.

Before mesh generation, the loader repairs isolated height spikes, reconciles
shared tile edges/corners, and applies eight separable `[1, 2, 1] / 4` filter
passes across neighbouring tiles at the same resolution. The filter has a
standard deviation of two source samples (about 4.9 m in the included tiles).
It rounds the narrow height jumps that otherwise become triangular mountain
teeth when sampled at stride two, while preserving broad landforms and planar
slopes. Missing neighbours use clamped edges. The original `.hght` files stay
unchanged; resident filtered samples supply height queries, grass placement,
mesh bounds, rendering and the PhysX mesh.

Terrain normals come from the full filtered heightfield. Mesh stitching blends
these normals at shared vertices instead of recomputing them from the coarser
triangles, so slope-based material blends and lighting do not reveal the mesh
diagonals. `vulkcore_terrain_smoke` covers cliff smoothing, isolated peaks,
shared edges/corners (including a missing tile), preserved slopes/plateaus,
and agreement between the mesh, height queries and heightfield normals.
