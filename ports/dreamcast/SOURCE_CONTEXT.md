# Ordered original source dependencies

2026-10-21. (AI-assisted). The original kernel files require earlier macros,
types, constants and declarations. Reading each file in an empty compiler
session blocked gkernel on missing version constants and gstate on missing
process/type context before native lowering could even be evaluated.

The typed exporter now accepts repeated `--preload CONTEXT.gc` arguments. It
compiles those unmodified source objects in order, then exports only the main
file's functions and static objects. The host wrapper records each dependency
path and SHA256 in the manifest. Duplicate, missing and self-referential
preloads reject. The original type system's partial-definition lookup provides
the declared reference parent's ABI for forward types; it does not fabricate
field layouts or accept unknown types.

Compiler context does not load a native module or run target startup. Native
tests link a main module with an initially unbound helper, load the dependency
separately, explicitly run its void initializer, and then verify its global,
type/field layout, forward reference and live cross-module call. Five new host
integration tests cover order, missing context, metadata, selected forms and
manifest hashes. Twenty-nine new target checks verify actual module behavior.

All **138 host tests, 8,336 native runtime checks and 39 numeric checks pass**
in stock 16 MiB Flycast. The runtime diagnostic emits 58,374 bytes (including
26,574 bytes of long-branch probes/padding and a 3,244-byte shift baseline),
with arena 335,056 / 1,048,576 bytes and 257 symbols. Executable, KOS, stacks
and renderer are excluded; whole-game resource fit is not established.

Original desktop-configured compilation inventory:

| Main source | Ordered explicit context | Typed functions exported | Functions lowering individually |
| --- | --- | ---: | ---: |
| gcommon.gc | Compiler bootstrap | 66 | 57 |
| gkernel.gc | gcommon.gc, gkernel-h.gc, gstring-h.gc | 76 | 50 |
| gstate.gc | The above, then gkernel.gc | 7 | 3 |

These are compilation counts, **not linked or executed kernel modules**.
PC_PORT/BIG_MEMORY/profiler branches and desktop memory bounds are unchanged.
Do not execute this original startup as a Dreamcast kernel: explicit target
configuration, module/bridge coverage, process/heap support and mixed/vector
ABIs remain open. Complete gameplay, sound, saves and progression are unfinished.
Physical Dreamcast is available later; no CDs are prepared or burned now.

Reproduce supported native tests from the repository root after building the
original frontend:

```powershell
./ports/dreamcast/build_host_windows.ps1
python ports/dreamcast/build_runtime_goal.py --verify
python ports/dreamcast/build_numeric_goal.py --verify
```

Export original gkernel for inspection, without target execution:

```powershell
$env:PATH = 'C:/msys64/mingw64/bin;' + $env:PATH
../og-host/bin/goalc-dreamcast-ir.exe . goal_src/jak1/kernel/gkernel.gc ports/dreamcast/build/gkernel-context.ir.json --preload goal_src/jak1/kernel/gcommon.gc --preload goal_src/jak1/kernel/gkernel-h.gc --preload goal_src/jak1/kernel/gstring-h.gc
```

To inspect gstate, use the same context plus gkernel.gc. Paths above assume the
documented adjacent og-host workspace. Relevant host tests skip without the
frontend/SDK. Build one SH-4 target at a time; do not relink the Windows frontend
while a compiler client is using it.
