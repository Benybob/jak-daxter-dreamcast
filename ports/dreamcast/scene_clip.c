/* One-plane Sutherland-Hodgman; view space avoids camera-zero poles (AI-assisted). */
#include "scene_clip.h"
#include <string.h>
/* IEEE binary32 on the supported host/SH4 compilers. Integer classification
 * rejects NaN/Inf without out-of-line libm calls on the clipping path. */
static inline int bounded(float v,uint32_t maximum){
 union {float f;uint32_t u;} bits;bits.f=v;return (bits.u&UINT32_C(0x7fffffff))<=maximum;
}
static int valid(const dc_clip_vertex *v){
 return bounded(v->x,UINT32_C(0x46000000))&&bounded(v->y,UINT32_C(0x46000000))&&
        bounded(v->z,UINT32_C(0x46000000))&&bounded(v->u,UINT32_C(0x47800000))&&
        bounded(v->v,UINT32_C(0x47800000));
}
static dc_clip_vertex intersection(dc_clip_vertex a,dc_clip_vertex b){
 dc_clip_vertex v;float t;unsigned shift;uint32_t color=0;
 if(a.z==DC_SCENE_NEAR)return a;
 if(b.z==DC_SCENE_NEAR)return b;
 t=(DC_SCENE_NEAR-a.z)/(b.z-a.z);
 v.x=a.x+(b.x-a.x)*t;v.y=a.y+(b.y-a.y)*t;v.z=DC_SCENE_NEAR;
 v.u=a.u+(b.u-a.u)*t;v.v=a.v+(b.v-a.v)*t;
 for(shift=0;shift<32;shift+=8){
  float c=(float)((a.argb>>shift)&255)+((float)((b.argb>>shift)&255)-(float)((a.argb>>shift)&255))*t;
  /* A crossing edge gives t in [0,1]; convex 8-bit endpoints stay in [0,255]. */
  uint32_t channel=(uint32_t)(c+.5f);color|=channel<<shift;
 }
 v.argb=color;return v;
}
static int same(const dc_clip_vertex *a,const dc_clip_vertex *b){
 return a->x==b->x&&a->y==b->y&&a->z==b->z&&a->u==b->u&&a->v==b->v&&a->argb==b->argb;
}
static void append(dc_clip_vertex *out,unsigned *n,dc_clip_vertex v){
 if(!*n||!same(&out[*n-1],&v))out[(*n)++]=v;
}
int dc_scene_clip_near(const dc_clip_vertex in[3],dc_clip_vertex out[4]){
 dc_clip_vertex poly[4],previous;unsigned i,n=0;int inside_previous;
 if(!in||!out)return -1;
 for(i=0;i<3;++i)if(!valid(&in[i]))return -1;
 previous=in[2];inside_previous=previous.z>=DC_SCENE_NEAR;
 for(i=0;i<3;++i){dc_clip_vertex current=in[i];int inside=current.z>=DC_SCENE_NEAR;
  if(inside!=inside_previous)append(poly,&n,intersection(previous,current));
  if(inside)append(poly,&n,current);
  previous=current;inside_previous=inside;
 }
 if(n>1&&same(&poly[0],&poly[n-1]))--n;
 if(n<3)return 0;
 memcpy(out,poly,n*sizeof(*out));return (int)n;
}
