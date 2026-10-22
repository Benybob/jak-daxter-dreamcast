/* Independent scalar-call ABI proposal check (AI-assisted), ISC. */
#include "goal_scalar_call.h"
#include "scalar_call_cases.h"
static unsigned calls;
#define C(x) check((uint64_t)!!(x),1,++*id,"scalar ABI argument/result delivery",context)
static uint64_t fold(const uint64_t *v,unsigned n){uint64_t r=0xabcdef0123456789ull;unsigned i;++calls;for(i=0;i<n;++i)r^=(v[i]*(2*i+3))+(r<<7)+(r>>3);return r;}
static uint64_t f0(void){return fold(0,0);}
static uint64_t f1(uint64_t a){const uint64_t v[]={a};return fold(v,1);}
static uint64_t f2(uint64_t a,uint64_t b){const uint64_t v[]={a,b};return fold(v,2);}
static uint64_t f3(uint64_t a,uint64_t b,uint64_t c){const uint64_t v[]={a,b,c};return fold(v,3);}
static uint64_t f4(uint64_t a,uint64_t b,uint64_t c,uint64_t d){const uint64_t v[]={a,b,c,d};return fold(v,4);}
static uint64_t f5(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e){const uint64_t v[]={a,b,c,d,e};return fold(v,5);}
static uint64_t f6(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){const uint64_t v[]={a,b,c,d,e,f};return fold(v,6);}
static uint64_t f7(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g){const uint64_t v[]={a,b,c,d,e,f,g};return fold(v,7);}
static uint64_t f8(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g,uint64_t h){const uint64_t v[]={a,b,c,d,e,f,g,h};return fold(v,8);}
void dc_scalar_call_cases(dc_runtime_check check,void *context,unsigned *id){
 uintptr_t entries[]={(uintptr_t)f0,(uintptr_t)f1,(uintptr_t)f2,(uintptr_t)f3,(uintptr_t)f4,(uintptr_t)f5,(uintptr_t)f6,(uintptr_t)f7,(uintptr_t)f8};
 unsigned n,k,j,before;uint64_t args[8],out,want,r;
 for(n=0;n<9;++n)for(k=0;k<16;++k){
  r=0xabcdef0123456789ull;
  for(j=0;j<8;++j){args[j]=(UINT64_C(0xfedc12345678abcd)^(UINT64_C(0x1020304050607081)*(j+1)))+UINT64_C(0x9988776655443322)*k;if(j<n)r^=(args[j]*(2*j+3))+(r<<7)+(r>>3);}
  want=r;out=0;before=calls;
  C(dc_goal_scalar_call(entries[n],n,n?args:0,&out)==1);C(calls==before+1);C(out==want);
 }
 out=0x1122334455667788ull;before=calls;
 C(!dc_goal_scalar_call(0,0,0,&out));C(!dc_goal_scalar_call(entries[0],9,args,&out));
 C(!dc_goal_scalar_call(entries[1],1,0,&out));C(!dc_goal_scalar_call(entries[0],0,0,0));
 C(out==0x1122334455667788ull);C(calls==before);

}
