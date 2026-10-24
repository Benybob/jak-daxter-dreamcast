# Measured suspension image budgets

2026-10-24. (AI-assisted). Native fibers now retain
`peak_requested_image_bytes`, the largest validated interval from the suspended
SP to the working stack top. The field is updated after continuation PC/SP/FPU
validation and before backup. A capacity failure leaves the measurement visible
in the faulted C context while invalidating the image. Init and clear reset it.
Malformed saved authorization cannot add a measurement.

This is an observation at suspension, not a body stack high-water mark, stack
overflow detector, ownership proof or automatic allocation/retry policy. A body
that never yields has no observation. Invalid pointers and metadata remain
subject to the existing pinned, caller-owned context contract.

The installed SH-4 GCC 15.1.0/KOS 2.2.1 build measured these existing fixtures:

| Explicit scalar arguments | Largest requested image | Original default | Verified fixture backup |
| --- | ---: | ---: | ---: |
| Six | 312 bytes | 256 bytes | 512 bytes |
| Eight, including nested six-argument call | 432 bytes | 256 bytes | 512 bytes |

The same peaks occur on 8 KiB and 16 KiB working stacks. Tests on a real original
activated main CPU first try the original 256-byte backup and observe OOM before
any partial backup or output write. They explicitly release those test contexts,
use the unchanged original stack-size setter to install a 256-byte-rounded
budget, and start fresh deterministic fixture calls. Both calls yield, survive
destruction of the working stack and complete. The eight-argument call crosses
two suspensions; its completed maximum is checked against the first observation
and the independent 16 KiB-stack run. Root CPU state, original CPU header, process
status and stack fences remain checked. The old capacity is restored before the
independent active-resize rejection probe.

Only these pure diagnostic fixtures are replayed: their original `remove-exit`
calls see a false frame chain and they own no cleanup resources. This does not
authorize retrying game code after effects or abandoning arbitrary continuations.
The global original stack-save default is unchanged. Other bodies, compiler
versions and future bridges require their own measurements and game-wide policy.

Verification: 196 host tests, 22,188 native runtime checks and 39 unchanged numeric
checks pass in stock 16 MiB Flycast. This adds 83 native checks; the host lifecycle
checker passes 98 checks. The numeric ELF and Windows GOAL frontend are unchanged.
Target C context size is now 296 bytes, up four bytes, with prior fields unchanged.
The main diagnostic still uses 141,438 bytes of GOAL/boxed code and
592,912 / 1,048,576 arena bytes with 684 symbols. These exclude executable/KOS,
C contexts/stacks and rendering; they do not establish a full-game memory budget.

Grok independently reviewed the design and integrated source. Codex implemented
and verified the change. Original scheduling, arming/cleanup, GC, unwinding,
platform startup and full gameplay remain unfinished. Physical Dreamcast
validation remains unfinished; no discs are prepared or burned.
