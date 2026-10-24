# Camera-space near-plane clipping (AI-assisted)

The native training renderer now clips triangles that cross the camera's near
plane. Previously, one corner behind the plane caused the entire triangle to
disappear. The new path retains the visible polygon, interpolates texture/color
attributes and emits one or two triangles. It also handles vertices directly
on the camera plane without dividing by zero. This is renderer progress toward
the full port; original GOAL gameplay, camera obstruction and the game remain
unfinished.

## Data path and SH-4 behavior

`dc_scene_prepare` uses upstream SH4ZAM to retain camera-space XYZ together with
a projected-position cache. Each `dc_render_vertex` occupies 24 bytes. Ordinary
front-side vertices need one cached reciprocal; mixed triangles project their
bounded three/four-vertex clipped polygon. The earlier `dc_scene_project` API
remains available for other viewers.

The new entry has its own integer-only SR.FD/FPSCR PR/SZ/FR guard before any C
prologue/FPU access. It preserves all XMTRX bits and FPU control state. Behind
or too-near vertices retain their view coordinates and get a zeroed projected
cache, with no reciprocal. A zero depth therefore stays usable by the clipper.

`scene_clip.c` clips against `z >= 0.05` in renderer units (0.2 meters in this
prototype). It preserves polygon order/winding and interpolates raw UV and color
before perspective division. Packed color channels round to eight bits. Invalid
input returns -1 without changing output. The function has fixed input3/output4
capacity, no allocation and no global state. The fan respects batch order and
opaque/masked texture state.

The GPU layer still shares one aligned 96-vertex, 3 KiB command scratch. Compile
checks establish 32-byte commands and aligned header strides. Every command
field is assigned directly, including zero offset color; there is no per-vertex
`memset`. IEEE binary32 integer classification replaces out-of-line math calls
in the clipping/draw paths. Color interpolation is a convex combination of
0..255 endpoints, so per-channel libm clamping is unnecessary. Installed GCC
object inspection confirms no libm references in these two objects. This is a
code-path observation, not a measured physical-console speed improvement.

The additional cache costs 12 bytes per unique render vertex: 78,708 payload
bytes for the current 6,559 character+terrain vertices. Measured ready live heap
is 2,449,272 bytes and free VRAM is 5,302,696 bytes in stock 16 MiB Flycast.
Heap excludes embedded game assets, static storage, KOS and stacks. There is
no full expanded-frame command buffer and no per-frame allocation.

## Verification

| Evidence | Result |
| --- | --- |
| Full host suite | 215 tests pass |
| New native view/clip oracle | 4,967 checks in stock 16 MiB Flycast |
| Analytic clip cases, included above | 285 checks on host and SH-4: all inside/outside patterns, cyclic order, winding reversal, UV/color interpolation, different edge ratios, exact plane, invalid inputs and output guards |
| Original-free raster fixture | 10 checks: crossing triangle and generated quad visible, front triangle visible, behind triangle rejected, exact-plane triangle visible, background, actual clip execution and clean exit |
| GPU layer instrumentation | 261 checks still pass, including offset-color initialization, allocation failures and 96+3 vertex flushes |
| Original projection regression | 2,382 native checks still pass |
| Original-terrain movement | 18 integrated checks pass, including actual clipping, walk/run/jump/landings, held-A edge, camera, boundary respawn, memory and exit |

The raster fixture uses only an original synthetic magenta triangle and white
texture. It samples RGB pixels, rather than treating a framebuffer file's byte
count as proof of rendering. The real-game idle and airborne captures were
also visually reviewed for recognizable Jak, terrain and clipping artifacts;
those images remain private.

The movement oracle now advances phases on a monotonic 60 Hz simulation-tick
counter, independent of rendering frame count. Respawning can reset a player's
local tick field, so the test clock remains separate. An overlapping Lua-symbol
placeholder initially corrupted the harness; token-aware one-pass substitution
and two regression tests fix it. The failed attempt is not counted as native
game failure or passing evidence.

## Reproduce

With KOS and upstream SH4ZAM installed, from `ports/dreamcast`:

```sh
source /your/kos/environ.sh
make -j1 jak-dc-view-clip.elf jak-dc-clip-scene.elf jak-dc-projection.elf
python verify_backend.py /path/flycast jak-dc-view-clip.elf --nm /path/sh-elf-nm --suite view-clip --tests 4967 --output /private/check-view-clip
python verify_clip_scene.py /path/flycast jak-dc-clip-scene.elf --nm /path/sh-elf-nm --output /private/check-raster
python verify_backend.py /path/flycast jak-dc-projection.elf --nm /path/sh-elf-nm --suite projection --tests 2382 --output /private/check-projection
```

No original game assets are needed for those three targets. Rebuild the real
training target with the dedicated private bundle and use `verify_training.py`
as documented in [TRAINING_SLICE.md](TRAINING_SLICE.md). Synthetic diagnostic
targets and the movement prototype are separate from the unfinished GOAL runtime.

## Limits and next gate

This clips one depth plane; other viewport clipping remains with PowerVR. It
does not provide a collision camera, full view-frustum clipping, transparency
sorting or original PS2 rendering fidelity. Textures remain reduced and model
animation changes can pop. Grok has a separate isolated collision-boom proposal
in progress; it has not been integrated or counted as verification here. Broader
world traversal/streaming and full kernel/gameplay remain required. No hardware
validation, disc preparation or console frame-rate claim is made.
