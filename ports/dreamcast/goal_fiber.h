/* Bounded cooperative native bootstrap contexts (AI-assisted), ISC. */
#ifndef DC_GOAL_FIBER_H
#define DC_GOAL_FIBER_H
#include "goal_register_snapshot.h"
#include "goal_thread_stack.h"
#include "goal_module.h"
#include "process_spans.h"
enum dc_goal_fiber_phase {DC_FIBER_EMPTY,DC_FIBER_NEW,DC_FIBER_RUNNING,DC_FIBER_SUSPENDED,DC_FIBER_DONE,DC_FIBER_FAULT};
enum dc_goal_fiber_event {DC_FIBER_FAIL,DC_FIBER_YIELDED,DC_FIBER_COMPLETED};
typedef struct {
 uint32_t magic;enum dc_goal_fiber_phase phase;dc_goal_runtime *runtime;
 uint32_t process,reservation,thread,stack_base,stack_bytes,entry,root_expected_sp,root_live;
 uint64_t value,resume_value;
 unsigned argument_count;uint64_t arguments[8];
 struct dc_goal_native_regs root,native_thread;
 dc_goal_thread_stack image;
 /* Largest validated [suspended SP, stack top) interval requested so far,
  * including a backup OOM. Reset by init/clear; not a stack high-water mark. */
 uint32_t peak_requested_image_bytes;
 /* Opaque KOS owner and suspension SR; host never executes these images. */
 uint32_t platform_owner,native_irq_state;
 /* Original-main import receipt: exact C object address and original PID.
  * Generic init leaves both zero. Not a process disarm or a native security token. */
 uintptr_t main_import_self;uint32_t main_import_pid;
 /* Original enter-state restart: the next root entry boots `entry` afresh on
  * the reset working stack; dead_on_return deactivates the process when the
  * new entry returns (original return-from-thread-dead). */
 uint32_t restart_pending,dead_on_return;
 /* Captured physical lifetime when a ledger is configured at init. Changed
  * or newly installed ownership rejects before guest reads/stack restoration. */
 dc_goal_process_span ownership;
} dc_goal_fiber;
/* Metadata is stable caller-owned C storage OUTSIDE rt's arena/working stack;
 * keep it live until clear. Code/stack/metadata pinned and quiescent. Default init
 * uses trusted zeroarg uint64 ABI; init_words uses its explicit scalar prototype.
 * No signature or ownership is inferred from a pointer.
 * Stack owned/adequate, >=4096B, base/top16aligned; original CPU/process spans
 * and backup capacity explicit. No original CPU PC/SP/register fields rewritten.
 * No implicit process binding/activation, exceptions/longjmp/GC/OS migration. */
/* Inactive initialization invalidates prior owned metadata even on failure;
 * active initialization is rejected before disturbing the running anchor. */
int dc_goal_fiber_init(dc_goal_fiber *,dc_goal_runtime *,uint32_t process,uint32_t reservation,
                       uint32_t thread,uint32_t stack_base,uint32_t stack_bytes,uint32_t entry);
/* Same lifecycle, with0..8 explicit uint64 scalar arguments. Input is a valid C
 * vector, copied before owned metadata is invalidated; later input changes do
 * not affect the context. Entry must match that exact prototype. No inference,
 * floats/structures/varargs or new implicit-self-with-arguments ABI. */
int dc_goal_fiber_init_words(dc_goal_fiber *,dc_goal_runtime *,uint32_t process,uint32_t reservation,
                             uint32_t thread,uint32_t stack_base,uint32_t stack_bytes,uint32_t entry,
                             unsigned argument_count,const uint64_t *arguments);
/* Root must run outside working stack. out is separate valid caller C storage
 * outside the runtime object/arena/context;
 * unchanged on failure. resume_value is returned by the blocked yield on resume.
 * This bootstrap allows only fixed native continuation sites, not arbitrary PCs.
 * SUSPENDED frames are copied exactly to original thread backup by the root and
 * restored before resumption, so contexts may share one owned working stack. */
int dc_goal_fiber_step(dc_goal_fiber *,uint64_t resume_value,uint64_t *out);
uint64_t dc_goal_fiber_yield(dc_goal_runtime *,uint64_t value);
/* From the running body only: finish the current context to its root as DONE
 * with value (FAULT if an error is pending), discarding the working stack.
 * Never returns on success; returns 0 with an error when no valid context runs. */
int dc_goal_fiber_abandon(dc_goal_runtime *,uint64_t value);
/* From the running body only: discard the working stack and continue this
 * context at registered `entry` with 0..8 scalar arguments on a fresh stack.
 * When that entry returns, the process deactivates through its original
 * deactivate method. Never returns on success. */
int dc_goal_fiber_restart(dc_goal_runtime *,uint32_t entry,unsigned argument_count,const uint64_t *arguments);
int dc_goal_fiber_clear(dc_goal_fiber *);
/* The original stack-size-set! resized the running fiber's own main-thread
 * backup (display-loop does, first thing, which the original allows while its
 * process heap has nothing after the thread). Reopens the image view on the new
 * size; only the current running fiber, whose image is restored (not saved). */
int dc_goal_fiber_backup_resized(dc_goal_fiber *);
/* C ownership witness only; does not bind/activate execution. Scheduler hooks
 * call this before process writes or using a suspended fiber's working stack. */
int dc_goal_fiber_ownership_check(const dc_goal_fiber *,dc_goal_runtime *);
/* Internal main-completion C witness: no GOAL writes. Requires an original-main
 * import, DONE, a live matching PID, quiescent storage and the native OS owner.
 * goal_main_start adds the canonical original header/argument checks. */
int dc_goal_fiber_main_completed(dc_goal_fiber *,dc_goal_runtime *);
dc_goal_fiber *dc_goal_fiber_current(void);
#endif
