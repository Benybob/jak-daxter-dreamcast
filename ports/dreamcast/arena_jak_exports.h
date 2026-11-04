/* Separate export receipt; original player ABI stays intact (AI-assisted), ISC. */
#ifndef DC_ARENA_JAK_EXPORTS_H
#define DC_ARENA_JAK_EXPORTS_H
#include "goal_module.h"
enum {DC_JAK_EXPORT_COUNT=4}; /* start, dispatcher, read-pad, init-target */
typedef struct {
  uint32_t symbol,value,owner,bytes,error;
  dc_goal_code_diagnostic diagnostic;
} dc_jak_export_row;
typedef struct {
  uint32_t used,capacity,first_failed;
  dc_jak_export_row exports[DC_JAK_EXPORT_COUNT];
} dc_jak_export_snapshot;
typedef struct {
  uint32_t version,completed,first_failed_stage,prior_error;
  dc_jak_export_snapshot stages[2]; /* before art login, before start */
} dc_jak_exports_evidence;
extern volatile dc_jak_exports_evidence dc_arena_jak_exports_evidence;
int dc_arena_jak_exports(dc_goal_runtime *,unsigned);
#endif
