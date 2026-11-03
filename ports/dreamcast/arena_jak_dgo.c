/* Compile the unchanged loader with Jak-only startup interception.
 * No existing ELF links this object (AI-assisted), ISC. */
#ifdef DC_ARENA_JAK
#include "goal_dgo.h"
#include "arena_jak.h"
#define dc_goal_run_initializer dc_arena_jak_initializer
#define dc_goal_run_initializer_void dc_arena_jak_initializer_void
#include "goal_dgo.c"
#endif
