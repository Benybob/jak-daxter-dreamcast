/* Original-terrain bundle checks, shared by host and SH-4 (AI-assisted), ISC. */
#include "world_manifest.h"
#include <math.h>
#include <string.h>

static uint32_t u32(const unsigned char *p){
 return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static float f32(const unsigned char *p){
 uint32_t bits=u32(p);float f;memcpy(&f,&bits,4);return f;
}
static dc_vec3 vec(const unsigned char *p){return (dc_vec3){f32(p),f32(p+4),f32(p+8)};}
static int bounded(dc_vec3 v,float limit){
 return isfinite(v.x)&&isfinite(v.y)&&isfinite(v.z)&&
        fabsf(v.x)<=limit&&fabsf(v.y)<=limit&&fabsf(v.z)<=limit;
}
const char *dc_world_manifest_load(FILE *f,dc_world_manifest *out){
 unsigned char raw[84];dc_world_manifest m;
 if(!f||!out||sizeof(float)!=4)return "invalid world manifest arguments";
 if(fread(raw,1,sizeof raw,f)!=sizeof raw || fgetc(f)!=EOF || ferror(f))
  return "world manifest length or I/O error";
 if(memcmp(raw,"WLD2",4)||u32(raw+4)!=2)return "world manifest version";
 m.origin=vec(raw+8);m.shift=vec(raw+20);m.scale=f32(raw+32);m.spawn=vec(raw+36);
 m.dct_bytes=u32(raw+48);m.dct_crc=u32(raw+52);m.dcc_bytes=u32(raw+56);m.dcc_crc=u32(raw+60);
 m.vertices=u32(raw+64);m.indices=u32(raw+68);m.batches=u32(raw+72);
 m.textures=u32(raw+76);m.triangles=u32(raw+80);
 if(!bounded(m.origin,4096)||!bounded(m.shift,256)||!bounded(m.spawn,256)||
    !isfinite(m.scale)||m.scale<1e-6f||m.scale>256)return "world transform bounds";
 if(!m.vertices||m.vertices>8192||!m.indices||m.indices>24576||m.indices%3||
    !m.batches||m.batches>128||!m.textures||m.textures>64||
    !m.triangles||m.triangles>8192)return "world manifest counts";
 if(m.dcc_bytes!=24+m.triangles*40 || m.dct_bytes>1024*1024 ||
    m.dct_bytes<32+m.vertices*24+m.indices*2+m.batches*16+m.textures*140)
  return "world manifest file budgets";
 *out=m;return NULL;
}
const char *dc_world_manifest_check_file(FILE *f,uint32_t expected,uint32_t crc){
 unsigned char raw[512];uint32_t hash=UINT32_MAX,total=0;size_t n,i;unsigned bit;
 if(!f||!expected||expected>1024*1024||fseek(f,0,SEEK_SET))return "world file seek/budget";
 while((n=fread(raw,1,sizeof raw,f))!=0){
  if(n>expected-total)return "world file length mismatch";
  total+=(uint32_t)n;
  for(i=0;i<n;++i){
   hash^=raw[i];
   for(bit=0;bit<8;++bit)hash=(hash>>1)^((hash&1)?UINT32_C(0xedb88320):0);
  }
 }
 if(ferror(f)||total!=expected||(hash^UINT32_MAX)!=crc)return "world file checksum/length mismatch";
 if(fseek(f,0,SEEK_SET))return "world file rewind failed";
 return NULL;
}
const char *dc_world_manifest_match(const dc_world_manifest *m,const dc_textured_scene *s,
                                   const dc_collision_world *w,dc_vec3 origin){
 uint32_t events[2];
 if(!m||!s||!w)return "invalid world match arguments";
 if(origin.x!=m->origin.x||origin.y!=m->origin.y||origin.z!=m->origin.z||
    s->vertex_count!=m->vertices||s->index_count!=m->indices||s->batch_count!=m->batches||
    s->texture_count!=m->textures||s->texture_bytes>512*1024||w->count!=m->triangles)
  return "world transform/count mismatch";
 if(!dc_collision_surface_events(w,events)||events[1]||(events[0]&~UINT32_C(1)))
  return "unsupported world surface events";
 return NULL;
}
