# Declared scalar enum ABI

2026-10-21. (AI-assisted). The original type system's EnumType now supplies
native ABI classification for unboxed GPR enums with 1-, 2-, 4- or 8-byte
underlying values. Signed declarations map to the existing 64-bit int container;
unsigned declarations map to uint. Original enum names remain in semantic type
metadata, and original typed loads retain their exact width/sign extension.
No narrowing or alternate calling convention is invented at function entry.

This fixes call/return rejection of real kernel enum values such as link-flag
and process-mask. Only original declared EnumTypes receive this classification;
unknown types and non-scalar/vector values remain rejected. Mixed stack and
mixed variadic calls remain outside the verified subset.

Four new host tests cover all eight signed/unsigned widths, preserved semantic
names, compact returns, typed-load width/extension, call metadata and retained
mixed-stack/vector/unclassified guards. One hundred sixty-eight new native checks exercise original enum
construction, both halves of 64-bit values, signed extremes, C/GOAL entries,
GOAL-to-GOAL calls, register-only mixed float/enum calls and all typed load widths.
The eight identity leaves remain six bytes each, including their RTS delay slot.

The unchanged desktop-configured compilation inventory now has gkernel 54/76
functions lowering individually, up from 50. Newly lowering routines are
load-package, stream<-process-mask, and the process-tree/process inspect methods.
These four are compile-only: package I/O and printing dependencies are not
implemented by this change. gcommon remains 57/66; gstate remains 3/7.
Whole-module startup is not established by per-function lowering.

All **154 host tests, 8,676 native runtime checks and 39 numeric checks pass**
in stock 16 MiB Flycast. Diagnostic code is 70,996 bytes, including 26,574 bytes
of far probes/padding and a retained 3,244-byte shift baseline. Arena use is
352,640 / 1,048,576 bytes with 337 symbols, excluding KOS/executable/stacks/
renderer. Whole-game fit remains unproven.

Reproduce after building the original typed frontend:

```powershell
./ports/dreamcast/build_host_windows.ps1
python ports/dreamcast/build_runtime_goal.py --verify
python ports/dreamcast/build_numeric_goal.py --verify
```

The previous original system-global prefix and scalar validator remain enabled.
Full kernel target setup, process heaps, scheduler/thread switching, structured
static kernel objects and remaining ABIs/platform bridges are unfinished.
Physical Dreamcast is available later; no CDs are prepared/burned, no physical
hardware result is claimed, and complete gameplay remains unfinished.
