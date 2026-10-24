/* Host execution of real C main-thread checker (AI-assisted), ISC. */
#include "main_thread_cases.h"
#include <stdio.h>
static unsigned passed;
static void check(uint64_t a,uint64_t b,unsigned id,const char *label,void *context){
 (void)context;if(a==b)++passed;else fprintf(stderr,"FAIL%u %s\n",id,label);
}
int main(void){unsigned total=0;dc_main_thread_cases(check,NULL,&total);
 printf("{\"tests\":%u,\"passed\":%u}\n",total,passed);return total!=passed;}
