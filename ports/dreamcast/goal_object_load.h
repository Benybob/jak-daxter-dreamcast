/* Native V4 data relocation with symbol/arena rollback (AI-assisted), ISC. */
#ifndef DC_GOAL_OBJECT_LOAD_H
#define DC_GOAL_OBJECT_LOAD_H
#include "goal_runtime.h"
#include "goal_object.h"
#define DC_GOAL_DATA_MAX_BYTES (2u*1024u*1024u)
typedef struct {
  uint32_t data,bytes,arena_bytes,temporary_bytes,imports,relocations;
} dc_goal_data_result;
/* Original retail V4 data only. Fully validates before mutation; no original
 * relocation method, MIPS code or startup is executed. Forward type imports
 * follow original interning rules; later deftype fills their metadata. */
int dc_goal_load_data_object(dc_goal_runtime *rt,const void *source,uint32_t bytes,dc_goal_data_result *result);
#endif
