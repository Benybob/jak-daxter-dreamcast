#ifndef JAK_DC_SCENE_IO_H
#define JAK_DC_SCENE_IO_H
#include <stdint.h>
#include <stdio.h>

#define DC_SCENE_MAX_VERTICES 24576u
typedef struct { float x, y, z; uint32_t argb; } dc_scene_vertex;
typedef struct { uint32_t count; dc_scene_vertex *vertices; } dc_scene;

/* DCM1: magic, little-endian u32 vertex count, then <fffI> vertices.
 * Triangle list only. Positions must lie in the unit sphere. */
const char *dc_scene_load(FILE *file, dc_scene *scene);
void dc_scene_free(dc_scene *scene);
#endif
