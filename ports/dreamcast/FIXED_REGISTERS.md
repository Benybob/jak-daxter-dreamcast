# Explicit symbol-table role for original rlet registers

Historical fixed-role checkpoint. [FAR_BRANCHES.md](FAR_BRANCHES.md) removes
valid?'s later branch-range blocker; target memory constants remain open.
(AI-assisted).

2026-10-21. (AI-assisted). Original rlet may reuse a constrained desktop
register through several RegVal aliases with the same virtual ID. The exporter
previously emitted duplicate register records. Original valid? encountered four
identical uint aliases of desktop r14, which means the GOAL symbol-table base.

The exporter now retains each fixed constraint's desktop register and role,
deduplicates only identical records with a known fixed constraint, and records
alias counts. Conflicting or unconstrained aliases reject. Actual fixed-value
resets retain their original IR positions and explicit binding metadata.

Native runtime lowering maps the supported r14 symbol-table role to the native
s7 pointer through the existing #f pointer relocation. This is a semantic role;
it does not use physical SH-4 r14, which remains a frame register. Missing
resets still fail definite assignment. Unknown desktop roles, malformed reset
metadata, process/self entry and symbol-table mutation remain rejected. The
backend's duplicate-register guard remains strict; no implicit zero values or
extra positional arguments are invented.

Six new original-frontend tests cover real alias reuse, explicit resets,
native-profile requirements, unknown roles, mutation, absent initialization,
metadata mismatch and duplicate rejection. Fifteen additional native checks
verify s7, original +/-32768 layout expressions, aliases and 64-bit arguments.

All **128 host tests, 8,267 native runtime checks and 39 numeric checks pass**
in Flycast with stock 16 MiB RAM. Runtime code is 30,986 bytes, arena use
306,992 / 1,048,576 bytes, and 243 symbols. These are separate diagnostics;
KOS, executable, stacks and renderer are excluded. Physical Dreamcast remains
untested, and no CDs are prepared at this stage.

Complete gcommon still exports 66 functions and 56 lower individually.
Original valid? now exports its fixed aliases correctly but hits the long
branch range limit. It is not executed. Its desktop END_OF_MEMORY is also
incompatible with native Dreamcast P0 aliases, so an explicit target memory
configuration is needed before using it as a native validator. Broader kernel
module dependencies, heaps, scheduling, mixed/vector ABIs and gameplay remain
unfinished. No original game bodies or memory constants were rewritten.

Reproduce after building the original frontend from the repository root:

```powershell
./ports/dreamcast/build_host_windows.ps1
python ports/dreamcast/build_runtime_goal.py --verify
python ports/dreamcast/build_numeric_goal.py --verify
```

The frontend integration and SDK oracle tests skip if tools are absent.
Rebuild the frontend before claiming coverage of exporter changes. Do not
relink its Windows executable while tests or compiler clients run.
