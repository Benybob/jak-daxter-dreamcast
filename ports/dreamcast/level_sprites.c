/* The level ELF's particles (AI-assisted), ISC. See level_sprites.h.
 * GOAL layout (retail offsets less the 4-byte type word): sparticle-system
 * blocks +0, length +8, cpuinfo-table +36, vecdata-table +40, adgifdata-table
 * +44; texture-id: index bits 8..19, page bits 20..31; gs-alpha a 0-1, b 2-3,
 * c 4-5, d 6-7. */
#include "level_sprites.h"
#include <kos.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "scene_projection.h"

enum {SLOTS=2048,MAX_TEXTURES=128,CPUINFO=144,VECDATA=48,ADGIF=80,VALID=128};
/* Group 0 of the 2d system holds 30 blocks of 64 particles; updating that many
 * in the original code costs about 60 ms of a frame here (the shore's sparkles
 * alone are about 1,500). The system is told it has CAP_BLOCKS: launches past
 * them find no free slot and are dropped, as when the PS2's system is full. */
enum {CAP_BLOCKS=4};
volatile dc_level_sprite_metrics_t dc_level_sprite_metrics;

typedef struct {uint16_t page,index,w,h;pvr_ptr_t ptr;pvr_poly_hdr_t hdr[2] __attribute__((aligned(32)));} sprite_texture;
static sprite_texture textures[MAX_TEXTURES];
static unsigned texture_count;
static uint32_t slot_texture[SLOTS];  /* texture-id per 2d slot, 0 none */
static dc_goal_runtime *runtime;
static uint32_t system_symbol,adgif_base,slot_total;
static pvr_dr_state_t dr;

typedef uint64_t (*two64)(uint64_t,uint64_t);
static two64 particle_adgif_original;

static uint32_t rd32(uint32_t at){uint32_t v=0;const void *p=dc_goal_resolve(runtime,at,4);if(p)memcpy(&v,p,4);return v;}
static int locate_system(void){
  uint32_t sys;
  if(adgif_base)return 1;
  sys=system_symbol?dc_goal_symbol_value(runtime,system_symbol):0;
  if(!sys || sys==runtime->s7)return 0;
  {unsigned char *blocks=dc_goal_resolve(runtime,sys,4);int32_t b0;
   if(blocks){memcpy(&b0,blocks,4);if(b0>CAP_BLOCKS){b0=CAP_BLOCKS;memcpy(blocks,&b0,4);}}}
  adgif_base=rd32(sys+44u);slot_total=rd32(sys+8u)+rd32(sys+12u);
  if(slot_total>SLOTS)slot_total=SLOTS;
  return adgif_base!=0;
}
/* particle-adgif (adgif texture-id): note the id, then the original. */
static uint64_t particle_adgif_native(uint64_t adgif,uint64_t id){
  enum dc_goal_error prior=runtime->error;
  if(locate_system()){
    uint32_t a=(uint32_t)adgif;
    if(a>=adgif_base && a<adgif_base+slot_total*ADGIF && !((a-adgif_base)%ADGIF)){
      slot_texture[(a-adgif_base)/ADGIF]=(uint32_t)id;++dc_level_sprite_metrics.recorded;}
    else ++dc_level_sprite_metrics.unknown_slot;
  }
  runtime->error=prior;
  return particle_adgif_original(adgif,id);
}
int dc_level_sprites_install(dc_goal_runtime *rt,dc_goal_cache_sync sync){
  static unsigned char code[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
  dc_goal_code_export function={"dc-native-particle-adgif",code,12,NULL,0};
  dc_goal_module_result result;uint32_t target=(uint32_t)(uintptr_t)particle_adgif_native,symbol,old,native;
  enum dc_goal_error prior=rt->error;
  runtime=rt;
  symbol=dc_goal_find(rt,"particle-adgif");old=symbol?dc_goal_symbol_value(rt,symbol):0;
  system_symbol=dc_goal_find(rt,"*sp-particle-system-2d*");
  if(rt->error!=DC_GOAL_OK || !old || !system_symbol){rt->error=prior;return 0;}
  particle_adgif_original=(two64)(uintptr_t)old;
  memcpy(code+8,&target,4);
  if(!dc_goal_load_module(rt,&function,1,sync,NULL,&result))return 0;
  native=dc_goal_symbol_value(rt,dc_goal_find(rt,"dc-native-particle-adgif"));
  return native && dc_goal_set_symbol(rt,symbol,native);
}

/* additive (Cs-0)*As+Cd, or blended (Cs-Cd)*As+Cd */
static void compile(sprite_texture *t){
  unsigned k;
  for(k=0;k<2;++k){pvr_poly_cxt_t cxt;
    pvr_poly_cxt_txr(&cxt,PVR_LIST_TR_POLY,PVR_TXRFMT_ARGB4444|PVR_TXRFMT_TWIDDLED,t->w,t->h,t->ptr,PVR_FILTER_BILINEAR);
    cxt.gen.culling=PVR_CULLING_NONE;cxt.depth.write=PVR_DEPTHWRITE_DISABLE;cxt.depth.comparison=PVR_DEPTHCMP_GEQUAL;
    cxt.txr.env=PVR_TXRENV_MODULATEALPHA;cxt.txr.uv_clamp=PVR_UVCLAMP_UV;
    cxt.blend.src=PVR_BLEND_SRCALPHA;cxt.blend.dst=k?PVR_BLEND_ONE:PVR_BLEND_INVSRCALPHA;
    pvr_poly_compile(&t->hdr[k],&cxt);++dc_level_sprite_metrics.headers;}
}
int dc_level_sprites_open(const char *path){
  FILE *f=fopen(path,"rb");unsigned char head[8];uint32_t count,i;int ok=1;
  if(!f){++dc_level_sprite_metrics.load_failures;return 0;}
  if(fread(head,1,8,f)!=8 || memcmp(head,"SPR1",4)){fclose(f);++dc_level_sprite_metrics.load_failures;return 0;}
  memcpy(&count,head+4,4);
  if(count>MAX_TEXTURES)count=MAX_TEXTURES;
  for(i=0;i<count && ok;++i){
    unsigned char e[16];uint32_t offset,bytes;void *buffer;long back;sprite_texture *t=&textures[texture_count];
    if(fseek(f,8L+16L*i,SEEK_SET) || fread(e,1,16,f)!=16){ok=0;break;}
    memcpy(&t->page,e,2);memcpy(&t->index,e+2,2);memcpy(&t->w,e+4,2);memcpy(&t->h,e+6,2);memcpy(&offset,e+8,4);memcpy(&bytes,e+12,4);
    if(t->w<8 || t->h<8 || t->w>1024 || t->h>1024 || bytes!=(uint32_t)t->w*t->h*2u){ok=0;break;}
    back=ftell(f);(void)back;
    if(!(buffer=malloc(bytes)) || fseek(f,(long)offset,SEEK_SET) || fread(buffer,1,bytes,f)!=bytes){free(buffer);ok=0;break;}
    if(!(t->ptr=pvr_mem_malloc(bytes))){free(buffer);ok=0;break;}
    pvr_txr_load(buffer,t->ptr,bytes);free(buffer);
    compile(t);++texture_count;dc_level_sprite_metrics.texture_bytes+=bytes;
  }
  fclose(f);
  dc_level_sprite_metrics.textures=texture_count;
  if(!ok)++dc_level_sprite_metrics.load_failures;
  return ok;
}
static sprite_texture *find_texture(uint32_t id){
  uint32_t index=(id>>8)&0xfffu,page=(id>>20)&0xfffu;unsigned i;
  for(i=0;i<texture_count;++i)if(textures[i].page==page && textures[i].index==index)return &textures[i];
  return NULL;
}
static void emit(const void *header){
  uint32_t *out=(uint32_t *)(void *)pvr_dr_target(dr);const uint32_t *in=header;unsigned i;
  for(i=0;i<8;++i)out[i]=in[i];
  pvr_dr_commit(out);
}
int dc_level_sprites_draw(dc_goal_runtime *rt,const dc_instance_camera *cam){
  enum dc_goal_error prior=rt->error;float b[10],c0[3],c1[3],k;uint32_t sys,cpu,vec,adg,i,drawn=0;const void *current=NULL;
  ++dc_level_sprite_metrics.frames;
  if(!texture_count || rt!=runtime || !locate_system())return 1;
  sys=dc_goal_symbol_value(rt,system_symbol);cpu=rd32(sys+36u);vec=rd32(sys+40u);adg=adgif_base;
  if(!cpu || !vec){rt->error=prior;return 1;}
  dc_instance_view(cam,b);
  /* view units a meter: the camera target and a point a meter beside it */
  {float w0[3]={cam->tx,cam->ty,cam->tz},w1[3]={cam->tx+1.f,cam->ty,cam->tz};
   dc_instance_to_view(b,cam,w0,c0);dc_instance_to_view(b,cam,w1,c1);
   k=sqrtf((c1[0]-c0[0])*(c1[0]-c0[0])+(c1[1]-c0[1])*(c1[1]-c0[1])+(c1[2]-c0[2])*(c1[2]-c0[2]));}
  pvr_dr_init(&dr);
  for(i=0;i<slot_total;++i){
    const unsigned char *cp=dc_goal_resolve(rt,cpu+CPUINFO*i+VALID,4),*vp;uint32_t valid,matrix,alpha;float s[12],w[3],o[3],half_x,half_y,cs,sn;
    sprite_texture *t;int k2;
    if(!cp)break;
    memcpy(&valid,cp,4);
    if(!valid || valid==rt->s7)continue;
    ++dc_level_sprite_metrics.valid;
    if(!slot_texture[i] || !(t=find_texture(slot_texture[i]))){++dc_level_sprite_metrics.no_texture;continue;}
    if(!(vp=dc_goal_resolve(rt,vec+VECDATA*i,VECDATA)))break;
    memcpy(s,vp,48);memcpy(&matrix,vp+20,4);
    if(matrix){++dc_level_sprite_metrics.screen;continue;}
    w[0]=s[0]/4096.f;w[1]=s[1]/4096.f;w[2]=s[2]/4096.f;
    dc_instance_to_view(b,cam,w,o);
    if(o[2]<DC_SCENE_NEAR*4.f){++dc_level_sprite_metrics.behind;continue;}
    half_x=0.5f*s[3]/4096.f*k;half_y=0.5f*s[7]/4096.f*k;
    {float r=s[6]*9.5873795e-5f;cs=cosf(r);sn=sinf(r);}  /* rot in 65,536ths of a turn (sprite.gc deg-to-rad) */
    {const unsigned char *ap=dc_goal_resolve(rt,adg+ADGIF*i+64u,4);alpha=0;if(ap)memcpy(&alpha,ap,4);}
    k2=((alpha>>2)&3u)==2u;  /* b = 0: additive */
    if(!drawn && (dc_level_sprite_metrics.frames&31u)==0u){memcpy((void *)dc_level_sprite_metrics.sample,s,48);dc_level_sprite_metrics.sample_texture=slot_texture[i];
      dc_level_sprite_metrics.sample_alpha=alpha;memcpy((void *)dc_level_sprite_metrics.sample_view,o,12);}
    {const void *h=&t->hdr[k2];if(h!=current){emit(h);current=h;}}
    {float r=s[8]*2.f,g=s[9]*2.f,bl=s[10]*2.f,a=s[11]*2.f,inv=1.f/o[2];uint32_t argb;int j;  /* 128 full, as the GS */
     static const float cx[4]={-1,1,-1,1},cy[4]={1,1,-1,-1},u[4]={0,1,0,1},v[4]={0,0,1,1};
     if(r>255.f){r=255.f;}
     if(g>255.f){g=255.f;}
     if(bl>255.f){bl=255.f;}
     if(a>255.f){a=255.f;}
     if(r<0){r=0;}
     if(g<0){g=0;}
     if(bl<0){bl=0;}
     if(a<0){a=0;}
     argb=(uint32_t)a<<24|(uint32_t)r<<16|(uint32_t)g<<8|(uint32_t)bl;
     for(j=0;j<4;++j){pvr_vertex_t *p=pvr_dr_target(dr);
       float dx=cx[j]*half_x,dy=cy[j]*half_y,vx=o[0]+dx*cs-dy*sn,vy=o[1]+dx*sn+dy*cs;
       p->flags=j==3?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX;p->x=320.f+390.f*vx*inv;p->y=260.f-390.f*vy*inv;p->z=inv;
       p->u=u[j];p->v=v[j];p->argb=argb;p->oargb=0;pvr_dr_commit(p);}}
    ++drawn;
  }
  dc_level_sprite_metrics.drawn+=drawn;dc_level_sprite_metrics.last_drawn=drawn;
  if(drawn>dc_level_sprite_metrics.max_drawn)dc_level_sprite_metrics.max_drawn=drawn;
  rt->error=prior;
  return 1;
}
