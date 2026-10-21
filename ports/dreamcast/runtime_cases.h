/* Shared host/SH-4 runtime acceptance cases (AI-assisted), ISC. */
#ifndef DC_RUNTIME_CASES_H
#define DC_RUNTIME_CASES_H
#include <stdint.h>
typedef void (*dc_runtime_check)(uint64_t actual,uint64_t expected,unsigned id,
                                const char *description,void *context);
typedef struct { uint32_t arena_bytes,boot_bytes,live_before_stress,symbols; } dc_runtime_stats;
/* memory must be 16-byte aligned and have 16 writable guard bytes at each end. */
void dc_runtime_cases(void *memory,uint32_t capacity,uint32_t base,
                       dc_runtime_check check,void *context,dc_runtime_stats *stats);
#endif
