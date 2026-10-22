/* Host execution of actual C entry registry (AI-assisted), ISC. */
#include <stdint.h>
#include <stdio.h>
#include "code_entry_cases.h"
static unsigned total,passed;
static void check(uint64_t actual,uint64_t expected,unsigned id,const char *label,void *context) {
 (void)context;++total;if(actual==expected)++passed;
 else fprintf(stderr,"FAIL%u %s actual=%llu expected=%llu\n",id,label,(unsigned long long)actual,(unsigned long long)expected);
}
int main(void) {unsigned id=0;dc_code_entry_cases(check,NULL,&id);printf("{\"tests\":%u,\"passed\":%u}\n",total,passed);return total!=passed;}
