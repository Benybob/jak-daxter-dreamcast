# Original training collision and native capsule controller (AI-assisted)

The native controller now executes against a bounded patch of original
training-island collision. This is a collision milestone; the committed
visible demo remains the synthetic test map. A private real-terrain renderer
prototype has also passed controller checks and visual inspection, but its
renderer integration is not part of this source checkpoint.

## What runs

`world_collision.c` sweeps a vertical capsule against triangle faces, edges
and vertices using capsule-axis distance and conservative advancement. It
retains multiple contact planes and projects remaining displacement onto their
feasible intersection, including crease motion. The same contact shell is used
for stepping and acceptance to avoid imaginary walls on welded triangle seams.
A first-hit cast handles placement and downward ground probes without sliding
the unused probe distance along a slope. Placement refuses intersecting bodies.

`player_world_motion.c` reuses the existing analog drive, jump-edge and animation
state rules. The original-free test-map controller still passes its input and
platform routes after this shared-rule refactor. World movement stages the
player state and commits it only after all sweeps/probes succeed.

Original PAT words survive ingestion. Bit 0 (`noentity`) filters entity queries.
Ground/obstacle/wall mode thresholds use 0.2/0.82/2.0 from `pat-mode-info`;
foot support also requires positive polygon up and contact coverage >=0.9.
A tagged wall does not become ground merely because it is flat. Ground PAT,
last-contact PAT, ceiling contact and all-contact event bitsets are retained.
Event effects, friction tables and original GOAL reaction code do not execute.

These are native prototype physics. Original character spheres, reaction
coverage, step handling, safe-landing search, moving objects, ledge grabbing,
damage/death and complete collision behavior still require integration.

## Measured evidence

| Gate | Result |
| --- | --- |
| Host suite | 203 tests pass |
| Synthetic capsule/controller | 789 checks pass on host and native SH-4/Flycast |
| DCC loader | 147 host checks, including every truncated length and unchanged outputs on malformed input |
| Original training patch | 2,063 combined checks pass on host and native SH-4/Flycast |
| Original ingestion | 61,576 collision triangles, FR3 version 43, exact serializer consumption |
| Selected patch | 576 whole intersecting triangles; 23,064-byte private DCC payload |
| Native patch routes | Rough rock-edge convergence and grass walk/jump/landing, held jump, valid placement and overlapping-spawn refusal |
| Collision distance work | Maximum 40 distance evaluations in either measured native route; not a hardware timing result |
| Existing visible prototype | Both controller/platform trajectories still pass, same rebuilt ELF |
| Platform | Stock 16 MiB Flycast; physical Dreamcast untested |

Grok's completed source audit distinguished standing response from safe-landing
search and informed the PAT handling. Its separate final algorithm review is
still pending at this checkpoint; it is not counted as approval. One audit
prose error is corrected here: the named material enum has 23 entries, 0..22.

## Ingestion and bounds

`export_collision_fr3.cpp` uses the existing exact `tfrag3::Level` serializer,
with its assertions enabled and complete-consumption checks. It exports original
triangle PATs and positions divided by 4096 into a host interchange file.
Zero-area triangles are excluded explicitly (none in this training file).
It is a host helper for the user's validated local extraction, not a general
untrusted-file service.

`prepare_collision_patch.py` decompresses the FR3 through an installed libzstd,
calls that helper in a private temporary directory, selects whole triangles
intersecting a local box and rebases them around a supplied meter-space origin.
The Dreamcast loader validates DCC1 lengths, count, coordinates, triangle area
and supported modes before exposing the cache. Caller-owned world/origin outputs
remain unchanged when loading fails.

Limits: 8,192 cached triangles; 1,024 swept-AABB candidates; 48 advancement
iterations per triangle; eight contact planes/slides; eight units per move.
The query scans cached AABBs, then tests only overlapping candidates. Movement
does not allocate or call external functions. Exceeding a bound refuses the
move without committing the result/player. It does not repair arbitrary spawn
penetration. Coordinate and mode support is deliberately bounded; broader
world streaming and spatial indexing remain future work.

## Reproduce

From `ports/dreamcast`, build the host reader using the existing project source:

```sh
g++ -std=c++20 -O2 -DFMT_HEADER_ONLY -I../.. -I../../third-party/fmt/include export_collision_fr3.cpp ../../common/custom_data/Tfrag3Data.cpp -o build/export-collision
python prepare_collision_patch.py /your/out/jak1/fr3/training.fr3 /private/world-assets/world.dcc --reader build/export-collision --zstd /path/to/libzstd --origin -1159.3880615234375 15.470051765441895 1001.1458129882812 --radius 8
source /your/kos/environ.sh
make WORLD_ASSET_DIR=/private/world-assets jak-dc-world.elf jak-dc-world-real.elf
python verify_backend.py /path/flycast jak-dc-world.elf --nm /path/sh-elf-nm --suite collision --tests 789 --output /private/check-world
python verify_backend.py /path/flycast jak-dc-world-real.elf --nm /path/sh-elf-nm --suite collision --tests 2063 --output /private/check-world-real
```

The original-patch oracle expects the documented training patch, including its
576 faces and two measured local routes. The synthetic target and host suite
need no game assets. DCC/DCR/decompressed FR3, embedding objects/ELFs and private
logs remain local; GitHub receives source and synthetic tests only.
