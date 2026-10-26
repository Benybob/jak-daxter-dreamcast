/* Private same-stack catch return primitive (AI-assisted), ISC. */
#ifndef DC_GOAL_CATCH_RAW_H
#define DC_GOAL_CATCH_RAW_H
#include "goal_register_snapshot.h"

typedef struct {
 struct dc_goal_native_regs regs;
 uint32_t reserved;
 uint64_t result;
} dc_goal_catch_raw;
typedef char dc_catch_result_offset[offsetof(dc_goal_catch_raw,result)==64?1:-1];
typedef char dc_catch_raw_size[sizeof(dc_goal_catch_raw)==72?1:-1];

#ifdef __cplusplus
extern "C" {
#endif

/* INTERNAL, trusted native C only. Not a GOAL import or general saved-context
 * API. Invoke returns exactly once, normally with callback's uint64 result or
 * through escape with frame->result (zero is valid). No returns-twice C site.
 *
 * Preconditions: frame is pinned writable C storage, >=4 aligned, disjoint
 * from callee-used stack; callback/argument follow exact C ABI. Both raw entries
 * run on the SAME live KOS owner/stack, supervisor C context with IRQ/FPU enabled,
 * FR/SZ/PR clear in FPSCR. No yield, restart, abandon, cross-stack transfer,
 * cancel, interrupt entry or migration may cross this scope yet. Nested scopes
 * on that same stack are permitted. The caller authenticates the active frame,
 * lifetime and stack before escape; arbitrary saved memory is NOT a context.
 * Neither raw routine validates these conditions or restores KOS stack records.
 *
 * The saved PC is a closed shim label, SP points at its wrapper's saved PR.
 * R8..R14, FPUL, full FPSCR and raw FR12..FR15 are restored on both exits.
 * Caller-clobbered GPR/FPR, SR/GBR/MAC and alternate FPR bank are not images.
 * This primitive alone does not implement original catch/throw or unwinding. */
uint64_t dc_goal_catch_invoke_raw(dc_goal_catch_raw *,uint64_t (*)(void *),void *);
void dc_goal_catch_escape_raw(dc_goal_catch_raw *) __attribute__((noreturn));
extern const unsigned char dc_goal_catch_invoke_return[];
#ifdef __cplusplus
}
#endif
#endif
