/* Synthetic DCA1 loader and interpolation oracle (AI-assisted), ISC. */
#include "player_anim.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static unsigned total,failed;
static const char *fixture_file;
#define CHECK(x) do{++total;if(!(x)){++failed;printf("animation failure line %u\n",(unsigned)__LINE__);}}while(0)
static void put32(unsigned char *p,uint32_t n){p[0]=(unsigned char)n;p[1]=(unsigned char)(n>>8);p[2]=(unsigned char)(n>>16);p[3]=(unsigned char)(n>>24);}
static const char *load(const unsigned char *bytes,size_t n,dc_player_anim *a){
 FILE *f=fopen(fixture_file,"w+b");const char *e;if(!f)return "fixture open failed";
 if(fwrite(bytes,1,n,f)!=n){fclose(f);return "write failed";}
 rewind(f);e=dc_player_anim_load(f,1,a);fclose(f);return e;
}
int main(int argc,char **argv){
 unsigned char data[189]={0},copy[189];unsigned i;dc_player_anim a;float v[3];
 const char *error;
 if(argc!=2)return 2;
 fixture_file=argv[1];
 setvbuf(stdout,NULL,_IONBF,0);
 memcpy(data,"DCA1",4);put32(data+4,1);put32(data+8,1);put32(data+12,6);put32(data+16,12);
 for(i=0;i<6;++i){put32(data+20+i*16,i*2);put32(data+24+i*16,2);put32(data+28+i*16,0x3f800000u);put32(data+32+i*16,i==3?0:1);}
 /* Every clip is (-1,0,1) -> (1,2,-1), Q12. */
 for(i=0;i<6;++i){unsigned j;const int16_t values[6]={-4096,0,4096,4096,8192,-4096};
  for(j=0;j<6;++j){uint16_t n=(uint16_t)values[j];data[116+i*12+j*2]=(unsigned char)n;data[117+i*12+j*2]=(unsigned char)(n>>8);}}
 error=load(data,188,&a);CHECK(error==NULL);if(error){printf("%s\n",error);return 1;}
 CHECK(a.frames==12 && a.vertices==1);
 dc_player_anim_sample(&a,0,0,v);CHECK(v[0]==-1 && v[1]==0 && v[2]==1);
 dc_player_anim_sample(&a,0,.5f,v);CHECK(v[0]==0 && v[1]==1 && v[2]==0);
 dc_player_anim_sample(&a,0,1.5f,v);CHECK(v[0]==0 && v[1]==1 && v[2]==0);
 dc_player_anim_sample(&a,3,2,v);CHECK(v[0]==1 && v[1]==2 && v[2]==-1);
 dc_player_anim_sample(&a,99,NAN,v);CHECK(v[0]==-1 && v[1]==0 && v[2]==1);
 dc_player_anim_free(&a);CHECK(!a.poses && !a.frames);
 for(i=0;i<188;++i){CHECK(load(data,i,&a)!=NULL);dc_player_anim_free(&a);}
 CHECK(load(data,189,&a)!=NULL);CHECK(!a.poses);
 for(i=0;i<8;++i){
  const unsigned offsets[8]={0,4,8,12,16,20,24,28};
  const uint32_t bad[8]={0,2,2,7,257,1,0,0x7fc00001u};
  memcpy(copy,data,sizeof copy);put32(copy+offsets[i],bad[i]);CHECK(load(copy,188,&a)!=NULL);dc_player_anim_free(&a);
 }
 CHECK(dc_player_anim_load(NULL,1,&a)!=NULL);CHECK(dc_player_anim_load(NULL,1,NULL)!=NULL);
 remove(fixture_file);
 printf("player animation %u/%u checks\n",total-failed,total);return failed?1:0;
}
