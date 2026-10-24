/* Little-endian DCA1 loader for the native movement prototype (AI-assisted), ISC. */
#include "player_anim.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static uint32_t u32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
void dc_player_anim_free(dc_player_anim *a){if(a){free(a->poses);memset(a,0,sizeof(*a));}}
const char *dc_player_anim_load(FILE *f,uint32_t vertices,dc_player_anim *a){
 unsigned char h[20],r[16],*raw;uint32_t i,next=0,bytes;
 if(!a)return "missing player animation output";
 memset(a,0,sizeof(*a));
 if(!f)return "missing player animation file";
 if(fread(h,1,20,f)!=20 || memcmp(h,"DCA1",4) || u32(h+4)!=1 ||
    u32(h+8)!=vertices || vertices<1 || vertices>8192 || u32(h+12)!=6 ||
    u32(h+16)<12 || u32(h+16)>256)return "invalid player animation header";
 a->vertices=vertices;a->frames=u32(h+16);
 for(i=0;i<6;++i){dc_player_clip *c=&a->clips[i];uint32_t bits;
  if(fread(r,1,16,f)!=16)return "short player clip table";
  c->first=u32(r);c->count=u32(r+4);bits=u32(r+8);memcpy(&c->duration,&bits,4);c->loop=u32(r+12);
  if(c->first!=next || c->count<2 || c->count>a->frames-next ||
     !isfinite(c->duration) || c->duration<=0 || c->duration>5 || c->loop>1)return "invalid player clip";
  next+=c->count;
 }
 if(next!=a->frames)return "incomplete player clip coverage";
 bytes=vertices*a->frames*6;
 if(bytes>3u*1024u*1024u)return "player pose byte budget exceeded";
 a->poses=malloc(bytes);if(!a->poses)return "player pose allocation failed";
 raw=(unsigned char *)a->poses;
 if(fread(raw,1,bytes,f)!=bytes || fgetc(f)!=EOF){dc_player_anim_free(a);return "invalid player pose payload";}
 /* Both supported targets (MinGW x86 and Dreamcast SH4) are little-endian.
  * Decode explicitly, including negative values, for portability elsewhere. */
 for(i=0;i<bytes/2;++i){uint32_t v=(uint32_t)raw[i*2]|(uint32_t)raw[i*2+1]<<8;int value=v<32768?(int)v:(int)v-65536;a->poses[i]=(int16_t)value;}
 return NULL;
}
void dc_player_anim_sample(const dc_player_anim *a,unsigned clip,float time,float *out){
 const dc_player_clip *c;uint32_t first,second,i;float phase,f;
 if(clip>=6)clip=0;
 c=&a->clips[clip];
 if(!isfinite(time) || time<0)time=0;
 phase=c->loop?fmodf(time,c->duration):fminf(time,c->duration);
 phase=phase/c->duration*(float)(c->count-1);first=(uint32_t)phase;
 if(first>=c->count-1){first=c->count-1;second=first;f=0;}else{second=first+1;f=phase-first;}
 first=(first+c->first)*a->vertices*3;second=(second+c->first)*a->vertices*3;
 for(i=0;i<a->vertices*3;++i)out[i]=((float)a->poses[first+i]*(1-f)+(float)a->poses[second+i]*f)/4096.0f;
}
