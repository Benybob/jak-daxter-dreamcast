/* The level ELF's ocean surface (AI-assisted), ISC. See level_ocean.h.
 * GOAL layout (retail offsets less the 4-byte type word): ocean-map
 * start-corner +12, far-color +28, ocean-colors +48 (a structure: rgba words,
 * r in the low byte, 128 full). */
#include "level_ocean.h"
#include <kos.h>
#include <math.h>
#include <string.h>
#include "render_frustum.h"
#include "scene_projection.h"

enum {RINGS=11,SECTORS=24,TEX=64,GRID=52,GRID_ROWS=49};
#define TILE_M 8.f
#define CELL_M 96.f
#define UNITS_PER_M 4096.f
volatile dc_level_ocean_metrics_t dc_level_ocean_metrics;
static const float radius[RINGS]={0,6,12,24,48,96,192,384,768,1536,3072};
static pvr_poly_hdr_t header __attribute__((aligned(32)));
static pvr_ptr_t texture;
static int texture_failed;
static pvr_dr_state_t dr;

/* 16-bit twiddled, mipmapped: level n x n starts at 6, 8, 0x10, 0x30, ... */
static uint32_t twiddle(uint32_t x,uint32_t y){
 uint32_t r=0,b;for(b=0;b<8;++b)r|=((y>>b)&1u)<<(2*b)|((x>>b)&1u)<<(2*b+1);return r;
}
static float wave(float u,float v){
 /* tiles at u, v in [0, 1): whole-number frequencies only */
 const float t=6.2831853f;
 float a=sinf(t*(u+2*v)+1.3f*sinf(t*(2*u-v))),b=sinf(t*(3*u-2*v)+1.1f*sinf(t*(u+3*v))+2.f),
       c=sinf(t*(5*u+4*v)+.9f*sinf(t*(4*u-5*v))+4.f);
 return .5f*a+.33f*b+.17f*c;  /* about -1..1 */
}
static int make_texture(void){
 enum {BYTES=6+2*(1+4+16+64+256+1024+4096)};
 static uint16_t level0[TEX*TEX];uint32_t size,at=6,x,y;
 if(texture)return 1;
 if(texture_failed)return 0;
 if(!(texture=pvr_mem_malloc((BYTES+31u)&~31u))){texture_failed=1;return 0;}
 for(y=0;y<TEX;++y)for(x=0;x<TEX;++x){
  float w=wave((x+.5f)/TEX,(y+.5f)/TEX),l=.80f+.14f*w,crest=w>.55f?(w-.55f)*1.1f:0;
  int r=(int)(31.f*(l+crest)),g=(int)(63.f*(l+crest)),b=(int)(31.f*(l+.6f*crest+.04f));
  if(r>31){r=31;}
  if(g>63){g=63;}
  if(b>31){b=31;}
  level0[y*TEX+x]=(uint16_t)(r<<11|g<<5|b);
 }
 memset(texture,0,6);
 /* each level the box filter of level 0, smallest first */
 for(size=1;size<=TEX;size*=2){uint32_t step=TEX/size;uint16_t *to=(uint16_t *)(void *)((unsigned char *)texture+at);
  for(y=0;y<size;++y)for(x=0;x<size;++x){uint32_t r=0,g=0,b=0,i,j,n=step*step;
   for(j=0;j<step;++j)for(i=0;i<step;++i){uint16_t p=level0[(y*step+j)*TEX+x*step+i];r+=p>>11;g+=(p>>5)&63u;b+=p&31u;}
   to[twiddle(x,y)]=(uint16_t)((r/n)<<11|(g/n)<<5|(b/n));}
  at+=2u*size*size;}
 {pvr_poly_cxt_t cxt;
  pvr_poly_cxt_txr(&cxt,PVR_LIST_OP_POLY,PVR_TXRFMT_RGB565|PVR_TXRFMT_TWIDDLED,TEX,TEX,texture,PVR_FILTER_BILINEAR);
  cxt.txr.mipmap=PVR_MIPMAP_ENABLE;cxt.txr.uv_clamp=PVR_UVCLAMP_NONE;cxt.gen.culling=PVR_CULLING_NONE;
  pvr_poly_compile(&header,&cxt);}
 dc_level_ocean_metrics.texture_bytes=BYTES;
 return 1;
}
static float f32at(dc_goal_runtime *rt,uint32_t address,int *ok){
 const unsigned char *q=dc_goal_resolve(rt,address,4);float f=0;
 if(q)memcpy(&f,q,4);else *ok=0;
 return f;
}
/* The map's colour at a point of its grid (cells of 96 m), ARGB for a
 * modulated texture: the PS2's 128 is full. */
static uint32_t colour(const unsigned char *grid,float gx,float gz){
 int ix,iz,k;float fx,fz,c[3];
 if(gx<0){gx=0;}
 if(gx>48.f){gx=48.f;}
 if(gz<0){gz=0;}
 if(gz>48.f){gz=48.f;}
 ix=(int)gx;iz=(int)gz;
 if(ix>47){ix=47;}
 if(iz>47){iz=47;}
 fx=gx-(float)ix;fz=gz-(float)iz;
 for(k=0;k<3;++k){
  const unsigned char *p=grid+4*(GRID*iz+ix)+k;
  float a=(float)p[0]+(float)(p[4]-p[0])*fx,b=(float)p[4*GRID]+(float)(p[4*GRID+4]-p[4*GRID])*fx;
  c[k]=2.f*(a+(b-a)*fz);
  if(c[k]>255.f){c[k]=255.f;}
  if(c[k]<0){c[k]=0;}
 }
 return 0xff000000u|(uint32_t)c[0]<<16|(uint32_t)c[1]<<8|(uint32_t)c[2];
}
int dc_level_ocean_draw(dc_goal_runtime *rt,const dc_instance_camera *cam,const float eye[3]){
 static dc_clip_vertex v[RINGS][SECTORS];
 static float cs[SECTORS][2];static int have_cs;
 enum dc_goal_error prior=rt->error;
 uint32_t symbol=dc_goal_find(rt,"*ocean-map*"),map=symbol?dc_goal_symbol_value(rt,symbol):0,colours=0;
 const unsigned char *q,*grid=NULL;float start[3]={0,0,0},b[10],base_u,base_v,drift;int ok=1;unsigned k,s;
 ++dc_level_ocean_metrics.frames;
 if(map && map!=rt->s7 && map>4u){
  start[0]=f32at(rt,map+12u,&ok);start[1]=f32at(rt,map+16u,&ok);start[2]=f32at(rt,map+20u,&ok);
  if((q=dc_goal_resolve(rt,map+48u,4)))memcpy(&colours,q,4);else ok=0;
  if(ok && colours && colours!=rt->s7)grid=dc_goal_resolve(rt,colours,4u*GRID*GRID_ROWS);
 }else ok=0;
 rt->error=prior;
 dc_level_ocean_metrics.map=map;
 if(!ok || !grid || !make_texture())return 1;
 for(k=0;k<3;++k){if(!(fabsf(start[k])<=1e9f))return 1;start[k]*=1.f/UNITS_PER_M;}
 dc_level_ocean_metrics.height=start[1];
 if(!have_cs){for(s=0;s<SECTORS;++s){float a=6.2831853f*(float)s/SECTORS;cs[s][0]=cosf(a);cs[s][1]=sinf(a);}have_cs=1;}
 dc_instance_view(cam,b);
 base_u=floorf(eye[0]/TILE_M)*TILE_M;base_v=floorf(eye[2]/TILE_M)*TILE_M;
 drift=(float)(dc_level_ocean_metrics.frames&1023u)*(1.f/1024.f);  /* a tile in 1024 frames, and no jump at the wrap */
 for(k=0;k<RINGS;++k)for(s=0;s<SECTORS;++s){
  float w[3]={eye[0]+radius[k]*cs[s][0],start[1],eye[2]+radius[k]*cs[s][1]},o[3];
  dc_instance_to_view(b,cam,w,o);
  v[k][s]=(dc_clip_vertex){o[0],o[1],o[2],(w[0]-base_u)*(1.f/TILE_M)+drift,(w[2]-base_v)*(1.f/TILE_M)+2.f*drift,
                           colour(grid,(w[0]-start[0])*(1.f/CELL_M),(w[2]-start[2])*(1.f/CELL_M))};
 }
 pvr_dr_init(&dr);
 {uint32_t *out=(uint32_t *)(void *)pvr_dr_target(dr);const uint32_t *in=(const uint32_t *)(const void *)&header;
  for(k=0;k<8;++k)out[k]=in[k];
  pvr_dr_commit(out);}
 for(k=0;k+1<RINGS;++k)for(s=0;s<SECTORS;++s){
  unsigned n=(s+1u)%SECTORS,t;
  dc_clip_vertex tri[2][3]={{v[k][s],v[k+1][s],v[k+1][n]},{v[k][s],v[k+1][n],v[k][n]}};
  for(t=0;t<(k?2u:1u);++t){
   dc_clip_vertex poly[DC_RENDER_CLIP_CAP];int count=dc_render_clip_viewport(tri[t],poly),fan,j;
   float px[DC_RENDER_CLIP_CAP],py[DC_RENDER_CLIP_CAP],pz[DC_RENDER_CLIP_CAP];
   ++dc_level_ocean_metrics.triangles;
   if(count<0)return 0;
   if(count<3)continue;
   for(j=0;j<count;++j){float inv=1.f/poly[j].z;px[j]=320.f+390.f*poly[j].x*inv;py[j]=260.f-390.f*poly[j].y*inv;pz[j]=inv;}
   for(fan=1;fan<count-1;++fan){
    int ids[3]={0,fan,fan+1};
    if((px[fan]-px[0])*(py[fan+1]-py[0])-(py[fan]-py[0])*(px[fan+1]-px[0])==0)continue;
    for(j=0;j<3;++j){int i=ids[j];pvr_vertex_t *p=pvr_dr_target(dr);
     p->flags=j==2?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX;p->x=px[i];p->y=py[i];p->z=pz[i];
     p->u=poly[i].u;p->v=poly[i].v;p->argb=poly[i].argb;p->oargb=0;pvr_dr_commit(p);}
    ++dc_level_ocean_metrics.submitted;
   }
  }
 }
 ++dc_level_ocean_metrics.drawn;
 return 1;
}
