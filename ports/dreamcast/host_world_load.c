/* Synthetic bounded collision loader oracle (AI-assisted), ISC. */
#include "world_collision.h"
#include <stdio.h>
#include <string.h>
static unsigned total,failed;
static const char *path;
#define CHECK(x) do{++total;if(!(x)){++failed;printf("world load failure line %u\n",(unsigned)__LINE__);}}while(0)
static void u32(unsigned char *p,uint32_t n){p[0]=(unsigned char)n;p[1]=(unsigned char)(n>>8);p[2]=(unsigned char)(n>>16);p[3]=(unsigned char)(n>>24);}
static void f32(unsigned char *p,float f){uint32_t n;memcpy(&n,&f,4);u32(p,n);}
static const char *load(const unsigned char *data,size_t size,dc_collision_world *w,dc_vec3 *origin){
 FILE *f=fopen(path,"w+b");const char *e;if(!f)return "fixture file failed";
 if(fwrite(data,1,size,f)!=size){fclose(f);return "fixture write failed";}
 rewind(f);e=dc_collision_load(f,w,origin);fclose(f);return e;
}
int main(int argc,char **argv){
 unsigned char data[65]={0},copy[65];dc_collision_world w={0},prior;dc_vec3 o={5,6,7},saved=o;unsigned i;
 const float positions[9]={-1,0,-1,-1,0,1,1,0,1};
 if(argc!=2)return 2;
 path=argv[1];
 memcpy(data,"DCC1",4);u32(data+4,1);u32(data+8,1);f32(data+12,-1000);f32(data+16,15);f32(data+20,900);
 for(i=0;i<9;++i)f32(data+24+i*4,positions[i]);
 u32(data+60,448);
 prior=w;
 for(i=0;i<64;++i){CHECK(load(data,i,&w,&o)!=NULL);CHECK(!memcmp(&w,&prior,sizeof w) && !memcmp(&o,&saved,sizeof o));}
 CHECK(load(data,65,&w,&o)!=NULL);CHECK(!memcmp(&w,&prior,sizeof w));
 for(i=0;i<7;++i){const unsigned offsets[7]={0,4,8,12,24,60,16};
  const uint32_t bad[7]={0,2,8193,0x7fc00001u,0x7f800000u,24,0x47800000u};
  memcpy(copy,data,sizeof copy);u32(copy+offsets[i],bad[i]);CHECK(load(copy,64,&w,&o)!=NULL);
  CHECK(!memcmp(&w,&prior,sizeof w) && !memcmp(&o,&saved,sizeof o));}
 CHECK(load(data,64,&w,&o)==NULL);CHECK(w.count==1 && o.x==-1000 && o.y==15 && o.z==900);
 dc_collision_free(&w);CHECK(!w.triangles);
 remove(path);printf("world load %u/%u checks\n",total-failed,total);return failed?1:0;
}
