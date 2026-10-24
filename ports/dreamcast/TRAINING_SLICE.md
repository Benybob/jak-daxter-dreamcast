# Native movement on original training terrain (AI-assisted)

This separate prototype target connects Jak's original baked skeletal clips to
original training-island rendering and collision. It implements the temporary
native C controller milestone; original GOAL gameplay and the full port remain
unfinished. The earlier checkerboard target stays available for diagnosis.

Analog stick moves (partial tilt walks, full tilt runs), A jumps, L/R orbit the
camera, and START exits. Holding A through a landing does not jump again. This
is a bounded patch: going beyond 6.8 meters from the patch origin on X/Z or
falling below -4 meters returns to the known nonoverlapping spawn.

## Current renderer update

[Near-plane clipping](NEAR_CLIPPING.md) replaces whole-triangle rejection, retains
SH4ZAM view coordinates and adds a projected cache. The current full suite has
215 host tests, 4,967 new native view/clip checks and 18 integrated training checks.
The table below preserves the original terrain-integration checkpoint.

## Integration and verification

The tested local bundle has 3,593 character triangles and 2,973 terrain triangles,
3,231 terrain vertices, 26 terrain batches, 12 terrain textures occupying 80,384
bytes, and 576 collision faces. The same six original animation clips and 92
baked poses from [PLAYABLE_SLICE.md](PLAYABLE_SLICE.md) drive the character.

| Check | Result |
| --- | --- |
| WLD2 metadata and event oracle | 416 checks on host and native SH-4/Flycast |
| GPU resource/submission instrumentation | 261 host checks: each allocation failure, consumed/unconsumed pixels, idempotent cleanup, list selection, finite vertices and 96+3 vertex flushing |
| Converter analytics | Transformed hierarchy/rotation/scale and rebase inverse, true triangle-box intersection, instances/cache, unsupported input refusal, deterministic new-directory output and scalar/NumPy agreement |
| Integrated controller | 17 checks in stock 16 MiB Flycast: walk/run, jumps, landings, held-A edge, camera, boundary respawn, geometry, memory and clean exit |
| Original collision regression | 2,063 combined native checks still pass |
| Visual review | Captured idle and airborne frames show recognizable Jak on grass among original rocks and foliage; framebuffer byte count alone does not prove visibility |

The new converter reconstructs all 3,231 terrain vertices within 1.63e-6 meters
of the earlier independently verified private prototype, with identical indices,
batches, UVs, colors and texture payloads. Differences come from rebasing before
float32 serialization. These are coordinate comparisons, not collision-fidelity
or hardware-performance measurements.

The renderer uses the existing state-preserving SH4ZAM projection twice per
frame and serial KOS `pvr_prim` submission. Two owned GPU layers share one
32-byte-aligned 96-vertex command scratch; there is no expanded complete-frame
vertex command buffer or per-frame allocation. Allocation failure releases only
owned resources; CPU texture pixels are nulled after upload, so scene cleanup
releases unconsumed pixels exactly once. Host instrumentation covers our layer
ownership and call contract, not KOS internals or GPU correctness.

Grok Build completed an isolated source-only alternate layer after the initial
checkpoint. It passed 261 host checks and compiled for SH-4 in isolation. The
integrated layer was independently implemented from our prior verified renderer
and tested in Flycast by Codex; the alternate was not integrated or native-executed.
No raw model streams, game assets or private logs are published.

## Render/collision contract

`prepare_training_scene.py` visits the active scene with accumulated node
transforms, caches source accessors, tests actual triangle/box intersection and
keeps whole intersecting triangles. It compacts selected position/UV/color
attributes and reuses the existing DCT1 material converter. Selected BLEND,
morph, skin and unsupported sampler/texture-coordinate cases fail explicitly.
Positions become collision-local meters before unit-radius DCT normalization.

`world.xfm` is WLD2, exactly 84 little-endian bytes, not a native struct dump:

| Offset | Contents |
| --- | --- |
| 0 | `WLD2`, version 2 (u32) |
| 8 | Original collision origin, three float32 values |
| 20 | Local render center/shift, three float32 values |
| 32 | Render normalization scale, float32 |
| 36 | Local spawn, three float32 values |
| 48 | DCT byte length and IEEE CRC32, two u32 values |
| 56 | DCC byte length and IEEE CRC32, two u32 values |
| 64 | Vertex/index/batch/texture/collision counts, five u32 values |

Reconstruction is `local meters = DCT position * scale + shift`. DCC positions
are already local. The loader checks finite bounded fields, exact lengths,
file CRCs, count agreement and exact collision origin before use. CRCs catch
accidentally mixed outputs; they are not authentication. Source provenance
hashes remain in the private conversion report. Bundle creation commits one
new directory, refuses to overwrite an old one, and leaves no partial bundle
after conversion failure.

Training limits are 8,192 render vertices, 8,192 render triangles, 128 batches,
64 textures with at most 512 KiB total texture pixels, 1 MiB DCT file size, and
8,192 collision faces. These are separate caps, not a full-game memory budget.
Movement also has the conservative solver/candidate bounds documented in
[WORLD_COLLISION.md](WORLD_COLLISION.md).

All entity-collidable surface events are inspected before movement. Only event
zero is supported here; a patch needing damage/death or another reaction is
refused. Noentity faces stay excluded consistently with movement. Ground and
wall/obstacle modes are retained. A clear capsule and grounded downward cast
are required at spawn; errors terminate the prototype rather than silently
continuing through unsupported geometry.

## Reproduce with your own extracted data

Use a compatible local OpenGOAL extraction: static training background GLB,
the already exported training DCC1 patch from [WORLD_COLLISION.md](WORLD_COLLISION.md),
and the original character GLB described in [PLAYABLE_SLICE.md](PLAYABLE_SLICE.md).
The DCC header supplies the rebase origin, so the converter does not require a
second independently typed origin. The tested spawn is patch-local `-4 3 -4`;
other patches need an appropriate clear spawn and matching selection extent.
Pillow is required offline; NumPy accelerates selection and is required by the
existing character baker. Neither is a Dreamcast dependency.

From `ports/dreamcast`, with upstream SH4ZAM installed in the KOS SDK:

```sh
python prepare_training_scene.py /private/training-background.glb /private/world.dcc /private/training-assets --spawn -4 3 -4
python bake_player.py /private/eichar-lod0.glb /private/player-assets
cp /private/player-assets/player.dct /private/player-assets/player.dca /private/training-assets/
source /your/kos/environ.sh
make -j1 TRAINING_ASSET_DIR=/private/training-assets jak-dc-training.elf jak-dc-world-manifest.elf
python verify_backend.py /path/flycast jak-dc-world-manifest.elf --nm /path/sh-elf-nm --suite world-manifest --tests 416 --output /private/check-manifest
python verify_training.py /path/flycast jak-dc-training.elf --nm /path/sh-elf-nm --output /private/check-training
```

On Windows, `build_dreamcast.py --msys /path/to/msys --target jak-dc-training.elf
--training-assets /private/training-assets` supplies short SDK paths and defaults
to one build job. Never run a second SH-4 build against the same checkout.
Host tests build through `build_host_windows.ps1` or `make host test`.

Keep the five asset files in a dedicated private directory; only `report.json`
is excluded by the ROM-disk rule. The files, embedding objects/ELF, frames and
private reports contain game-derived content and must not enter GitHub.

## Remaining work

The original GOAL runtime is a separate unfinished path. This prototype has
retuned native physics, bounded static collision and offline vertex-pose baking.
It does not implement original surface reactions/friction, ledges or moving
objects, combat, full-map streaming, animation blending, facial animation,
camera obstruction, audio, enemies, UI, progression, saves or a complete game
startup. Near-plane triangles now use the bounded clipper described above. Reduced textures
and frozen facial/environment effects remain visible limitations. No disc image,
physical hardware validation or measured console frame rate is claimed.
