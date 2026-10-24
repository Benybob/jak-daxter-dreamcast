/* Closest capsule-axis/triangle distance and conservative sweep (AI-assisted), ISC. */
#include "world_collision.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
struct dc_world_cached_triangle {dc_world_triangle t;dc_vec3 normal,lo,hi;};
static dc_vec3 add(dc_vec3 a,dc_vec3 b){dc_vec3 r={a.x+b.x,a.y+b.y,a.z+b.z};return r;}
static dc_vec3 sub(dc_vec3 a,dc_vec3 b){dc_vec3 r={a.x-b.x,a.y-b.y,a.z-b.z};return r;}
static dc_vec3 mul(dc_vec3 a,float b){dc_vec3 r={a.x*b,a.y*b,a.z*b};return r;}
static float dot(dc_vec3 a,dc_vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static dc_vec3 cross(dc_vec3 a,dc_vec3 b){dc_vec3 r={a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};return r;}
static float clamp(float a,float lo,float hi){return fmaxf(lo,fminf(hi,a));}
static int valid(dc_vec3 p){return isfinite(p.x)&&isfinite(p.y)&&isfinite(p.z)&&fabsf(p.x)<=4096&&fabsf(p.y)<=4096&&fabsf(p.z)<=4096;}
static dc_vec3 nearest_triangle(dc_vec3 p,const dc_world_triangle *t){
 dc_vec3 ab=sub(t->b,t->a),ac=sub(t->c,t->a),ap=sub(p,t->a),bp,cp;
 float d1=dot(ab,ap),d2=dot(ac,ap),d3,d4,d5,d6,va,vb,vc,s;
 if(d1<=0 && d2<=0)return t->a;
 bp=sub(p,t->b);d3=dot(ab,bp);d4=dot(ac,bp);
 if(d3>=0 && d4<=d3)return t->b;
 vc=d1*d4-d3*d2;if(vc<=0 && d1>=0 && d3<=0)return add(t->a,mul(ab,d1/(d1-d3)));
 cp=sub(p,t->c);d5=dot(ab,cp);d6=dot(ac,cp);
 if(d6>=0 && d5<=d6)return t->c;
 vb=d5*d2-d1*d6;if(vb<=0 && d2>=0 && d6<=0)return add(t->a,mul(ac,d2/(d2-d6)));
 va=d3*d6-d5*d4;
 if(va<=0 && d4-d3>=0 && d5-d6>=0)return add(t->b,mul(sub(t->c,t->b),(d4-d3)/((d4-d3)+(d5-d6))));
 s=1/(va+vb+vc);return add(t->a,add(mul(ab,vb*s),mul(ac,vc*s)));
}
static void nearest_segments(dc_vec3 p,dc_vec3 q,dc_vec3 a,dc_vec3 b,dc_vec3 *u,dc_vec3 *v){
 dc_vec3 d=sub(q,p),e=sub(b,a),r=sub(p,a);float dd=dot(d,d),ee=dot(e,e),de=dot(d,e),dr=dot(d,r),er=dot(e,r),s,t,den;
 if(dd<1e-12f){s=0;t=ee<1e-12f?0:clamp(er/ee,0,1);}
 else if(ee<1e-12f){t=0;s=clamp(-dr/dd,0,1);}
 else{den=dd*ee-de*de;s=den>1e-12f?clamp((de*er-dr*ee)/den,0,1):0;
  t=(de*s+er)/ee;
  if(t<0){t=0;s=clamp(-dr/dd,0,1);}else if(t>1){t=1;s=clamp((de-dr)/dd,0,1);}}
 *u=add(p,mul(d,s));*v=add(a,mul(e,t));
}
static void consider(dc_vec3 p,dc_vec3 q,float *best,dc_vec3 *u,dc_vec3 *v){
 float d=dot(sub(p,q),sub(p,q));if(d<*best){*best=d;*u=p;*v=q;}
}
static float axis_distance(dc_vec3 p,dc_vec3 q,const dc_world_cached_triangle *tri,dc_vec3 *normal){
 dc_vec3 u=p,v=nearest_triangle(p,&tri->t),a,b,ends[3]={tri->t.a,tri->t.b,tri->t.c};
 float best=dot(sub(u,v),sub(u,v)),den=dot(tri->normal,sub(q,p)),t;unsigned i;
 consider(q,nearest_triangle(q,&tri->t),&best,&u,&v);
 if(fabsf(den)>1e-9f){
  t=dot(tri->normal,sub(tri->t.a,p))/den;
  if(t>=0 && t<=1){a=add(p,mul(sub(q,p),t));b=nearest_triangle(a,&tri->t);consider(a,b,&best,&u,&v);}
 }
 for(i=0;i<3;++i){nearest_segments(p,q,ends[i],ends[(i+1)%3],&a,&b);consider(a,b,&best,&u,&v);}
 if(best>1e-12f){float distance=sqrtf(best);*normal=mul(sub(u,v),1/distance);return distance;}
 *normal=tri->normal;
 if(dot(*normal,sub(mul(add(p,q),.5f),tri->t.a))<0)*normal=mul(*normal,-1);
 return 0;
}
const char *dc_collision_build(dc_collision_world *world,const dc_world_triangle *input,uint32_t count){
 dc_world_cached_triangle *out;uint32_t i;
 if(!world || !input || !count || count>8192 || world->triangles)return "invalid collision build arguments";
 out=malloc(count*sizeof(*out));if(!out)return "collision allocation failed";
 for(i=0;i<count;++i){dc_vec3 n;float length;unsigned mode=(input[i].pat>>3)&7;
  if(!valid(input[i].a)||!valid(input[i].b)||!valid(input[i].c)||mode>2){free(out);return "invalid collision triangle";}
  n=cross(sub(input[i].b,input[i].a),sub(input[i].c,input[i].a));length=sqrtf(dot(n,n));
  if(!isfinite(length)||length<1e-7f){free(out);return "degenerate collision triangle";}
  out[i].t=input[i];out[i].normal=mul(n,1/length);
  out[i].lo=(dc_vec3){fminf(input[i].a.x,fminf(input[i].b.x,input[i].c.x)),fminf(input[i].a.y,fminf(input[i].b.y,input[i].c.y)),fminf(input[i].a.z,fminf(input[i].b.z,input[i].c.z))};
  out[i].hi=(dc_vec3){fmaxf(input[i].a.x,fmaxf(input[i].b.x,input[i].c.x)),fmaxf(input[i].a.y,fmaxf(input[i].b.y,input[i].c.y)),fmaxf(input[i].a.z,fmaxf(input[i].b.z,input[i].c.z))};
 }
 world->triangles=out;world->count=count;world->candidate_count=world->distance_tests=0;return NULL;
}
void dc_collision_free(dc_collision_world *world){if(world){free(world->triangles);memset(world,0,sizeof(*world));}}
int dc_collision_surface_events(const dc_collision_world *w,uint32_t out[2]){
 uint32_t bits[2]={0,0},i,event;
 if(!w||!w->triangles||!w->count||w->count>8192||!out)return 0;
 for(i=0;i<w->count;++i){
  uint32_t pat=w->triangles[i].t.pat;
  if(pat&1)continue;
  event=(pat>>14)&63;bits[event>>5]|=UINT32_C(1)<<(event&31);
 }
 out[0]=bits[0];out[1]=bits[1];return 1;
}
static uint32_t read32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static float read_float(const unsigned char *p){uint32_t bits=read32(p);float f;memcpy(&f,&bits,4);return f;}
const char *dc_collision_load(FILE *file,dc_collision_world *world,dc_vec3 *origin){
 unsigned char h[24],raw[40];dc_world_triangle *input;dc_vec3 o;uint32_t i,count;const char *error;
 if(!file||!world||!origin||world->triangles)return "invalid collision load arguments";
 if(fread(h,1,sizeof h,file)!=sizeof h || memcmp(h,"DCC1",4) || read32(h+4)!=1)return "invalid collision header";
 count=read32(h+8);o=(dc_vec3){read_float(h+12),read_float(h+16),read_float(h+20)};
 if(!count || count>8192 || !valid(o))return "invalid collision count/origin";
 input=malloc(count*sizeof(*input));if(!input)return "collision input allocation failed";
 for(i=0;i<count;++i){
  if(fread(raw,1,sizeof raw,file)!=sizeof raw){free(input);return "short collision payload";}
  input[i].a=(dc_vec3){read_float(raw),read_float(raw+4),read_float(raw+8)};
  input[i].b=(dc_vec3){read_float(raw+12),read_float(raw+16),read_float(raw+20)};
  input[i].c=(dc_vec3){read_float(raw+24),read_float(raw+28),read_float(raw+32)};input[i].pat=read32(raw+36);
 }
 if(fgetc(file)!=EOF || ferror(file)){free(input);return "trailing/unreadable collision payload";}
 error=dc_collision_build(world,input,count);free(input);if(!error)*origin=o;return error;
}
static int candidates(dc_collision_world *w,dc_vec3 p,dc_vec3 motion,float r,float h){
 dc_vec3 end=add(p,motion),lo={fminf(p.x,end.x)-r-.002f,fminf(p.y,end.y)-.002f,fminf(p.z,end.z)-r-.002f},
  hi={fmaxf(p.x,end.x)+r+.002f,fmaxf(p.y,end.y)+h+.002f,fmaxf(p.z,end.z)+r+.002f};uint32_t i;
 w->candidate_count=0;
 for(i=0;i<w->count;++i){const dc_world_cached_triangle *t=&w->triangles[i];
  if((t->t.pat&1) || t->lo.x>hi.x || t->hi.x<lo.x || t->lo.y>hi.y || t->hi.y<lo.y || t->lo.z>hi.z || t->hi.z<lo.z)continue;
  if(w->candidate_count==1024){w->failure=1;return 0;}
  w->candidates[w->candidate_count++]=(uint16_t)i;
 }return 1;
}
static int sweep_one(dc_collision_world *w,dc_vec3 start,dc_vec3 delta,float radius,float height,
                     const dc_world_cached_triangle *tri,float *hit,dc_vec3 *normal){
 float time=0;unsigned step;
 for(step=0;step<48;++step){
  dc_vec3 p=add(start,mul(delta,time)),q,n;float distance,gap,closing,advance;
  p.y+=radius;q=p;q.y+=height-2*radius;
  distance=axis_distance(p,q,tri,&n);++w->distance_tests;gap=distance-radius;
  closing=-dot(delta,n);
  /* The distance to this convex triangle/capsule pair is convex along a line:
   * a nonclosing separating normal cannot become a hit later on that line. */
  if(closing<=1e-6f)return 0;
  /* Use the same shell for advancement and contact. A wider acceptance band
   * than stopping distance creates false internal-edge impulses on tessellated
   * planes, even when the capsule only moves tangent to the whole surface. */
  if(gap<=0.001002f){*hit=time;*normal=n;return 1;}
  advance=(gap-0.001f)/closing;
  if(advance<1e-7f)advance=1e-7f;
  time+=advance;
  if(time>1)return 0;
 }
 return -1;
}
int dc_collision_clear(dc_collision_world *w,dc_vec3 p,float radius,float height){
 dc_vec3 q,n;uint32_t i;
 if(!w||!w->triangles||!valid(p)||!isfinite(radius)||!isfinite(height)||radius<.05f||radius>2||height<2*radius||height>8)return 0;
 if(!candidates(w,p,(dc_vec3){0},radius,height))return 0;
 p.y+=radius;q=p;q.y+=height-2*radius;
 for(i=0;i<w->candidate_count;++i)if(axis_distance(p,q,&w->triangles[w->candidates[i]],&n)<radius-.0001f)return 0;
 return 1;
}
static int feasible(dc_vec3 v,const dc_vec3 *planes,unsigned count){
 unsigned i;for(i=0;i<count;++i)if(dot(v,planes[i])<-1e-7f)return 0;return 1;
}
static dc_vec3 constrain(dc_vec3 desired,const dc_vec3 *planes,unsigned count){
 dc_vec3 best={0},candidate;float error=dot(desired,desired);unsigned i,j;
 if(feasible(desired,planes,count))return desired;
 /* Projection onto the intersection of contact half-spaces. The nearest
  * feasible vector in 3D lies in the interior, one plane, a two-plane crease,
  * or the zero vertex. Keep all planes to avoid oscillation at rock seams. */
 for(i=0;i<count;++i){
  candidate=sub(desired,mul(planes[i],dot(desired,planes[i])));
  if(feasible(candidate,planes,count)){
   float d=dot(sub(candidate,desired),sub(candidate,desired));if(d<error){error=d;best=candidate;}
  }
  for(j=0;j<i;++j){dc_vec3 crease=cross(planes[i],planes[j]);float length=dot(crease,crease);
   if(length<1e-10f)continue;
   candidate=mul(crease,dot(desired,crease)/length);
   if(feasible(candidate,planes,count)){
    float d=dot(sub(candidate,desired),sub(candidate,desired));if(d<error){error=d;best=candidate;}
   }
  }
 }return best;
}
static int collide(dc_collision_world *w,dc_vec3 start,dc_vec3 displacement,float radius,float height,dc_collision_result *out,int sliding){
 dc_collision_result result={0};dc_vec3 p=start,remaining=displacement,planes[8];unsigned slide,plane_count=0;
 if(!w||!out||!w->triangles||!valid(start)||!valid(displacement)||!isfinite(radius)||!isfinite(height)||
    radius<0.05f||radius>2||height<2*radius||height>8||dot(displacement,displacement)>64)return 0;
 w->distance_tests=0;w->failure=0;w->failed_triangle=UINT32_MAX;
 for(slide=0;slide<8;++slide){
  float earliest=2;dc_vec3 normal={0};uint32_t best=0,i;int found=0;
  if(dot(remaining,remaining)<1e-12f)break;
  if(!candidates(w,p,remaining,radius,height))return 0;
  for(i=0;i<w->candidate_count;++i){uint32_t index=w->candidates[i];dc_vec3 n;float hit;
   int status=sweep_one(w,p,remaining,radius,height,&w->triangles[index],&hit,&n);
   if(status<0){w->failure=2;w->failed_triangle=index;return 0;}
   if(status && hit<earliest){earliest=hit;normal=n;best=index;found=1;}
  }
  if(!found){p=add(p,remaining);remaining=(dc_vec3){0};break;}
  p=add(p,mul(remaining,earliest));remaining=mul(remaining,1-earliest);
  planes[plane_count++]=normal;remaining=constrain(remaining,planes,plane_count);
  ++result.contacts;result.normal=normal;result.pat=w->triangles[best].t.pat;
  {unsigned mode=(result.pat>>3)&7,event=(result.pat>>14)&63;
   float threshold=mode==0?.2f:(mode==2?.82f:2.0f);
   result.event_bits[event/32]|=UINT32_C(1)<<(event%32);
   if(normal.y>=threshold && w->triangles[best].normal.y>0 && dot(normal,w->triangles[best].normal)>=.9f){
    result.grounded=1;result.ground_normal=normal;result.ground_pat=result.pat;
   }
  }
  if(normal.y<=-.55f)result.ceiling=1;
  if(!sliding){remaining=(dc_vec3){0};break;}
 }
 if(dot(remaining,remaining)>1e-8f){w->failure=3;return 0;}
 result.position=p;result.distance_tests=w->distance_tests;*out=result;return 1;
}
int dc_collision_move(dc_collision_world *w,dc_vec3 p,dc_vec3 d,float r,float h,dc_collision_result *out){return collide(w,p,d,r,h,out,1);}
int dc_collision_cast(dc_collision_world *w,dc_vec3 p,dc_vec3 d,float r,float h,dc_collision_result *out){return collide(w,p,d,r,h,out,0);}
