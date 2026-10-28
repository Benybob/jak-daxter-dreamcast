/* Bounded native kernel bridges (AI-assisted), ISC. */
#ifndef DC_GOAL_BRIDGE_H
#define DC_GOAL_BRIDGE_H
#include "goal_module.h"
#include "goal_fiber.h"
/* One active runtime, quiescent installation, scalar GCC ABI only.
 * Installs pair new (global selector only), namespaced type-of and missing-call
 * error bridges. Install before linking runtime modules containing calls.
 * No complete original heap/process allocator or full kernel API yet. */
int dc_goal_install_core_bridges(dc_goal_runtime *rt,dc_goal_cache_sync sync,
                                  dc_goal_module_result *result);
void dc_goal_clear_core_bridges(dc_goal_runtime *rt);
/* Borrowed ownership check; does not install or infer execution identity. */
int dc_goal_core_bridges_active(const dc_goal_runtime *rt);
/* Requires the same active core runtime; installs original type/new and
 * method-set! paths plus bounded basic allocation (global or explicit process span). */
int dc_goal_install_type_bridges(dc_goal_runtime *rt,dc_goal_cache_sync sync,
                                  dc_goal_module_result *result);
/* Scalar integer faults return zero and set runtime error status. This is a
 * bootstrap fault ABI, not complete original exception/process semantics. */
int dc_goal_install_integer_bridge(dc_goal_runtime *rt,dc_goal_cache_sync sync,
                                    dc_goal_module_result *result);
/* Explicit sized object.new(heap,type,size): global, or explicitly bound process
 * reservation. No GC, thread switching or exception/failure unwinding. */
int dc_goal_install_object_bridge(dc_goal_runtime *rt,dc_goal_cache_sync sync,
                                   dc_goal_module_result *result);
/* Caller owns the full unmoved process reservation, including 112-byte header/
 * fields. Same-runtime binding failure clears the old target, with no global
 * fallback. No implicit pp/self ABI or thread context is installed. */
int dc_goal_bind_process_heap(dc_goal_runtime *rt,uint32_t process,uint32_t allocation_bytes);
void dc_goal_clear_process_heap(dc_goal_runtime *rt);
/* Read-only implicit self entry: explicit, quiescent, unmoved base-process
 * reservation, and the initialized original *kernel-context* object. Binding
 * sets its current-process field; clear sets it back to #f if still owned.
 * Allocation-only rebinding/clearing invalidates execution identity. A failed
 * getter clears both bindings and returns zero with BAD_ARGUMENT before body
 * memory access. No scheduler, nested contexts, GC or failure unwinding. */
int dc_goal_install_process_bridge(dc_goal_runtime *rt,dc_goal_cache_sync sync,
                                    dc_goal_module_result *result);
int dc_goal_bind_process_context(dc_goal_runtime *rt,uint32_t process,uint32_t allocation_bytes);
void dc_goal_clear_process_context(dc_goal_runtime *rt);
/* Explicit pinned lexical pp + allocation domain over the independently bound
 * scheduler process. Caller-owned metadata stays outside the arena/fiber stack.
 * Registration alone establishes no identity. No frontend mutable-pp lowering,
 * GOAL exit/unwind or scheduler support. Native step/yield refuse live scopes;
 * leaked scopes on native return restore metadata and fault the fiber. */
struct dc_goal_pp_scope_stack;
int dc_goal_register_pp_scope(dc_goal_runtime *,struct dc_goal_pp_scope_stack *);
int dc_goal_enter_lexical_scope(dc_goal_runtime *,uint32_t process,uint32_t bytes);
int dc_goal_leave_lexical_scope(dc_goal_runtime *,uint32_t process,uint32_t bytes);
/* Internal fixed native-return site, before the raw transition to the root. */
void dc_goal_lexical_native_return(dc_goal_runtime *);
struct dc_goal_process_spans;
struct dc_goal_process_span;
/* Configure pinned caller-owned ownership metadata before process construction.
 * Symbol/global process allocations are captured from the actual allocator size.
 * Other raw buffers require an explicit TRUSTED C ownership note. Neither path
 * binds execution. Moving/nested process suballocations remain unsupported. */
int dc_goal_register_process_spans(dc_goal_runtime *,struct dc_goal_process_spans *,struct dc_goal_process_span *,uint32_t capacity);
int dc_goal_note_process_span(dc_goal_runtime *,uint32_t process,uint32_t bytes,struct dc_goal_process_span *optional_out);
/* An explicit request selects an OWNED span; byte counts are never inferred
 * from process fields. Restore is a separate operation, not a value heuristic. */
int dc_goal_enter_known_scope(dc_goal_runtime *,uint32_t process);
int dc_goal_leave_known_scope(dc_goal_runtime *,uint32_t previous_process);
/* Installs dc-c-enter-process-scope/dc-c-leave-process-scope for compiled
 * original lexical pp brackets; requires registered spans and pp scopes. */
int dc_goal_install_scope_bridge(dc_goal_runtime *,dc_goal_cache_sync,dc_goal_module_result *);
/* Installs dc-c-return-from-thread (fiber abandon to its root) and dc-c-suspend
 * (fiber yield). Without a running fiber for the bound scheduler process both
 * record DC_GOAL_UNSUPPORTED_TRANSFER and the compiled body exits. */
int dc_goal_install_transfer_bridge(dc_goal_runtime *,dc_goal_cache_sync,dc_goal_module_result *);
/* dc-c-thread-resume (also installed above) is the native original cpu-thread
 * resume-hook: it steps the fiber registered for that thread. Fibers are
 * caller-owned; register before the original kernel-dispatcher runs and
 * unregister before clearing. At most eight registrations. */
int dc_goal_register_thread_fiber(dc_goal_runtime *,dc_goal_fiber *);
int dc_goal_unregister_thread_fiber(dc_goal_runtime *,dc_goal_fiber *);
/* Caller-owned original *kernel-dram-stack* span used by dc-c-reset-and-call for
 * post hooks; [base, base+bytes) with top equal to the original stack-top. */
int dc_goal_register_kernel_stack(dc_goal_runtime *,uint32_t base,uint32_t bytes);
/* Desktop C++-kernel services called by the original kernel startup: _format
 * (bounded directive subset, capture ring plus debug output) and global malloc. */
#define DC_GOAL_FORMAT_CAPTURE 2048u
/* __pc-get-mips2c lookups, and how many were answered with a native SH-4 mips2c
 * function (mips2c_dc.h); the rest get the stub that faults if called. */
uint32_t dc_goal_mips2c_requests(void);
uint32_t dc_goal_mips2c_native(void);
/* The current process for a mips2c call's pp (s6): #f at the root. */
uint32_t dc_goal_mips2c_current_process(void);
int dc_goal_install_kernel_services(dc_goal_runtime *,dc_goal_cache_sync,dc_goal_module_result *);
/* Wraps original stack-size-set! so main-thread backups keep a 512-byte native floor. */
int dc_goal_install_backup_floor(dc_goal_runtime *,dc_goal_cache_sync,dc_goal_module_result *);
/* PC-port pc-* and file-stream services with desktop's no-display answers. */
int dc_goal_install_pc_services(dc_goal_runtime *,dc_goal_cache_sync,dc_goal_module_result *);
uint32_t dc_goal_format_capture(const char **text);
void dc_goal_format_capture_clear(void);
void dc_goal_clear_kernel_stack(void);
/* Data-only first CPU-thread construction through the unchanged original
 * native constructor. Caller owns the unmoved process reservation and a
 * disjoint aligned execution-stack span in rt's arena. Main/top thread must
 * both be false. Base cpu-thread only, aligned backup size, strict process
 * heap fit. No temporary threads, PC initialization, execution or switching.
 * Allocation/execution bindings are not changed. No body writes on rejection. */
uint32_t dc_goal_construct_main_thread(dc_goal_runtime *rt,uint32_t process,uint32_t reservation_bytes,
                                       uint32_t stack_base,uint32_t stack_bytes,uint32_t name,uint32_t backup_bytes);
/* Explicit cooperative-yield bridge; no original scheduler/activation. */
int dc_goal_install_fiber_bridge(dc_goal_runtime *,dc_goal_cache_sync,dc_goal_module_result *);
/* Original process.activate on a fresh base process and empty base tree only.
 * Explicit pinned owner/stack spans; no allocation/self binding is inferred.
 * Preflight rejects unsupported schema, live context, scratchpad, bad entries,
 * stack overlap and insufficient original 256-byte backup reservation before
 * body effects. Success creates/attaches a ready process; never runs its code.
 * No rollback of target effects, derived process, scheduler or GC support. */
uint32_t dc_goal_activate_process(dc_goal_runtime *,uint32_t process,uint32_t reservation,
                                 uint32_t destination,uint32_t stack_base,uint32_t stack_bytes,uint32_t name);
/* Original void stack-size-set! on an owned fresh main CPU thread. No live
 * native contexts may use this thread; active fibers are rejected. Checked
 * aligned sizes and strict heap headroom, no intervening allocation for a size
 * change. Same-size no-op may follow later allocation. Borrowed image views
 * must be reopened. No payload clearing, bindings, execution or scheduler. */
int dc_goal_resize_main_backup(dc_goal_runtime *,uint32_t process,uint32_t reservation,
                               uint32_t thread,uint32_t stack_base,uint32_t stack_bytes,uint32_t backup_bytes);
/* First original catch adapter: only live root C-stack lexical pp scopes.
 * Registered process ownership and supported KOS owner/stack required. No
 * fiber/yield/cross-stack catch lifetime yet; guest RA/SP is never executed. */
int dc_goal_install_catch_bridge(dc_goal_runtime *,dc_goal_cache_sync,dc_goal_module_result *);
/* Global-heap objects of type_name with at least min_bytes go to the runtime's
 * extension region (dc_goal_register_extension). Placed count, and their bytes. */
int dc_goal_place_in_extension(dc_goal_runtime *rt,const char *type_name,uint32_t min_bytes);
uint32_t dc_goal_extension_placed(uint32_t *bytes);
#endif
