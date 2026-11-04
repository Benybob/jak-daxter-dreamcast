/* Retail v4 DATA only; original GOAL relocate/login boundary (AI-assisted), ISC. */
#ifndef DC_GOAL_ART_H
#define DC_GOAL_ART_H
#include "goal_runtime.h"
#include <stdio.h>
typedef struct {
  uint32_t before,capacity,temporary,payload,available,shortfall;
  uint32_t objects,linked,relocated,pointers,bindings,art[2];
} dc_goal_art_result;
/* Caller owns a seekable, trusted JAKART.DGO staged by stage_jak_art.py.
 * No retail instructions are executed. The callback must invoke original
 * art-group.relocate (which performs login and set-loaded-art), on the KOS
 * stack with the default loading-level selected. Only the new art ELF uses it.
 * Payload admission precedes all interning/allocation; symbol strings and login
 * can require additional bytes. Earlier callback effects cannot be rolled back.
 * Refuses any preexisting runtime fault and never silently clears one. */
typedef int (*dc_goal_art_relocate)(dc_goal_runtime *,uint32_t art,void *);
int dc_goal_art_admit(dc_goal_runtime *,uint32_t bytes,dc_goal_art_result *);
int dc_goal_load_art_dgo(dc_goal_runtime *,FILE *,uint32_t expected_payload,
                         dc_goal_art_relocate,void *,dc_goal_art_result *);
#ifdef DC_ARENA_JAK_PLAYER
/* Diagnostic: sample the code-registry head record and the whole symbol type
 * object header (object offsets 0,4,8,12,16,20) without dc_goal_type_of or
 * allocation. The directory loader calls original texture-page-dir.relocate. */
void dc_goal_art_probe(dc_goal_runtime *,uint32_t step);
int dc_goal_load_tpage_dir(dc_goal_runtime *,FILE *,uint32_t,dc_goal_art_relocate,void *);
#endif
#endif
