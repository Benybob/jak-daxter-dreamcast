/* Refactor of our verified private actor/terrain submission (AI-assisted), ISC. */
#include "training_layer.h"
#include <malloc.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

void dc_training_layer_free(dc_training_layer *layer){
 uint32_t i;
 if(!layer)return;
 if(layer->textures)
  for(i=0;i<layer->texture_count;++i)if(layer->textures[i])pvr_mem_free(layer->textures[i]);
 free(layer->textures);free(layer->headers);free(layer->projected);
 memset(layer,0,sizeof(*layer));
}
int dc_training_layer_make(dc_training_layer *layer,dc_textured_scene *scene){
 uint32_t i;dc_training_layer made={0};
 if(!layer||!scene||layer->headers||layer->textures||layer->projected||layer->texture_count)return 0;
 /* Bounds from the validated DCT1 loader also make allocation products safe on SH-4. */
 if(!scene->batch_count||scene->batch_count>128||!scene->texture_count||scene->texture_count>64||
    !scene->vertex_count||scene->vertex_count>24576)return 0;
 made.headers=memalign(32,scene->batch_count*sizeof(*made.headers));
 made.textures=calloc(scene->texture_count,sizeof(*made.textures));
 made.projected=malloc(scene->vertex_count*sizeof(*made.projected));
 made.texture_count=scene->texture_count;
 if(!made.headers||!made.textures||!made.projected)goto fail;
 for(i=0;i<scene->texture_count;++i){
  dc_texture *t=&scene->textures[i];
  if(!t->pixels)goto fail;
  made.textures[i]=pvr_mem_malloc(t->bytes);if(!made.textures[i])goto fail;
  pvr_txr_load(t->pixels,made.textures[i],t->bytes);
  free(t->pixels);t->pixels=NULL;
 }
 for(i=0;i<scene->batch_count;++i){
  const dc_textured_batch *b=&scene->batches[i];const dc_texture *t=&scene->textures[b->texture];
  pvr_poly_cxt_t c;
  pvr_poly_cxt_txr(&c,(b->flags&1)?PVR_LIST_PT_POLY:PVR_LIST_OP_POLY,
   PVR_TXRFMT_ARGB1555|PVR_TXRFMT_TWIDDLED|(t->format==DC_TEXTURE_VQ_ARGB1555?PVR_TXRFMT_VQ_ENABLE:0),
   t->width,t->height,made.textures[b->texture],PVR_FILTER_BILINEAR);
  c.gen.culling=PVR_CULLING_NONE;c.gen.shading=PVR_SHADE_GOURAUD;
  c.txr.uv_clamp=((b->flags&4)?PVR_UVCLAMP_U:0)|((b->flags&8)?PVR_UVCLAMP_V:0);
  c.txr.uv_flip=((b->flags&16)?PVR_UVFLIP_U:0)|((b->flags&32)?PVR_UVFLIP_V:0);
  pvr_poly_compile(&made.headers[i],&c);
 }
 *layer=made;return 1;
fail:
 dc_training_layer_free(&made);return 0;
}
static pvr_vertex_t commands[96] __attribute__((aligned(32)));
static void flush(unsigned *count){
 if(*count){pvr_prim(commands,*count*sizeof(*commands));*count=0;}
}
void dc_training_layer_draw(const dc_training_layer *layer,const dc_textured_scene *scene,int list){
 uint32_t b,i,j;unsigned count;
 for(b=0;b<scene->batch_count;++b){
  const dc_textured_batch *batch=&scene->batches[b];
  if(((batch->flags&1)?PVR_LIST_PT_POLY:PVR_LIST_OP_POLY)!=list)continue;
  count=0;pvr_prim(&layer->headers[b],sizeof(layer->headers[b]));
  for(i=0;i<batch->count;i+=3){
   uint16_t ids[3];int visible=1;
   for(j=0;j<3;++j){
    const dc_projected_vertex *v;
    ids[j]=scene->indices[batch->first+i+j];v=&layer->projected[ids[j]];
    if(!isfinite(v->x)||!isfinite(v->y)||!(v->z>0&&v->z<20))visible=0;
   }
   if(!visible)continue;
   for(j=0;j<3;++j){
    uint16_t k=ids[j];pvr_vertex_t *v=&commands[count++];
    const dc_projected_vertex *p=&layer->projected[k];const dc_textured_vertex *src=&scene->vertices[k];
    memset(v,0,sizeof(*v));v->flags=j==2?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX;
    v->x=p->x;v->y=p->y;v->z=p->z;v->u=src->u;v->v=src->v;v->argb=src->argb;
   }
   if(count==96)flush(&count);
  }
  flush(&count);
 }
}
