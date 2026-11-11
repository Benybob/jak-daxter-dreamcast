/* Skinned merc models of the level ELF's processes (AI-assisted), ISC.
 * See level_models.h. GOAL layouts (retail offsets less the 4-byte type word):
 * process-tree brother +12, child +16 ((pointer process-tree) to the self
 * slot); process-drawable node-list +112, draw +116; draw-control status +0,
 * mgeo +12, origin +108; art name +4; string chars +4; cspace-array length +0,
 * data +12, 32-byte cspaces with joint +4 and bone +16; joint number +4,
 * bind-pose +12; bone transform +0. Not compiled with the slab redirect: the
 * models live in the KOS heap and VRAM, beside the renderer's slab. */
#ifdef DC_LEVEL_DRAW
#include "level_models.h"
#include <kos.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
void *sbrk(ptrdiff_t); /* hidden from unistd.h by -std=c99 */
#include <zlib.h>
#include "render_frustum.h"
#include "scene_projection.h"

enum {MAX_MODELS=32,MAX_KEPT=32,MAX_JOINTS=255,MAX_BATCHES=64,MAX_TEXTURES=64,MAX_CSPACES=256,
      MAX_VERTICES=8192,MAX_TEXTURE_BYTES=1024*1024*2,WALK_NODES=1024,WALK_DEPTH=64};
volatile dc_level_model_metrics_t dc_level_model_metrics;

typedef struct {int16_t x,y,z,u,v;uint8_t joint[3],weight[2],pad;uint32_t argb;} dsk_vertex;
typedef struct {uint32_t first,count;uint16_t texture,flags;} dsk_batch;
typedef struct {
 char name[32];uint32_t offset,bytes,crc;
 int state; /* 0 not loaded, 1 loaded, -1 failed */
 uint32_t joints,verts,indices,batch_count,lists; /* lists: bit per list index 0 op, 1 pt, 2 tr */
 float centre[3],scale,radius;
 const float *ibm;const dsk_vertex *v;const uint16_t *ix;const dsk_batch *batches;
 pvr_poly_hdr_t *headers;int *batch_list;
 uint32_t used[8]; /* joints the vertices name */
 int bind_checked;
 void *geometry;
} model;
typedef struct {uint32_t proc;unsigned model;int target,drawn;} kept;

static FILE *bundle;
static model models[MAX_MODELS];
static unsigned model_count;
static kept frame_kept[MAX_KEPT];
static unsigned kept_count;
/* Per vertex of the model being drawn: the screen position (x, y, 1/z) when the
 * vertex is in front of the near plane and within the guard band, else the
 * view position; and flags. One skin, one viewport test and one divide per
 * vertex serve every triangle that uses it (as instance_pass.c). */
enum {VERTEX_GUARD=0x80};  /* else the low five bits are the planes it is clearly outside */
static float (*scratch)[3];
static uint8_t *vertex_flags;
static uint32_t scratch_verts;
static uint32_t frame_triangles;
static int frame_jak,frame_actor;
static pvr_poly_hdr_t colour[3] __attribute__((aligned(32)));
static pvr_dr_state_t dr;  /* direct rendering: vertices written at their store-queue address (instance_pass.c) */
static float V[MAX_JOINTS+1][12];
static uint32_t have[8];

static uint32_t heap_free(void){
 struct mallinfo mi=mallinfo();uintptr_t top=(uintptr_t)sbrk(0);
 return (uint32_t)mi.fordblks+(top<0x8d000000u?(uint32_t)(0x8d000000u-top):0u);
}
static int list_index(int list){return list==PVR_LIST_OP_POLY?0:list==PVR_LIST_PT_POLY?1:list==PVR_LIST_TR_POLY?2:-1;}
static int read_exact(void *to,uint32_t bytes,uint32_t *crc){
 if(fread(to,1,bytes,bundle)!=bytes)return 0;
 *crc=crc32(*crc,to,bytes);return 1;
}

int dc_level_models_open(const char *path){
 unsigned char head[16];uint32_t count,i;
 static const int lists[3]={PVR_LIST_OP_POLY,PVR_LIST_PT_POLY,PVR_LIST_TR_POLY};
 dc_level_model_metrics.heap_free_before=heap_free();
 if(!(bundle=fopen(path,"rb")))return 0;
 if(fread(head,1,16,bundle)!=16 || memcmp(head,"DSKB",4))return 0;
 memcpy(&count,head+8,4);
 {uint32_t version;memcpy(&version,head+4,4);if(!count || count>MAX_MODELS || version!=1u)return 0;}
 for(i=0;i<count;++i){
  unsigned char e[48];
  if(fread(e,1,48,bundle)!=48)return 0;
  memset(&models[i],0,sizeof models[i]);
  memcpy(models[i].name,e,32);models[i].name[31]=0;
  memcpy(&models[i].offset,e+32,4);memcpy(&models[i].bytes,e+36,4);memcpy(&models[i].crc,e+40,4);
  if(models[i].offset<16u+48u*count || models[i].bytes<48u)return 0;
 }
 model_count=count;
 for(i=0;i<3;++i){
  pvr_poly_cxt_t cxt;pvr_poly_cxt_col(&cxt,lists[i]);
  cxt.gen.culling=PVR_CULLING_NONE;cxt.gen.shading=PVR_SHADE_GOURAUD;cxt.depth.comparison=PVR_DEPTHCMP_GEQUAL;
  cxt.depth.write=lists[i]==PVR_LIST_TR_POLY?PVR_DEPTHWRITE_DISABLE:PVR_DEPTHWRITE_ENABLE;
  pvr_poly_compile(&colour[i],&cxt);
 }
 return 1;
}

static int pow2(uint32_t v){return v>=8u && v<=1024u && !(v&(v-1u));}
/* Reads and validates one model; textures go to VRAM one at a time. */
static int load(model *m){
 uint32_t head[12],crc=0,geometry,rest,i,texture_count,texture_bytes;
 unsigned char *g=NULL,*pixels=NULL;pvr_ptr_t slots[MAX_TEXTURES];uint32_t sizes[MAX_TEXTURES][3];
 if(fseek(bundle,(long)m->offset,SEEK_SET) || !read_exact(head,48,&crc) || memcmp(head,"DSK1",4) || head[1]!=1u)goto fail;
 m->joints=head[2];m->verts=head[3];m->indices=head[4];m->batch_count=head[5];texture_count=head[6];texture_bytes=head[7];
 memcpy(m->centre,&head[8],12);memcpy(&m->scale,&head[11],4);
 if(m->joints<3u || m->joints>MAX_JOINTS || !m->verts || m->verts>MAX_VERTICES || !m->indices || m->indices%3u ||
    m->indices>196608u || !m->batch_count || m->batch_count>MAX_BATCHES || texture_count>MAX_TEXTURES ||
    texture_bytes>MAX_TEXTURE_BYTES || !(m->scale>0) || !isfinite(m->scale))goto fail;
 for(i=0;i<3;++i)if(!isfinite(m->centre[i]))goto fail;
 geometry=m->joints*48u+m->verts*20u+((m->indices*2u+3u)&~3u)+m->batch_count*12u;
 if(48u+geometry>m->bytes || !(g=malloc(geometry)) || !read_exact(g,geometry,&crc))goto fail;
 m->ibm=(const float *)(void *)g;m->v=(const dsk_vertex *)(void *)(g+m->joints*48u);
 m->ix=(const uint16_t *)(void *)(g+m->joints*48u+m->verts*20u);
 m->batches=(const dsk_batch *)(void *)(g+m->joints*48u+m->verts*20u+((m->indices*2u+3u)&~3u));
 for(i=0;i<m->joints*12u;++i)if(!isfinite(m->ibm[i]))goto fail;
 for(i=0;i<m->verts;++i){const dsk_vertex *v=&m->v[i];unsigned k;
  if(v->weight[0]+v->weight[1]>255)goto fail;
  for(k=0;k<3;++k){if(v->joint[k]>=m->joints)goto fail;m->used[v->joint[k]>>5]|=1u<<(v->joint[k]&31u);}
 }
 for(i=0;i<m->indices;++i)if(m->ix[i]>=m->verts)goto fail;
 for(i=0;i<m->batch_count;++i){const dsk_batch *b=&m->batches[i];
  if(b->first>m->indices || b->count>m->indices-b->first || b->count%3u || (b->flags&~3u) || (b->flags&3u)==3u ||
     (b->texture!=0xffffu && b->texture>=texture_count))goto fail;}
 /* textures */
 rest=m->bytes-48u-geometry;
 if(texture_count && !(pixels=malloc(128u*128u*2u)))goto fail;
 for(i=0;i<texture_count;++i){
  uint32_t rec[3];uint16_t wh[2];
  if(rest<12u || !read_exact(rec,12,&crc))goto fail;
  memcpy(wh,rec,4);rest-=12u;
  if(!pow2(wh[0]) || !pow2(wh[1]) || (rec[1]!=1u && rec[1]!=3u) || rec[2]!=(uint32_t)wh[0]*wh[1]*2u || rec[2]>rest)goto fail;
  if(rec[2]>128u*128u*2u){free(pixels);if(!(pixels=malloc(rec[2])))goto fail;}
  if(!read_exact(pixels,rec[2],&crc))goto fail;
  rest-=rec[2];
  {uint32_t pad=(4u-(rec[2]&3u))&3u;unsigned char z[4];if(pad){if(rest<pad||!read_exact(z,pad,&crc))goto fail;rest-=pad;}}
  if(!(slots[i]=pvr_mem_malloc(rec[2])))goto fail;
  pvr_txr_load(pixels,slots[i],rec[2]);
  sizes[i][0]=wh[0];sizes[i][1]=wh[1];sizes[i][2]=rec[1];
  dc_level_model_metrics.texture_bytes+=rec[2];
 }
 free(pixels);pixels=NULL;
 if(rest || crc!=m->crc)goto fail;
 if(!(m->headers=memalign(32,m->batch_count*sizeof *m->headers)) || !(m->batch_list=malloc(m->batch_count*sizeof(int))))goto fail;
 for(i=0;i<m->batch_count;++i){
  const dsk_batch *b=&m->batches[i];
  int list=(b->flags&2u)?PVR_LIST_TR_POLY:(b->flags&1u)?PVR_LIST_PT_POLY:PVR_LIST_OP_POLY;
  pvr_poly_cxt_t cxt;
  if(b->texture==0xffffu)pvr_poly_cxt_col(&cxt,list);
  else{const uint32_t *s=sizes[b->texture];
   pvr_poly_cxt_txr(&cxt,list,(s[2]==3u?PVR_TXRFMT_ARGB4444:PVR_TXRFMT_ARGB1555)|PVR_TXRFMT_TWIDDLED,
                    (int)s[0],(int)s[1],slots[b->texture],PVR_FILTER_BILINEAR);
   cxt.txr.env=PVR_TXRENV_MODULATE;}
  cxt.gen.culling=PVR_CULLING_NONE;cxt.gen.shading=PVR_SHADE_GOURAUD;cxt.depth.comparison=PVR_DEPTHCMP_GEQUAL;
  cxt.depth.write=list==PVR_LIST_TR_POLY?PVR_DEPTHWRITE_DISABLE:PVR_DEPTHWRITE_ENABLE;
  pvr_poly_compile(&m->headers[i],&cxt);
  m->batch_list[i]=list;m->lists|=1u<<list_index(list);
 }
 if(m->verts>scratch_verts){
  free(scratch);free(vertex_flags);vertex_flags=NULL;
  if(!(scratch=memalign(32,m->verts*sizeof *scratch)) || !(vertex_flags=malloc(m->verts))){
   free(scratch);scratch=NULL;scratch_verts=0;goto fail;}
  scratch_verts=m->verts;
 }
 m->radius=sqrtf(3.f)*32767.f*m->scale+sqrtf(m->centre[0]*m->centre[0]+m->centre[1]*m->centre[1]+m->centre[2]*m->centre[2]);
 m->geometry=g;m->state=1;
 ++dc_level_model_metrics.loaded;dc_level_model_metrics.geometry_bytes+=geometry;
 dc_level_model_metrics.heap_free_after=heap_free();
 return 1;
fail:
 free(pixels);free(g);m->geometry=NULL;m->state=-1;++dc_level_model_metrics.load_failures;
 return 0;
}

/* GOAL memory reads: 0 for anything that does not resolve. */
static uint32_t rd(dc_goal_runtime *rt,uint32_t at){
 uint32_t v=0;const void *p=dc_goal_resolve(rt,at,4);if(p)memcpy(&v,p,4);return v;
}
static int ptr(dc_goal_runtime *rt,uint32_t v){return v>4u && v!=rt->s7 && !(v&3u);}
static uint32_t named(dc_goal_runtime *rt,const char *name){
 uint32_t s=dc_goal_find(rt,name);return s?dc_goal_symbol_value(rt,s):0;
}
static const float *mat(dc_goal_runtime *rt,uint32_t at){
 const float *f=dc_goal_resolve(rt,at,64);unsigned i;
 if(!f)return NULL;
 for(i=0;i<16;++i)if(!isfinite(f[i]))return NULL;
 return f;
}
static int find_model(dc_goal_runtime *rt,uint32_t mgeo){
 uint32_t name=rd(rt,mgeo+4u),length;const char *chars;unsigned i;
 if(!ptr(rt,name))return -1;
 length=rd(rt,name);
 if(!length || length>31u || !(chars=dc_goal_resolve(rt,name+4u,length)))return -1;
 for(i=0;i<model_count;++i)if(!strncmp(models[i].name,chars,length) && !models[i].name[length])return (int)i;
 ++dc_level_model_metrics.last_missing;
 if(!dc_level_model_metrics.missing_name[0]){unsigned k;for(k=0;k<length&&k<31u;++k)dc_level_model_metrics.missing_name[k]=chars[k];}
 return -2;
}

int dc_level_models_frame(dc_goal_runtime *rt,const dc_instance_camera *cam){
 enum dc_goal_error prior=rt->error;
 uint32_t stack[WALK_DEPTH],pd=named(rt,"process-drawable"),dc=named(rt,"draw-control"),root=named(rt,"*active-pool*");
 uint32_t target=named(rt,"*target*"),drawables=0,missing=0;unsigned depth=0,visits=0;float b[10];
 ++dc_level_model_metrics.frames;kept_count=0;frame_triangles=0;frame_jak=frame_actor=0;
 dc_level_model_metrics.last_missing=0;
 if(!pd || !dc || !ptr(rt,root) || !model_count){rt->error=prior;return 1;}
 dc_instance_view(cam,b);
 stack[depth++]=root;
 while(depth && visits<WALK_NODES){
  uint32_t node=stack[--depth],next,draw,mgeo;int index;
  ++visits;
  /* brother, then child */
  next=rd(rt,node+12u);if(ptr(rt,next) && (next=rd(rt,next)) && ptr(rt,next) && depth<WALK_DEPTH)stack[depth++]=next;
  next=rd(rt,node+16u);if(ptr(rt,next) && (next=rd(rt,next)) && ptr(rt,next) && depth<WALK_DEPTH)stack[depth++]=next;
  if(!dc_goal_is_subtype(rt,dc_goal_type_of(rt,node),pd)){rt->error=DC_GOAL_OK;continue;}
  draw=rd(rt,node+116u);
  if(!ptr(rt,draw) || rd(rt,draw-4u)!=dc)continue;
  ++drawables;
  if(rd(rt,draw)&2u)continue; /* draw-status hidden */
  mgeo=rd(rt,draw+12u);
  if(!ptr(rt,mgeo))continue;
  index=find_model(rt,mgeo);
  if(index==-2){++missing;continue;}
  if(index<0)continue;
  {const float *o=dc_goal_resolve(rt,draw+108u,12);float w[3],c[3],dx,dz;model *m=&models[index];
   if(!o || !isfinite(o[0]) || !isfinite(o[1]) || !isfinite(o[2]))continue;
   w[0]=o[0]/4096.f;w[1]=o[1]/4096.f;w[2]=o[2]/4096.f;dx=w[0]-cam->tx;dz=w[2]-cam->tz;
   if(dx*dx+dz*dz>cam->draw_distance*cam->draw_distance){++dc_level_model_metrics.culled;continue;}
   if(!m->state && !load(m))continue;
   if(m->state<0)continue;
   dc_instance_to_view(b,cam,w,c);
   /* the origin is the skeleton's, not the bind-space centre: twice the radius */
   if(dc_instance_sphere_outside(c,2.f*m->radius*0.25f)){
    ++dc_level_model_metrics.culled;continue;}
  }
  if(kept_count<MAX_KEPT)frame_kept[kept_count++]=(kept){node,(unsigned)index,node==target,0};
 }
 dc_level_model_metrics.drawables=drawables;dc_level_model_metrics.missing=missing;
 dc_level_model_metrics.kept+=kept_count;dc_level_model_metrics.last_kept=kept_count;
 if(kept_count>dc_level_model_metrics.max_kept)dc_level_model_metrics.max_kept=kept_count;
 rt->error=prior;return 1;
}

/* Joint matrices of one process into V (view rows: 3 coefficients and a constant
 * per view axis), with have[] marking the joint numbers found. */
static int joints(dc_goal_runtime *rt,model *m,uint32_t proc,const float b[10],const dc_instance_camera *cam){
 uint32_t list=rd(rt,proc+112u),length,i;
 memset(have,0,sizeof have);
 if(!ptr(rt,list))return 0;
 length=rd(rt,list);
 if(!length || length>MAX_CSPACES)return 0;
 for(i=0;i<length;++i){
  uint32_t cs=list+12u+32u*i,joint=rd(rt,cs+4u),bone=rd(rt,cs+16u),n;const float *bind,*bt;float S[4][3];unsigned r,c,k;
  if(!ptr(rt,joint) || !ptr(rt,bone))continue;
  n=rd(rt,joint+4u);
  if(n>=m->joints || !(bind=mat(rt,joint+12u)) || !(bt=mat(rt,bone)))continue;
  /* S = bind-pose x bone transform, row vectors (rows 0-2 and the translation) */
  for(r=0;r<4;++r)for(c=0;c<3;++c){float s=0;for(k=0;k<4;++k)s+=bind[4*r+k]*bt[4*k+c];S[r][c]=s;}
  if(!m->bind_checked){
   float e=0;const float *f=&m->ibm[12u*n];
   for(r=0;r<3;++r)for(c=0;c<3;++c){float d=fabsf(bind[4*r+c]-f[3*r+c]);if(d>e)e=d;}
   for(c=0;c<3;++c){float d=fabsf(bind[12+c]/4096.f-f[9+c]);if(d>e)e=d;}
   if(e*1e6f>(float)dc_level_model_metrics.bind_error_ppm)dc_level_model_metrics.bind_error_ppm=e*1e6f>4e9f?4000000000u:(uint32_t)(e*1e6f);
  }
  {float t[3]={S[3][0]/4096.f-cam->tx,S[3][1]/4096.f-cam->ty,S[3][2]/4096.f-cam->tz};
   for(r=0;r<3;++r){
    for(k=0;k<3;++k)V[n][4*r+k]=b[3*r]*S[k][0]+b[3*r+1]*S[k][1]+b[3*r+2]*S[k][2];
    V[n][4*r+3]=b[3*r]*t[0]+b[3*r+1]*t[1]+b[3*r+2]*t[2]+(r==2?b[9]:0.f);
   }}
  have[n>>5]|=1u<<(n&31u);
 }
 if(!m->bind_checked){m->bind_checked=1;++dc_level_model_metrics.bind_checked;}
 for(i=0;i<8;++i)if(m->used[i]&~have[i])return 0;
 return 1;
}

/* A vertex's view position: the weighted sum of its joints' transforms. */
static void skin(const model *m,uint32_t i,float o[3]){
 const dsk_vertex *v=&m->v[i];float p[3];unsigned k;
 const unsigned w[3]={v->weight[0],v->weight[1],255u-v->weight[0]-v->weight[1]};
 o[0]=o[1]=o[2]=0;
 p[0]=m->centre[0]+v->x*m->scale;p[1]=m->centre[1]+v->y*m->scale;p[2]=m->centre[2]+v->z*m->scale;
 for(k=0;k<3;++k){const float *q;float f;
  if(!w[k])continue;
  q=V[v->joint[k]];f=w[k]*(1.f/255.f);
  o[0]+=f*(q[0]*p[0]+q[1]*p[1]+q[2]*p[2]+q[3]);
  o[1]+=f*(q[4]*p[0]+q[5]*p[1]+q[6]*p[2]+q[7]);
  o[2]+=f*(q[8]*p[0]+q[9]*p[1]+q[10]*p[2]+q[11]);}
}
/* The planes a view position is clearly outside of, by dc_render_clip_viewport's
 * own single-precision test (instance_pass.c clearly_outside). */
static unsigned outside_planes(float x,float y,float z){
 unsigned mask=z<DC_SCENE_NEAR,plane;
 if(!(fabsf(x)<=4000000.f)||!(fabsf(y)<=4000000.f)||!(fabsf(z)<=4000000.f))return 0;
 for(plane=1;plane<5;++plane){float a,b2;
  switch(plane){
  case 1:a=390.f*x;b2=320.f*z;break;
  case 2:a=-390.f*x;b2=320.f*z;break;
  case 3:a=-390.f*y;b2=260.f*z;break;
  default:a=390.f*y;b2=220.f*z;break;
  }
  if(a+b2< -((fabsf(a)+fabsf(b2))*(4.f*1.1920928955078125e-7f)))mask|=1u<<plane;
 }
 return mask;
}
static void header_out(const void *header){
 uint32_t *out=(uint32_t *)(void *)pvr_dr_target(dr);const uint32_t *in=header;unsigned i;
 for(i=0;i<8;++i)out[i]=in[i];
 pvr_dr_commit(out);
}

int dc_level_models_draw(dc_goal_runtime *rt,const dc_instance_camera *cam,int list){
 enum dc_goal_error prior=rt->error;int li=list_index(list);float b[10];unsigned e;
 if(li<0)return 0;
 dc_instance_view(cam,b);
 pvr_dr_init(&dr);
 for(e=0;e<kept_count;++e){
  kept *kp=&frame_kept[e];model *m=&models[kp->model];uint32_t i,start;const void *current=NULL;
  if(!(m->lists&(1u<<li)))continue;
  if(!joints(rt,m,kp->proc,b,cam)){++dc_level_model_metrics.joint_misses;continue;}
  start=(uint32_t)timer_us_gettime64();
  for(i=0;i<m->verts;++i){
   float o[3];
   skin(m,i,o);
   vertex_flags[i]=0;
   if(o[2]>=DC_SCENE_NEAR&&o[2]<=4e6f){float inv=1.f/o[2],px=320.f+390.f*o[0]*inv,py=260.f-390.f*o[1]*inv;
    if(px>=-1024.f&&px<=1664.f&&py>=-1024.f&&py<=1504.f){
     scratch[i][0]=px;scratch[i][1]=py;scratch[i][2]=inv;vertex_flags[i]=VERTEX_GUARD;continue;}
   }
   vertex_flags[i]=(uint8_t)outside_planes(o[0],o[1],o[2]);
  }
  {uint32_t us=(uint32_t)timer_us_gettime64()-start;if(us>dc_level_model_metrics.skin_us_max)dc_level_model_metrics.skin_us_max=us;}
  if(!kp->drawn){kp->drawn=1;++dc_level_model_metrics.drawn;if(kp->target)frame_jak=1;else frame_actor=1;}
  for(i=0;i<m->batch_count;++i){
   const dsk_batch *bt=&m->batches[i];uint32_t k;int textured=bt->texture!=0xffffu;const void *header;
   if(m->batch_list[i]!=list)continue;
   header=textured?(const void *)&m->headers[i]:(const void *)&colour[li];
   if(header!=current){header_out(header);current=header;}
   for(k=0;k<bt->count;k+=3){
    const uint16_t *ix=&m->ix[bt->first+k];dc_clip_vertex input[3],poly[DC_RENDER_CLIP_CAP];
    float px[DC_RENDER_CLIP_CAP],py[DC_RENDER_CLIP_CAP],pz[DC_RENDER_CLIP_CAP];unsigned j;int n,fan;
    const unsigned f0=vertex_flags[ix[0]],f1=vertex_flags[ix[1]],f2=vertex_flags[ix[2]];
    if(f0&f1&f2&VERTEX_GUARD){  /* the PVR clips it to the screen */
     for(j=0;j<3;++j){const float *s=scratch[ix[j]];const dsk_vertex *v=&m->v[ix[j]];pvr_vertex_t *pv=pvr_dr_target(dr);
      pv->flags=j==2?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX;pv->x=s[0];pv->y=s[1];pv->z=s[2];
      pv->u=textured?v->u*(1.f/1024.f):0.f;pv->v=textured?v->v*(1.f/1024.f):0.f;pv->argb=v->argb;pv->oargb=0;pvr_dr_commit(pv);}
     ++frame_triangles;++dc_level_model_metrics.triangles;
     continue;
    }
    if(f0&f1&f2&31u)continue;  /* wholly outside one plane: the clipper returns it empty */
    /* crosses the near plane or leaves the guard band: the clipper, from view positions */
    for(j=0;j<3;++j){float s[3];const dsk_vertex *v=&m->v[ix[j]];
     skin(m,ix[j],s);
     input[j]=(dc_clip_vertex){s[0],s[1],s[2],textured?v->u*(1.f/1024.f):0.f,textured?v->v*(1.f/1024.f):0.f,v->argb};}
    n=dc_render_clip_viewport(input,poly);
    if(n<0){++dc_level_model_metrics.invalid;rt->error=prior;return 0;}
    if(n<3)continue;
    for(j=0;j<(unsigned)n;++j){float inv=1.f/poly[j].z;px[j]=320.f+390.f*poly[j].x*inv;py[j]=260.f-390.f*poly[j].y*inv;pz[j]=inv;}
    for(fan=1;fan<n-1;++fan){
     unsigned ids[3]={0,(unsigned)fan,(unsigned)fan+1};
     for(j=0;j<3;++j){unsigned q=ids[j];pvr_vertex_t *pv=pvr_dr_target(dr);
      pv->flags=j==2?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX;pv->x=px[q];pv->y=py[q];pv->z=pz[q];
      pv->u=poly[q].u;pv->v=poly[q].v;pv->argb=poly[q].argb;pv->oargb=0;pvr_dr_commit(pv);}
     ++frame_triangles;++dc_level_model_metrics.triangles;
    }
   }
  }
 }
 if(frame_triangles>dc_level_model_metrics.max_triangles)dc_level_model_metrics.max_triangles=frame_triangles;
 rt->error=prior;return 1;
}

void *dc_level_heap_alloc(size_t bytes){void *p=memalign(32,bytes);if(!p)dc_level_model_metrics.heap_free_before=heap_free();return p;}
uint32_t dc_level_heap_free(void){return heap_free();}
/* Called once per frame after the last list: per-frame counters. */
void dc_level_models_end_frame(void){
 if(frame_jak)++dc_level_model_metrics.jak_frames;
 if(frame_actor)++dc_level_model_metrics.actor_frames;
}
#endif
