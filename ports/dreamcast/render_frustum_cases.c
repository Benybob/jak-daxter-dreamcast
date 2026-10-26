/* Exact rational host-generated hulls, no original assets (AI-assisted), ISC. */
#include "render_frustum.h"
#include "render_frustum_cases.h"
#include <math.h>
#include <string.h>
struct frustum_case {dc_clip_vertex in[3];uint32_t count;dc_clip_vertex expected[8];};
#include "build/frustum_fixture.h"
static void check(dc_frustum_report *r,int ok,unsigned line) {
 ++r->total;if(ok)++r->passed;else if(!r->failed_line)r->failed_line=line;
}
#define CHECK(ok) check(r,!!(ok),__LINE__)
static int close_to(float a,float b) {return isfinite(a)&&fabs((double)a-b)<=.00001+.000002*fabs((double)b);}
static double area(const dc_clip_vertex *v,unsigned n) {
 unsigned i;double sum=0;
 for(i=0;i<n;++i) {
  unsigned next=(i+1)%n;double ax=320.+390.*v[i].x/v[i].z,ay=260.-390.*v[i].y/v[i].z;
  double bx=320.+390.*v[next].x/v[next].z,by=260.-390.*v[next].y/v[next].z;sum+=ax*by-bx*ay;
 }
 return sum;
}
void dc_render_frustum_cases(dc_frustum_report *r) {
 unsigned i,j,k,shift;memset(r,0,sizeof(*r));
 for(i=0;i<sizeof(frustum_cases)/sizeof(frustum_cases[0]);++i) {
  const struct frustum_case *c=&frustum_cases[i];dc_clip_vertex input[3];uint32_t used=0;
  struct {uint32_t before[4];dc_clip_vertex v[8];uint32_t after[4];} output;int n;
  memcpy(input,c->in,sizeof input);memset(&output,0x6d,sizeof output);n=dc_render_clip_viewport(input,output.v);
  CHECK(n==(int)c->count);CHECK(!memcmp(input,c->in,sizeof input));
  CHECK(output.before[0]==0x6d6d6d6d&&output.before[3]==0x6d6d6d6d&&output.after[0]==0x6d6d6d6d&&output.after[3]==0x6d6d6d6d);
  if(n<0||n>8)continue;
  for(j=0;j<(unsigned)n;++j) {
   dc_clip_vertex *v=&output.v[j];double best=HUGE_VAL;unsigned match=8;const dc_clip_vertex *e;
   for(k=0;k<c->count;++k)if(!(used&(1u<<k))) {
    const dc_clip_vertex *p=&c->expected[k];double dx=(double)v->x-p->x,dy=(double)v->y-p->y,dz=(double)v->z-p->z;
    double d=dx*dx+dy*dy+dz*dz;if(d<best){best=d;match=k;}
   }
   if(match==8){CHECK(0);continue;}
   used|=1u<<match;e=&c->expected[match];
   CHECK(close_to(v->x,e->x));CHECK(close_to(v->y,e->y));CHECK(close_to(v->z,e->z));
   CHECK(close_to(v->u,e->u));CHECK(close_to(v->v,e->v));
   for(shift=0;shift<32;shift+=8) {int a=(v->argb>>shift)&255,b=(e->argb>>shift)&255;CHECK(a-b>=-1&&a-b<=1);}
   CHECK(v->z>=DC_SCENE_NEAR-.0000001f);
   {double x=320.+390.*v->x/v->z,y=260.-390.*v->y/v->z;
    CHECK(x>=-.002);CHECK(x<=640.002);CHECK(y>=-.002);CHECK(y<=480.002);}
  }
  if(n>=3){double a=area(c->expected,c->count),b=area(output.v,(unsigned)n);CHECK(fabs(a)<.001||a*b>0);}
  ++r->cases;if((unsigned)n>r->max_vertices)r->max_vertices=(unsigned)n;
 }
}
