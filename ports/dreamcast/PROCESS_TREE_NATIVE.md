# Original process-tree traversal and reparenting on SH-4

2026-10-21. (AI-assisted). Four unchanged original gkernel.gc functions now
execute natively against bounded, original-layout fixtures in stock 16 MiB
Flycast: iterate-process-tree, execute-process-tree, search-process-tree and
change-parent. No original function body is copied or rewritten.

Ordered compiler context includes original gcommon, the explicit scalar memory
context, original gkernel-h/gstring-h and original gkernel for declarations.
Only the four selected reader forms export to the native module. Source,
selection and ordered dependency hashes are pinned. Compiler preloads do not
run desktop startup. Their audited native imports are the three recursive
function cells plus #f, #t and dead symbol pointers; no profiler/MMIO bridge is
required by these functions.

Fixtures use the installed original process-tree (32-byte) and process
(112-byte) schemas. Their seven common fields match original compiler offsets:
name0, mask4, parent8, brother12, child16, ppointer20 and self24. Links contain
pointers to stable self fields, not direct object addresses. A bounded 48-byte
kernel-context fixture matches its original type flags and first mask field.
Only tree-prefix fields are exercised; fixtures have no runnable threads.

Two host tests verify original body selection/imports, provenance, exact field
offsets and original kernel-context flags. 604 new native checks cover:

- depth-first traversal and skipping organizational tree nodes;
- all 128 combinations of three prevention bits and four conditional per-node
  run-logic results, including traversing children of a blocked parent;
- dead callback returns pruning their subtree while later siblings continue;
- first-match search, excluding organizational nodes;
- sibling pointers saved before a callback mutates its own sibling link;
- moving first/only/last/later siblings, insertion at the new parent's front,
  same-parent reparenting, subsequent traversal and stable self/ppointer links.

The conditional callbacks are real boxed C probe functions, including an
installed run-logic? method in original slot12, restored afterward. They are
test probes rather than replacement runtime methods or scheduler stubs.
Valid bounded graphs are used; arbitrary malformed/cyclic graphs are not made
safe by this change. Objects are not relocated by these tests, and no GC
relocation or complete process runtime is claimed.

All **161 host tests, 11,217 native runtime checks and 39 numeric checks pass**
in stock 16 MiB Flycast. The four original functions total 3,768 bytes, plus
24 bytes of boxed callback entries. Diagnostic code totals 116,358 bytes;
arena use is 408,816 / 1,048,576 bytes with 577 symbols. Totals include far
probes/padding and the retained shift baseline, excluding KOS/executable/
stacks/renderer; whole-game resource fit is unproven.

Reproduce after building the original typed frontend:

```powershell
./ports/dreamcast/build_host_windows.ps1
python ports/dreamcast/build_runtime_goal.py --verify
python ports/dreamcast/build_numeric_goal.py --verify
```

Full target configuration, original process allocation/heap management, thread
switching, scheduler startup and remaining platform/ABI dependencies remain
unfinished. Physical Dreamcast is available later; no CDs are prepared/burned,
no physical hardware result is claimed, and full gameplay remains unfinished.
