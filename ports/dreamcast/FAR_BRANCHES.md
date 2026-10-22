# Far branches on SH-4

2026-10-21. (AI-assisted). SH-4 BRA has a signed 12-bit displacement measured
in halfwords. Conditional expansion to BRA/NOP was insufficient for larger
functions such as original valid?, whose exits crossed that range.

Native typed lowering now permits a far jump using a local 32-bit relative
delta and BRAF. It temporarily pushes r0 to the writable GCC stack and restores
it in BRAF's delay slot. Both return halves, T, PR and the destination stack
pointer are preserved. Conditional jumps invert their condition over the whole
far sequence, including its aligned data. Copied code needs no absolute target
relocation. Far jumps, literal islands and conditional expansion share one
iterative final layout. Existing compact code uses its previous machine bytes.

This path requires the supported writable native stack ABI. Unsupported far
BSR calls and BRA delay slots containing useful instructions remain rejected.
Strict emitter mode retains its original range errors. This is not exception
unwinding or a general process/thread transfer implementation.

Five new host checks match GNU assembler output for forward/backward jumps,
conditional skips, concurrent symbol pools and relative deltas, and verify
rejection guards. Six separate native probes (not game bodies) add 40 target
checks of both 64-bit halves, stack restoration, taken/untaken conditions,
carry arithmetic, clear/set T flags and symbol relocations after code rebasing.

All **133 host tests, 8,307 native runtime checks and 39 numeric checks pass**
in stock 16 MiB Flycast. Diagnostic code is 57,560 bytes, including 26,574 bytes
of deliberate long-branch padding/probes and the 3,244-byte shift baseline.
Arena use is 333,824 / 1,048,576 bytes with 249 symbols. These separate figures
exclude executable, KOS, stacks and renderer; they do not measure game fit.

Full original gcommon exports 66 functions; 57 now lower individually. Original
valid? emits 8,248 bytes with seven far jumps and seven literal islands. It is
compile-only: desktop memory constants and broader platform dependencies still
need explicit Dreamcast configuration. Full original kernel/game boot,
processes/heaps, mixed/vector ABIs and gameplay remain unfinished. Physical
Dreamcast is available later; no CDs are prepared or burned at this stage.

Reproduce after building the original frontend from the repository root:

```powershell
./ports/dreamcast/build_host_windows.ps1
python ports/dreamcast/build_runtime_goal.py --verify
python ports/dreamcast/build_numeric_goal.py --verify
python ports/dreamcast/compile_goal_subset.py --frontend ../og-host/bin/goalc-dreamcast-ir.exe --source goal_src/jak1/kernel/gcommon.gc --function valid? --runtime --header ports/dreamcast/build/original_valid.h
```

The valid? command intentionally reads the complete original source for its
macro/type context. It compiles only the selected function and does not execute
startup or its desktop-configured memory checks. Host oracle tests require the
original frontend and SH-4 SDK. Build one SH-4 target at a time.
