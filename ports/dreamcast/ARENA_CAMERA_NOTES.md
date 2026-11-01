(AI-assisted)

# Original camera boot probe

ISC. Handoff updated 2026-11-01. This is the first probe from
[GAME_CAMERA_ROUTE.md](GAME_CAMERA_ROUTE.md), using original GAME.CGO startups,
dispatcher and display-loop with empty PVR scenes. No C pose script, beach
rendering, duplicate camera spawn or synthetic pad input is used.

## Recorded failure and fix

`build/arena-camera-run.txt` records build exit 0, backend state 3,
1135 passed out of 1136 checks reached, failed case 11136:
`camera original update-camera completed all 61 display dispatches`.
`build/arena-camera-review/result.json` agrees and records ELF SHA-256
`12b28e43df413cd5eb9b8f6fbe66bcb7f1912a5c55170974c5cf74e7af49dc25`.
These are the owner's prior results, not a run performed this session.
The saved directory has no `dump.bin`; an attempted read failed. No numeric
update counter was read from that run.

The cause is the dispatch schedule. Original `display-loop` in
`goal_src/jak1/engine/game/main.gc` suspends after initialization, before the
`while *run*` loop containing `update-camera`. The old 60 display dispatches
provide 59 updates, followed by one update on the post-deactivation dispatch:
60 rather than the required 61. This is source-derived, consistent with the
saved total (60 output checks before the finish failure). The previous notes'
61-update/64-dispatch prediction was incorrect.

The fix preserves the exact 61-call/61-return requirement. In
`kernel_boot_cases.c`, only `DC_ARENA_CAMERA` now allows 61 display dispatches:
one initialization followed by 60 loop iterations. The existing final
post-deactivation dispatch supplies the 61st update. Including the three
initial dispatches, `arena_camera.c` now requires exactly 65 dispatcher returns
and `arena_coexist.c` requires exactly 65 PVR clears. Every other check remains
intact. Other modes retain 60 display dispatches and 64 clears.
No next-step mode was added because the recorded probe failed.

`tests/arena_camera_check.c` adds labeled host doubles for the finish contract:
60, excess and mismatched update counts fail; 64/66 dispatches fail; exactly
61 updates/65 dispatches passes and restores the original update binding.
These counters are fixtures, not evidence of original camera execution.

## Probe behavior and metrics

All probe behavior remains under `DC_ARENA_CAMERA` in separately compiled
bridge/kernel/coexistence objects. The target retains its object list and
`.dc_stage=0x8cd80000`. It excludes `arena_game.o`, flythrough and beach assets.
The wrapper requires exact loader exports for `update-camera` and
`cam-free-floating-move`, installs a native trampoline through the module
loader/cache synchronization, and calls the saved original zero-argument,
object-return export only from the original `*dproc*` fiber.

Before the first original call, the wrapper writes a single diagnostic
-12345.0 sentinel to math-camera `trans.w`. Original code must replace it with
1.0 without a runtime fault. Later samples must retain finite translation/
inverse lanes and `trans.w=1`; pose-change counts exclude the sentinel.
No usable pose is supplied by C. Zero pose changes can pass with neutral input.
The original binding is restored only on successful completion.

Completion requires the startup's master/combiner identities, exactly one
registered original slave, observed successful yielded resumptions for all
three, and a slave suspended in `cam-free-floating`. It also requires a serviced
valid selected Maple controller, enabled read flags, game mode and finite
positive display time ratio. Connect a controller; absence deliberately fails.
Memory-span, math-camera type/size, root-stack and KOS owner checks remain.

`dc_arena_camera_metrics` retains its 172-byte / 43 little-endian u32 ABI:

| Words | Contents |
| --- | --- |
| 0-9 | version=1, producer=1, reason, dispatches, update_calls, update_returns, diagnostic_seeds, original_wrote_camera, pose_changes, camera address |
| 10-14 | kernel prevent mask, selected pad index, validity byte, pad services, valid selected-pad services |
| 15-19 | read-analog boolean, read-buttons boolean, master-mode symbol, float bits of time ratio, registered slave |
| 20-21 | runtime error, first error site (file code / source line) |
| 22-28 | master: process, status, state, mask, resume attempts, yielded resumptions, free-floating frames (slave only) |
| 29-35 | slave: same seven fields |
| 36-42 | combiner: same seven fields |

Producer 1 identifies the configured probe, not proof of execution. Interpret
it with the output-store witness, update returns, process counters and backend
result. Reasons: 1 export/install, 2 schema/output, 3 update fault/count,
4 no witnessed store, 5 process/state/mode, 6 controller, 7 dispatcher/root.

## Disc-reader dialog fix

`tests/test_disc_reader.py` links both reader and mutant executables with
`-static` on Windows, removing external MinGW runtime DLL requirements.
The common `tests/disc_reader_check.c` main calls `SetErrorMode` with
`SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX` before
running a group, suppressing Windows crash/error dialogs. Existing assertion
diagnostics, nonzero-exit and specific-assertion checks remain. Temporary
mutations never modify repository `disc_reader.c`.

## Checks actually run this session

All tool paths were appended to PATH, never prepended.

- Source/file reads and read-only `git diff` inspection.
- `python -m unittest discover -s ports/dreamcast/tests -p 'test_disc_*.py' -v`:
  12 passed; all four temporary mutants rejected by their specific assertions.
- `python -m unittest discover -s ports/dreamcast/tests -p test_arena_camera.py -v`:
  1 passed, including the new strict finish-count cases. Host doubles only.
- `python -m unittest discover -s ports/dreamcast/tests -p test_arena_game.py -v`:
  4 passed, including O2/fast-math checks.
- `python ports/dreamcast/build/arena-camera-validation/compile_objects.py`:
  six single objects compiled using the requested dc-sdk SH-4 GCC:
  arena_camera.c, goal_bridge.c, arena_coexist.c, kernel_boot_cases.c,
  disc_reader.c, disc_reader_kos.c. O2/C99/Wall/Wextra/Werror, KOS includes,
  m4-single/ml/mfsrra/mfsca, DC_ARENA_COEXIST/DC_ARENA_CAMERA;
  kernel cases also use DC_KERNEL_DGO. No ELF linked.
- Direct calls to that same GCC also compiled arena_coexist.c and
  kernel_boot_cases.c with DC_ARENA_COEXIST but without DC_ARENA_CAMERA;
  kernel cases also use DC_KERNEL_DGO. Outputs: build/arena-camera-validation/
  arena_coexist_baseline.o and kernel_boot_cases_baseline.o. The first attempt
  used an incorrect KOS relative include path and failed at kos.h; correcting
  the root to ../kos-source made both compilations pass.
- `git diff --check` passed (Git also printed a CRLF-to-LF advisory).

Initial discovery also found no `rg` on PATH and no root-level
`tests/test_disc_reader.py`; the relevant tests are under ports/dreamcast/tests.
No build_dreamcast.py, full build, Flycast, or Git-state-changing command was
run. The edited schedule's target behavior and final ELF fit remain unverified.

## Reviewer commands (full build/device steps not run here)

From the repository root:

```powershell
$env:PATH += ';C:/msys64/mingw64/bin;C:/Users/sullins server/Documents/Codex/2026-10-20/continue-our-jak-and-daxter-dreamcast/work/dc-sdk/sh-elf/bin'
python -m unittest discover -s ports/dreamcast/tests -p 'test_disc_*.py' -v
python -m unittest discover -s ports/dreamcast/tests -p test_arena_camera.py -v
python -m unittest discover -s ports/dreamcast/tests -p test_arena_game.py -v
git diff --check
```

In the owner's configured KOS shell, from ports/dreamcast, with the staged
326-module GAME.CGO assets and a connected Maple controller:

```sh
make jak-dc-arena-camera.elf
python3 verify_backend.py "$FLYCAST" jak-dc-arena-camera.elf --nm "$NM" --output build/arena-camera-schedule-review --suite arena-camera --tests 1148 --timeout 300 --dump dc_arena_camera_metrics 172
python3 -c 'import struct; from pathlib import Path; w=struct.unpack("<43I",Path("build/arena-camera-schedule-review/dump.bin").read_bytes()); print(dict(zip(("version","producer","reason","dispatches","update_calls","update_returns","diagnostic_seeds","original_wrote_camera","pose_changes","camera"),w[:10]))); print("master",w[22:29],"slave",w[29:36],"combiner",w[36:43])'
```

Use this new output directory to preserve the owner's original failing result.
Expected success is **1148/1148**, source-derived, not measured here:
528 GOAL + 10 stage + 4 prepare + 65*5 dispatch wrapper + 8 finish wrapper
+ 6 probe install + 65*3 root diagnostics + 61 original output checks
+ 11 probe finish checks. Relative to the old prediction of 1140, the extra
dispatch adds five arena checks and three root diagnostics. Timeout or a
smaller check total is not success.

Require backend state 2, all 1148 checks passed, reason=0, dispatches=65,
update_calls=update_returns=61, diagnostic_seeds=original_wrote_camera=1,
the process/free-state counters and serviced-controller checks. If another
check fails, keep its guards and investigate the saved reason/metrics.
Only after this probe passes should a separate new define implement the next
step in GAME_CAMERA_ROUTE.md. Affine renderer readback, response to input,
neutral stability, beach retargeting, original level services/collision and
Jak-follow gameplay remain later work.
