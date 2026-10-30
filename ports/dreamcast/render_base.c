/* Packed geometry base pass, with one world transform per frame (AI-assisted). */
#include "render_base.h"
#include "render_frustum.h"
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#ifdef DC_RENDER_BASE_TEST_ALLOC
extern void *dc_base_malloc(size_t);
extern void *dc_base_calloc(size_t,size_t);
extern void *dc_base_memalign(size_t,size_t);
extern void dc_base_free(void *);
#define malloc dc_base_malloc
#define calloc dc_base_calloc
#define memalign dc_base_memalign
#define free dc_base_free
#endif
enum {MASK=1,CLAMP_U=4,CLAMP_V=8,MIRROR_U=16,MIRROR_V=32,DOUBLE_SIDED=64,NEAREST=128,BLEND=256};
static uint32_t read32(const unsigned char *p) {
 return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static float read_float(const unsigned char *p) {uint32_t bits=read32(p);float f;__builtin_memcpy(&f,&bits,4);return f;}
void dc_render_base_bank_free(dc_render_base_bank *bank) {
 if(!bank)return;
 free(bank->headers);free(bank->materials);memset(bank,0,sizeof(*bank));
}
const char *dc_render_base_bank_make(const dc_render_catalog *catalog,const dc_render_gpu *gpu,dc_render_base_bank *out) {
 dc_render_base_bank made={0};uint32_t i;const char *error=NULL;
 if(!out||!catalog||!catalog->storage||!catalog->material_count||catalog->material_count>256u)return "base catalog arguments";
 if(out->headers||out->materials||out->count)return "base catalog owned";
 if(catalog->texture_count&&(!gpu||!gpu->slots||gpu->count!=catalog->texture_count||gpu->catalog_crc!=catalog->crc||
    memcmp(gpu->source_sha256,catalog->storage+164,32)))return "base GPU identity";
 made.count=catalog->material_count;made.catalog_crc=catalog->crc;made.catalog_bytes=catalog->bytes;
 memcpy(made.source_sha256,catalog->storage+164,32);
 made.headers=memalign(32,made.count*sizeof(*made.headers));made.materials=calloc(made.count,sizeof(*made.materials));
 if(!made.headers||!made.materials){error="base catalog allocation";goto fail;}
 for(i=0;i<made.count;++i) {
  dc_render_material material;pvr_poly_cxt_t context;int list;
  if(!dc_render_material_get(catalog,i,&material)){error="base material";goto fail;}
  list=(material.base_flags&BLEND)?PVR_LIST_TR_POLY:(material.base_flags&MASK)?PVR_LIST_PT_POLY:PVR_LIST_OP_POLY;
  if(material.base_texture==DC_RENDER_ABSENT_TEXTURE) {
   if(material.alpha_mode){error="base absent MASK unsupported";goto fail;}
   pvr_poly_cxt_col(&context,list);
  }else {
   dc_render_texture texture;
   if(!gpu||material.base_texture>=gpu->count||!gpu->slots[material.base_texture]||
      !dc_render_texture_get(catalog,material.base_texture,&texture)||texture.format!=((material.base_flags&BLEND)?3u:1u)){error="base texture role";goto fail;}
   pvr_poly_cxt_txr(&context,list,((material.base_flags&BLEND)?PVR_TXRFMT_ARGB4444:PVR_TXRFMT_ARGB1555)|PVR_TXRFMT_TWIDDLED,
     texture.width,texture.height,gpu->slots[material.base_texture],(material.base_flags&NEAREST)?PVR_FILTER_NONE:PVR_FILTER_BILINEAR);
   context.txr.env=PVR_TXRENV_MODULATE;
   context.txr.uv_clamp=((material.base_flags&CLAMP_U)?PVR_UVCLAMP_U:0)|((material.base_flags&CLAMP_V)?PVR_UVCLAMP_V:0);
   context.txr.uv_flip=((material.base_flags&MIRROR_U)?PVR_UVFLIP_U:0)|((material.base_flags&MIRROR_V)?PVR_UVFLIP_V:0);
  }
  /* The CPU performs sidedness after clipping. No unbounded coordinates reach
   * the PVR culling determinant, including very long global faces. */
  context.gen.culling=PVR_CULLING_NONE;context.gen.shading=PVR_SHADE_GOURAUD;
  context.depth.comparison=PVR_DEPTHCMP_GEQUAL;
  /* the translucent list blends over what is drawn (source alpha, its inverse)
   * and leaves depth to the opaque passes */
  context.depth.write=list==PVR_LIST_TR_POLY?PVR_DEPTHWRITE_DISABLE:PVR_DEPTHWRITE_ENABLE;
  pvr_poly_compile(&made.headers[i],&context);
  made.materials[i]=(dc_render_base_material){material.base_flags,(uint32_t)list};
  if(material.strength_texture!=DC_RENDER_ABSENT_TEXTURE||material.spec_texture!=DC_RENDER_ABSENT_TEXTURE)++made.secondary_materials;
 }
 *out=made;return NULL;
fail:dc_render_base_bank_free(&made);return error;
}
void dc_render_base_frame_free(dc_render_base_frame *frame) {
 if(!frame)return;
 free(frame->vertices);free(frame->order);memset(frame,0,sizeof(*frame));
}
static int hex_digit(char c) {
 if(c>='0'&&c<='9')return c-'0';
 if(c>='a'&&c<='f')return c-'a'+10;
 if(c>='A'&&c<='F')return c-'A'+10;
 return -1;
}
const char *dc_render_base_frame_make(const dc_render_window *window,const dc_render_base_bank *bank,dc_render_base_frame *out) {
 dc_render_base_frame made={0};uint32_t counts[256]={0},i;
 if(!out||!window||!bank||!bank->headers||!bank->materials||!bank->count||bank->count>256u||
    window->valid!=1||window->ready!=1||window->count>DC_RENDER_WINDOW_CAP||
    (window->count?!window->faces:window->faces!=NULL)||window->material_count!=bank->count||
    window->catalog_crc!=bank->catalog_crc||window->catalog_bytes!=bank->catalog_bytes||!window->source_sha)return "base window binding";
 if(out->vertices||out->order||out->faces||out->count||out->materials||out->ready||out->valid)return "base frame owned";
 for(i=0;i<32;++i) {
  int hi=hex_digit(window->source_sha[i*2]),lo=hex_digit(window->source_sha[i*2+1]);
  if(hi<0||lo<0||(unsigned)(hi*16+lo)!=bank->source_sha256[i])return "base window SHA";
 }
 if(window->source_sha[64])return "base window SHA";
 made.count=window->count;made.materials=bank->count;made.catalog_crc=bank->catalog_crc;made.faces=window->faces;
 for(i=0;i<made.count;++i) {
  uint32_t material=read32(made.faces+(size_t)i*116u+4u);
  if(material>=bank->count)return "base window material";
  ++counts[material];
 }
 for(i=0;i<bank->count;++i)made.first[i+1]=made.first[i]+counts[i];
 if(made.count) {
  made.vertices=malloc(made.count*3u*sizeof(*made.vertices));made.order=malloc(made.count*sizeof(*made.order));
  if(!made.vertices||!made.order){dc_render_base_frame_free(&made);return "base frame allocation";}
  memcpy(counts,made.first,bank->count*sizeof(counts[0]));
  for(i=0;i<made.count;++i) {
   uint32_t material=read32(made.faces+(size_t)i*116u+4u);made.order[counts[material]++]=(uint16_t)i;
  }
 }
 made.valid=1;*out=made;return NULL;
}
int dc_render_base_prepare(dc_render_base_frame *frame,const dc_render_window *window,const dc_render_camera *camera) {
 if(!frame)return 0;
 frame->ready=0;
 if(!frame->valid||!window||frame->faces!=window->faces||frame->count!=window->count||frame->catalog_crc!=window->catalog_crc)return 0;
 if(!dc_render_world_prepare(window,frame->vertices,frame->count*3u,camera))return 0;
 frame->ready=1;return 1;
}
static pvr_vertex_t commands[96] __attribute__((aligned(32)));
typedef char base_command_size[(sizeof(pvr_vertex_t)==32&&sizeof(pvr_poly_hdr_t)%32==0)?1:-1];
static void flush(unsigned *n) {if(*n){pvr_prim(commands,*n*sizeof(*commands));*n=0;}}
int dc_render_base_draw(const dc_render_base_bank *bank,const dc_render_base_frame *frame,int list,dc_render_base_stats *stats) {
 uint32_t material,index,j;
 if(!bank||!frame||!stats||!frame->valid||!frame->ready||frame->catalog_crc!=bank->catalog_crc||frame->materials!=bank->count||
    (list!=PVR_LIST_OP_POLY&&list!=PVR_LIST_PT_POLY&&list!=PVR_LIST_TR_POLY))return 0;
 for(material=0;material<bank->count;++material) {
  unsigned queued=0;
  if(bank->materials[material].list!=(uint32_t)list||frame->first[material]==frame->first[material+1])continue;
  pvr_prim(&bank->headers[material],sizeof(bank->headers[material]));
  for(index=frame->first[material];index<frame->first[material+1];++index) {
   uint32_t face=frame->order[index];dc_clip_vertex input[3],poly[8];dc_projected_vertex projected[8];int n,fan;unsigned front=0;
   const unsigned char *record=frame->faces+(size_t)face*116u+8u;++stats->faces;
   for(j=0;j<3;++j,record+=36) {
    dc_view_vertex v=frame->vertices[face*3u+j].view;
    if(v.z>=DC_SCENE_NEAR)++front;
    input[j]=(dc_clip_vertex){v.x,v.y,v.z,read_float(record+12),read_float(record+16),read32(record+32)};
   }
   n=dc_render_clip_viewport(input,poly);
   if(n<0){++stats->invalid;flush(&queued);return 0;}
   if(n<3){++stats->culled;continue;}
   if(front&&front<3)++stats->near_clipped;
   if(n==3&&!memcmp(input,poly,3*sizeof(*input))) {
    for(j=0;j<3;++j)projected[j]=frame->vertices[face*3u+j].projected;
   }else {
    ++stats->clipped;
    for(j=0;j<(unsigned)n;++j) {
     float inv=1.f/poly[j].z;projected[j]=(dc_projected_vertex){320.f+390.f*poly[j].x*inv,260.f-390.f*poly[j].y*inv,inv};
    }
   }
   for(fan=1;fan<n-1;++fan) {
    unsigned ids[3]={0,(unsigned)fan,(unsigned)fan+1};dc_projected_vertex a=projected[0],b=projected[fan],c=projected[fan+1];
    float area=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
    if(area==0||(!(bank->materials[material].flags&DOUBLE_SIDED)&&area<0)){++stats->culled;continue;}
    for(j=0;j<3;++j) {
     unsigned k=ids[j];pvr_vertex_t *v=&commands[queued++];
     v->flags=j==2?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX;v->x=projected[k].x;v->y=projected[k].y;v->z=projected[k].z;
     v->u=poly[k].u;v->v=poly[k].v;v->argb=poly[k].argb;v->oargb=0;
    }
    ++stats->triangles;if(queued==96)flush(&queued);
   }
  }
  flush(&queued);
 }
 return 1;
}
