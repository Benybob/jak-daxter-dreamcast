/* Native DGO load-and-link of DCSH objects (AI-assisted), ISC.
 * Native counterpart of the C++ kernel's load_and_link_dgo: the original DGO
 * container layout, with each object a DCSH native module instead of a PS2/x86
 * object file. Objects link and run their startup one at a time, in order. */
#ifndef DC_GOAL_DGO_H
#define DC_GOAL_DGO_H
#include "goal_native_file.h"
#include <stdio.h>
#define DC_GOAL_DGO_MAX_OBJECTS 512u  /* GAME.CGO has 326 code objects */
typedef struct {
  char name[60];
  uint32_t objects,completed,failed_object,code_bytes,initializers;
  /* runtime arena use before the first object and after each completed one */
  uint32_t arena_before,arena_after[DC_GOAL_DGO_MAX_OBJECTS];
  char object_names[DC_GOAL_DGO_MAX_OBJECTS][60];
} dc_goal_dgo_result;
/* Reads the whole DGO from the current position of a caller-owned FILE. Each
 * object links through dc_goal_load_native_stream, then its initializer (if any)
 * runs before the next object is read. The first failure stops loading, keeps
 * earlier objects and their startup effects (as the original does), records
 * failed_object and leaves rt->error set. Returns 1 when every object loaded and
 * started and the file has no trailing data. */
int dc_goal_load_dgo(dc_goal_runtime *,FILE *,dc_goal_cache_sync,void *context,dc_goal_dgo_result *);
#endif
