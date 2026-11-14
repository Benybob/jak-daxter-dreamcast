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
/* Group 0 of the 2d system holds 30 blocks of 64 particles; updating them all
 * in the original code costs about 30 ms of a frame here (the shore's sparkles
 * alone are about 1,500). A capped system starves the effects that launch once
 * and live on (blue eco's tracker, which its sparks and puffs bind to), so the
 * port instead lets no launch group spawn past SPAWN_RADIUS meters from the
 * camera's target: what is near keeps its full effect, what is far has none,
 * as a group fades out past fade-after in the original. */
enum {SPAWN_RADIUS=40};
static float spawn_ref[3];static int spawn_have_ref;
volatile dc_level_sprite_metrics_t dc_level_sprite_metrics;

typedef struct {uint16_t page,index,w,h;pvr_ptr_t ptr;pvr_poly_hdr_t hdr[2] __attribute__((aligned(32)));} sprite_texture;
static sprite_texture textures[MAX_TEXTURES];
static unsigned texture_count;
static dc_goal_runtime *runtime;
static uint32_t system_symbol,adgif_base,slot_total,group0;
static pvr_dr_state_t dr;

static uint32_t rd32(uint32_t at){uint32_t v=0;const void *p=dc_goal_resolve(runtime,at,4);if(p)memcpy(&v,p,4);return v;}
static int locate_system(void){
  uint32_t sys;
  if(adgif_base)return 1;
  sys=system_symbol?dc_goal_symbol_value(runtime,system_symbol):0;
  if(!sys || sys==runtime->s7)return 0;
  adgif_base=rd32(sys+44u);group0=rd32(sys+8u);slot_total=group0+rd32(sys+12u);
  if(slot_total>SLOTS)slot_total=SLOTS;
  return adgif_base!=0;
}
typedef uint64_t (*one64)(uint64_t);
static one64 lookup_original;
static uint32_t texture_type,fake_texture[MAX_TEXTURES];
enum {MARKER=0x3000};  /* tex0 tbp0 of a texture this pass draws: MARKER + its number */
static sprite_texture *find_texture(uint32_t id);
/* lookup-texture-by-id: the port has no texture pages, so the original finds
 * nothing. For a texture SPRITES.DSK has it returns a stand-in texture whose
 * dest[0] is MARKER + the texture's number; adgif-shader<-texture! puts that in
 * the adgif's tex0, and every copy of the adgif (particle-adgif's cache) carries
 * it to the particle's slot. GOAL texture: w +0, h +2, num-mips +4, psm +6,
 * dest[0] +10, width[0] +26. */
static uint64_t lookup_native(uint64_t id){
  sprite_texture *t=runtime?find_texture((uint32_t)id):NULL;unsigned n;
  if(t){n=(unsigned)(t-textures);
    if(!fake_texture[n]){uint32_t base=dc_goal_alloc(runtime,80u);unsigned char *q=base?dc_goal_resolve(runtime,base,80u):NULL;
      if(q){uint16_t w=t->w,h=t->h,dest=(uint16_t)(MARKER+n);memset(q,0,80);memcpy(q,&texture_type,4);
        memcpy(q+4,&w,2);memcpy(q+6,&h,2);q[8]=1;memcpy(q+14,&dest,2);q[30]=1;fake_texture[n]=base+4u;}
      else runtime->error=DC_GOAL_OK;}
    if(fake_texture[n]){++dc_level_sprite_metrics.recorded;return fake_texture[n];}}
  ++dc_level_sprite_metrics.unknown_slot;
  return lookup_original(id);
}
static int install_spawn(dc_goal_runtime *rt,dc_goal_cache_sync sync);
int dc_level_sprites_install(dc_goal_runtime *rt,dc_goal_cache_sync sync){
  static unsigned char code[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
  dc_goal_code_export function={"dc-native-lookup-texture-by-id",code,12,NULL,0};
  dc_goal_module_result result;uint32_t target=(uint32_t)(uintptr_t)lookup_native,symbol,old,native;
  enum dc_goal_error prior=rt->error;
  runtime=rt;
  symbol=dc_goal_find(rt,"lookup-texture-by-id");old=symbol?dc_goal_symbol_value(rt,symbol):0;
  system_symbol=dc_goal_find(rt,"*sp-particle-system-2d*");
  texture_type=dc_goal_symbol_value(rt,dc_goal_find(rt,"texture"));
  if(rt->error!=DC_GOAL_OK || !old || !system_symbol || !texture_type){rt->error=prior;return 0;}
  lookup_original=(one64)(uintptr_t)old;
  memcpy(code+8,&target,4);
  if(!dc_goal_load_module(rt,&function,1,sync,NULL,&result))return 0;
  native=dc_goal_symbol_value(rt,dc_goal_find(rt,"dc-native-lookup-texture-by-id"));
  return native && dc_goal_set_symbol(rt,symbol,native) && install_spawn(rt,sync);
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
  spawn_ref[0]=cam->tx;spawn_ref[1]=cam->ty;spawn_ref[2]=cam->tz;spawn_have_ref=1;
  /* view units a meter: the camera target and a point a meter beside it */
  {float w0[3]={cam->tx,cam->ty,cam->tz},w1[3]={cam->tx+1.f,cam->ty,cam->tz};
   dc_instance_to_view(b,cam,w0,c0);dc_instance_to_view(b,cam,w1,c1);
   k=sqrtf((c1[0]-c0[0])*(c1[0]-c0[0])+(c1[1]-c0[1])*(c1[1]-c0[1])+(c1[2]-c0[2])*(c1[2]-c0[2]));}
  pvr_dr_init(&dr);
  memset((void *)dc_level_sprite_metrics.live,0,256);
  for(i=0;i<slot_total;++i){
    const unsigned char *cp=dc_goal_resolve(rt,cpu+CPUINFO*i+VALID,4),*vp;uint32_t valid,matrix,alpha;float s[12],w[3],o[3],half_x,half_y,cs,sn;
    sprite_texture *t;int k2;
    if(!cp)break;
    memcpy(&valid,cp,4);
    if(!valid || valid==rt->s7)continue;
    ++dc_level_sprite_metrics.valid;
    {const unsigned char *tp=dc_goal_resolve(rt,adg+ADGIF*i,4);uint32_t tex0=0,tbp;if(tp)memcpy(&tex0,tp,4);tbp=tex0&0x3fffu;
     if(tbp<MARKER || tbp-MARKER>=texture_count){++dc_level_sprite_metrics.no_texture;continue;}
     t=&textures[tbp-MARKER];if(tbp-MARKER<64u)++dc_level_sprite_metrics.live[tbp-MARKER];}
    if(!(vp=dc_goal_resolve(rt,vec+VECDATA*i,VECDATA)))break;
    memcpy(s,vp,48);memcpy(&matrix,vp+20,4);
    /* group 1 is the HUD's (screen space); a world sprite's matrix only turns its fog off */
    (void)matrix;if(i>=group0){++dc_level_sprite_metrics.screen;continue;}
    w[0]=s[0]/4096.f;w[1]=s[1]/4096.f;w[2]=s[2]/4096.f;
    dc_instance_to_view(b,cam,w,o);
    if(o[2]<DC_SCENE_NEAR*4.f){++dc_level_sprite_metrics.behind;continue;}
    half_x=0.5f*s[3]/4096.f*k;half_y=0.5f*s[7]/4096.f*k;
    {float r=s[6]*9.5873795e-5f;cs=cosf(r);sn=sinf(r);}  /* rot in 65,536ths of a turn (sprite.gc deg-to-rad) */
    {const unsigned char *ap=dc_goal_resolve(rt,adg+ADGIF*i+64u,4);alpha=0;if(ap)memcpy(&alpha,ap,4);}
    k2=((alpha>>2)&3u)==2u;  /* b = 0: additive */
    if(!drawn && (dc_level_sprite_metrics.frames&31u)==0u){memcpy((void *)dc_level_sprite_metrics.sample,s,48);dc_level_sprite_metrics.sample_texture=(uint32_t)(t-textures);
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
    ++drawn;if((unsigned)(t-textures)<64u)++dc_level_sprite_metrics.shown[t-textures];
  }
  dc_level_sprite_metrics.drawn+=drawn;dc_level_sprite_metrics.last_drawn=drawn;
  if(drawn>dc_level_sprite_metrics.max_drawn)dc_level_sprite_metrics.max_drawn=drawn;
  rt->error=prior;
  return 1;
}

/* sparticle-launch-control method 11, spawn (this, position): the original's
 * only when the position is within SPAWN_RADIUS of the camera's target. */
typedef uint64_t (*two64)(uint64_t,uint64_t);
static two64 spawn_original;
static uint64_t spawn_near(uint64_t self,uint64_t pos){
  const void *q=spawn_have_ref?dc_goal_resolve(runtime,(uint32_t)pos,12):NULL;
  if(q){float p[3],d=0,e;int k;memcpy(p,q,12);
    for(k=0;k<3;++k){e=p[k]/4096.f-spawn_ref[k];d+=e*e;}
    if(d>(float)(SPAWN_RADIUS*SPAWN_RADIUS)){++dc_level_sprite_metrics.culled;return 0;}}
  ++dc_level_sprite_metrics.spawned;
  return spawn_original(self,pos);
}
static int install_spawn(dc_goal_runtime *rt,dc_goal_cache_sync sync){
  static unsigned char code[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
  dc_goal_code_export function={"dc-native-spawn-near",code,12,NULL,0};
  dc_goal_module_result result;uint32_t target=(uint32_t)(uintptr_t)spawn_near,type,old,native;
  type=dc_goal_symbol_value(rt,dc_goal_find(rt,"sparticle-launch-control"));
  old=type?dc_goal_get_method(rt,type,11):0;
  if(rt->error!=DC_GOAL_OK || !old)return 0;
  spawn_original=(two64)(uintptr_t)old;
  memcpy(code+8,&target,4);
  if(!dc_goal_load_module(rt,&function,1,sync,NULL,&result))return 0;
  native=dc_goal_symbol_value(rt,dc_goal_find(rt,"dc-native-spawn-near"));
  return native && dc_goal_method_set(rt,type,11,native,1)==native && rt->error==DC_GOAL_OK;
}
