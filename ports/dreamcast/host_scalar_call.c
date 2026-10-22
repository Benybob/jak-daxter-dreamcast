/* Execute the actual private adapter on the host C ABI (AI-assisted), ISC. */
#include "scalar_call_cases.h"
#include <stdio.h>
static unsigned total,passed;
static void check(uint64_t actual,uint64_t expected,unsigned id,const char *label,void *context){
 (void)context;++total;if(actual==expected)++passed;else fprintf(stderr,"FAIL%u %s\n",id,label);
}
int main(void){unsigned id=0;dc_scalar_call_cases(check,NULL,&id);printf("{\"tests\":%u,\"passed\":%u}\n",total,passed);return total!=passed;}
