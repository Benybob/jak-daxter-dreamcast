/* Host validation/rejection of the real stack-call C boundary (AI-assisted), ISC. */
#include "goal_stack_call.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned tests,passed;
#define CHECK(x) do {++tests;if(x)++passed;else fprintf(stderr,"FAIL%u line%u\n",tests,(unsigned)__LINE__);}while(0)
int main(void) {
 const uint32_t capacity=512u*1024u;
 unsigned char *allocation=malloc(capacity+16),*memory,*before=malloc(capacity);
 dc_goal_runtime rt;dc_goal_module_result result;
 const unsigned char body[4]={0x0b,0,9,0};
 dc_goal_code_export f={"dc-stack-host-entry",body,4,NULL,0};
 uint32_t entry,stack,used,count;
 uint64_t out=0x123456789abcdef0ull;
 CHECK(allocation && before);if(!allocation||!before){free(allocation);free(before);return 1;}
 memory=(unsigned char *)(((uintptr_t)allocation+15u)&~(uintptr_t)15u);
 CHECK(dc_goal_init(&rt,memory,capacity,0x04000000));
 CHECK(dc_goal_load_module(&rt,&f,1,NULL,NULL,&result));
 entry=dc_goal_symbol_value(&rt,dc_goal_find(&rt,f.name));stack=dc_goal_alloc(&rt,8192);
 CHECK(entry && stack);used=rt.used;count=rt.code_registry_count;memcpy(before,memory,used);
 CHECK(!dc_goal_call_on_stack(&rt,entry,stack,8192,&out));CHECK(rt.error==DC_GOAL_BAD_ARGUMENT);
 CHECK(out==0x123456789abcdef0ull);
 CHECK(!dc_goal_call_on_stack(NULL,entry,stack,8192,&out));
 CHECK(!dc_goal_call_on_stack(&rt,entry,stack,8192,NULL));
 CHECK(!dc_goal_call_on_stack(&rt,0,stack,8192,&out));CHECK(rt.error==DC_GOAL_BAD_FUNCTION);
 CHECK(!dc_goal_call_on_stack(&rt,entry+2,stack,8192,&out));
 CHECK(!dc_goal_call_on_stack(&rt,entry,stack,0,&out));
 CHECK(!dc_goal_call_on_stack(&rt,entry,stack,4080,&out));
 CHECK(!dc_goal_call_on_stack(&rt,entry,stack+1,8192,&out));
 CHECK(!dc_goal_call_on_stack(&rt,entry,stack,8191,&out));
 CHECK(!dc_goal_call_on_stack(&rt,entry,0xfffffff0u,8192,&out));
 CHECK(!dc_goal_call_on_stack(&rt,entry,rt.base+rt.used,8192,&out));CHECK(rt.error==DC_GOAL_BOUNDS);
 CHECK(!dc_goal_call_on_stack(&rt,entry,entry&~15u,4096,&out));
 CHECK(!dc_goal_call_on_stack(&rt,entry,stack,8192,dc_goal_resolve(&rt,stack+32,8)));
 CHECK(memcmp(before,memory,used)==0);CHECK(rt.used==used);CHECK(rt.code_registry_count==count);
 CHECK(out==0x123456789abcdef0ull);
 free(allocation);free(before);printf("{\"tests\":%u,\"passed\":%u}\n",tests,passed);return tests!=passed;
}
