(AI-assisted)

# jak-dc-arena-game.elf

ISC. C-scripted original GOAL camera readback, between dispatcher returns. This is
a Milestone 3 plumbing demonstration, not player-controlled gameplay. No full build
or Flycast run was performed here.

## Original camera

`goal_src/jak1/engine/camera/camera-h.gc` defines `camera-slave`: its inline
`trans` is the eye position; `tracking.inv-mat` holds its orientation. Slaves
feed `camera-combiner` (`cam-combiner.gc`), which blends their positions, FOVs
and inverse rotations. `cam-update.gc`, `update-camera`, copies the combiner's
`trans` and `inv-camera-rot` to `*math-camera*`, adds the camera smush Y offset,
and transposes the inverse into `camera-rot`. Later in the same function it
sets `camera-rot` vector 3 to the rotated negative eye and `inv-camera-rot`
vector 3 to `trans` (`cam-update.gc:353-365`). Thus the final original matrices
are affine, not merely transposes of one another. External cameras can override
the pose copy. Reading a slave directly would miss those steps.

`engine/gfx/math-camera-h.gc` defines the final `math-camera` basic object.
`math-camera.gc` constructs `*math-camera*` with zero XYZ, W=1 and identity
rotation. The original layout, relative to the **tagged basic pointer**:

| Field | Byte offset | Read/write |
| --- | ---: | --- |
| type tag | -4 | read |
| camera-rot | 364 | 16 float transpose read/validate/write |
| inv-camera-rot | 428 | 16 float read/write |
| trans | 844 | 4 float read/write |

Raw allocation size is 1060 bytes, including the four-byte basic tag. These
numbers follow the original field alignment, not a C struct packing rule. The
host test independently walks that GOAL declaration and checks all three
offsets and the size. The existing typed frontend's constructor IR also uses
364, 428 and 844; its deftype flags have low 16 bits 1060. No GOAL source or
DCSH object is changed.

`cam-interface.gc` identifies `inv-camera-rot` as local-to-world and
`camera-rot` as world-to-local. `engine/geometry/geometry.gc`,
`forward-down->inv-matrix`, explicitly makes vector 2 forward (+Z) and down
-Y. Vector 0 is right, vector 1 is up. GLB export preserves XYZ and divides
positions by 4096 (`decompiler/level_extractor/fr3_to_gltf.cpp`). No axis swap
or handedness flip is needed.

## Conversion

Let P be `trans.xyz`, and F be inverse-rotation vector 2 (matrix lanes 8,9,10).
The inverse is column-major. For a representable, roll-free pose:

```text
eye = P / 4096                        # GOAL units -> world meters
h = sqrt(F.x*F.x + F.z*F.z)
yaw = atan2(-F.x, F.z)                # radians
pitch = atan2(F.y, h)                 # radians
right = (cos(yaw), 0, sin(yaw))
up = (sin(pitch)*sin(yaw), cos(pitch), -sin(pitch)*cos(yaw))
forward = (-cos(pitch)*sin(yaw), sin(pitch), cos(pitch)*cos(yaw))
target = eye + forward * 55           # renderer's orbit anchor, meters
distance = 55
view = 0.25 * (dot(right, world-target),
               dot(up, world-target),
               dot(forward, world-target) + distance)
```

The 55 m anchor is an adapter to the existing renderer, not a GOAL field. It
places the rendered eye at the original GOAL eye. Four world meters equal one
view unit. Terrain and instances receive the same target, yaw, pitch and
distance. Instance streaming/LOD selection stays centered on that derived
anchor, matching the existing instance pass. The terrain query is anchor XZ
plus/minus the fixture's half-width (14 m for the current beach fixture).
Instance draw distance is the fixture's 80 m. Both radii must be positive and
at most 128 m. Neither query position nor draw orientation comes directly from
a fixture frame after readback. Projection stays the renderer's fixed
390-pixel focal length; GOAL FOV, projection, smoothing and roll are not ported.

`dc_arena_game_convert` reconstructs all 16 inverse lanes from yaw/pitch.
Deviation greater than 0.0001 rejects roll, scale, shear, reflection or broken
homogeneous lanes. Horizontal forward length below 0.001 rejects pole
ambiguity. The current yaw/pitch renderer cannot honestly represent roll. This script
adapter also requires a pure rotation inverse (vector 3 = (0,0,0,1)) and a full
transpose in `camera-rot`. It is not yet a reader for the translated affine
matrices produced at the end of original `update-camera`; see the route document.

## Scripted fallback

The headless boot has no loaded level or target. `cam-start.gc` already calls
`cam-start #f` at top level and can create master/combiner processes; this
change does not claim that they are absent or spawn duplicates. The demo
requires the observed post-dispatch pose to be the constructor origin/identity
or the previous scripted pose (float equality, allowing signed zero). Any
other pose fails `arena game pose is idle or previous C-script` before writing.
This guard only recognizes those two sampled poses; it does not prove what
processes ran between samples. No headless camera sample was collected here.

After that check, the C producer writes the original `*math-camera*` fields:
eye = fixture target minus forward times 55, multiplied by 4096; W=1; the
inverse basis above; and its transpose in `camera-rot`. Across each 16-frame
path it adds 0..0.02 radians yaw and 0..0.01 radians pitch. This is deliberately
scripted through the original GOAL layout, not an original camera process
following Jak. The renderer reads the object again, validates it, compares the
stored translation and inverse with the proposed script, validates every
`camera-rot` lane against the inverse transpose, then converts only that
readback. A missing or untransposed `camera-rot` write now refuses on device.
Both C-scripted position and C-scripted orientation readbacks must change on
all 63 transitions across 64 dispatches, including each path wrap. The counters
measure the C producer, not an autonomous GOAL camera process. The source flag
is assigned by C; it is not a GOAL scripted-camera flag.

`goal_bridge.c` binds the live runtime while installing PC services, only with
`DC_ARENA_GAME`. Symbol lookup waits until GAME.CGO has loaded. The original
`*math-camera*` symbol value is fetched each frame. The C read/write helper
does not allocate, call GOAL, yield or walk the process tree. The existing
coexistence hook requires the restored KOS owner/stack and no current fiber
before and after drawing; it also checks unchanged GOAL bump usage.

## Validation and ownership

Every consumed float is checked by exponent bits before arithmetic, including
with fast-math. Translation XYZ must be finite and within +/-4,096,000,000
GOAL units (+/-1,000,000 m); W must equal 1. Every inverse lane must be finite
and within +/-1.001, then pass reconstruction. Converted targets must stay
within +/-1,000,000 m. Adapter distance is finite, 0..1000 m.

Before any field access, the reader checks a healthy runtime, tagged-pointer
alignment, nonwrapping arena limits, and the full raw camera/type-header spans
inside `[base, base+used)`. It reads via `rt->memory + checked_offset` and
`memcpy`, never a cast from an arbitrary GOAL address. Extension/VRAM, free
bump tail, truncated objects, wrong camera tag, wrong type tag or a type size
other than 1060 refuse. Translation, inverse and rotation are copied/validated; only translation and
inverse are returned in the pose. Type parent,
methods, FOV and other camera fields are not read. The allocated arena and
runtime descriptor themselves remain trusted; there is no per-object global
allocation ledger to prove that an in-range, correctly tagged object is live.

Failed reads/conversions leave their output unchanged. Failed writes validate
all input and both destination spans before changing any field. Refusal becomes
a counted backend CHECK failure before geometry or a scene is submitted.

The new target reuses the fly ELF's staging, slab, PVR and renderer objects.
`arena_game.c` implements the existing `dc_arena_fly_frame/finish` seam in its
own object; `arena_fly.o` is not linked. The old three ELF recipes are unchanged.
`kernel_boot_cases.c` only selects the game report under `DC_ARENA_GAME`.
No edits to `arena_coexist.c`, `arena_fly.c` or the concurrent design document.

The game renderer adds 64 face slots, 128 vertex slots, 256 remap slots, 32
instance records and 65536 cache bytes over the fixture caps. This covers small
query changes caused by float round trips without changing the old target's
budgets. These are still bounded buffers; an oversized window or cell set
fails its existing check. Terrain/instance count and CRC expectations from the
fixture are not camera oracles here. They are replaced with nonempty geometry
checks plus an orientation-sensitive terrain oracle after prepare. For every
actual terrain vertex, the oracle computes `0.25 * dot(inv axis, world - eye)`
directly from the readback GOAL eye/basis, independently of draw yaw, pitch,
target and distance, and compares all three prepared view coordinates.
Tolerance is 0.001 view units plus 0.00001 times the absolute expected value.
It requires at least three vertices and at least one with absolute view X or Y
greater than one, refusing empty or central-axis-only samples. Host negative
cases change draw yaw by pi, reverse pitch, and shift the anchor while leaving
the GOAL pose unchanged. The query rectangle alone is not an orientation oracle.
The original package CRC, invalid-geometry, slab, PVR and vertex-buffer checks
remain. Use `--suite arena-game`; the old `flythrough` scope promises fixture
counts/CRCs that this target does not check.

`dc_arena_game_metrics` retains the fly metrics' 80-byte word layout with the
new symbol name. `dc_arena_game_script_camera_metrics` is 40 bytes: five u32
words (camera address, c_scripted=1, c_script_reads=64, c_script_moved=63,
c_script_turned=63), then five floats (eye XYZ in meters, yaw and pitch in
radians). These names explicitly identify C-scripted motion. The backend report
begins `BOOT C-script=` and reports this file's checks, not the full backend
total. Read count remains diagnostic; the redundant `reads==64` finish CHECK
was removed because successful frame completion already implies it.

## Checks run here

- `python -m unittest discover -s ports/dreamcast/tests -p test_arena_game.py -v`:
  4 tests passed. C groups: 49/49 conversion/actual renderer checks, 189/189
  hostile-float checks, 93/93 native-memory checks, at `-O2` and again with
  `-ffast-math`: 662 C assertions. The fourth Python test checks original layout.
  The old zero-distance equality assertion is replaced by a nonzero-distance,
  translated mixed-axis pose with numeric expected eye/anchor coordinates.
  Memory checks corrupt each of the 16 rotation lanes and test the untransposed
  inverse, checking refusal and unchanged output.
- Three single SH-4 objects compiled using the requested
  `work/dc-sdk/sh-elf/bin/sh-elf-gcc`: game renderer (with slab redirect), game
  bridge and game kernel cases. Flags included `-O2 -std=c99 -Wall -Wextra
  -Werror`, `-m4-single -ml -mfsrra -mfsca`, KOS includes and game defines.
  Outputs are under `build/arena-game-review-fixes/`; existing objects/ELFs
  were not overwritten.
- `sh-elf-nm -u build/arena-game-review-fixes/arena_game.o`: direct `_snprintf`
  import is present. File and aligned-allocation calls use `_dc_arena_*`
  redirects; no direct libc heap allocation imports were listed. This object
  is **not stdio-free**. Listing: `arena_game-undefined.txt` in that directory.

- `python verify_backend.py --help`: the new `arena-game` suite is accepted;
  no verifier run was started.
- Source CHECK arithmetic: 17 init, 2 once-only lookups, 64*(8 camera + 17 draw),
  6 finish = 1625 renderer checks; adding 813 existing checks gives 2438.
- SHA-256 comparison against the pre-edit snapshot: all seven existing ELFs
  unchanged. Route source-link targets/line bounds and its first line checked.

## Exact reviewer commands and expected count

From the repository root, host tests (host GCC required):

```sh
python -m unittest discover -s ports/dreamcast/tests -p test_arena_game.py -v
```

From `ports/dreamcast`, in the reviewer's configured KOS shell, using the
existing 16-frame beach fixture. If it must be regenerated, set BACKGROUND_GLB
and PACKAGE_DCI2 to the reviewer's beach inputs and run this first:

```sh
python3 make_flythrough_fixture.py "$BACKGROUND_GLB" "$PACKAGE_DCI2" build/arena-fly-disc/fly --frames 16 --length 6 --half 14 --draw-files --instance-cells --header build/arena_fly_fixture.h
```

Then build and run with the reviewer's Flycast and SH-4 nm paths:

```sh
make jak-dc-arena-game.elf
python3 verify_backend.py "$FLYCAST" jak-dc-arena-game.elf --nm "$NM" --output build/arena-game-review --suite arena-game --tests 2438 --timeout 300 --dump dc_arena_game_script_camera_metrics 40
python3 verify_backend.py "$FLYCAST" jak-dc-arena-game.elf --nm "$NM" --output build/arena-game-render-review --suite arena-game --tests 2438 --timeout 300 --dump dc_arena_game_metrics 80
```

Expected success count: **2438/2438** backend checks, **1625/1625** game
renderer checks, 64 scenes, camera metrics c_scripted=1, c_script_reads=64,
c_script_moved=63, c_script_turned=63. Arithmetic: 528 GOAL + 15 stage + 5 prepare + 2 mount + 256 dispatch
wrapper + 7 finish wrapper + 17 renderer initialize + 2 camera lookup once +
64*(8 camera + 17 draw) + 6 renderer finish = 2438.
Relative to the reviewed 2375: +64 terrain-view CHECKs, -1 redundant read-count
CHECK. Renderer total is 17 + 2 + 64*25 + 6 = 1625. Early refusal has a smaller
count and state 3. This expected count is source-derived, not emulator-verified.

Assumptions: the current compiled GAME.CGO and original math-camera layout;
origin/identity or previous seed after each headless dispatch; the same beach
assets and 16-frame fixture; existing fly VRAM staging and 512 KiB vertex buffer;
the KOS main stack and frontend stack configuration that ran arena-fly. This
ELF has the same `.dc_stage=0x8cb00000` and `.dc_fly` placement as arena-fly.
All mounts, slab ownership and DMA loans retain their existing runtime checks.

Unverified: final link and image/stack/heap fit; actual headless camera samples
and the stationary-pose guard; all 2438 backend checks; PVR/VRAM usage, added
slab headroom and draw submission size with the yaw/pitch changes; framebuffer
appearance and capture timing; ordinary player/level-driven camera operation,
FOV, roll, smoothing and cutscenes. No Flycast or full build was run here.

## Review disposition

All six findings in `ARENA_GAME_REVIEW.md` are accepted. Script metrics, check
names and backend description now identify C-scripted motion; rotation readback
is enforced; actual prepared terrain has a basis/eye oracle; the implied read
count assertion is removed; the zero-distance assertion is replaced; and the
stdio claim is corrected from the new undefined-symbol listing. That review's
2375-check run describes the older code, not validation of these fixes.

See [GAME_CAMERA_ROUTE.md](GAME_CAMERA_ROUTE.md) for the source-traced original
camera route. That route is documentation only.
