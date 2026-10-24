# Temporary playable milestone (AI-assisted)

Human-approved priority, 2026-10-24: a native C movement prototype using Jak's
real model and animations, walking, running and jumping around a small test map.
The existing full-port goal remains active. This slice is not original GOAL
gameplay and does not establish full-game completion.

Acceptance requires controller-driven analog walking/running, edge-triggered
jumping, gravity and stable floor/platform collision, a follow camera, and
recognizable Jak with appropriate original animation clips. Verify idle, walk,
run, takeoff, airborne, landing, movement bounds, memory and clean START exit in
stock 16 MiB Flycast. No disc preparation or hardware validation is implied.

Implement movement as a portable fixed-step module, independently exercise its
collision/jump behavior, then connect it to a separate Dreamcast executable.
Use a small synthetic map to isolate movement from unfinished level collision.
Reuse the existing PowerVR texture path and upstream SH4ZAM transforms. Keep
animation and character data in private local outputs generated from the
user's existing extraction. Publish source and synthetic fixtures only.

The local extraction contains `eichar-lod0.glb`: 83 skin joints and 95 clips,
including stance, walk, run, jump and landing. This is a structural inventory;
conversion, appearance, sampling and memory budgets still need verification.
Facial morphs, combat, original movement tuning and complete world streaming
are outside this temporary milestone.

## Verified prototype

The temporary milestone now runs in Flycast: Jak's extracted model and six
original skeletal clips, analog walking/running, jump edges, gravity, a follow
camera, two solid platforms and a bounded checkerboard map. START cleans up.
This is a separate `jak-dc-play.elf`; the original GOAL runtime stays separate.

Controls: analog stick moves (partial tilt walks, full tilt runs), A jumps,
L/R triggers orbit the camera, START exits. Facing follows the stick. Hold A
through a landing to verify it does not automatically jump again.

| Check | Verified result |
| --- | --- |
| Host suite | 201 tests pass |
| Portable native movement | 318 checks on host and SH-4/Flycast |
| Synthetic animation input | 208 host checks, including every truncated byte length, malformed clips and interpolation |
| Independent offline math | 10 analytical checks: quaternion direction/sign, interpolation and matrix decomposition |
| Controller paths | Walk/run, airborne/landing, held-A rejection, second press, camera and START pass |
| Platform path | Wall blocks movement, jump onto platform, jump from platform, walk off and land on floor pass |
| Rendered geometry | 3,593 character triangles + 860 synthetic map triangles |
| Character data | 3,328 referenced vertices, 92 baked poses, six clips |
| Animation payload | 1,837,172 bytes, including headers; maximum accepted pose payload is 3 MiB |
| Textures | 32,768 bytes; original images resampled to at most 64 pixels per side |
| Memory at ready | 16,777,216 system bytes; 2,152,256 live allocator bytes; 5,383,464 free VRAM bytes |

Allocator figures exclude the executable, embedded approximately 2 MiB ROM
disk, stacks, static data and other KOS allocations. Both emulator trajectories
use the same ELF. Captured frames were visually checked for Jak and the map;
the framebuffer-file size check alone does not establish character visibility.
No physical hardware or performance measurements are claimed.

SH4ZAM performs the batched camera transform with the existing FPU-mode guard
and XMTRX preservation. Commands go through KOS `pvr_prim` in 96-vertex,
32-byte-aligned scratch chunks, preserving triangle order and end flags.
The fixed 3 KiB command scratch avoids a complete expanded frame buffer of
vertices. This reduces call count; it is not a measured hardware speed claim.

The first implementation bakes skinning offline into signed Q12 vertex poses
at approximately 15 Hz and interpolates positions at render time. This spends
about 1.8 MiB on poses to avoid per-frame skeletal skinning on SH-4. Grok's
source audit also proposed compact joint tracks; that is a later memory/CPU
tradeoff to measure. Bind/inverse-bind error in this extraction is below
8e-7. Decompose bind matrices and replace animated TRS components, normalize
quaternions, use shortest-arc interpolation and freeze main-root X/Z motion;
the native controller owns planar displacement. Native movement is retuned
prototype behavior, not matching original GOAL physics.

Remaining prototype limitations: animation changes can pop, facial morphs and
environment mapping are frozen/omitted, textures are reduced, camera has no
obstacle avoidance, and near-plane map triangles are dropped rather than
polygon-clipped. Collision is the synthetic map's floor, box faces and tops;
no original level collision, combat or full game systems are implied.

## Reproduce using your own local extraction

With numpy/Pillow installed in the chosen Python interpreter:

```sh
python bake_player.py /your/extraction/levels/common/eichar-lod0.glb /private/player-assets
source /your/kos/environ.sh
make PLAY_ASSET_DIR=/private/player-assets jak-dc-play.elf jak-dc-motion.elf
python verify_backend.py /path/flycast jak-dc-motion.elf --nm /path/sh-elf-nm --suite movement --tests 318 --output /private/check-motion
python verify_player.py /path/flycast jak-dc-play.elf --nm /path/sh-elf-nm --output /private/check-play
python verify_player.py /path/flycast jak-dc-play.elf --nm /path/sh-elf-nm --output /private/check-platform --platform
python verify_player_bake.py
```

The selected source uses the exporter-specific identity mesh node and named
`main` skeletal root; other skin layouts fail explicitly. `player.dct`,
`player.dca`, their embedding object/ELF and captured frames contain game-derived
content and stay local. Public GitHub contains converter/controller/verifier
source and synthetic tests only.
