/* Bounded cooperative native bootstrap contexts (AI-assisted), ISC. */
#ifndef DC_GOAL_FIBER_H
#define DC_GOAL_FIBER_H
#include "goal_register_snapshot.h"
#include "goal_thread_stack.h"
#include "goal_module.h"
enum dc_goal_fiber_phase {DC_FIBER_EMPTY,DC_FIBER_NEW,DC_FIBER_RUNNING,DC_FIBER_SUSPENDED,DC_FIBER_DONE,DC_FIBER_FAULT};
enum dc_goal_fiber_event {DC_FIBER_FAIL,DC_FIBER_YIELDED,DC_FIBER_COMPLETED};
typedef struct {
 uint32_t magic;enum dc_goal_fiber_phase phase;dc_goal_runtime *runtime;
 uint32_t process,reservation,thread,stack_base,stack_bytes,entry,root_expected_sp,root_live;
 uint64_t value,resume_value;
 struct dc_goal_native_regs root,native_thread;
 dc_goal_thread_stack image;
} dc_goal_fiber;
/* Metadata is stable caller-owned C storage OUTSIDE rt's arena/working stack;
 * keep it live until clear. Code/stack/metadata pinned and quiescent. User entry
 * is trusted zeroarg uint64 ABI; no signature or ownership inferred from pointer.
 * Stack owned/adequate, >=4096B, base/top16aligned; original CPU/process spans
 * and backup capacity explicit. No original CPU PC/SP/register fields rewritten.
 * No implicit process binding/activation, exceptions/longjmp/GC/OS migration. */
/* Inactive initialization invalidates prior owned metadata even on failure;
 * active initialization is rejected before disturbing the running anchor. */
int dc_goal_fiber_init(dc_goal_fiber *,dc_goal_runtime *,uint32_t process,uint32_t reservation,
                       uint32_t thread,uint32_t stack_base,uint32_t stack_bytes,uint32_t entry);
/* Root must run outside working stack. out is separate valid caller C storage
 * outside the runtime object/arena/context;
 * unchanged on failure. resume_value is returned by the blocked yield on resume.
 * This bootstrap allows only fixed native continuation sites, not arbitrary PCs.
 * SUSPENDED frames are copied exactly to original thread backup by the root and
 * restored before resumption, so contexts may share one owned working stack. */
int dc_goal_fiber_step(dc_goal_fiber *,uint64_t resume_value,uint64_t *out);
uint64_t dc_goal_fiber_yield(dc_goal_runtime *,uint64_t value);
int dc_goal_fiber_clear(dc_goal_fiber *);
dc_goal_fiber *dc_goal_fiber_current(void);
#endif
