# SH4ZAM in the textured geometry viewer

2026-10-24. (AI-assisted). This accelerates the viewer; gameplay remains unfinished.

The default textured viewer now uses Falco Girgis and contributors'
[SH4ZAM](https://github.com/gyrovorbis/sh4zam) library for batched camera transforms.
The verified upstream revision is `11aad5f9c7da0d8d5038633d418477d682d915f5`
(MIT license). The complete upstream library is installed outside this repository
in the KOS ports SDK and linked with `-lsh4zam`. No upstream routines or headers
are copied into the project. Its license is retained with the SDK installation.

## Implementation and boundary

`scene_projection_sh4zam.c` loads one column-major camera matrix, transforms
vertices with SH4ZAM's `FTRV` API and computes one reciprocal depth per vertex.
It saves/restores all 16 caller XMTRX registers around the batch, including opaque
NaN, signed-zero and subnormal bit patterns. It allocates nothing per frame and
reuses the viewer's existing projected-vertex cache and texture/VRAM path.
Only this translation unit and the oracle need GNU C17; runtime code stays C99.

The assembly entry gate rejects SR.FD and FPSCR.PR/SZ/FR before any floating
instruction or C prologue. Accepted viewer calls use the normal KOS FPU mode,
finite unit-sphere positions, distance >=1.5, trusted disjoint sized arrays and
four-byte-aligned input/stride with stride >=12. Source records begin with three contiguous floats. Explicit
GCC builtin copies avoid struct/array aliasing assumptions without a loop call.
The implementation performs no callbacks or GOAL fiber yields while XMTRX is
borrowed. Live XF state across a GOAL suspension is still unsupported. This does
not change the GOAL compiler's scalar arithmetic/precision semantics.

## Verification and costs

2,382 independent native checks pass in stock 16 MiB Flycast. Twelve camera
poses spanning pitch -1.3 through +1.3 compare 64 strided points, including origin
and unit-axis extrema, against a scalar reference: <0.01 screen pixels and
<0.00001 reciprocal depth error. Tests check exact matrix preservation, FPU
control, output fences, empty input and integer-only rejection of all four
unsupported modes. Disassembly confirms one `FTRV` and one division per vertex.
There are no function calls inside the transform loop.

A warmed benchmark alternates path order over three rounds and reports median
emulated times for 131,072 vertices per path: scalar 24,522 us, SH4ZAM 19,315 us
in the sealed run (about 21% less emulated time). Timing is diagnostic, never a
pass condition; no physical Dreamcast speedup is established. Earlier runs varied.
The scalar baseline already shares reciprocal depth, avoiding an unfair
three-division baseline. SH4ZAM body size is 404 bytes versus scalar 256 bytes,
plus the 40-byte integer gate: setup and full matrix preservation cost code size
in exchange for a tighter repeated transform. These are function bytes, not a
whole-game memory measurement.

The existing 7,784-triangle, 6,934-vertex textured scene passes renderer telemetry,
actual framebuffer capture, stick orbit, trigger zoom and normal cleanup with
16 MiB RAM. No game geometry, screenshots or embedding ELF is in the public
source upload. Grok's integrated runtime review also found two rejection edge cases: masked
yields now preserve running phase/value/saved SR until validation succeeds, and
synchronous targets must be disjoint from the whole current KOS stack reservation,
including its canonical physical alias. Eleven native regressions verify refusal,
unchanged metadata/output/stack and no target execution, then a valid later yield.
The runtime's 22,469 checks and 39 numeric checks are verified;
198 host tests pass. Full startup/scheduling, animation, GC and gameplay remain.

## Build and repeat

For a standard KOS SDK, install its official `kos-ports/sh4zam` port with
`make install`. For this workspace's Windows SDK, run:

```powershell
python ports/dreamcast/setup_sh4zam.py --msys C:/msys64
python ports/dreamcast/build_dreamcast.py --target jak-dc-projection.elf
python ports/dreamcast/build_dreamcast.py --target jak-dc-textured.elf
```

Pass your actual MSYS root to both build commands if it differs. The setup helper
fetches the pinned full source, refuses to reset a modified/different checkout,
builds all upstream library translation units and installs headers/library/license
into the local SDK. It does not update system tools or vendor source here.
Run `verify_backend.py` with `--tests 2382 --suite projection` on the projection
ELF. Use `verify_target.py` with the matching privately supplied scene's triangle
count/source for the viewer. No discs or hardware testing are required.

