# Original kernel system-global startup prefix

2026-10-21. (AI-assisted). A bounded, contiguous prefix of original gkernel.gc
now runs natively in stock 16 MiB Flycast. It initializes its first nine system
globals, ending at `*last-loado-debug-usage*`. This is initial global setup,
not a complete kernel boot, dispatcher, process scheduler or playable game.

The frontend's explicit `--prefix-through-global NAME` reads original forms in
order through a unique top-level `(define NAME ...)`. It retains every earlier
form, stops before later forms and records the boundary/form count in IR and
manifest provenance. Missing, nested or duplicate boundaries reject, as do
mixed prefix/defun selection and full-source self-preloads. Native compilation
requires an explicit initializer. This mechanism does not certify earlier forms
as safe: the selected original prefix and dependencies must still be audited.

For this slice, earlier forms are original package/require declarations, one
forward declaration and these nine definitions:

| Global | Original initialized value |
| --- | --- |
| *use-old-listener-print* | #f |
| *kernel-version* | boxed binteger 2.0 |
| *irx-version* | boxed binteger 1.2 |
| *kernel-boot-mode* | listener symbol |
| *kernel-boot-level* | #f |
| *deci-count* | 0 |
| *last-loado-length* | 0 |
| *last-loado-global-usage* | 0 |
| *last-loado-debug-usage* | 0 |

Ordered compiler context is original gcommon, explicit scalar-validation
memory context, original gkernel-h and original gstring-h. Source and dependency
SHA256 hashes are pinned. The startup body is neither copied nor rewritten.
These particular definitions use no PC/profiler/MMIO/scratchpad paths. Later
package-list, vector inspection, kernel-context and stack/scratchpad forms are
excluded. Their platform choices remain unaudited and are not executed.

Four new host tests verify contiguous ordering, strict boundary guards, explicit
initialization and original prefix provenance. Thirty-two new native checks
verify linking leaves globals uninitialized, explicit startup, original values,
boxed version encoding, absence of later globals, and deliberate reinitialization
resetting a changed counter. The runner's Makefile dependency now includes its
own C source, so incremental builds incorporate newly added checks.

All **150 host tests, 8,508 native runtime checks and 39 numeric checks pass**
in stock 16 MiB Flycast. This initializer is 1,172 bytes. Total diagnostic code
is 69,296 bytes, including 26,574 bytes of far probes/padding and a retained
3,244-byte shift baseline. Arena use is 350,016 / 1,048,576 bytes with 316
symbols, excluding KOS/executable/stacks/renderer. Whole-game fit is unproven.

Reproduce after building the original typed frontend:

```powershell
./ports/dreamcast/build_host_windows.ps1
python ports/dreamcast/build_runtime_goal.py --verify
python ports/dreamcast/build_numeric_goal.py --verify
```

Compile only this original startup prefix for inspection:

```powershell
python ports/dreamcast/compile_goal_subset.py --frontend ../og-host/bin/goalc-dreamcast-ir.exe --source goal_src/jak1/kernel/gkernel.gc --preload goal_src/jak1/kernel/gcommon.gc --preload ports/dreamcast/scalar_validation_context.gc --preload goal_src/jak1/kernel/gkernel-h.gc --preload goal_src/jak1/kernel/gstring-h.gc --prefix-through-global "*last-loado-debug-usage*" --initializer dc-original-kernel-globals-init --runtime --header ports/dreamcast/build/original_kernel_globals.h
```

Full-file target configuration, structured static kernel objects, original
module ordering, remaining ABIs, process heaps, thread switching and platform
bridges remain open. The existing full original-source export remains available
for compile-only inspection. Physical Dreamcast is available later; no CDs are
prepared/burned and no physical hardware test is claimed. Full gameplay remains
unfinished.
