/* Streamed (spooled) animation chunks for the level ELF (AI-assisted), ISC. */
#ifndef DC_LEVEL_STR_H
#define DC_LEVEL_STR_H
#include "goal_runtime.h"
/* Serves rpc-call port 4 (STR chunks from /cd/STR) and binds the C kernel's
 * link (retail v2 objects) through the bridge's hooks. */
void dc_level_str_install(dc_goal_runtime *rt);
/* At boot: the KOS block the spool heaps move to, before the heap fragments. */
void dc_level_str_reserve(void);
/* Each frame: the spool buffers' status (dc_level_spool_probe). */
void dc_level_str_sample(dc_goal_runtime *rt);
/* Chunks and files served, objects linked, refused links, frames a spool
 * buffer was 'active. */
typedef struct {unsigned served,linked,link_errors,active_frames,files,chunk_requests,chunk_served;} dc_level_str_counts;
void dc_level_str_summary(dc_level_str_counts *out);
#endif
