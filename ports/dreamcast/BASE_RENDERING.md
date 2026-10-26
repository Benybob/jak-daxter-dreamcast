# Cached background base-pass rendering

2026-10-25. (AI-assisted). The cached geometry now reaches PowerVR through
SH4ZAM world transforms, full viewport clipping and independently resident
textures. This is a separate renderer diagnostic. The walking prototype still
uses its earlier bounded terrain path; integration is the next step.

## Verified behavior

An original training-area window contains **3,547 unchanged source faces**.
Its independent payload CRC matches on SH-4; the base pass submits 1,759
triangles after clipping and sidedness, including 220 clipped polygons.
All 70 color maps remain resident (392,192 requested VRAM bytes). The CPU
catalog retains strength maps and other material inputs. Forty-two catalog
materials have secondary maps; this pass does not shade those reflections.

The original window and an original-free procedural scene each pass **42 native
ownership/readiness checks** in stock 16 MiB Flycast. Every bank/frame allocation
failure preserves active owners and releases partial candidates. Complete
replacement owners can coexist and be freed independently. Invalid cameras
invalidate readiness, preventing stale-view drawing. CPU resources and borrowed
GPU textures are released after PVR completion; allocator bookkeeping is primed
before comparing free VRAM. The original frame was visually reviewed locally.

Six procedural framebuffer samples independently check opaque texture alpha,
MASK holes, single-sided backface removal, double-sided backface visibility,
depth and a large triangle clipped across the viewport. These are actual
RGB565 framebuffer samples. They are not PS2 image-equivalence tests.

The full host suite passes **367 tests**, including eight newly reviewed Grok
cache boundary tests and five independent full-frustum tests.

## Geometry and lifetime

`render_world_view` transforms packed 116-byte faces directly, with one SH4ZAM
matrix borrow for the entire window. Target subtraction precedes rotation,
preserving small camera-relative offsets near world coordinates ±1,000,000.
Four world metres equal one existing actor/view unit. No intermediate XYZ
deinterleave buffer is allocated. The caller owns the output view cache.

164,852 native checks cover 15 camera cases, including large finite angles,
all 24,576 vertices of a maximum window, near/behind projection masks, capacity,
output fences, exact caller XMTRX bits and FPU controls. SR.FD and FPSCR.PR/SZ/FR
are rejected by an integer assembly gate before the floating C ABI. Matrix
preservation includes trigonometric setup; arithmetic status flags may change.

`render_frustum` clips against the near plane and all four 640×480 viewport
planes. A single-precision error interval handles clear plane sides; ambiguous
boundaries and crossing edges use binary64. The interval does not expand the
visible volume. Raw UV and color channels interpolate before final rounding.
Output is bounded by eight vertices, with winding retained and no face splitting
in the stored package. 12,620 native checks compare 256 original-free cases
against exact rational barycentric half-space hulls. Host checks exercise 384
cases, the eight-vertex case, long faces and adjacent boundary values.

`render_base_bank` owns CPU headers/metadata and borrows resident texture
addresses. `render_base_frame` owns a view cache and stable material-group
indices, borrowing immutable window faces. Neither frees catalog pixels or
another owner's geometry. Prepare each frame once, then submit its OP and PT
groups. Unclipped triangles reuse cached projection; clipped polygons use bounded
fans. Header and command arrays meet KOS's 32-byte store-queue alignment.

Build a separate candidate cache/frame pair before replacing either live owner.
Wait for the last PVR frame before releasing borrowed geometry or textures.
The measured original **frame/bank allocation peak is 529,236 bytes**, including
a complete replacement frame beside the active frame. It excludes catalog,
cache/decoder memory, fixed frame structs, allocator overhead, executable,
ROM disk, KOS/stacks and GPU. It is not a full application RAM budget.

## Reproduce without original data

From `ports/dreamcast`, with the documented KOS/SH4ZAM SDK:

```sh
python3 make_render_base_fixture.py build/private-render-base-assets --header build/render_base_fixture.h
make jak-dc-render-base.elf
python3 verify_backend.py /path/to/flycast jak-dc-render-base.elf --nm /path/to/sh-elf-nm --suite render-base --tests 42 --capture --timeout 120 --output build/base-check
python3 check_render_base_capture.py build/base-check --fixture build/private-render-base-assets/BASE_CASE.json
```

Use a new fixture directory. Optional `--real`, `--query x0 x1 z0 z1` and
`--camera x y z yaw pitch distance` select a privately prepared original window.
The generated header binds its complete payload CRC and catalog source SHA.
Make refreshes both ROM disk and binding object, including restored older headers.
Never upload private fixtures, generated headers or embedding ELFs.

The independent math targets are `jak-dc-world-view.elf` and
`jak-dc-frustum.elf`, verified with suites `world-view`/`frustum` and test counts
164852/12620. Windows builds accept `--render-base-assets`.

## Remaining work

This is the **base pass**, using the packet's baked ARGB1555 colors and MASK
alpha. Absent-texture MASK is explicitly refused. Secondary maps remain intact;
original FR3 normals/proto tint, environment coordinates, original draw modes,
runtime time-of-day/fog and reflection passes still need implementation and
verification. Static GLB flat normals are not original ETIE lighting.

Controller-driven cache replacement, streaming scheduling and performance,
full original GOAL gameplay, progression and the complete game remain unfinished.
Emulator timings are diagnostic; physical Dreamcast hardware remains untested.
See [RENDER_CACHE.md](RENDER_CACHE.md), [RESIDENT_TEXTURES.md](RESIDENT_TEXTURES.md)
and [SH4ZAM.md](SH4ZAM.md).
