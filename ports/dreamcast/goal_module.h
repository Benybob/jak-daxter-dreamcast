/* Native scalar-module binding API (AI-assisted), ISC.
 * This is an in-memory compiler bootstrap format, not the original GOAL object
 * format. Load while the runtime is quiescent; no process scheduling yet.
 */
#ifndef DC_GOAL_MODULE_H
#define DC_GOAL_MODULE_H
#include <stddef.h>
#include <stdint.h>
#include "goal_runtime.h"
typedef struct { uint32_t offset; const char *symbol; } dc_goal_code_relocation;
typedef struct {
  const char *name;
  const unsigned char *code;
  uint32_t bytes;
  const dc_goal_code_relocation *relocations;
  uint32_t relocation_count;
} dc_goal_code_export;
typedef struct {
  const char *name;
  const unsigned char *data;
  uint32_t bytes,address_offset;
  const dc_goal_code_relocation *relocations;
  uint32_t relocation_count;
} dc_goal_data_export;
typedef void (*dc_goal_cache_sync)(uint32_t allocation,uint32_t bytes,void *context);
typedef struct {
  uint32_t functions,code_bytes,arena_bytes,temporary_bytes,data_objects,data_bytes;
} dc_goal_module_result;
typedef struct { uint32_t entry,bytes,symbol; } dc_goal_code_entry_info;
/* Read-only provenance query for an EXACT current export entry, not an
 * interior continuation or code-integrity/security check. Runtime/code/records
 * must be pinned and quiescent; out is separate caller-owned C storage.
 * Clears out on failure. Successful lookup never executes the function. */
int dc_goal_code_entry(dc_goal_runtime *rt,uint32_t entry,dc_goal_code_entry_info *out);
int dc_goal_load_module(dc_goal_runtime *rt,const dc_goal_code_export *functions,uint32_t count,
                         dc_goal_cache_sync sync,void *context,dc_goal_module_result *result);
int dc_goal_load_module_data(dc_goal_runtime *rt,const dc_goal_code_export *functions,uint32_t count,
                              const dc_goal_data_export *data,uint32_t data_count,
                              dc_goal_cache_sync sync,void *context,dc_goal_module_result *result);
/* Explicit startup after linking/cache synchronization. The named function must
 * use the zero-argument scalar ABI. Startup effects are not rolled back.
 * Host builds validate the entry but refuse SH-4 execution. */
int dc_goal_run_initializer(dc_goal_runtime *rt,const char *name,uint64_t *value);
/* Use the C void ABI for original (none) top-level functions. */
int dc_goal_run_initializer_void(dc_goal_runtime *rt,const char *name);
#endif
