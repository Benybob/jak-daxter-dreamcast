/* Original fixed type metadata bootstrap (AI-assisted), ISC. */
#ifndef DC_GOAL_KERNEL_TYPES_H
#define DC_GOAL_KERNEL_TYPES_H
#include "goal_runtime.h"
typedef struct { uint32_t types,new_types,placeholders,arena_bytes,temporary_bytes; } dc_kernel_types_result;
/* Install original type metadata/fixed names before game asset imports. Existing
 * complete types and their methods are preserved. No process, heap object,
 * printer/inspector or other unimplemented kernel methods are synthesized. */
int dc_goal_install_kernel_types(dc_goal_runtime *rt,dc_kernel_types_result *result);
#endif
