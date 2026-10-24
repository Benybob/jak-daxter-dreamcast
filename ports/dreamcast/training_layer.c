/* Refactor of our verified private actor/terrain submission (AI-assisted), ISC. */
#include "training_layer.h"
#include "scene_clip.h"
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

void dc_training_layer_free(dc_training_layer *layer){
 uint32_t i;
 if(!layer)return;
 if(layer->textures)
  for(i=0;i<layer->texture_count;++i)if(layer->textures[i])pvr_mem_free(layer->textures[i]);
 free(layer->textures);free(layer->headers);free(layer->vertices);
 memset(layer,0,sizeof(*layer));
}
int dc_training_layer_make(dc_training_layer *layer,dc_textured_scene *scene){
 uint32_t i;dc_training_layer made={0};
 if(!layer||!scene||layer->headers||layer->textures||layer->vertices||layer->texture_count)return 0;
 /* Bounds from the validated DCT1 loader also make allocation products safe on SH-4. */
 if(!scene->batch_count||scene->batch_count>128||!scene->texture_count||scene->texture_count>64||
    !scene->vertex_count||scene->vertex_count>24576)return 0;
 made.headers=memalign(32,scene->batch_count*sizeof(*made.headers));
 made.textures=calloc(scene->texture_count,sizeof(*made.textures));
 made.vertices=malloc(scene->vertex_count*sizeof(*made.vertices));
 made.texture_count=scene->texture_count;
 if(!made.headers||!made.textures||!made.vertices)goto fail;
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
static inline int finite32(float v){
 union {float f;uint32_t u;} bits;bits.f=v;return (bits.u&UINT32_C(0x7f800000))!=UINT32_C(0x7f800000);
}
static void flush(unsigned *count){
 if(*count){pvr_prim(commands,*count*sizeof(*commands));*count=0;}
}
/* These layout assumptions also keep every header/flush aligned for KOS SQ. */
typedef char command_size_is_32[sizeof(pvr_vertex_t)==32?1:-1];
typedef char header_size_is_aligned[sizeof(pvr_poly_hdr_t)%32==0?1:-1];
volatile uint32_t dc_training_clipped_triangles;
static inline void push(dc_projected_vertex p,float u,float v,uint32_t color,int last,unsigned *count){
 pvr_vertex_t *command=&commands[(*count)++];
 command->flags=last?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX;
 command->x=p.x;command->y=p.y;command->z=p.z;command->u=u;command->v=v;command->argb=color;command->oargb=0;
}
void dc_training_layer_draw(const dc_training_layer *layer,const dc_textured_scene *scene,int list){
 uint32_t b,i,j;unsigned count;
 for(b=0;b<scene->batch_count;++b){
  const dc_textured_batch *batch=&scene->batches[b];
  if(((batch->flags&1)?PVR_LIST_PT_POLY:PVR_LIST_OP_POLY)!=list)continue;
  count=0;pvr_prim(&layer->headers[b],sizeof(layer->headers[b]));
  for(i=0;i<batch->count;i+=3){
   uint16_t ids[3];unsigned front=0;int valid=1;
   for(j=0;j<3;++j){
    const dc_view_vertex *v;
    ids[j]=scene->indices[batch->first+i+j];v=&layer->vertices[ids[j]].view;
    if(!finite32(v->x)||!finite32(v->y)||!finite32(v->z))valid=0;
    if(v->z>=DC_SCENE_NEAR)++front;
   }
   if(!valid||!front)continue;
   if(front==3){
    for(j=0;j<3;++j){const dc_projected_vertex *v=&layer->vertices[ids[j]].projected;
     if(!finite32(v->x)||!finite32(v->y)||!finite32(v->z)||v->z<=0)valid=0;
    }
    if(!valid)continue;
    for(j=0;j<3;++j){const dc_textured_vertex *v=&scene->vertices[ids[j]];
     push(layer->vertices[ids[j]].projected,v->u,v->v,v->argb,j==2,&count);
    }
    if(count==96)flush(&count);
   }else{
    dc_clip_vertex input[3],polygon[4];int n,fan;
    dc_projected_vertex projected[4];
    for(j=0;j<3;++j){const dc_view_vertex *v=&layer->vertices[ids[j]].view;const dc_textured_vertex *a=&scene->vertices[ids[j]];
     input[j]=(dc_clip_vertex){v->x,v->y,v->z,a->u,a->v,a->argb};
    }
    n=dc_scene_clip_near(input,polygon);if(n<3)continue;
    ++dc_training_clipped_triangles;
    for(j=0;j<(unsigned)n;++j){float inv=1.0f/polygon[j].z;
     projected[j]=(dc_projected_vertex){320+390*polygon[j].x*inv,260-390*polygon[j].y*inv,inv};
    }
    for(fan=1;fan<n-1;++fan){unsigned k[3]={0,(unsigned)fan,(unsigned)fan+1};
     for(j=0;j<3;++j){const dc_clip_vertex *v=&polygon[k[j]];
      push(projected[k[j]],v->u,v->v,v->argb,j==2,&count);
     }
     if(count==96)flush(&count);
    }
   }
  }
  flush(&count);
 }
}
