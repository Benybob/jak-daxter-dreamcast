(AI-assisted)

# Original camera boot probe

ISC. `jak-dc-arena-camera.elf` implements the first probe step of
[GAME_CAMERA_ROUTE.md](GAME_CAMERA_ROUTE.md). It uses the original GAME.CGO
startups, dispatcher and display loop, with the arena's empty PVR scenes.
It does not link `arena_game.o`, the C pose script, flythrough objects or beach
assets. The existing target recipes retain their original object lists.

The probe installs after GAME.CGO and before the first dispatcher call. It
requires exact loader exports for `update-camera` and `cam-free-floating-move`.
It wraps the `update-camera` symbol using a separately named native trampoline,
then calls the saved original SH-4 export with its zero-argument, object-return
ABI. The existing display-loop reads that symbol cell. Only the original
`*dproc*` fiber may enter this wrapper. It neither calls `cam-start` nor spawns
another camera. The original binding is restored on successful completion.

The wrapper writes **one diagnostic sentinel**, -12345.0, into math-camera's
`trans.w` immediately before its first original update call. Original camera
code must replace it with 1.0. This makes a store observable even with neutral
input; an unchanged constructor pose alone cannot pass. `original_wrote_camera`
is set only after that original call returns without a runtime error and its
output replaces the sentinel. Later calls must also produce `trans.w=1` and
finite translation/inverse matrix lanes. No C code supplies a usable pose or
restores the sentinel after a failure. This witnesses that output lane, not
every camera store or input-driven motion. `pose_changes` excludes the sentinel
and compares completed original samples with one another; zero is legitimate.

The separate bridge object observes actual resume attempts and successful
`DC_FIBER_YIELDED` returns for camera master, slave and combiner. Root samples
also capture waiting/skipped processes. Completion requires the startup's
master/combiner identities, exactly one registered original slave, actual
resumptions of all three, and a slave suspended in `cam-free-floating`.
It samples masks, selected controller, validity/read flags, mode and display
time ratio. Controller counters observe the real cpad service boundary;
`valid_services` counts the selected controller only. There is no synthetic
input. Connect a Maple controller; absence deliberately fails rather than
switching to a script. The first three dispatches may precede controller
initialization; readiness is required at completion.

All new runtime behavior is guarded by `DC_ARENA_CAMERA`. The new target uses
separate bridge, kernel-case and coexistence objects and the existing arena
romdisk placement `.dc_stage=0x8cd80000`. Its trampoline uses the normal module
loader and cache synchronization. Main-arena reads validate allocated spans;
math-camera additionally validates its tag, metatype and allocation size.
PVR work still follows the existing KOS owner/root-stack checks.

## Reviewer commands (full build/device commands not run here)

From the repository root, append tools to PATH and run the host tests:

```powershell
$env:PATH += ';C:/msys64/mingw64/bin;C:/Users/sullins server/Documents/Codex/2026-10-20/continue-our-jak-and-daxter-dreamcast/work/dc-sdk/sh-elf/bin'
python -m unittest discover -s ports/dreamcast/tests -p 'test_disc_*.py' -v
python -m unittest discover -s ports/dreamcast/tests -p test_arena_camera.py -v
python -m unittest discover -s ports/dreamcast/tests -p test_arena_game.py -v
```

In the reviewer's configured KOS shell, from `ports/dreamcast`, with the staged
326-module GAME.CGO boot assets:

```sh
make jak-dc-arena-camera.elf
python3 verify_backend.py "$FLYCAST" jak-dc-arena-camera.elf --nm "$NM" --output build/arena-camera-review --suite arena-camera --tests 1140 --timeout 300 --dump dc_arena_camera_metrics 172
python3 -c 'import struct; from pathlib import Path; w=struct.unpack("<43I",Path("build/arena-camera-review/dump.bin").read_bytes()); print(dict(zip(("version","producer","reason","dispatches","update_calls","update_returns","diagnostic_seeds","original_wrote_camera","pose_changes","camera"),w[:10]))); print("master",w[22:29],"slave",w[29:36],"combiner",w[36:43])'
```

Expected successful backend count: **1140/1140**, source-derived, not executed
here. Arithmetic: existing arena 870 (528 GOAL + 10 stage + 4 prepare + 64*5
dispatch wrapper + 8 finish wrapper), plus 6 probe installation + 64*3 root
diagnostic checks + 61 original update output checks + 11 probe finish checks.
There are 3 initial dispatches, 60 display dispatches and 1 final dispatch;
the display process still runs on the last one, giving 61 updates, 64 clears.
Any early refusal has a smaller count and backend state 3 with its first
failure description. A timeout is not success.

`dc_arena_camera_metrics` is 172 bytes / 43 little-endian u32 words:

| Words | Contents |
| --- | --- |
| 0-9 | version=1, producer=1 (original-probe mode), reason=0 on success, dispatches=64, update_calls=61, update_returns=61, diagnostic_seeds=1, original_wrote_camera=1, pose_changes (observed, no required count), math-camera address |
| 10-14 | kernel prevent mask, selected pad index, selected pad validity byte, total pad service calls, valid selected-pad service calls |
| 15-19 | read-analog GOAL boolean, read-buttons GOAL boolean, master-mode symbol, float bits of time-adjust-ratio, registered slave process address |
| 20-21 | runtime error and first error site (file code / source line), captured before subsequent lookups |
| 22-28 | master: process, status symbol, state object, mask, resume attempts, successful yielded resumptions, free-floating frames (slave only) |
| 29-35 | slave: same seven fields |
| 36-42 | combiner: same seven fields |

`producer=1` identifies this probe's configured mode, not proof of execution.
Use `original_wrote_camera`, update returns, process counters and backend result
together. Reasons: 1 missing export/install, 2 schema/output, 3 original update
fault/count, 4 no witnessed output store, 5 process/state/mode path, 6 no serviced
valid selected controller, 7 dispatcher/root failure. The backend description
states the failing check. A fault earlier in the original boot uses the existing
boot failure description; it may precede probe installation entirely.

## Checks run here

- `python -m unittest discover -s ports/dreamcast/tests -p 'test_disc_*.py' -v`:
  12 passed. Four temporary source mutations were rejected by their specific
  assertions: overwrite the previous completed destination on poll, route all
  queued payloads to slot zero, copy only one byte of the successful failure-path
  prefix, and publish FAILED as READY. Repository reader code was not mutated.
- `python -m unittest discover -s ports/dreamcast/tests -p test_arena_camera.py -v`:
  1 passed. Labeled host doubles exercise the output-store witness, stationary
  samples, no-store/error/nonfinite/schema/wrong-fiber/missing-export refusals,
  controller pointer-array indexing and selected-controller service counters.
  They never execute original GOAL or SH-4 code. During development, the extended
  host fixture initially requested allocations smaller than its declared type;
  the runtime refused them. The fixture was corrected to allocate its full type.
- `python -m unittest discover -s ports/dreamcast/tests -p test_arena_game.py -v`:
  4 passed, including the existing C checks at O2 and fast-math.
- `python ports/dreamcast/build/arena-camera-validation/compile_objects.py`:
  single objects compiled with the requested `dc-sdk/sh-elf/bin/sh-elf-gcc`,
  O2, C99, Wall/Wextra/Werror, m4-single/ml/mfsrra/mfsca, KOS includes and
  DC_ARENA_COEXIST/DC_ARENA_CAMERA (plus DC_KERNEL_DGO for kernel cases).
  Sources: arena_camera.c, goal_bridge.c, arena_coexist.c, kernel_boot_cases.c,
  disc_reader.c and disc_reader_kos.c. Outputs stay in that validation directory.
- `sh-elf-nm -S .../arena_camera.o`: camera metric size is 0xac (172).
  `sh-elf-nm -u .../goal_bridge.o`: the camera pad/resume hooks are imported.
  `python ports/dreamcast/verify_backend.py --help`: accepts `arena-camera`.
- SHA-256 comparison with the pre-edit snapshot: all seven existing ELFs are
  unchanged. Staged artifact inspection confirmed update-camera's zero-argument
  object-return ABI and display-loop's `cell:update-camera` relocation.
  `git diff --check` passed; the notes' required first line was checked.

## Unverified

Final ELF link, image/stack/heap fit, added trampoline stack use, actual camera
initialization/resumptions, original output writes, controller readiness, 61
updates, all 1140 backend checks and PVR clears are unverified on target.
The expected counts are conditional on those original paths completing.
Input response, neutral-input stability on device, affine renderer readback,
beach rendering/retargeting, FOV/roll, original levels, collision and Jak-follow
gameplay remain later steps. The host doubles are probe-logic tests only.
