# Fixed mixed scalar calls with integer stack arguments

2026-10-21. (AI-assisted). Fixed mixed integer/float signatures now support
integer stack spills, still limited to eight total arguments. SH-4 GCC's
independent integer-pair and float-register streams continue across spills:
the first two integer containers use r4:r5 and r6:r7, later integer containers
occupy consecutive 8-byte stack slots, and singles use FR5,4,7,6,9,8,11,10.
Incoming spills are addressed relative to the saved incoming stack; outgoing
slots are reserved and released around each call. Existing lowering already
implemented this layout; its mixed-stack guards were removed after measurement.

An independent GCC15.1 oracle compiles 2,767 return probes across **all 376**
mixed signatures with 3..7 integer containers and 4..8 total arguments. It
checks each argument's actual register or stack offset. The earlier 88-probe
register-only oracle remains enabled. Mixed variadic calls and more than eight
arguments still reject; vector/128-bit and float stack arguments are not enabled.

Two new frontend tests lower 226 generated functions (sixteen representative
entry/call layouts plus live-value/loop probes) and retain the variadic/argument
limit guards. One new host test performs the exhaustive GCC measurement.
1,937 new native checks cover direct GCC entry, GOAL-to-GOAL calls, all return
positions, both integer halves, signed extremes, raw float zero/infinity/NaN
payloads, native stack restoration, live values and repeated stack calls.

All **157 host tests, 10,613 native runtime checks and 39 numeric checks pass**
in stock 16 MiB Flycast. Total diagnostic code is 120,514 bytes, including the
new 49,518-byte mixed-stack probe module, 26,574 bytes of far probes/padding,
and a retained 3,244-byte shift baseline. The probe module is diagnostic
coverage, not game code. Arena use is 411,872 / 1,048,576 bytes with 563 symbols,
excluding KOS/executable/stacks/renderer. Whole-game resource fit is unproven.

The unchanged desktop-configured individual-function inventory remains
gcommon 57/66, gkernel 54/76 and gstate 3/7. Whole modules are not started.
The bounded original nine-global startup prefix and original scalar validator
continue to pass; full target configuration, process/heap/thread support and
remaining platform/ABI dependencies are unfinished.

Reproduce after building the original typed frontend:

```powershell
./ports/dreamcast/build_host_windows.ps1
python ports/dreamcast/build_runtime_goal.py --verify
python ports/dreamcast/build_numeric_goal.py --verify
```

make_mixed_stack_fixture.py writes synthetic GOAL and C probes into build/.
No original assets are required. Build one SH-4 target at a time.
Physical Dreamcast is available later; no CDs are prepared/burned and no
physical hardware test is claimed. Full gameplay remains unfinished.
