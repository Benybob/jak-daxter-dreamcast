# Native kernel stack objects

2026-10-26. (AI-assisted: Codex, with a reviewed human-authorized Grok proposal.)

The original frontend now exports typed stack addresses and its complete stack
reservation. The SH-4 lowerer allocates a separate function-lifetime region after
register spills and lexical cleanup state. Original eight-byte slot spacing and
sixteen-byte base alignment are preserved. Native P0/P1 addresses are carried as
zero-extended scalar pointers, including root C stacks outside the GOAL arena.
Malformed metadata and oversized frames refuse before emission. Reservations
cannot bypass frame allocation through leaf fast paths.

Both optimized and baseline fixtures execute stack arrays and a boxed object's
original typed constructor. Native checks cover distinct arrays, bounds,
alignment, high pointer words, 64-bit fields, outgoing and mixed calls, register
spills, lexical cleanup and original suspension. The suspension oracle destroys
the shared working stack before resuming and checks restored local data, root
canaries and floating-point state. These fixtures do not return escaped pointers.

Verification: 443 host tests, 23,639 runtime checks (83 new), and a freshly built
39-check numeric regression pass in stock 16 MiB Flycast. Runtime generated code
and bridges occupy 172,606 bytes; the diagnostic arena uses 881,360 of 1,048,576
bytes with 780 symbols. These figures include diagnostic working stacks and
exclude KOS, the executable, root C stacks and the renderer. They do not prove
whole-game memory fit or physical Dreamcast behavior.

The unchanged original `run-function-in-process` now lowers in both modes, with
its parameter array and catch-frame stack allocations. The reproducible kernel
survey advances to 167/184 functions (gkernel 66/75). This is compilation
coverage, not kernel execution coverage. Native catch construction and
throw-dispatch still need authenticated saved contexts, per-fiber lifetime and
scope unwinding before this original function can execute. Full startup, moving
heaps/GC and full gameplay remain unfinished.

Run `build_host_windows.ps1`, `build_runtime_goal.py --verify`, and
`build_numeric_goal.py --verify` with the documented local SDK setup. The host
suite includes malformed metadata, frame-budget and unchanged-original-source
tests in `tests/test_stack_objects.py`; the native cases are in
`runtime_stack_objects_cases.c` and its separate baseline translation unit.
