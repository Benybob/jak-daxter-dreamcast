/* DCT1 bounded indexed scene with twiddled ARGB1555 textures (AI-assisted). */
#ifndef JAK_DC_TEXTURED_SCENE_H
#define JAK_DC_TEXTURED_SCENE_H
#include <stdint.h>
#include <stdio.h>
enum { DC_TEXTURE_ARGB1555=1,DC_TEXTURE_VQ_ARGB1555=2 };

typedef struct { float x,y,z,u,v; uint32_t argb; } dc_textured_vertex;
typedef struct { uint32_t first,count,texture,flags; } dc_textured_batch;
typedef struct { uint32_t width,height,format,bytes; unsigned char *pixels; } dc_texture;
typedef struct {
  uint32_t vertex_count,index_count,batch_count,texture_count,texture_bytes;
  dc_textured_vertex *vertices;
  uint16_t *indices;
  dc_textured_batch *batches;
  dc_texture *textures;
} dc_textured_scene;
const char *dc_textured_load(FILE *file,dc_textured_scene *scene);
void dc_textured_free(dc_textured_scene *scene);
#endif
