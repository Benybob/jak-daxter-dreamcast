/* Analytic clipping/UV/color/area oracle, shared host/SH4 (AI-assisted), ISC. */
#include "scene_clip.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
static unsigned count,bad;
#define CHECK(x) do{++count;if(!(x))++bad;}while(0)
static float area(const dc_clip_vertex *p,int n){
 int i;float a=0;for(i=0;i<n;++i)a+=p[i].x*p[(i+1)%n].y-p[(i+1)%n].x*p[i].y;return a*.5f;
}
unsigned dc_scene_clip_cases(unsigned *total){
 dc_clip_vertex tri[3]={{0,0,0,0,0,0xff000000},{2,0,.1f,1,0,0xffffffff},{0,2,.1f,0,1,0xffffffff}};
 dc_clip_vertex out[6],saved[6],turned[4];int n,i,j;unsigned mask,rotation;
 count=bad=0;
 memset(out,0x5a,sizeof out);memcpy(saved,out,sizeof out);
 n=dc_scene_clip_near(tri,out+1);CHECK(n==4);
 CHECK(!memcmp(out,saved,sizeof *out)&&!memcmp(out+5,saved+5,sizeof *out));
 CHECK(out[1].x==0&&out[1].y==1&&out[1].z==DC_SCENE_NEAR);
 CHECK(out[2].x==1&&out[2].y==0&&out[2].z==DC_SCENE_NEAR);
 CHECK(out[1].u==0&&out[1].v==.5f&&out[2].u==.5f&&out[2].v==0);
 CHECK(out[1].argb==0xff808080&&out[2].argb==0xff808080);
 CHECK(fabsf(area(out+1,4)-1.5f)<1e-6f);
 /* Linear UV/color interpolation happens before division, not on screen edges. */
 CHECK(fabsf(out[2].u/out[2].z-10)<1e-5f);
 /* Every channel across different edge ratios; independent double reference. */
 for(i=2;i<=16;++i){unsigned channel;
  dc_clip_vertex colors[3]={{0,0,0,0,0,0x00ff3480},{2,0,.05f*i,1,0,0xff00b30f},{0,2,.05f*i,0,1,0xff00b30f}};
  CHECK(dc_scene_clip_near(colors,out+1)==4);
  for(channel=0;channel<32;channel+=8){
   double t=(double)DC_SCENE_NEAR/(double)colors[1].z;
   double a=(double)((colors[0].argb>>channel)&255),b=(double)((colors[1].argb>>channel)&255);
   unsigned expected=(unsigned)(a+(b-a)*t+.5),observed=(out[2].argb>>channel)&255;
   CHECK(abs((int)expected-(int)observed)<=1);
  }
 }
 tri[0].z=-.1f;tri[1].z=tri[2].z=0;
 CHECK(dc_scene_clip_near(tri,out+1)==0);
 tri[0].z=tri[1].z=tri[2].z=.05f;
 CHECK(dc_scene_clip_near(tri,out+1)==3&&!memcmp(out+1,tri,sizeof tri));
 tri[0].z=.025f;tri[1].z=.1f;tri[2].z=.025f;
 CHECK(dc_scene_clip_near(tri,out+1)==3);
 /* Every inside/outside pattern and cyclic order, plus winding reversal. */
 for(mask=0;mask<8;++mask)for(rotation=0;rotation<3;++rotation){
  const float xy[3][2]={{0,0},{2,0},{0,2}};unsigned inside=0;
  for(i=0;i<3;++i){unsigned k=((unsigned)i+rotation)%3;
   tri[i]=(dc_clip_vertex){xy[k][0],xy[k][1],(mask&(1u<<k))?.1f:0,0,0,0xffffffff};
   if(mask&(1u<<k))++inside;
  }
  n=dc_scene_clip_near(tri,out+1);CHECK(n==(inside==0?0:(inside==2?4:3)));
  for(i=0;i<n;++i){CHECK(out[i+1].z>=DC_SCENE_NEAR);CHECK(isfinite(out[i+1].x)&&isfinite(out[i+1].y));}
  if(n){dc_clip_vertex tmp=tri[0];float prior=area(out+1,n);tri[0]=tri[2];tri[2]=tmp;
   j=dc_scene_clip_near(tri,turned);CHECK(j==n&&fabsf(area(turned,j)+prior)<1e-6f);
  }
 }
 for(i=0;i<5;++i){
  dc_clip_vertex in[3]={{0,0,.1f,0,0,0},{1,0,.1f,0,0,0},{0,1,.1f,0,0,0}};
  if(i==0)in[0].x=NAN;else if(i==1)in[0].z=INFINITY;else if(i==2)in[0].u=NAN;
  else if(i==3)in[0].v=65537;else in[0].x=8193;
  memcpy(out,saved,sizeof out);CHECK(dc_scene_clip_near(in,out+1)==-1);
  CHECK(!memcmp(out,saved,sizeof out));
 }
 *total=count;return bad;
}
