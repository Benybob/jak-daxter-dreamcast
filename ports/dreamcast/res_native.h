/* res-lump lookup-tag-idx in C for the level ELF (AI-assisted), ISC. res_native.c */
#ifndef DC_RES_NATIVE_H
#define DC_RES_NATIVE_H
#include <stdint.h>
#include "goal_runtime.h"
#include "goal_module.h"
/* Calls since install; how many also ran the original GOAL method; how many
 * of those differed, with the first one's results and name symbol.
 * The by_name counts are entity-by-name's (calls, answers from the table,
 * hits also run in GOAL, of those that differed, table clears).
 * --dump dc_res_native 56 */
typedef struct {uint32_t calls,checked,mismatches,name;uint64_t got,want;
 uint32_t by_name_calls,by_name_hits,by_name_checked,by_name_mismatches,by_name_flushes,bones_skipped;} dc_res_native_counts;
extern volatile dc_res_native_counts dc_res_native;
/* After GAME.CGO: res-lump's method 19 (and descendants holding it) becomes
 * the C version. Returns 0, changing nothing, if the type or method is not
 * as expected. */
int dc_res_native_install(dc_goal_runtime *rt,dc_goal_cache_sync sync);
/* entity-by-name answered from a table under the levels' signature. */
int dc_res_native_install_by_name(dc_goal_runtime *rt,dc_goal_cache_sync sync);
/* bones-mtx-calc-execute only empties its list (the PS2 renderers are off). */
int dc_res_native_install_bones(dc_goal_runtime *rt,dc_goal_cache_sync sync);
#endif
