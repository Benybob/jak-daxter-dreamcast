# Six-byte SH-4 stack argument returns

2026-10-21. (AI-assisted). A copy-only function returning an integer/tagged
stack argument now loads its low word from the incoming R15 stack, executes
RTS and loads its high word in the delay slot. The body is three fixed 16-bit
instructions: six bytes, without a frame or saved registers. Integer register
returns remain six bytes; float register returns remain four.

The shortcut retains the existing definite-assignment, type and side-effect
guards. Stack slots use the measured GCC 8-byte integer container layout.
With at most eight arguments, stack offsets are 0..40, with high words at
4..44, all representable by MOV.L's scaled displacement. Calls, side effects,
unsupported operations and wider signatures retain ordinary lowering/rejection.

One new GNU assembler test independently verifies the exact bytes and useful
RTS delay slot for all six integer stack positions. One new original-frontend
test checks the representative mixed returns against framed baselines.
Existing native mixed and homogeneous stack cases exercise actual entry,
return halves, float payloads, stack restoration, live values and repeated calls.

For the same pinned synthetic IR, **45** functions shrink to six bytes:
the 226-function mixed-stack module falls from **49,518 to 41,918 bytes**,
saving **7,600 bytes (15.3%)**. Unchanged functions include call wrappers and
live/loop bodies. See stack_copy_sizes.json for the source IR hash and each
changed function. These are code-size measurements, not cycle/cache benchmarks.

All **159 host tests, 10,613 native runtime checks and 39 numeric checks pass**
in stock 16 MiB Flycast. Whole diagnostic code falls from 120,514 to 112,566
bytes (7,948 saved, including existing homogeneous copy returns). Arena use
falls from 411,872 to 403,920 / 1,048,576 bytes, with 563 symbols unchanged.
Code totals include 26,574 bytes of far probes/padding and a retained 3,244-byte
shift baseline. KOS/executable/stacks/renderer are excluded; whole-game fit
remains unproven.

Reproduce after building the original typed frontend:

```powershell
./ports/dreamcast/build_host_windows.ps1
python ports/dreamcast/build_runtime_goal.py --verify
python ports/dreamcast/build_numeric_goal.py --verify
```

The bounded original validator and nine-global startup remain enabled. Full
kernel target setup, structured static objects, process/heap/thread support,
remaining platform/ABI dependencies and complete gameplay are unfinished.
Physical Dreamcast is available later; no CDs are prepared/burned and no
physical hardware test is claimed.
