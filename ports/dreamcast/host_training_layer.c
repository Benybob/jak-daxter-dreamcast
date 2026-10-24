/* Allocation-fault, ownership and submission oracle (AI-assisted), ISC. */
#include "training_layer.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#undef malloc
#undef calloc
#undef memalign
#undef free
static unsigned total,failed,attempt,fail_at,live,uploads,prim_calls,vertices,headers;
static unsigned batch_sizes[8],batch_calls;
static void *allocated[32],*base[32];
#define CHECK(x) do{++total;if(!(x)){++failed;printf("training layer failure line %u\n",(unsigned)__LINE__);}}while(0)
void *dc_test_malloc(size_t n){return dc_test_memalign(32,n);}
void *dc_test_memalign(size_t alignment,size_t n){
 unsigned i;uintptr_t aligned;void *raw;
 ++attempt;if(fail_at&&attempt==fail_at)return NULL;
 raw=malloc(n+alignment);if(!raw)return NULL;
 aligned=((uintptr_t)raw+alignment-1)&~(uintptr_t)(alignment-1);
 for(i=0;i<32;++i)if(!allocated[i]){allocated[i]=(void *)aligned;base[i]=raw;++live;return allocated[i];}
 free(raw);CHECK(0);return NULL;
}
void *dc_test_calloc(size_t n,size_t size){void *p=dc_test_malloc(n*size);if(p)memset(p,0,n*size);return p;}
void dc_test_free(void *p){
 unsigned i;if(!p)return;
 for(i=0;i<32;++i)if(allocated[i]==p){free(base[i]);allocated[i]=base[i]=NULL;--live;return;}
 CHECK(0);
}
pvr_ptr_t pvr_mem_malloc(size_t n){return dc_test_malloc(n);}
void pvr_mem_free(pvr_ptr_t p){dc_test_free(p);}
void pvr_txr_load(const void *src,pvr_ptr_t dst,size_t n){CHECK(src&&dst&&n==128);++uploads;}
void pvr_poly_cxt_txr(pvr_poly_cxt_t *c,int list,int format,unsigned w,unsigned h,pvr_ptr_t p,int filter){
 CHECK((list==PVR_LIST_OP_POLY||list==PVR_LIST_PT_POLY)&&format==3&&w==8&&h==8&&p&&filter==1);
 memset(c,0,sizeof(*c));
}
void pvr_poly_compile(pvr_poly_hdr_t *h,const pvr_poly_cxt_t *c){
 CHECK(c->gen.culling==0&&c->gen.shading==1);memset(h,0,sizeof(*h));h->words[0]=0x80000000u;
}
int pvr_prim(const void *data,size_t bytes){
 const uint32_t *words=data;unsigned i;
 CHECK(!((uintptr_t)data&31)&&bytes&&!(bytes&31));++prim_calls;
 if(words[0]==0x80000000u){CHECK(bytes==32);++headers;return 0;}
 CHECK(bytes<=96*sizeof(pvr_vertex_t));
 if(batch_calls<8)batch_sizes[batch_calls++]=(unsigned)(bytes/sizeof(pvr_vertex_t));
 for(i=0;i<bytes/sizeof(pvr_vertex_t);++i){const pvr_vertex_t *v=(const pvr_vertex_t *)data+i;
  CHECK(v->flags==(i%3==2?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX));
  CHECK(isfinite(v->x)&&isfinite(v->y)&&v->z==.25f&&v->argb==0xff123456u&&v->oargb==0);++vertices;
 }
 return 0;
}
static void scene_init(dc_textured_scene *s,dc_texture *t,dc_textured_vertex *v,
                        uint16_t *indices,dc_textured_batch *b){
 unsigned i;memset(s,0,sizeof(*s));memset(t,0,2*sizeof(*t));memset(v,0,3*sizeof(*v));
 s->vertex_count=3;s->index_count=6;s->batch_count=2;s->texture_count=2;s->texture_bytes=256;
 s->vertices=v;s->indices=indices;s->batches=b;s->textures=t;
 for(i=0;i<3;++i)v[i].argb=0xff123456u;
 for(i=0;i<99;++i)indices[i]=(uint16_t)(i%3);
 b[0]=(dc_textured_batch){0,3,0,0};b[1]=(dc_textured_batch){3,3,1,1};
 for(i=0;i<2;++i){t[i]=(dc_texture){8,8,DC_TEXTURE_ARGB1555,128,NULL};t[i].pixels=dc_test_malloc(128);}
 attempt=uploads=0;
}
int main(void){
 dc_training_layer layer={0},zero={0};dc_textured_scene scene;dc_texture texture[2];
 dc_textured_vertex vertex[3];uint16_t indices[99];dc_textured_batch batch[2];unsigned fault,i;
 for(fault=1;fault<=5;++fault){
  scene_init(&scene,texture,vertex,indices,batch);fail_at=fault;
  CHECK(!dc_training_layer_make(&layer,&scene));CHECK(!memcmp(&layer,&zero,sizeof layer));
  dc_training_layer_free(&layer);dc_training_layer_free(&layer);
  for(i=0;i<2;++i)dc_test_free(texture[i].pixels);
  CHECK(live==0);fail_at=0;
 }
 scene_init(&scene,texture,vertex,indices,batch);CHECK(dc_training_layer_make(&layer,&scene));
 CHECK(uploads==2&&!texture[0].pixels&&!texture[1].pixels);
 CHECK(!dc_training_layer_make(&layer,&scene));CHECK(live==5);
 for(i=0;i<3;++i)layer.vertices[i]=(dc_render_vertex){{0,0,4},{10+i,20+i,.25f}};
 dc_training_layer_draw(&layer,&scene,PVR_LIST_OP_POLY);CHECK(headers==1&&vertices==3);
 dc_training_layer_draw(&layer,&scene,PVR_LIST_PT_POLY);CHECK(headers==2&&vertices==6);
 layer.vertices[0].view.x=NAN;dc_training_layer_draw(&layer,&scene,PVR_LIST_OP_POLY);CHECK(vertices==6);
 layer.vertices[0].view.x=0;layer.vertices[0].view.z=INFINITY;
 dc_training_layer_draw(&layer,&scene,PVR_LIST_OP_POLY);CHECK(vertices==6);
 layer.vertices[0].view.z=4;scene.batch_count=1;scene.index_count=batch[0].count=99;
 batch_calls=vertices=0;dc_training_layer_draw(&layer,&scene,PVR_LIST_OP_POLY);
 CHECK(vertices==99&&batch_calls==2&&batch_sizes[0]==96&&batch_sizes[1]==3);
 dc_training_layer_free(&layer);dc_training_layer_free(&layer);CHECK(live==0&&!memcmp(&layer,&zero,sizeof layer));
 CHECK(prim_calls>0);printf("training layer %u/%u checks\n",total-failed,total);return failed?1:0;
}
