# Missing native function call guards (AI-assisted)

Native runtime call sites now reject zero and #f function targets before JSR.
This covers live symbol cells, typed dynamic function arguments and virtual
method loads. A fault calls the strict `dc-c-call-error` bridge, sets
DC_GOAL_BAD_FUNCTION and exits the current caller through its normal saved
frame/PR/callee-saved epilogue, skipping its remaining body.

Integer/reference callers receive uint64 zero. Float callers explicitly set
FR0 to positive zero with FLDI0; void callers promise no result value. The
epilogue resets SP from the frame pointer even if outgoing stack arguments
were already reserved. The immutable numeric-only profile is unchanged.

Core bridges must be installed before linking runtime modules containing
calls. Core installation now provides three 12-byte tail trampolines: pair
allocation, type-of and missing-call error. An absent fault bridge makes module
linking reject transactionally. The native acceptance runner tests this rollback
before installing the core bridge, rather than binding a dummy function.

## Limits

This is a caller-local bootstrap fault ABI, not general exception/failure
unwinding. It does not roll back effects before the bad call or propagate a
callee's error through arbitrary higher callers. The embedding application must
inspect runtime error status and stop dependent execution. Nonzero nonfalse
targets still require a valid typed function pointer; these guards are not
arbitrary-address validation, execute permissions or a security sandbox.
They do not activate/switch processes, implement catch/throw, or infer an
execution context from an allocation binding.

## Verification

184 host tests pass, including three additional tests for original typed call
signatures, per-caller strict fault imports, and an independent GNU assembler
check of the floating zero return. Existing original-source import audits now
include the explicit call-error dependency.

78 additional native checks cover missing-bridge rollback/early core install,
zero and #f targets for integer, float, void, eight-argument stack calls,
dynamic function arguments and virtual methods. They verify explicit error
status, defined returns, skipped continuation/callee side effects, caller stack
canaries, restored live bindings, valid high-half results and recovery without
heap allocation. All prior native cases still pass: total 12,434 runtime checks
plus 39 numeric checks in stock 16 MiB Flycast.

Diagnostic GOAL code is 133,148 bytes, arena 433,440 / 1,048,576 bytes, 646
symbols. This includes probe modules and excludes executable/KOS/stacks and
renderer; it does not establish whole-game fit or performance. No physical
Dreamcast result or disc image is claimed. Full kernel/gameplay is unfinished.
