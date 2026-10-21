#include "scene_io.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t le32(const unsigned char *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static float le_float(const unsigned char *p) {
  uint32_t bits = le32(p);
  float value;
  memcpy(&value, &bits, 4);
  return value;
}
void dc_scene_free(dc_scene *scene) {
  free(scene->vertices);
  scene->vertices = NULL;
  scene->count = 0;
}
const char *dc_scene_load(FILE *file, dc_scene *scene) {
  unsigned char header[8], raw[16];
  uint32_t i;
  scene->count = 0;
  scene->vertices = NULL;
  if (sizeof(float) != 4) return "32-bit float required";
  if (fread(header, 1, 8, file) != 8 || memcmp(header, "DCM1", 4)) return "invalid scene header";
  scene->count = le32(header + 4);
  if (!scene->count || scene->count > DC_SCENE_MAX_VERTICES || scene->count % 3) {
    scene->count = 0;
    return "invalid scene vertex count";
  }
  scene->vertices = malloc(scene->count * sizeof(dc_scene_vertex));
  if (!scene->vertices) { scene->count = 0; return "scene allocation failed"; }
  for (i = 0; i < scene->count; ++i) {
    dc_scene_vertex *v = &scene->vertices[i];
    if (fread(raw, 1, 16, file) != 16) { dc_scene_free(scene); return "short scene payload"; }
    v->x = le_float(raw); v->y = le_float(raw + 4); v->z = le_float(raw + 8);
    v->argb = le32(raw + 12) | 0xff000000u;
    if (!isfinite(v->x) || !isfinite(v->y) || !isfinite(v->z) ||
        v->x*v->x + v->y*v->y + v->z*v->z > 1.001f) {
      dc_scene_free(scene); return "scene outside unit sphere";
    }
  }
  if (fgetc(file) != EOF || ferror(file)) { dc_scene_free(scene); return "scene trailing data or I/O error"; }
  return NULL;
}
