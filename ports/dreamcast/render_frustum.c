/* Bounded convex clipping, including huge crossing faces (AI-assisted), ISC. */
#include "render_frustum.h"
#include <float.h>
#include <string.h>
typedef char frustum_binary64[(sizeof(double)==8&&DBL_MANT_DIG==53&&sizeof(float)==4)?1:-1];
typedef struct {double x,y,z,u,v,color[4];} vertex64;
static int bounded(float value,float limit) {
 uint32_t bits;__builtin_memcpy(&bits,&value,4);
 return (bits&UINT32_C(0x7fffffff))<UINT32_C(0x7f800000)&&value>=-limit&&value<=limit;
}
static double distance(vertex64 v,unsigned plane) {
 switch(plane) {
 case 0:return v.z-(double)DC_SCENE_NEAR;
 case 1:return 390.*v.x+320.*v.z;
 case 2:return 320.*v.z-390.*v.x;
 case 3:return 260.*v.z-390.*v.y;
 default:return 220.*v.z+390.*v.y;
 }
}
/* Most triangles require no binary64 arithmetic. A conservative single-
 * precision error interval decides clear sides; ambiguous boundary values use
 * the binary64 plane expression rather than expanding the visible volume. */
static int outside(const dc_clip_vertex *v,unsigned plane) {
 float a,b,d,error;double precise;
 switch(plane) {
 case 0:return v->z<DC_SCENE_NEAR;
 case 1:a=390.f*v->x;b=320.f*v->z;break;
 case 2:a=-390.f*v->x;b=320.f*v->z;break;
 case 3:a=-390.f*v->y;b=260.f*v->z;break;
 default:a=390.f*v->y;b=220.f*v->z;break;
 }
 d=a+b;error=(__builtin_fabsf(a)+__builtin_fabsf(b))*(4.f*FLT_EPSILON);
 if(d>error)return 0;
 if(d< -error)return 1;
 switch(plane) {
 case 1:precise=390.*v->x+320.*v->z;break;
 case 2:precise=320.*v->z-390.*v->x;break;
 case 3:precise=260.*v->z-390.*v->y;break;
 default:precise=220.*v->z+390.*v->y;break;
 }
 return precise<0;
}
static int equal(vertex64 a,vertex64 b) {
 unsigned i;if(a.x!=b.x||a.y!=b.y||a.z!=b.z||a.u!=b.u||a.v!=b.v)return 0;
 for(i=0;i<4;++i)if(a.color[i]!=b.color[i])return 0;
 return 1;
}
static int append(vertex64 *out,unsigned *n,vertex64 v) {
 if(*n&&equal(out[*n-1],v))return 1;
 if(*n>=DC_RENDER_CLIP_CAP)return 0;
 out[(*n)++]=v;return 1;
}
static vertex64 crossing(vertex64 a,vertex64 b,double da,double db,unsigned plane) {
 vertex64 r;unsigned i;double t=da/(da-db);
 /* A crossing edge has opposite signs or an exact plane endpoint. Returning
  * endpoints avoids duplicate vertices and preserves their complete attributes. */
 if(da==0)return a;
 if(db==0)return b;
 r.x=a.x+(b.x-a.x)*t;r.y=a.y+(b.y-a.y)*t;r.z=a.z+(b.z-a.z)*t;
 r.u=a.u+(b.u-a.u)*t;r.v=a.v+(b.v-a.v)*t;
 for(i=0;i<4;++i)r.color[i]=a.color[i]+(b.color[i]-a.color[i])*t;
 /* Make the active constraint exact in the working precision. */
 switch(plane) {
 case 0:r.z=DC_SCENE_NEAR;break;
 case 1:r.x=-320.*r.z/390.;break;
 case 2:r.x=320.*r.z/390.;break;
 case 3:r.y=260.*r.z/390.;break;
 default:r.y=-220.*r.z/390.;break;
 }
 return r;
}
int dc_render_clip_viewport(const dc_clip_vertex input[3],dc_clip_vertex output[DC_RENDER_CLIP_CAP]) {
 vertex64 buffers[2][DC_RENDER_CLIP_CAP];unsigned i,c,plane,n=3,current=0,all=31,any=0;
 if(!input||!output)return -1;
 for(i=0;i<3;++i) {
  const dc_clip_vertex *s=&input[i];unsigned mask=0;
  if(!bounded(s->x,4000000.f)||!bounded(s->y,4000000.f)||!bounded(s->z,4000000.f)||
     !bounded(s->u,65536.f)||!bounded(s->v,65536.f))return -1;
  for(plane=0;plane<5;++plane)if(outside(s,plane))mask|=1u<<plane;
  all&=mask;any|=mask;
 }
 if(all)return 0;
 if(!any){memcpy(output,input,3*sizeof(*input));return 3;}
 for(i=0;i<3;++i) {
  const dc_clip_vertex *s=&input[i];vertex64 *v=&buffers[0][i];
  v->x=s->x;v->y=s->y;v->z=s->z;v->u=s->u;v->v=s->v;
  for(c=0;c<4;++c)v->color[c]=(s->argb>>(8*c))&255u;
 }
 for(plane=0;plane<5;++plane) {
  vertex64 *in=buffers[current],*out=buffers[current^1u],previous=in[n-1];
  unsigned next=0;double dp=distance(previous,plane);
  for(i=0;i<n;++i) {
   vertex64 v=in[i];double d=distance(v,plane);
   if((dp>=0)!=(d>=0)&&!append(out,&next,crossing(previous,v,dp,d,plane)))return -1;
   if(d>=0&&!append(out,&next,v))return -1;
   previous=v;dp=d;
  }
  if(next>1&&equal(out[0],out[next-1]))--next;
  if(next<3)return 0;
  n=next;current^=1u;
 }
 for(i=0;i<n;++i) {
  const vertex64 *v=&buffers[current][i];dc_clip_vertex r;
  r.x=(float)v->x;r.y=(float)v->y;r.z=(float)v->z;r.u=(float)v->u;r.v=(float)v->v;r.argb=0;
  for(c=0;c<4;++c) {
   double channel=v->color[c];if(channel<0)channel=0;if(channel>255)channel=255;
   r.argb|=(uint32_t)(channel+.5)<<(8*c);
  }
  output[i]=r;
 }
 return (int)n;
}
