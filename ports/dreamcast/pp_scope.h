/* Caller-owned lexical process spans (AI-assisted), ISC.
 * Separates lexical pp from scheduler current-process and from the allocation
 * record. This is not process deactivation, a fiber switch, or an SR image. */
#ifndef DC_GOAL_PP_SCOPE_H
#define DC_GOAL_PP_SCOPE_H
#include <stdint.h>
#include "goal_process_heap.h"
#include "goal_fiber.h"

/* Bootstrap bound, not an original kernel limit. Original r13 nesting is the
 * GOAL call stack. Overflow refuses the new enter and keeps every outer frame. */
#define DC_GOAL_PP_SCOPE_MAX_DEPTH 8
typedef char dc_goal_pp_scope_depth_check[
    (DC_GOAL_PP_SCOPE_MAX_DEPTH >= 2 && DC_GOAL_PP_SCOPE_MAX_DEPTH <= 32) ? 1 : -1];

enum dc_goal_pp_scope_result {
  DC_GOAL_PP_SCOPE_DENIED = -1, /* live span failed auth; do not run the body */
  DC_GOAL_PP_SCOPE_ABSENT = 0,  /* no live span; caller uses the scheduler path */
  DC_GOAL_PP_SCOPE_READY = 1    /* *lexical_process is the authenticated top */
};

/* current_owner is the only platform hook. Dreamcast integration passes
 * dc_goal_kos_stack_owner. A NULL hook is the host direct-C contract and stores
 * owner 0. That 0 is not a native security token. A non-NULL hook that returns
 * 0 is rejected. Native builds require a non-NULL owner hook. The hook must not re-enter this module, allocate, or write
 * GOAL memory. No SR value is accepted or executed. */
typedef struct dc_goal_pp_owner_hooks {
  uint32_t (*current_owner)(void);
} dc_goal_pp_owner_hooks;

/* One lexical span. Addresses and sizes only: no interior GOAL pointer, no
 * native SP, no SR, no register image. pid_witness is process+36 at enter.
 * Original r13 does not snapshot PID. The witness is a bridge fail-closed
 * contract until a pool-reuse epoch exists. It uses DC_GOAL_BAD_ARGUMENT. */
typedef struct dc_goal_pp_scope_frame {
  uint32_t context;
  uint32_t scheduler_process;
  uint32_t scheduler_bytes;
  uint32_t scheduler_pid_witness;
  uint32_t lexical_process;
  uint32_t lexical_bytes;
  uint32_t pid_witness;
  dc_goal_process_heap saved_allocation;
} dc_goal_pp_scope_frame;

/* Caller-owned C storage. Keep it outside rt's arena, the runtime object, the
 * current fiber object, and the fiber working stack, and do not move it until
 * detach or abort returns. magic detects uninitialized storage. It is not an
 * OS capability. The fiber anchor for the whole nest lives here, not in each
 * frame, and it is not a copy of SP or SR. */
typedef struct dc_goal_pp_scope_stack {
  uint32_t magic;
  dc_goal_runtime *runtime;
  dc_goal_pp_owner_hooks hooks;
  uint32_t owner;
  dc_goal_fiber *fiber;
  int anchor_direct;
  unsigned depth;
  dc_goal_pp_scope_frame frames[DC_GOAL_PP_SCOPE_MAX_DEPTH];
} dc_goal_pp_scope_stack;

/* Registers one stack for rt. Success preserves rt->error. Failure does not
 * write a stack that overlaps the arena or displace a different registered
 * stack. Depth above zero on this same stack is left in place. */
int dc_goal_pp_scope_init(dc_goal_pp_scope_stack *stack, dc_goal_runtime *rt,
                          const dc_goal_pp_owner_hooks *hooks);

/* Pushes one lexical reservation under an authenticated scheduler reservation.
 * current_allocation is copied into the frame before *installed_allocation is
 * written. The two pointers may be identical. A partial overlap is rejected.
 * Both must be caller-owned C objects outside the arena and outside this stack.
 * On success *installed_allocation is the lexical identity the bridge installs
 * as its process heap. Failure does not change depth, either heap object, or
 * any GOAL word. Enter requires rt->error == DC_GOAL_OK and does not clear a
 * retained fault. Depth 0 requires current_allocation to be the scheduler
 * reservation. A nested enter requires it to be the parent lexical reservation.
 * The scheduler reservation must stay the base frame's reservation for the
 * whole nest. *kernel-context* current-process is read and not written. */
int dc_goal_pp_scope_enter(dc_goal_pp_scope_stack *stack, dc_goal_runtime *rt,
                           uint32_t context, uint32_t scheduler_process,
                           uint32_t scheduler_bytes, uint32_t lexical_process,
                           uint32_t lexical_bytes,
                           const dc_goal_process_heap *current_allocation,
                           dc_goal_process_heap *installed_allocation);

/* Pops the named top span and writes the saved allocation record.
 * *restored_allocation is unmodified on failure. A stale lexical reservation
 * still pops: leave restores the saved record by value and does not reopen the
 * span it is leaving. The saved record must name this runtime. rt->error is
 * preserved, including DC_GOAL_OK and a retained fault. The context current
 * field is not written. Out-of-order names, the wrong owner, and a lost fiber
 * anchor do not pop. */
int dc_goal_pp_scope_leave(dc_goal_pp_scope_stack *stack, dc_goal_runtime *rt,
                           uint32_t lexical_process, uint32_t lexical_bytes,
                           dc_goal_process_heap *restored_allocation);

/* Authenticates the registered top for this runtime. ABSENT means live magic
 * with depth 0, or no registration for this runtime. The getter then keeps its
 * scheduler path, including the allocation/execution equality check. Bad magic
 * for this runtime is DENIED even at depth 0. READY writes *lexical_process
 * and restores the rt->error from entry, so a retained fault survives
 * authentication, including through the scoped getter. DENIED leaves the stack
 * and both bindings in place. Authentication may read the context schema, type
 * and heap fields, and the PID word at process+36. It does not write GOAL
 * memory. A non-OK rt->error is kept on DENIED. */
int dc_goal_pp_scope_resolve(dc_goal_runtime *rt, uint32_t context,
                             uint32_t scheduler_process, uint32_t scheduler_bytes,
                             const dc_goal_process_heap *allocation,
                             uint32_t *lexical_process);

/* 1 when a live or corrupt registration must refuse fiber yield, fiber resume,
 * and any future thread switch. Depth 0 with live magic does not block.
 * This predicate does not drop a registration. */
int dc_goal_pp_scope_blocks_transfer(const dc_goal_runtime *rt);

/* 1 when process-context clear may run for rt. Depth above zero, corrupt
 * magic, and unsafe stack storage return 0. The predicate does not write and
 * does not drop a registration. */
int dc_goal_pp_scope_runtime_clear_allowed(const dc_goal_runtime *rt);

/* 0 when the registration is missing, unusable, or not live magic. Switch and
 * clear use the predicates above, not this count. */
unsigned dc_goal_pp_scope_depth(const dc_goal_runtime *rt);
dc_goal_pp_scope_stack *dc_goal_pp_scope_active(const dc_goal_runtime *rt);

/* Depth must be 0. Zeroes the caller-owned object and drops the registration.
 * Does not write GOAL memory. Preserves rt->error. */
int dc_goal_pp_scope_detach(dc_goal_pp_scope_stack *stack, dc_goal_runtime *rt);

/* Teardown only. Drops every frame without running GOAL exits and without
 * restoring an allocation record. The bridge clears its own heap bindings
 * afterwards. Preserves rt->error. A safe stack returns 1. Unsafe overlap
 * returns 0 after dropping the registration and does not memset the stack. */
int dc_goal_pp_scope_abort(dc_goal_pp_scope_stack *stack, dc_goal_runtime *rt);
/* Internal fixed native-return site only, while the original fiber is current.
 * ABSENT: nothing live. READY: leaked nest restored to its saved base allocation
 * and zero depth. DENIED: invalid metadata dropped without GOAL writes. Caller
 * must make either non-ABSENT result a retained native fault; on DENIED it also
 * invalidates execution/allocation bindings. Never runs original GOAL exits. */
int dc_goal_pp_scope_native_return(dc_goal_runtime *rt,dc_goal_process_heap *restored_allocation);
#endif
