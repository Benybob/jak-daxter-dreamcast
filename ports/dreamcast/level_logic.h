/* A level's logic BSP through original login (AI-assisted), ISC. */
#ifndef DC_LEVEL_LOGIC_H
#define DC_LEVEL_LOGIC_H
#include "goal_runtime.h"
/* Links /cd/TRA-LOGIC.DGO and runs original bsp-header login. 1 on success;
 * dc_level_logic_evidence.stage names the step that stopped otherwise. */
int dc_level_logic(dc_goal_runtime *rt);
#endif
