/* Shared host/SH-4 DCT1 validation and loading (AI-assisted). */
#include "textured_scene.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t le32(const unsigned char *p) {
  return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static float le_float(const unsigned char *p) {
  uint32_t bits=le32(p);
  float value;
  memcpy(&value,&bits,4);
  return value;
}
void dc_textured_free(dc_textured_scene *scene) {
  uint32_t i;
  if (scene->textures)
    for (i=0;i<scene->texture_count;++i) free(scene->textures[i].pixels);
  free(scene->textures); free(scene->vertices); free(scene->indices); free(scene->batches);
  memset(scene,0,sizeof(*scene));
}
static const char *fail(dc_textured_scene *scene,const char *message) {
  dc_textured_free(scene);
  return message;
}
const char *dc_textured_load(FILE *file,dc_textured_scene *scene) {
  unsigned char raw[32];
  uint32_t i,expected=0,texture_bytes=0;
  memset(scene,0,sizeof(*scene));
  if (sizeof(float)!=4) return "32-bit float required";
  if (fread(raw,1,32,file)!=32 || memcmp(raw,"DCT1",4) || le32(raw+4)!=1 || le32(raw+28))
    return "invalid textured header";
  scene->vertex_count=le32(raw+8); scene->index_count=le32(raw+12);
  scene->batch_count=le32(raw+16); scene->texture_count=le32(raw+20);
  scene->texture_bytes=le32(raw+24);
  if (!scene->vertex_count || scene->vertex_count>24576 || !scene->index_count ||
      scene->index_count>24576 || scene->index_count%3 || !scene->batch_count ||
      scene->batch_count>128 || !scene->texture_count || scene->texture_count>64 ||
      scene->texture_bytes>2*1024*1024)
    return fail(scene,"textured count/budget limit");
  scene->vertices=malloc(scene->vertex_count*sizeof(*scene->vertices));
  scene->indices=malloc(scene->index_count*sizeof(*scene->indices));
  scene->batches=malloc(scene->batch_count*sizeof(*scene->batches));
  scene->textures=calloc(scene->texture_count,sizeof(*scene->textures));
  if (!scene->vertices || !scene->indices || !scene->batches || !scene->textures)
    return fail(scene,"textured allocation failed");
  for (i=0;i<scene->vertex_count;++i) {
    dc_textured_vertex *v=&scene->vertices[i];
    if (fread(raw,1,24,file)!=24) return fail(scene,"short textured vertices");
    v->x=le_float(raw); v->y=le_float(raw+4); v->z=le_float(raw+8);
    v->u=le_float(raw+12); v->v=le_float(raw+16); v->argb=le32(raw+20);
    if (!isfinite(v->x)||!isfinite(v->y)||!isfinite(v->z)||!isfinite(v->u)||!isfinite(v->v)||
        v->x*v->x+v->y*v->y+v->z*v->z>1.001f || fabsf(v->u)>65536 || fabsf(v->v)>65536)
      return fail(scene,"textured vertex bounds");
  }
  for (i=0;i<scene->index_count;++i) {
    if (fread(raw,1,2,file)!=2) return fail(scene,"short textured indices");
    scene->indices[i]=(uint16_t)(raw[0]|(uint16_t)raw[1]<<8);
    if (scene->indices[i]>=scene->vertex_count) return fail(scene,"textured index bounds");
  }
  for (i=0;i<scene->batch_count;++i) {
    dc_textured_batch *b=&scene->batches[i];
    if (fread(raw,1,16,file)!=16) return fail(scene,"short textured batches");
    b->first=le32(raw); b->count=le32(raw+4); b->texture=le32(raw+8); b->flags=le32(raw+12);
    if (b->first!=expected || !b->count || b->count%3 || b->count>scene->index_count-expected ||
        b->texture>=scene->texture_count || (b->flags&~61u))
      return fail(scene,"textured batch bounds");
    expected+=b->count;
  }
  if (expected!=scene->index_count) return fail(scene,"textured batch coverage");
  for (i=0;i<scene->texture_count;++i) {
    dc_texture *t=&scene->textures[i];
    if (fread(raw,1,12,file)!=12) return fail(scene,"short textured descriptors");
    t->width=raw[0]|(uint32_t)raw[1]<<8; t->height=raw[2]|(uint32_t)raw[3]<<8;
    t->format=le32(raw+4); t->bytes=le32(raw+8);
    if (t->width<8 || t->width>512 || (t->width&(t->width-1)) ||
        t->height<8 || t->height>512 || (t->height&(t->height-1)) ||
        (t->format!=DC_TEXTURE_ARGB1555 && t->format!=DC_TEXTURE_VQ_ARGB1555) ||
        (t->format==DC_TEXTURE_VQ_ARGB1555 && t->width!=t->height) ||
        t->bytes!=(t->format==DC_TEXTURE_ARGB1555?t->width*t->height*2:
                   (2048u+t->width*t->height/4u+31u)&~31u) || t->bytes>scene->texture_bytes-texture_bytes)
      return fail(scene,"textured texture bounds");
    texture_bytes+=t->bytes;
    t->pixels=malloc(t->bytes);
    if (!t->pixels) return fail(scene,"texture allocation failed");
    if (fread(t->pixels,1,t->bytes,file)!=t->bytes) return fail(scene,"short texture pixels");
  }
  if (texture_bytes!=scene->texture_bytes) return fail(scene,"texture byte total mismatch");
  if (fgetc(file)!=EOF || ferror(file)) return fail(scene,"textured trailing data or I/O error");
  return NULL;
}
