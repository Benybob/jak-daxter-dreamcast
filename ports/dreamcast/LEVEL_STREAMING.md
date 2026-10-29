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

## Village1's prototypes, decoded

`prototype_geometry.py` cuts each TIE and shrub prototype out of the GLB: it
takes the triangles of the GLB nodes named after the prototype (the wind-swayed
TIE, palms and fish, is exported one `wind-group-*` node an instance, so those
are pooled), keeps the ones inside an instance's bounding sphere, and maps them
into the prototype's frame with the inverse of that instance's matrix, decoded as
the decompiler's `extract_tie_matrix` builds it (rows 0 to 2 in 4.12 fixed point,
row 3 an s16 shifted left 6 plus the bounding-sphere centre). Where instances
crowd together a sphere also takes in neighbours' triangles, so it keeps only
the triangles two instances' cuts share in the prototype frame (within 2 mm),
confirmed by the BSP's LOD 0 count for TIE and by a third instance for shrubs.

All 116 prototypes with instances decode: 102 confirmed by two instances, 14 that
have a single instance. The result is a DCI1 package (described in the tool):
indexed prototypes with 24-byte vertices (float position in meters, float UV,
ARGB8888) and u16 indices, and an instance table of a prototype index and a 3x4
float matrix each.

| | Value |
| --- | ---: |
| prototypes | 116 |
| vertices | 17,977 (431 KB) |
| indices | 59,841 (120 KB) |
| instances | 7,342 (382 KB) |
| package | 938,960 bytes (the index run now pads to 4 bytes) |

Expanding every instance from the package gives 504,814 triangles, and every one
of them matches a GLB triangle within 2 mm. The GLB has 506,462 TIE and shrub
triangles, so the package reproduces 99.7% of them; about 1,650 fall outside the
cuts (a prototype's triangles that reach past 1% beyond its instance's sphere).
Textures are referenced by GLB material and not converted yet.

The same tool also fixed a scan error: an `array`'s content-type field is a type
link too, so `level_contents.basics` now keeps only 16-byte-aligned type slots
(a basic's pointer is 16n + 4), which drops three phantom TIE buckets.

## Village1's prototypes drawn in Flycast

`instance_pass.c` draws a DCI1 package on the PowerVR, and `jak-dc-instances.elf`
is its diagnostic. The pass shares the cached-terrain renderer's pieces: the
camera of `render_world_view.c` (yaw, pitch and boom distance around a target,
four meters a view unit), `dc_render_clip_viewport` for the near plane and the
viewport, and `render_base.c`'s projection and batched vertex submission. Each
instance whose translation lies within the draw distance has its prototype's
vertices taken to view space once (the instance matrix, then the camera), and
each triangle is clipped, projected and sent as untextured, double-sided Gouraud
in the package's vertex colours. The loader checks the whole package before
anything uses it (sizes, ranges, indices, finite values) and leaves its output
alone on a refusal.

The package stays out of the repository: `make_instance_fixture.py` stages
`build/private-instance-assets/package.dci` (written there by
`prototype_geometry.py --out`, or a synthetic one when it is missing) and a
header with its size, CRC, a camera aimed at its densest 16 m cell and the
instance and triangle counts the pass must visit, worked out on the host in
single precision.

    python prototype_geometry.py DGO GLB --out build/private-instance-assets/package.dci
    python build_dreamcast.py --target jak-dc-instances.elf --instance-assets build/private-instance-assets
    python verify_backend.py FLYCAST jak-dc-instances.elf --nm NM --output OUT --suite instances \
        --tests 29 --capture --dump dc_instance_metrics 40

With village1's package, 16 MiB stock Flycast, the camera at yaw 2.2, pitch
-0.45 and 55 m over the village's densest cell, and an 80 m draw distance:

| | Value |
| --- | ---: |
| checks | 29 of 29 |
| instances visited | 7,342 |
| instances within the draw distance | 765 (host: 765) |
| triangles visited | 59,815 (host: 59,815) |
| triangles sent to the PowerVR | 56,672 |
| culled by the viewport | 3,515 |
| invalid clips | 0 |
| TA vertex buffer | 3 MiB |

The captured frame was compared with an independent host render of the same
package and camera (painter's algorithm, flat colours): the pixels covered by
geometry agree on 99.4% of a half-resolution sample. The exact check is
`dci1_reference.py` (a z-buffered, perspective-correct Gouraud rasterizer with
the same camera, draw rule and near clipping) and `frame_compare.py`: against
the Flycast capture, quantised to RGB565, coverage agrees on 99.998% of the
frame (5 of 307,200 pixels differ), the mean colour error is about 2.5 levels a
channel, and it visits the same 765 instances and 59,815 triangles.

    python dci1_reference.py PACKAGE --camera=TX,TY,TZ,YAW,PITCH,DIST,DRAW --out ref.ppm
    python frame_compare.py OUT/frame.ppm ref.ppm --quantize565 --min-agreement 0.99 There is no ground in the
picture, since tfrag is a separate pass, and no textures. The time the pass
reports (about 0.45 s a frame for 60k triangles, all on the CPU) is Flycast's
and says nothing about the hardware; it does say the next work is LOD and a
tighter draw distance, not more triangles.

## Draw distance and LOD, budgeted

`instance_budget.py` puts the camera target at the centre of each of village1's
414 16 m cells that hold an instance and applies the instance pass's draw rule
there (the same single-precision test as `make_instance_fixture.expected`). With
the DGO it also estimates LOD: each TIE prototype's bucket carries its own
switch planes (`dists`: near, mid and far, from 5/6.5/10 m for small props to
90/105/140 m), geometry 0 being the tie-near renderer's and 1 to 3 normal TIE's,
and a lower LOD is taken as the package's LOD 0 triangles scaled by the BSP's
LOD n / LOD 0 ratio. Shrubs stay at LOD 0: their four geometry slots are the
same plant in different shrub formats, not reductions.

TIE and shrub triangles drawn, village1:

| Draw distance | Worst cell, LOD 0 | Worst cell, game LOD | Median, game LOD | 90th percentile, game LOD |
| ---: | ---: | ---: | ---: | ---: |
| 20 m | 27,753 | 27,753 | 4,087 | 12,760 |
| 30 m | 43,217 | 41,381 | 10,506 | 27,131 |
| 40 m | 66,617 | 60,031 | 18,629 | 42,885 |
| 60 m | 129,448 | 102,353 | 38,980 | 72,788 |
| 80 m | 184,461 | 129,570 | 52,894 | 96,837 |

The game's own LOD planes take about 30% off at 80 m and almost nothing inside
30 m, where most prototypes are still at geometry 0. So LOD alone does not bring
the dense centre of the village down to a small budget: the draw distance has
to be short there (30 m is about 41k triangles at worst), or the near geometry
itself has to be cheaper. The pass cannot draw the lower LODs yet: the package
holds LOD 0 only, because the GLB does, and the lower geometries still have to
be decoded from the tie fragments. The planes' meaning follows the field names
and `tie-methods.gc`'s per-geometry stats; the instance-TIE asm's comparisons
are not checked against it.

## TIE's lower LODs, decoded and drawn

The GLB holds TIE geometry 0 only, but the decompiler's `extract_tie` emulates
all four geometries, and with its level dump on (the `dump_level` argument that
`extract_level.cpp` passes, `false` upstream; flipped in a local, uncommitted
build) it writes each prototype's strips per geometry as an OBJ in the
prototype frame at 1/16 m. Geometry 0 there lines up with the cut prototypes
exactly (village1's `lil-rock-06`: the same 80 triangles and bounds).

`tie_lods.py` turns a DCI1 package and that dump into DCI2: the geometry table
gains each TIE prototype's geometries 1 to 3 (a geometry identical to the one
before it is shared), and a prototype table gives every prototype its geometry
per LOD and its BSP near, mid and far planes. The OBJ has no colours, so a lower
LOD's vertex takes the colour of the nearest geometry-0 vertex. `instance_pass.c`
reads DCI1 and DCI2 and picks an instance's LOD with the same single-precision
distance as the draw rule; `tie_lods.flatten` resolves the same choice for one
target and gives the DCI1 that `dci1_reference.py` draws.

| Village1 | Value |
| --- | ---: |
| TIE prototypes with LODs | 97 |
| geometries added | 148 (142 LOD slots shared) |
| geometries in all | 264 |
| package | 1,182,128 bytes (from 938,960) |
| prototypes whose cut has more tris than the dump's geometry 0 | 16, by up to 13% |

The same camera as above, 80 m draw distance, in Flycast: 30 of 30 checks,
instances at LOD 0 to 3 567, 81, 88 and 29 (as the host predicts), 56,565
triangles visited (from 59,815) and 53,942 sent; against `dci1_reference.py` on
the flattened package, coverage agrees on 99.998% (5 pixels). From the village
centre most instances are close, so this view only saves 5%; the per-cell table
above is what LOD does across the level.

    python tie_lods.py build/private-instance-assets/package.dci DGO DUMP_DIR --out PACKAGE.dci2

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

- The 0.3% of TIE and shrub triangles the cuts miss, and textures for the
  package's materials.
- Textures on the instance pass, and tfrag drawn under it in the same frame.
- A draw distance chosen against the LOD table above, and the 16 prototypes
  whose cut and dump geometry 0 differ.
- Village1's textures converted to PowerVR formats, and their VRAM total.
- The decompiler fixes, if wanted upstream of this port: BLEND support in the
  sector builder, and the wind instances' m[15].
- The same numbers for the densest levels (lavatube, maincave, rolling).
