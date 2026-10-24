/* Viewer-only projection contract (AI-assisted), ISC. */
#ifndef DC_SCENE_PROJECTION_H
#define DC_SCENE_PROJECTION_H
#include <stddef.h>
#include <stdint.h>
typedef struct {float x,y,z;} dc_projected_vertex;
/* Trusted nonoverlapping arrays: each input record begins with three floats,
 * stride >=12, input and stride aligned to4, spans sized for count. Unit sphere, distance>=1.5.
 * SH4 implementation is synchronous: no GOAL fiber yield or callbacks inside.
 * Returns zero, without output writes, for unsupported FPU control mode. */
int dc_scene_project(const void *,size_t,dc_projected_vertex *,uint32_t,float,float,float);
int dc_scene_project_reference(const void *,size_t,dc_projected_vertex *,uint32_t,float,float,float);
#endif

