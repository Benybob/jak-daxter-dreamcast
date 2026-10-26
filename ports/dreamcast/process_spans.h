/* Exact pinned process-memory ownership, not execution (AI-assisted), ISC. */
#ifndef DC_GOAL_PROCESS_SPANS_H
#define DC_GOAL_PROCESS_SPANS_H
#include "goal_runtime.h"
typedef struct dc_goal_process_span {uint32_t process,bytes;uint64_t generation;} dc_goal_process_span;
typedef struct {
 uint32_t (*owner)(void);
 int (*quiescent)(const dc_goal_runtime *);
} dc_goal_process_span_hooks;
typedef struct dc_goal_process_spans {
 uint32_t magic;dc_goal_runtime *runtime;dc_goal_process_span *rows;
 uint32_t count,capacity,owner;dc_goal_process_span_hooks hooks;
} dc_goal_process_spans;
/* Stable opaque C table/array outside the runtime arena/object and each other.
 * Caller keeps them live and unmodified until detach. cap is the actual sized
 * array capacity, not a field inferred from game data. Hooks are pure and cannot
 * re-enter. Native owner hook is mandatory. One registered runtime at a time. */
int dc_goal_process_spans_init(dc_goal_process_spans *,dc_goal_runtime *,
                              dc_goal_process_span *rows,uint32_t cap,const dc_goal_process_span_hooks *);
/* -1 invalid/full, 0 absent/foreign, 1 configured and room for a fresh note.
 * A configured full table is OOM, before an allocator commits a new process. */
int dc_goal_process_span_room(dc_goal_runtime *);
/* Trusted C owner ONLY: exact declared bytes obtained from its actual allocation
 * or externally pinned buffer. Not a guest-callable ownership assertion. No
 * process-body read or heap-validity/execution inference. Existing exact note is
 * idempotent; overlap/conflicting extent refuses. Fresh notes may occur while
 * another process runs; existing rows/generations are never evicted/replaced. */
int dc_goal_process_span_note(dc_goal_runtime *,uint32_t process,uint32_t bytes,dc_goal_process_span *optional_out);
/* Output is valid disjoint C storage (current working-stack scratch is valid).
 * Lookup alone changes no GOAL word or execution/allocation binding. */
int dc_goal_process_span_lookup(dc_goal_runtime *,uint32_t process,dc_goal_process_span *out);
int dc_goal_process_span_check(dc_goal_runtime *,const dc_goal_process_span *witness);
/* Retirement forgets authority, NOT a physical free or a pool-return operation.
 * It requires the current exact generation and global runtime quiescence. Core
 * teardown detaches only after execution/allocation/scopes are cleared. */
int dc_goal_process_span_retire(dc_goal_runtime *,const dc_goal_process_span *);
int dc_goal_process_spans_detach(dc_goal_runtime *);
/* 0 absent/foreign, 1 registered, -1 invalid owning metadata/context. */
int dc_goal_process_spans_registered(dc_goal_runtime *);
#endif
