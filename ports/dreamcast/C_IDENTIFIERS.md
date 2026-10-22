# Distinct native identifiers for original GOAL symbols

2026-10-21. (AI-assisted). Replacing punctuation with underscores caused +, -,
* and / to collide, and 1/ began with a digit. Their original functions could
not coexist in a generated C header even though native bindings were distinct.

The generator now groups candidate identifiers case-insensitively (generated
offset macros are uppercase). Collisions, leading digits and the reserved
encoding prefix use a reversible full UTF-8 hexadecimal spelling. A reserved
prefix prevents real source names from impersonating encoded names. Unique
legacy spellings stay readable. Output is deterministic for a selected symbol
set regardless of selection order; adding a colliding name can rename generated
internal C labels. Runtime exports, import strings and function bindings always
retain exact original GOAL names. This changes packaging labels, not the ABI.

Three host tests cover punctuation, case, digit prefixes, Unicode, reserved
names, module entry preservation, selection order and empty/duplicate/NUL
guards. Native tests load unchanged original 1/, +, -, *, /, mod and rem in one
module. An additional 59 checks verify distinct bindings and 64-bit add/sub,
low-32 signed multiply/divide/remainders and float reciprocal edge cases.

The reciprocal oracle follows the original compiler's compile_floating_point_division in
goalc/compiler/compilation/Math.cpp: zero divisors produce signed maximum finite
values. An initial test incorrectly assumed IEEE infinities; the unchanged
generated code was correct. The final oracle retains that original behavior.

All **122 host tests, 8,252 native runtime checks and 39 numeric checks pass**
in stock 16 MiB Flycast. Physical Dreamcast remains untested; current work
requires no CDs. The original kernel/game, processes, heaps, vector/mixed ABIs,
sound, animation and playable progression remain unfinished.

Reproduce from the repository root after building the original frontend:

```powershell
./ports/dreamcast/build_host_windows.ps1
python ports/dreamcast/build_runtime_goal.py --verify
python ports/dreamcast/build_numeric_goal.py --verify
```

The host integration/oracle coverage needs the original frontend and SH-4 SDK;
the relevant tests skip when those tools are absent. Compile one SH-4 target
at a time. Do not relink the Windows frontend while compiler clients run.
