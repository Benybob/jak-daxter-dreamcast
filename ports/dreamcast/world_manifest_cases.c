/* Synthetic WLD2/CRC oracle shared with SH-4 (AI-assisted), ISC. */
#include "world_manifest.h"
#include <stdio.h>
#include <string.h>
static unsigned total,failed;
static const char *fixture;
#define CHECK(x) do{++total;if(!(x)){++failed;printf("manifest failure line %u\n",(unsigned)__LINE__);}}while(0)
static void put32(unsigned char *p,uint32_t v){unsigned i;for(i=0;i<4;++i)p[i]=(unsigned char)(v>>(i*8));}
static void putf(unsigned char *p,float v){uint32_t b;memcpy(&b,&v,4);put32(p,b);}
static FILE *file(const void *p,size_t n){
 FILE *f=fopen(fixture,"w+b");
 if(!f)return NULL;
 if(fwrite(p,1,n,f)!=n){fclose(f);return NULL;}
 rewind(f);return f;
}
static const char *load(const unsigned char *p,size_t n,dc_world_manifest *m){
 FILE *f=file(p,n);const char *error;if(!f)return "fixture failed";
 error=dc_world_manifest_load(f,m);fclose(f);return error;
}
unsigned dc_world_manifest_cases(const char *path,unsigned *count){
 unsigned char good[85]={0},bad[85];dc_world_manifest m,before;
 dc_collision_world w={0};dc_textured_scene s={0};dc_vec3 origin={10,2,-4};
 dc_world_triangle tri={{-1,0,-1},{-1,0,1},{1,0,1},0};
 uint32_t events[2]={123,456};FILE *f;unsigned i;
 total=failed=0;fixture=path;memcpy(good,"WLD2",4);put32(good+4,2);
 putf(good+8,10);putf(good+12,2);putf(good+16,-4);putf(good+32,2);
 put32(good+48,266);put32(good+56,64);put32(good+64,3);put32(good+68,3);
 put32(good+72,1);put32(good+76,1);put32(good+80,1);
 memset(&m,0x5a,sizeof m);before=m;
 for(i=0;i<84;++i){CHECK(load(good,i,&m)!=NULL);CHECK(!memcmp(&m,&before,sizeof m));}
 CHECK(load(good,85,&m)!=NULL);CHECK(!memcmp(&m,&before,sizeof m));
 for(i=0;i<20;++i){
  const unsigned offsets[20]={0,4,8,12,20,24,32,32,36,40,48,56,64,64,68,68,72,76,80,80};
  const uint32_t values[20]={0,1,0x7fc00001,0x7f800000,0x47800000,0xff800000,0,0x7f800000,
      0x7fc00000,0x47800000,0x100001,65,0,8193,0,4,129,65,0,8193};
  memcpy(bad,good,sizeof good);put32(bad+offsets[i],values[i]);
  CHECK(load(bad,84,&m)!=NULL);CHECK(!memcmp(&m,&before,sizeof m));
 }
 CHECK(load(good,84,&m)==NULL);CHECK(m.origin.x==10&&m.origin.y==2&&m.origin.z==-4&&m.scale==2);
 /* IEEE CRC32 check vector from the standard polynomial, independent of converter. */
 f=file("123456789",9);CHECK(f!=NULL);
 if(f){CHECK(dc_world_manifest_check_file(f,9,UINT32_C(0xcbf43926))==NULL);
  CHECK(ftell(f)==0&&fgetc(f)=='1');CHECK(dc_world_manifest_check_file(f,8,UINT32_C(0xcbf43926))!=NULL);
  CHECK(dc_world_manifest_check_file(f,10,UINT32_C(0xcbf43926))!=NULL);
  CHECK(dc_world_manifest_check_file(f,9,UINT32_C(0xcbf43927))!=NULL);fclose(f);}
 s.vertex_count=s.index_count=3;s.batch_count=s.texture_count=1;s.texture_bytes=128;
 CHECK(dc_collision_build(&w,&tri,1)==NULL);CHECK(dc_world_manifest_match(&m,&s,&w,origin)==NULL);
 CHECK(dc_collision_surface_events(&w,events)&&events[0]==1&&events[1]==0);
 origin.x+=1;CHECK(dc_world_manifest_match(&m,&s,&w,origin)!=NULL);origin.x-=1;
 ++s.vertex_count;CHECK(dc_world_manifest_match(&m,&s,&w,origin)!=NULL);--s.vertex_count;
 s.texture_bytes=512*1024+1;CHECK(dc_world_manifest_match(&m,&s,&w,origin)!=NULL);s.texture_bytes=128;
 dc_collision_free(&w);CHECK(!dc_collision_surface_events(&w,events)&&events[0]==1&&events[1]==0);
 for(i=1;i<=63;++i){
  tri.pat=i<<14;CHECK(dc_collision_build(&w,&tri,1)==NULL);
  CHECK(dc_collision_surface_events(&w,events)&&events[i>>5]==(UINT32_C(1)<<(i&31))&&events[1-(i>>5)]==0);
  CHECK(dc_world_manifest_match(&m,&s,&w,origin)!=NULL);dc_collision_free(&w);
 }
 tri.pat=(63u<<14)|1;CHECK(dc_collision_build(&w,&tri,1)==NULL);
 CHECK(dc_world_manifest_match(&m,&s,&w,origin)==NULL);dc_collision_free(&w);
 {unsigned char modern[97]={0};dc_world_manifest saved_m=m;
  memcpy(modern,good,84);memcpy(modern,"WLD3",4);put32(modern+4,3);
  putf(modern+84,18);putf(modern+88,12);putf(modern+92,18);
  for(i=84;i<96;++i){CHECK(load(modern,i,&m)!=NULL);CHECK(!memcmp(&m,&saved_m,sizeof m));}
  CHECK(load(modern,97,&m)!=NULL);CHECK(!memcmp(&m,&saved_m,sizeof m));
  for(i=0;i<5;++i){unsigned char invalid[96];const unsigned offsets[5]={84,88,92,84,88};
   const uint32_t values[5]={0,0x7fc00001,0x7f800000,0xc1800000,0x42820000};
   memcpy(invalid,modern,96);put32(invalid+offsets[i],values[i]);
   CHECK(load(invalid,96,&m)!=NULL);CHECK(!memcmp(&m,&saved_m,sizeof m));
  }
  CHECK(load(modern,96,&m)==NULL);CHECK(m.extent.x==18&&m.extent.y==12&&m.extent.z==18);
 }
 remove(path);*count=total;return failed;
}
