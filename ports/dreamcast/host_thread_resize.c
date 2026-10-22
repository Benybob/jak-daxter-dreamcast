/* Real read-only resize-preflight checker, no SH4 execution (AI-assisted), ISC. */
#include "thread_resize_cases.h"
#include <stdio.h>
static unsigned total,passed;
static void check(uint64_t a,uint64_t b,unsigned id,const char *label,void *context){(void)context;++total;if(a==b)++passed;else fprintf(stderr,"FAIL%u %s actual%llu expected%llu\n",id,label,(unsigned long long)a,(unsigned long long)b);}
int main(void){unsigned id=0;dc_thread_resize_cases(check,NULL,&id);printf("{\"tests\":%u,\"passed\":%u}\n",total,passed);return total!=passed;}
