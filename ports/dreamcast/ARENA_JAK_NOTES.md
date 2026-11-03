(AI-assisted)

ISC. Milestone 5: original camera debug data and an original-player prerequisite
probe. `jak-dc-arena-jak.elf` is a new Makefile target, **not a completed Jak
gameplay milestone**. No ELF was linked and no Flycast was run in this session.
The user's reported live baseline is 6984/6984; it was not rerun on target here.

## Step A: original data initializer, digital input

Only the new target links `arena_jak_dgo.o` instead of `goal_dgo.o`. That wrapper
compiles the existing loader with its two startup-call boundaries redirected to
`arena_jak_debug.c`. At the original `dc-game-cam-debug-h` load position it:

1. Authenticates the exact resident temporary startup through the loader's code
   registry and requires debug off, layout value zero, and no previous call.
2. Temporarily enables `*debug-segment*`, calls the original startup once, and
   restores its old value even on initializer failure.
3. Checks the original five disjoint 1600-byte tables, their zero indices, the
   8000-byte allocation, and the `define-perm` result `*cam-layout* = GOAL #f`.

The initializer is retained by the existing `--no-debug-segment` module; its
debug functions are discarded. This enables that original file's **data**
startup, not the rest of debug gameplay. The existing malloc bridge maps a
request for the absent debug heap to the global arena, as its null-heap fallback
already does. No debug heap, debugger process, or replacement initializer is
invented. The loaded DCSH has just the 948-byte original startup export.

In this target `arena_live.c` requires that evidence and the resulting false
value. It has no C layout repair. Removing startup interception therefore fails
before camera observation; the host regression explicitly checks that refusal.
The typed-IR test replays the original startup and original
`cam-free-floating-input`: the same circle+L2 samples produce zero rotation
with skipped/truthy numeric zero, and nonzero digital rotation after startup.
It also checks `define-perm` preserves an already-active layout.

Only Jak's input object changes completed slots 42/43 to B/circle+synthetic L2.
Analog sticks are centered during those slots; right X is 128, hardware buttons
are 0x2100 and pressure bytes are zero. Original pad hardware decoding,
`service-cpads`, digital input, free-camera movement, combiner, `update-camera`
and renderer remain the execution route. L2 adds no translation at centered
sticks. All strafe samples, stage boundaries and timing/motion/draw checks are
inherited. Output 41/42 must stay neutral, and 43/44 must each turn by 0.6
degrees times the consumed display ratio, with the existing quantization bound.
These are target checks awaiting execution, not a new observed digital run.

## Step B: precise refusal before attempting an uninitialized player

After the completed camera/renderer proof and restoration of `update-camera`,
`arena_jak.c` authenticates original exports for `start`, `init-target`,
`get-or-create-continue! game-info` and `load-to-heap-by-name load-dir-art-group`.
It requires `*target*` false and both original target pools present. It calls
the original continue factory with the existing `*game-info*`, validates a
128-byte continue point (the factory writes through tagged offset 116), then
calls the original art lookup for the exact skeleton dependency. Both calls
are counted after returning; faults and any unexpected target change fail.

The dependency comes from original source and emitted typed IR:

- `logic-target.gc:1280` defines `start`: stop the old target, then process-spawn
  target with `init-target`, the target pools and `*kernel-dram-stack*`.
- `logic-target.gc:1164..1230` constructs control/collision data, selects original
  cpad 0, then calls `initialize-skeleton self *jchar-sg* '()`.
- `target-util.gc:12` defines that descriptor for **eichar**, with
  `eichar-lod0-jg`, `eichar-lod0-mg`, `eichar-shadow-mg`, texture-level 2.
- `process-drawable.gc:324` selects `*level*.level-default` for texture-level 2,
  irrespective of a player entity. Its original IR uses tagged group+5312,
  level+48 for art directory, skeleton+0 for name and +61 for version. The
  probe also checks that directory's method at type+52 is the original export.
- `level.gc` startup allocates an empty 50-entry art directory for that default
  level. `loader.gc:52` looks up the name; absent art calls
  `art-group-load-check`, which returns #f with debug off (`loader.gc:113`).
  The probe bounds both arrays and requires debug off before that call.
- Missing art makes `initialize-skeleton` transition to
  `process-drawable-art-error`, before a usable player skeleton is initialized.

The expected terminal failure is:

```
jak spawn blocked: missing eichar art-group in default level (init-target:1230)
```

This is an executed original art-lookup prerequisite check **when run on target**,
not an observed failure inside `init-target`. This harness does not call `start`
or allocate a placeholder target. It does not establish the first fault that a
real spawn attempt would hit before the skeleton call. Merely allocating a
target process could reach an error state; that is not a running original player.
The probe never writes target state, control position or follow-camera matrices.
Even supplying an art-group alone reaches a second explicit integration refusal
(blocker 11), so it cannot silently turn this fixture into gameplay success.

## Remaining implementation, in dependency order

1. Add native loading/relocation and original login for complete common art from
   the owner's GAME.CGO: start with `eichar-ag.go` (retail object name `eichar`),
   including its joint geometry, mesh, shadow and subsequent animation resources.
   Register it through original `set-loaded-art` in the default level directory.
   Include sidekick art for `init-target`'s later `init-sidekick` spawn. The
   326-module native GAME.CGO contains compiled GOAL sources, not these original
   common-art objects. Existing `anim_case_data.py` samples are only differential
   test inputs, not a resident complete art group or player skeleton.
2. Supply an original world/continue path with BSP, collision/PAT, level info and
   entity/resource links. The private C beach renderer package is not a GOAL
   level. Normal loading needs real RPC-DGO returned buffers plus `link-begin` /
   `link-resume` support (`load-dgo.gc`, `level.gc`); the existing no-display RPC
   bridges do not provide it. A staged resident alternative must perform the
   corresponding relocation/login and original level birth honestly. Measure
   arena, process heap and render/VRAM budgets before claiming it fits.
3. In a new guarded phase, call original `start 'play <continue>`, with process
   ownership/scope ledgers registered and the kernel stack/fiber context valid.
   Dispatch until original `init-target` reaches `target-continue`, then a
   controllable state. Record returned target identity, type, process state,
   runtime fault/site, resumptions and original control/cpad linkage. Do not
   set those fields from C. A direct `start` takes no entity argument; an actor
   or camera region entity must come from original level data when used.
4. Let `target-continue` (`target-death.gc:34`) drive the original follow events:
   `change-target self`, `change-state cam-string`, `teleport`, `no-intro`,
   `clear-entity` for the default continue. A named continue also waits for active
   levels and calls `cam-start #t`; `reset-cameras` births active BSP camera
   entities (`entity.gc:996`). Do not force free-camera state to count as follow.
   `cam-string` (`cam-states.gc:1579`) needs the original target spline and LOS /
   collision route. `cam-master` reads real control, surface, neck and water data.
5. Add a player recording at the same hardware boundary, preserving original
   `service-cpads` and target control input (`logic-target.gc:272..281`, selected
   at 1225). Require neutral/moving input comparisons, increasing target resume
   counts, legitimate state transitions and measured control position changes.
   Rework the Jak-only camera observer: the inherited observer deliberately
   requires the free-floating slave and inactive levels before poisoning outputs.
   A following camera needs a safe store witness for the active-level branch,
   and may require support for original roll/FOV/local-down in the adapter.
   Feed the resulting original affine camera into bounded terrain/instance draws
   and capture a frame. Then replace the integration refusal with measured
   player and follow checks. None of these steps is already proved by this ELF.

## Expected target counts and evidence (unmeasured)

With the existing assets and accepted recording timing, the source-derived
expectation is **FAIL, 6996 passed / 6997 total, failed_case 16997**:
6984 inherited checks + 3 original debug startup checks + 9 successful player
prerequisite checks + 1 missing-eichar failure. No all-pass target count is
claimed. An earlier failure is a distinct regression/dependency to diagnose.
The live proof should retain 181 draws, 185 dispatcher returns, 6516 witnessed
output lanes, 50 neutral / 71 moving / 2 turning pairs. Its existing 6 m X/Z,
1 m Y, 4.5..20 m travel and timing limits are unchanged; a constant PAL 4.8
full recording still exceeds its travel budget, as documented in live notes.

`dc_arena_jak_evidence` is version 1, **212 bytes**: 29 little-endian u32 values,
then a 96-byte NUL-terminated missing-dependency string at offset 116.

| Words | Values |
| --- | --- |
| 0..8 | version, debug_calls, debug_entry, debug_before, debug_after, layout_before, layout_after, debug_bytes, debug_restored |
| 9..14 | blocker, start_entry, init_entry, continue_entry, continue_calls, continue_point |
| 15..19 | lookup_entry, lookup_calls, art_directory, art_name, art_result |
| 20..26 | target_before, target_after, spawn_calls, target_resumes, target_state, pad_position_updates, follow_frames |
| 27..28 | runtime_error, error_site |

Expected: debug_calls/restored=1, debug_bytes=8000, debug_before=debug_after=
layout_after=GOAL #f, layout_before=0, blocker=10, continue_calls=lookup_calls=1,
art_result=target_before=target_after=GOAL #f; words 22..28 all zero.
Zero player counters mean **no player execution**, never a passing player check.
Blockers 1..9 name the earlier failing prerequisite; blocker 11 means art was
present but the original spawn/follow integration remains unimplemented.
`dc_arena_live_evidence` retains its separate v3, 39476-byte ABI.

## Reviewer commands (build/link/Flycast commands below were NOT run here)

From the repository root, host and single-object checks:

```powershell
$env:PATH += ';C:/msys64/mingw64/bin;C:/Users/sullins server/Documents/Codex/2026-10-20/continue-our-jak-and-daxter-dreamcast/work/dc-sdk/sh-elf/bin'
python -m unittest discover -s ports/dreamcast/tests -p test_arena_jak.py -v
python -m unittest discover -s ports/dreamcast/tests -p test_arena_live.py -v
python -m unittest discover -s ports/dreamcast/tests -p test_mmi_pad.py -v
python ports/dreamcast/tests/check_arena_jak_isolation.py
python ports/dreamcast/tests/compile_arena_jak_objects.py
git diff --check
```

In a configured KOS shell, from `ports/dreamcast`, with the existing 326-module
boot staging and live beach fixtures. `FLYCAST` and `NM` must name the reviewer's
Flycast executable and SH-4 nm. Use new output directories to preserve old runs.

```sh
make -B CFLAGS='-std=c99 -Wall -Wextra -Werror' jak-dc-arena-jak.elf
python3 verify_backend.py "$FLYCAST" jak-dc-arena-jak.elf --nm "$NM" --output build/arena-jak-review --tests 6997 --timeout 300 --dump dc_arena_jak_evidence 212
```

The generic backend runner must exit nonzero and write FAIL at the precise
dependency above. Do not change its success test to accept this failure. Decode
the new dump separately (no emulator launch):

```sh
python3 - <<'PY'
from pathlib import Path
import struct
b = Path('build/arena-jak-review/dump.bin').read_bytes()
assert len(b) == 212
w = struct.unpack_from('<29I', b)
print('debug/version', w[:9], 'blocker', w[9])
print('original exports / continue / lookup', w[10:20])
print('target before/after', tuple(hex(x) for x in w[20:22]))
print('spawn/resumes/state/pad-position/follow', w[22:27], 'runtime fault/site', w[27:29])
print(b[116:].split(b'\0', 1)[0].decode())
PY
```

For the complete digital free-camera evidence, repeat the same ELF into another
directory; this is also expected to exit nonzero at the player dependency:

```sh
python3 verify_backend.py "$FLYCAST" jak-dc-arena-jak.elf --nm "$NM" --output build/arena-jak-camera-review --tests 6997 --timeout 300 --dump dc_arena_live_evidence 39476
python3 -c 'from pathlib import Path; from verify_arena_live import audit; print(audit(Path("build/arena-jak-camera-review/dump.bin").read_bytes()))'
```

The imported audit checks camera records only. The existing live CLI requires
an all-pass 6984-check live backend and is intentionally unsuitable for this
expected-failing Jak probe. No Jak-follow framebuffer is expected yet.

## Validation actually run

- First commands: `git diff`, `git status --short`; inspected all unfinished
  `arena_jak*`, its C/Python tests and modified files. No sound files edited.
- `test_arena_jak.py`: **6 tests passed**; **926** host assertions at O2 and
  **926** with fast-math, plus **768** observer/hardware assertions. Original
  startup and loader IR are replayed on the host; native exports pass DCSH CRC
  decoding and six relevant modules match the packed GAME.CGO byte for byte.
  Call-boundary doubles prove precise refusal, fault preservation, no silent
  target mutation, wrong-method rejection and no C layout fallback. These do
  not execute original SH-4 instructions.
- `test_arena_live.py`: **4 tests passed**; **6232** math/input/assets assertions
  and **4008** observer assertions at both O2 and fast-math, plus full synthetic
  evidence auditing and its refusal cases.
- `test_mmi_pad.py`: **4 tests passed**.
- `check_arena_jak_isolation.py`: **18** old-mode SH-4 preprocessing comparisons
  against HEAD identical, including live; existing Makefile prefix identical.
  Existing ELF hashes recorded in `build/arena-jak-validation/isolation.json`.
- `compile_arena_jak_objects.py`: six separate SH-4 objects compiled with the
  requested sh-elf-gcc, KOS headers, O2/C99/Wall/Wextra/Werror. Objects are under
  `build/arena-jak-validation`; no link step. Tool directories were appended to
  PATH, never prepended.
- `sh-elf-nm --undefined-only` on the Jak DGO and debug objects confirmed the
  loader calls the Jak wrappers and the wrappers call the normal initializer
  functions. Final readback matched all nine existing non-sound ELF hashes
  against the earlier isolation snapshot.
- `git diff --check` passed (only the existing CRLF-to-LF advisory); new files
  also passed AI-assisted/ISC header, trailing-whitespace and final-newline checks.

Final link size/placement, 16 MiB fit, extra startup allocation on target, digital
renderer output, original prerequisite calls, actual spawn faults, player motion,
following camera and physical hardware remain unverified. No build_dreamcast.py,
Flycast or Git-state-changing command was run.
