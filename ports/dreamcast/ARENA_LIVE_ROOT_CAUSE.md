(AI-assisted), ISC.

# Review 2 diagnosis before changing the recording

The supplied run is 2088 passed of 2090, first failure 12089. Its ELF SHA-256
is 8e756dcbb462c16f844f2fcc990bd73218b7cd7342321aab04962edf35949bc2,
matching the existing jak-dc-arena-live.elf. SH-4 objdump of
_dc_live_recorded_pad at 0x8c028d9e loads 0x10002 for completed slots 42/43:
B/circle plus synthetic L2, NOT the right-X recording now on disk.
The dump SHA-256 is 7a49438ba469a7919fb0e93735687907f59d80d6be8c2d6769f9cb04f971f3ca.
Decoded header: reason 8, ratio 4, calls/returns/witnesses 43, affine reads/draws
42, orientation changes 0. Rows 41/42/43 have delta 1 and yaw
2.1999999427825365; all three process resume counts increase each row.

The missing initialization is the root cause of the digital yaw failure.
`goal_src/jak1/engine/camera/cam-states-dbg.gc:71` gates circle/square rotation
on `(not *cam-layout*)`. `goal_src/jak1/engine/camera/cam-debug-h.gc:150` defines
`(define-perm *cam-layout* symbol #f)` at its end, in the debug segment
(the `(declare-file (debug))` at line 73). The emitted
`build/game_cam_debug_h.ir.json` top-level instructions 0..2 skip the entire
initializer when *debug-segment* is #f. `kernel_boot_cases.c:143` explicitly sets
that symbol to #f before loading GAME.CGO. `goal_runtime.c:274` interns new
symbols with value 0. Numeric 0 is NOT GOAL #f. Thus *cam-layout* remains
truthy and the digital rotation block does not run. The live observer did
not check that gate. Analog translation at cam-states-dbg.gc:212..246 is
outside it, explaining the twelve successful strafe outputs.

The notes and reviews incorrectly called the centered right-stick code an
overwrite: cam-states-dbg.gc:243 subtracts from existing rv.y. It does NOT
erase circle yaw. With L2 held, lines 238..241 instead add translation and
leave rv unchanged; centered sticks add zero. The historical circle+L2
recording would turn after the missing false initialization is supplied.
The current right-X recording bypasses the digital gate; it was a later,
unmeasured edit, not evidence that a new input fixes this run.

This is neither camera smoothing nor a legitimate dead zone. analog-input
at pad.gc:199..214 maps right-X 207 to -0.5. cam-free-floating-move at
cam-states-dbg.gc:261..275 immediately applies rv through axis-angle matrices.
The single-slave/nontracking combiner directly copies tracking inv-mat
at cam-combiner.gc:214..246. update-camera at cam-update.gc:290..303 copies
that raw basis. Its later smoothing at 321..339 writes a separate smooth
matrix, not the basis this observer reads.

There is no evidence of a mode transition or stalled process: free-floating
state is unchanged, all resumes increase, runtime_error is zero, game mode
and the ordinary branch witnesses passed. Nor is an extra input latency a
supported explanation: raw strafe deltas are exactly outputs 17..28 for
completed slots 16..27, and zero on 16/29. Outputs 41/42 correctly remain
neutral because slots 40/41 are neutral; 43 was the first expected turn.
Moving the turn gate later would hide this actual missing response. Retain
the two exact response samples and require the neutral lead-in too.

The angle check has a separate error: display.gc:40 clamps slowdown to 4;
43..44 multiply PAL adjust by 1.2, admitting 4.8. A legal 0.6-degree step at
4.8 is 0.0502655 rad, above 0.043. The corrected gate must use the ratio
consumed by each original update, with an error allowance for the original
integer-angle sin/cos implementation, rather than widening a fixed ceiling.
The same maximum adjust must inform recording bounds while preserving the
6 m X/Z and 1 m Y caps and the 4.5..20 m raw-eye path requirement.

The 13312-byte v2 dump is explicitly a 61-slot prefix with 120 uncaptured
slots. It contains row 43 and does not hide this failure. A successful run
must request the complete versioned evidence size automatically and the
strict auditor must continue refusing all truncated evidence.

The host typed-IR diagnostic `tests/check_arena_live_original_input.py` replayed
that original frontend function, using its branch/load/store operations. With
truthy layout zero, circle+L2 yields rv=(0,0,0), while left-X 180 still yields
tv.x=-825.806396484375 at ratio 4. With GOAL false, the same circle+L2 yields
rv.y=-436.90667724609375 (2.4 degrees) and zero translation. Centered analog
without L2 preserves exactly that digital yaw too. Right-X 207 was checked
at ratios 1, 1.2, 4 and 4.8. These are host IR results, not target execution.
