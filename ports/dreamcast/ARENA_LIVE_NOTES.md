(AI-assisted), ISC.

# Original camera live beach integration

The supplied real run remains FAIL: `build/arena-live-run.txt` and
`build/arena-live-fix-review/result.json` report **2088 passed / 2090 total**,
first failed case **12089**. Its original ELF is preserved. No Flycast run or
ELF link was performed during this repair. Current expected success is
**6984/6984**, calculated from source, not an observed target result.

## Root cause established before input changes

[ARENA_LIVE_ROOT_CAUSE.md](ARENA_LIVE_ROOT_CAUSE.md) records the source lines,
ELF disassembly, raw evidence and host original-input IR replay. In particular:

- The supplied ELF hash matches `8e756dcbb462c16f844f2fcc990bd73218b7cd7342321aab04962edf35949bc2`.
  Its `_dc_live_recorded_pad` disassembly loads **0x10002**, circle plus L2,
  for completed slots 42/43. It does not contain the later right-X recording.
- The decoded v2 dump has ratio **4.0**, 43 witnessed outputs, 42 draws,
  reason 8, runtime_error 0. Yaw is **2.1999999427825365** on outputs 41..43,
  delta is 1, and all three process resume counts increase on each row.
- Outputs 41/42 are deliberately neutral. Output 43 misses digital yaw because
  `cam-states-dbg.gc:71` requires `(not *cam-layout*)`. The default at
  `cam-debug-h.gc:150` is inside its debug segment (line 73). The emitted
  original initializer skips it when debug is disabled (`kernel_boot_cases.c:143`).
  An interned numeric zero is truthy in GOAL (`goal_runtime.c:274`).
- The live begin path now supplies that skipped false default once, accepting
  only zero or already-false. It refuses an active layout. Every observed
  update and subsequent root sample requires the false value to remain set.
- This is not smoothing: the observed raw basis is copied by
  `cam-combiner.gc:214..246` and `cam-update.gc:290..303`. Later smoothing
  writes a separate matrix. The original move applies rotation immediately.
- The decoded strafe starts exactly at output 17, ends at 28, and is absent
  on 16/29, matching completed input slots 16..27. There is no evidence for
  another input-frame delay. Moving the turn window would mask the failure.

The earlier notes' claim that centered analog overwrites digital yaw was
wrong: `cam-states-dbg.gc:243` subtracts from existing rv.y. The L2 branch
at 238..241 adds translation and preserves rv; centered sticks add zero.
The host typed-IR replay measured circle+L2 rv.y=0 with truthy layout zero,
and **-436.90667724609375 GOAL angle units** with GOAL false, at ratio 4.
Both cases retain strafe tv.x=-825.806396484375 for left-X 180.
These are HOST IR results, not execution of original SH-4 instructions.

No input bytes or stage boundaries were changed during this repair. The
already-present recording supplies **right X=207** only on completed slots
42/43, centered left/right Y, no buttons, and zero pressure bytes. Left X is
+52 on 16..27, 100..123 and 160..171; -52 on 64..87 and 136..159. All other
slots are neutral. Synthetic L2 support remains unused. Combining L2 with
left-stick motion legally changes both translation and rotation; that is
outside this deliberately orientation-only recording, not a camera defect.

## Review 2 findings and preserved contracts

Findings 4/5: the historical zero-yaw cause is the skipped layout default,
now initialized and guarded. The current input bytes are documented above.
Finding 6: retain exact turn responses 43/44, reject missing or delayed first
response, and additionally require outputs 41/42 to remain neutral. The host
observer exercises early, missing, stale and legal PAL responses.

Findings 1/7: ordinary observation now requires adjust in **1..4.8**, the
bounded NTSC/PAL fixture timing domain, before poisoning. Arbitrary positive
ratios, including custom rates outside that range, no longer pass as ordinary.
The yaw gate uses **0.6 degrees times the ratio consumed by that output**;
0.043 is gone. Error allowance is 0.00021 rad: two original integer-angle
quanta (2*pi/65536 each) plus affine float error. A correct PAL 4.8 step
(0.0502655 rad) passes; zero, doubled, mistimed and ratio-mismatched steps fail.
The ratio is recorded BEFORE frame-start computes the next frame's value.
The full auditor independently checks every recorded ratio and yaw magnitude.

The **6 m X/Z, 1 m Y** anchor cap and **4.5..20 m** raw eye path remain strict.
These bound this fixture, not all legal free-camera motion. They must still
refuse a legal camera that leaves the staged proof's bounds. The fixed +52
recording was sized for the measured NTSC run: at constant adjust 1..4 its
predicted eye travel is 4.838710..19.354839 m; at 4 its worst anchor displacement
across both yaw signs is X=4.003082 m, Z=5.633331 m. A constant PAL 4.8
recording would travel 23.225806 m and is **not an accepted full-run fixture**;
PAL yaw semantics are tested independently. There is no claim that every
ratio sequence in 1..4.8 satisfies the footprint/path budget. Supporting that
would require a separately budgeted recording; widening these correct caps
would conceal fixture overflow. Runtime coverage failures continue to name
the staged coverage/travel contract rather than claim incorrect original motion.

Findings 2/3 remain enforced: raw GOAL anchor versus the cache's returned
query, prepared terrain vertices versus raw basis/eye, and actual submitted
terrain/instance cameras versus original lanes. Temporary producer mutations
(target X +1 and yaw +0.1) are rejected at O2 and fast-math. Crop containment,
occupied-cell CRC/count ledger, affine validation, all 36 poison lanes, fresh
pad bytes, process resumptions and draw acknowledgments remain in place.

Finding 9: evidence is now **version 3, 39476 bytes**: 236-byte summary,
144-byte initial pose, 181 rows of **216** bytes. Existing row fields keep
their offsets; each row appends its consumed time-adjust ratio at byte 212.
`verify_backend.py --suite arena-live` automatically selects the full dump
and expected count, and rejects explicit short/wrong dumps or counts before
launch. This was tested without launching a backend. Do not reuse an older
13312/38752-byte request. The diagnostic analyzer still reads historical v1/v2
prefixes but reports complete_capture=false, uncaptured_slots=120 and
acceptance=false. The strict success auditor refuses every prefix.

## Staging and expected checks

The production input exporter and `make_arena_live_path.py` were run into a
fresh `build/arena-live-disc/review2` directory. The new 419 non-JSON files
and generated fixture header were byte-identical to existing live staging.
Only live `FLY.json` metadata was refreshed to the current right-X recording:
SHA-256 **5ac9955453c1a9a7ee693f64fa91f9c6d090f985e4b7ae70e4151d95001381ef**.
The existing ROMFS image remains **1339392 bytes**, hash
`8fba3b581ca62a96d35e504b8f0ad1e422f9b8f85222f4d53eaaca397165d150`.
All 419 staged files were read back from it. Placement is
0x8ccd9800..0x8ce20800, below 0x8d000000 and the 1700000-byte image ceiling.

Capacity analysis over the unchanged entire cap square measured terrain
union faces/vertices/remap/bytes = 4614/4627/6803/277048, within
4960/5112/10899/539192. Expanded instance union 293 is within 400.
This is a host bound, not measured target slab/VRAM/PVR high-water.

Success arithmetic, 181 original updates and 185 dispatcher returns:

- Base: 528 existing GOAL + 15 stage + 5 prepare + 2 mounts + 185*4 dispatch
  wrappers + 7 finish wrappers = **1297**.
- Observer: 11 install (two new default checks) + 6 initialize + 185*3 root
  + 181*3 witness + 181*2 draw acknowledgments + 13 finish = **1490**.
- Renderer: 17 initialize + 1 ledger + 185 sample + 4*2 warmup
  + 181*22 draw checks + 4 finish = **4197**.

Total **6984**. Neutral/moving/turning required pairs stay 50/71/2, with
181 draws and 6516 freshly replaced poison lanes. The extra turn lead-in
stability conditions are within existing checks and do not change that total.

## Validation actually run

PATH was appended, never prepended, with the requested MinGW and SH-4 bins.
The first command was `git diff`; all tracked changes and untracked Dreamcast
files were inspected. The unfinished undeclared `hw` buffer in
`tests/arena_live_math_check.c` was completed before camera changes.

- Unittest discovery: test_arena_live.py, test_arena_camera.py,
  test_arena_game.py, test_mmi_pad.py and test_flythrough_fixture.py.
- Live C harnesses at O2 and fast-math: **6232** math/input/assets assertions
  and **4008** observer assertions per build; complete mixed-ratio host
  evidence passed the strict audit. These use host doubles.
- Original typed input IR replay; Python/struct decoding of the real dump;
  SH-4 objdump of the preserved ELF; ELF .dc_stage comparison with existing
  boot ROMFS; packed camera module comparison with generated DCSH modules.
- tests/check_arena_live_mutations.py: all eight outcomes; query/camera
  mutations rejected. tests/check_arena_live_isolation.py: all 20 old-mode
  SH-4 preprocessing comparisons against HEAD identical.
- tests/compile_arena_live_objects.py: nine separate SH-4 objects,
  O2/C99/Wall/Wextra/Werror and KOS headers; no link or target execution.
- Production input exporter, separate asset generation, image readback and
  conservative capacity analysis listed above.
- Existing sound edits were inspected and tested: test_vag_adpcm.py **28**
  tests and test_sbk_bank.py **4** tests passed. Codec mutation script passed
  baseline and rejected all **14** mutants with assertion failures. The six
  SOUND_REVIEW literals, final SPU history, signed rails, silence and pad are
  already present and passing; no extra sound behavior was added here.
- `git diff --check` passed, with only CRLF normalization warnings.

This work supplies host and compile evidence only. A new original-code run,
its complete v3 dump, full success audit and framebuffer are unmeasured.
No Jak-follow camera, original level streaming or hardware result is claimed.
