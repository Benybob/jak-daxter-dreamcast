# Native training camera obstruction (AI-assisted)

The training prototype now queries original collision geometry before placing
the camera. An obstruction shortens the boom immediately; extension eases at
at most 6 meters per second and stays below the latest checked limit. Every
frame uses the current ray. An old world-space eye is never interpolated onto
an unchecked new ray. Original GOAL camera behavior and the full game remain
unfinished.

The pure sphere-boom query began as a human-authorized Grok Build proposal.
Codex reviewed it, corrected integration guidance, added independent analytic
and native tests, and implemented the state update and renderer integration.
The supplied `(cos(pitch)*sin(yaw), -sin(pitch), -cos(pitch)*cos(yaw))` vector
already points from target to eye: do not negate it. Player and target positions
are already collision-local meters. Only the 2.6-unit boom becomes 10.4 meters;
the checked length is divided by four for the SH4ZAM view transform.

## Query and loaded coverage

`camera_boom.c` uses a sphere with radius 0.12 meters, expressed as capsule feet
one radius below its center. It requires a clear start and uses at most two
first-hit casts of at most 7.9 meters each. A single 10.4-meter cast would exceed
the collision solver's eight-meter displacement cap. It never slides the unused
boom along a wall. A contact gets an additional 0.002-meter inset. Failure leaves
the output unchanged.

`camera_rig.c` prefers the existing smoothed/frozen floor anchor plus 0.75 meters.
If that target is invalid, it tries one independently checked target at the
player's actual height plus 0.75. It commits camera state only after checking
the new ray and reconstructed eye. Failure preserves the state and the renderer
reports an error; it does not draw using an unchecked old eye.

The original 576-face patch was not large enough for all camera rays. The new
private bundle uses 2,032 original collision faces with complete coverage inside
an origin-centered box of half-extent `(18,12,18)` meters. The character and
terrain render selection stay at 3,593 and 2,973 source triangles. Physics still
uses the bounded native capsule controller and the same 6.8-meter X/Z movement
boundary. Camera coverage is wider than the playable/rendered patch.

The exporter records its conservative whole-triangle box selection and payload
SHA256. The scene converter requires that matching report to produce **WLD3**:
the same first 84-byte layout as WLD2, with magic `WLD3`, version 3, and three
float32 half-extents at offsets 84/88/92 (exactly 96 bytes total). WLD2 remains
readable for legacy validators but has no coverage proof; the current training
camera target requires WLD3.

The rig clips the requested arm to the loaded box, inset by the sphere radius
and 0.003 meters. It refuses if there is insufficient covered room for a query.
This prevents missing collision sectors from appearing to be empty space. The
box, target and both ray endpoints bound the swept sphere; future streaming
must preserve that completeness, not just the current position's neighborhood.

## Verified scope

| Evidence | Result |
| --- | --- |
| Full host suite | 218 tests pass |
| Synthetic camera/rig | 500 host and native checks: clear arms over 8m, walls in each segment, floor/ceiling, diagonal, noentity, invalid input, overlap, 1,025-candidate refusal, atomic output, inverse view, smoothing, fallback and coverage |
| Original collider query | 705 additional host/native checks across 64 directions; combined 1,205 pass in stock 16 MiB Flycast |
| WLD2/3 input contract | 454 host/native checks, including truncated/invalid coverage and unchanged output |
| Existing movement trajectory | 20 integrated checks pass with the expanded collider and camera: movement/animation, jumps, landings, held-A edge, camera bounds/eye, near clipping, boundary respawn and exit |
| Full stationary camera orbit | 11 checks across 241 samples: actual terrain obstruction, immediate shortening, eased extension, fresh limits, loaded coverage and clean exit |

The measured orbit spans a full turn and moves the boom between approximately
6.53 and 10.4 meters. Captured occluded/orbit and airborne frames were visually
reviewed for recognizable Jak and terrain. Frame-file size alone does not prove
visibility. These tests establish native prototype behavior, not original GOAL
camera fidelity, hardware performance or full-world traversal.

Ready live heap is 2,559,928 bytes and free VRAM is 5,302,696 bytes in stock
16 MiB Flycast. Heap excludes embedded assets, executable/static storage, KOS and
stacks. The larger cached collider costs 110,656 bytes over the previous one,
plus additional private ROM-disk payload. There is no per-query/frame allocation.
The renderer retains guarded state-preserving SH4ZAM projection and its fixed
3 KiB command scratch. No physical Dreamcast or disc-image validation is claimed.

## Build from your own local extraction

Re-export the collision patch using the same origin from your existing private
training export report, a radius of 18, and the host reader described in
[WORLD_COLLISION.md](WORLD_COLLISION.md). `X Y Z` below stand for those three
meter-space origin values. Do not choose a second origin for rendering.

```sh
python prepare_collision_patch.py /private/training.fr3 /private/wide-world.dcc --reader /private/export-collision --zstd /path/libzstd --origin X Y Z --radius 18
python prepare_training_scene.py /private/training-background.glb /private/wide-world.dcc /private/training-camera --spawn -4 3 -4 --collision-report /private/wide-world.json
cp /private/player-assets/player.dct /private/player-assets/player.dca /private/training-camera/
source /your/kos/environ.sh
make -j1 TRAINING_ASSET_DIR=/private/training-camera jak-dc-training.elf jak-dc-world-manifest.elf
python verify_training.py /path/flycast jak-dc-training.elf --nm /path/sh-elf-nm --output /private/check-movement-camera
python verify_camera_orbit.py /path/flycast jak-dc-training.elf --nm /path/sh-elf-nm --output /private/check-camera-orbit
python verify_backend.py /path/flycast jak-dc-world-manifest.elf --nm /path/sh-elf-nm --suite world-manifest --tests 454 --output /private/check-manifest
```

The camera-only original-data oracle needs a separate dedicated directory
containing the expanded DCC as `world.dcc`:

```sh
make -j1 CAMERA_ASSET_DIR=/private/camera-collision jak-dc-camera-real.elf
python verify_backend.py /path/flycast jak-dc-camera-real.elf --nm /path/sh-elf-nm --suite camera --tests 1205 --output /private/check-camera-query
```

`jak-dc-camera.elf` needs no original assets and runs the 500 synthetic cases.
The Windows builder also supports `--camera-assets` for the original query
oracle. Keep conversion reports, extracted/converted geometry, frames and any
embedding binary private. GitHub receives source, synthetic fixtures and summary
measurements only.

## Remaining work

This is a sphere-boom approximation, not the original camera volume/PAT rules.
Its 0.12m sphere does not enclose every corner of the 0.2m near plane. Tight
corners, visual occluders, other camera modes, moving objects and transitions
need broader checks. Surface effects and full gameplay remain unimplemented.
The finite patch is not streaming: broader traversal requires transactional
collision/render sector loading and measured memory budgets. Grok has a separate
source-only sector partition proposal in progress; it is not integrated here.
Complete original kernel startup, scheduling/GC and the other game systems are
still required for the full port.
