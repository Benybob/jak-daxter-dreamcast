# Level streaming: village1 converted, measured, and a design (AI-assisted)

2026-10-29. Measured from the user's own disc (USA Rev 1); nothing from it is
committed. This builds on [LEVEL_CONTENTS.md](LEVEL_CONTENTS.md) (what a level DGO
holds) and the existing cached-terrain streaming slice
([TRAINING_STREAM.md](TRAINING_STREAM.md), [RENDER_CACHE.md](RENDER_CACHE.md)).
It is a design, not an implementation; no figure here is a hardware measurement.

## How village1 was converted

The existing converters (`convert_scene.py`, `prepare_render_sectors.py`) read the
background GLB that OpenGOAL's decompiler writes with `rip_levels`. No desktop
build existed on this machine, so the decompiler was built with MSYS2's mingw64
GCC, CMake and Ninja:

```bash
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTATICALLY_LINK=ON -DCMAKE_ASM_NASM_COMPILER=C:/msys64/mingw64/bin/as.exe <repo>
```

```bash
ninja decompiler
```

- `STATICALLY_LINK` avoids a shared `libcommon.dll`, which MinGW cannot link
  against tree-sitter's unexported symbols.
- The NASM path is a placeholder: only the `game` target assembles, and it is
  not built.
- The decompiler finds its project folder only from a path containing
  `jak-project`, so the executable ran from the git-ignored `out/bin/`. It read
  the disc's files from `<folder>/jak1/{CGO,DGO}` with a minimal inputs file
  (KERNEL.CGO, GAME.CGO, VI1.DGO; no text, STR or audio files) and
  `--config-override '{"inputs_file": ..., "rip_levels": true, "decompile_code": false}'`.
  It writes `decompiler_out/jak1/levels/village1/village1-background.glb` and
  `out/jak1/fr3/`, both git-ignored.

Two things in the GLB stop `prepare_render_sectors.py`, which was written
against training: four BLEND materials (220 tris; it needs a translucent path),
and 302 TIE wind instances whose node matrix carries a value (about 54) in
m[15] of an otherwise affine transform, which it rejects as projective. For
sizing, copies were made with BLEND as OPAQUE and m[15] = 1. The builder then
stops with "sector face cap exceeded": village1 is denser than training.

## What village1 is, converted

| | Triangles in the GLB | Notes |
| --- | ---: | --- |
| tfrag (terrain) | 64,404 | normal, trans, lowres and dirt trees |
| TIE (instanced props) | 405,043 | every instance expanded |
| shrub (instanced plants) | 101,419 | every instance expanded |
| all | 570,866 | 736,355 unique vertices, 4,051 degenerate tris |

The GLB flattens instancing. In the BSP itself, TIE is 1,093 prototype fragments
with 41,688 vertices and 35,269 tris (all LODs), drawn as 3,853 instances. So
about 91% of the flattened TIE triangles are copies.

Density, by 16 m XZ cell (the renderer's default): 3,887 occupied cells, the
median holding one tri (the low-res distant background spreads over 14.7 km),
the busiest 11,469 (7,406 TIE, 2,270 tfrag, 1,793 shrub). Six cells exceed the
8,192-face sector cap. The busiest 3 x 3 window of cells (48 m, the most a 36.8 m
query can touch) holds 48,869 tris.

Converted sizes, from those counts:

| Form | Size |
| --- | ---: |
| The original BSP's background geometry (PS2 formats) | 5.1 MiB |
| Flattened, in the cache's 116-byte faces | 63 MiB |
| Flattened, indexed (24-byte vertex: float position, UV, ARGB; 2-byte indices) | 20 MiB |
| tfrag alone, indexed (about a vertex a tri) | about 1.9 MiB |
| TIE and shrub kept instanced, at LOD 0 (measured below) | 1.1 MB |

## What has to fit

The Dreamcast has 16 MiB of main RAM. The GOAL arena is 13 MiB and the engine
takes 10.7 of it, leaving about 2.8 MiB; the ELF, KOS and the native renderer
live outside it. VRAM (8 MiB) holds the framebuffer (1 MiB), the PS2 DMA buffers
(1 to 6 MiB, until a PowerVR renderer replaces those packets) and the staged
romdisk (6 to 8 MiB). Village1's textures are 1.9 MiB as PS2 texture pages
(171 images in the GLB).

So, at the measured sizes:

- **Two levels resident, as on the PS2, does not fit.** Neither does one level as
  the PS2 lays it out (10.8 MiB of DGO).
- **Flattened geometry is out of the question.** At 20 MiB or more for one level,
  any converter must keep TIE and shrub instancing.
- **Instanced, one level's background is about 2.9 MiB**: tfrag about 1.8 MiB,
  TIE and shrub prototypes 0.6 MB, instance records 0.5 MB. That is still more
  than the free arena, but only the prototypes have to stay resident; tfrag and
  the instance records divide into cells.

## Village1 kept instanced

`instanced_level.py` reads the BSP from the user's DGO and keeps instancing: each
TIE and shrub prototype once, with its vertices and triangles per LOD, and each
instance as a reference to it with its position. Expanding TIE at LOD 0
reproduces the GLB's TIE count exactly (405,043 tris), which checks the instance
links and shows the GLB holds LOD 0.

| | Prototypes | Instances | LOD 0 vertices | LOD 0 tris | Indexed LOD 0 | Instance records |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| TIE | 100 | 3,853 | 20,239 | 19,195 (exact) | 600,906 bytes | 246,592 bytes |
| shrub | 19 | 3,489 | 1,066 | 1,066 (estimate) | 31,980 bytes | 223,296 bytes |

Shrub prototypes record no triangle count: their vertices come from the generic
shrub fragments' `vtx-cnt`, and one triangle a vertex is an upper bound. Expanded,
it gives 146,719 shrub tris against the GLB's 101,419. Instance records are
64 bytes (a 4x4 matrix); 116 of the 119 prototypes have instances.

Per 16 m cell, TIE and shrub instances only:

| Measure | Value |
| --- | ---: |
| cells with instances | 408 |
| instance records in the busiest cell | 153 (9,792 bytes) |
| median records a cell | 9 |
| LOD 0 tris drawn in the busiest cell | 9,954 |
| LOD 0 tris drawn in the busiest 3 x 3 window | 41,312 |

Kept instanced, the busiest cell streams under 10 KB of instance records instead
of copies of its geometry; the triangles it draws are the same as flattened, so
instancing changes memory, not the renderer's per-frame load. That load, about
41,000 TIE and shrub tris in the densest window at LOD 0 before tfrag, is what
LOD and the draw distance have to bring down.

## Proposed design

1. **Resident per level: what gameplay needs everywhere.** Code (0.25 MiB),
   collision (0.25 MiB), entities and their res data (about 0.25 MiB), the
   level's art groups for spawned actors, and TIE/shrub prototypes. Visibility
   data (0.9 MiB) is the PS2 renderer's occlusion; whether a PowerVR renderer
   wants it is open.
2. **Streamed: tfrag and TIE/shrub instance lists, by XZ cell.** This is the
   training slice's model (window queries over a catalog, transactional
   replacement, bounded reads), extended with instancing: a cell lists instance
   records that reference resident prototypes, so a cell costs 64 bytes per
   instance rather than a copy of its geometry.
3. **Textures resident in VRAM per level**, converted offline to PowerVR
   formats, in the space the PS2 DMA buffers occupy today once the renderer
   replaces those packets.
4. **Level transitions: one full level plus a boundary set.** The PS2 kept the
   next level fully loaded; here the next level's resident set (step 1) loads
   while the current one's streamed cells near the border keep drawing, and the
   old level's resident set is freed after the switch.
5. **Caps from data, not guesses.** Village1's busiest cell already passes the
   8,192-face sector cap when flattened, so the cap and the cell size have to be
   chosen against measured instanced density, with LOD.

## Next measurements

- The prototypes' own geometry in a PowerVR form. `instanced_level.py` gives
  the instanced model (prototypes, LOD counts, placements), not vertices;
  decoding the tie-fragment and shrub data, or cutting one instance of each
  prototype out of the GLB, comes next.
- LOD selection and draw distance against the 41,312-tri densest window.
- Village1's textures converted to PowerVR formats, and their VRAM total.
- The decompiler fixes, if wanted upstream of this port: BLEND support in the
  sector builder, and the wind instances' m[15].
- The same numbers for the densest levels (lavatube, maincave, rolling).
