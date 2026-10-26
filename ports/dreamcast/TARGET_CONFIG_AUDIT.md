# Original kernel target-configuration audit

2026-10-26. (AI-assisted, Claude Code). This is a source inventory for the open
"remaining full-kernel target configuration" task. It changes no original source,
compiler, runtime or native target and executes nothing.

`audit_target_config.py` reads unmodified GOAL text with a strict reader
(nested block comments, strings, character literals, quote/quasiquote prefixes;
unbalanced or unterminated input rejects). It reports:

- **Roots**: every `defglobalconstant`, plus compiler-supplied `INSTRUCTION_SET`
  and any `--root` names.
- **Configuration branches**: each `#cond`/`#when`/`#unless`/`#if` whose
  condition names a root or dependent constant, its enclosing top-level form and
  whether it sits inside a macro template (and so expands at every use).
- **Dependent definitions**: macros, constants and globals that sit in a
  configuration branch, reference a dependent name or embed a PS2 hardware
  address. This closes transitively, so `in-scratchpad?` follows
  `scratchpad-start` back to `PC_PORT`. Functions, methods and types are reported
  as use sites but do not propagate further.
- **Use sites**: every top-level form referencing a root or dependent name,
  with per-name line numbers. A definition's own name is not a reference.
- **Hardware literals**: integers in EE registers `0x10000000-0x1000FFFF`,
  VU memory `0x11000000-0x1100FFFF`, GS privileged `0x12000000-0x12001FFF`
  and scratchpad `0x70000000-0x70004000` (inclusive top pointer). These are
  candidates; a literal may be a bit mask.
- **Inline-assembly functions** (`declare (asm-func ...)`) and whether each has
  an `INSTRUCTION_SET` branch. SH-4 has neither the x86-64 nor the ARM64 body.

## Original kernel result

All eight `goal_src/jak1/kernel/*.gc` files at the verified upstream baseline:

| Category | Count | Notes |
| --- | ---: | --- |
| Roots | 6 | `PC_PORT`, `BIG_MEMORY`, `PC_BIG_MEMORY`, `USE_VM`, `PC_PROFILER_ENABLE`, `KERNEL_DEBUG` |
| Configuration branches | 24 | 12 `INSTRUCTION_SET`, 4 `PC_PORT`, 4 profiler, 2 `KERNEL_DEBUG`, 1 `PC_BIG_MEMORY`, 1 `USE_VM`; 7 inside macro templates |
| Dependent definitions | 27 | scratchpad macros/globals, `PROCESS_STACK_SIZE`, `PROCESS_HEAP_MULT/SIZE`, dead pools, profiler, `suspend`, `process-spawn` |
| Use sites | 39 | includes `kernel-dispatcher`, `activate`, `deactivate`, `enter-state` |
| Hardware literals | 2 | scratchpad start and `*scratch-memory-top*` |
| Inline-assembly functions | 8 | all have x86-64/ARM64 branches only; none has an SH-4 body |

The committed report is
[evidence/target-config-audit.json](evidence/target-config-audit.json). Source
hashes are taken over LF-normalized text, so Windows `autocrlf` and Linux
checkouts pin the same bytes as the Git blobs.

What this shows for Dreamcast startup: the eight asm functions (thread
return/suspend/resume, `reset-and-call`, `throw-dispatch`, bootstrap and
`catch-frame` `new`) need explicit SH-4 implementations or the existing native fiber bridges.
`PC_PORT` currently selects the fake 128 KiB scratchpad allocation and a 24 KiB
process stack; `PC_BIG_MEMORY` triples the shared process heap feeding every
dead pool. Each must be set deliberately before original kernel startup.

## Limits

This is a static text inventory. It cannot see PC changes made by editing a
value without a conditional (for example `DPROCESS_STACK_SIZE`, increased to
32 KiB in a comment-documented edit), C++ runtime behavior, or meaning that
depends on macro arguments at each call. An unlisted form is not proven
portable. The same reader parses all 533 jak1 `.gc` files without error; pass
`--source` for wider scans. Full kernel startup and gameplay remain unfinished.

## Repeat

```powershell
python ports/dreamcast/audit_target_config.py --check ports/dreamcast/evidence/target-config-audit.json
python -m unittest ports/dreamcast/tests/test_target_config_audit.py
```

Regenerate with `--output` only after reviewing an upstream source change. Ten
host tests cover the reader, comments/strings/characters, dependency reasons,
transitive closure, templates, region boundaries, extra roots, fail-closed
parsing and evidence drift. No ISO, SDK or emulator is needed.
