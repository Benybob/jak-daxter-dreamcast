/* Real C lifecycle validation on host; target execution refused (AI-assisted), ISC. */
#include "goal_fiber.h"
#include "fiber_bounds_cases.h"
#include "goal_kernel_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned total,passed;
#define CHECK(x) do {++total;if(x)++passed;else fprintf(stderr,"FAIL%u line%u\n",total,(unsigned)__LINE__);}while(0)
static void put(dc_goal_runtime *rt,uint32_t p,unsigned off,uint32_t value){memcpy(dc_goal_resolve(rt,p+off,4),&value,4);}
static void boundary_check(uint64_t a,uint64_t b,unsigned id,const char *label,void *context){(void)id;(void)label;(void)context;CHECK(a==b);}
int main(void) {
 unsigned boundary_id=0;dc_fiber_bounds_cases(boundary_check,NULL,&boundary_id);
 const uint32_t capacity=512u*1024u;
 unsigned char *allocation=malloc(capacity+16),*memory,*before=malloc(capacity);
 dc_kernel_types_result installed;
 dc_goal_runtime rt;dc_goal_module_result module;dc_goal_process_heap heap;
 dc_goal_fiber fiber;
 uint32_t process,type,raw,thread,stack,entry,used;
 uint64_t value=0x1122334455667788ull;
 const unsigned char code[4]={0x0b,0,9,0};
 dc_goal_code_export f={"dc-fiber-host",code,4,NULL,0};
 CHECK(allocation && before);if(!allocation||!before){free(allocation);free(before);return 1;}
 memory=(unsigned char *)(((uintptr_t)allocation+15u)&~(uintptr_t)15u);
 CHECK(dc_goal_init(&rt,memory,capacity,0x04000000));
 CHECK(dc_goal_install_kernel_types(&rt,&installed));
 CHECK(dc_goal_load_module(&rt,&f,1,NULL,NULL,&module));entry=dc_goal_symbol_value(&rt,dc_goal_find(&rt,f.name));
 type=dc_goal_define_type(&rt,"cpu-thread",dc_goal_symbol_value(&rt,rt.s7+DC_FIX_SYM_THREAD_TYPE),128,0,12);
 process=dc_goal_new_basic(&rt,dc_goal_symbol_value(&rt,rt.s7+DC_FIX_SYM_PROCESS_TYPE),12400);
 CHECK(type && process);if(!type||!process){free(allocation);free(before);return 1;}put(&rt,process,68,12288);put(&rt,process,76,process+108);put(&rt,process,80,process+108+12288);put(&rt,process,84,process+108);
 CHECK(dc_goal_process_heap_open(&heap,&rt,process,12400));raw=dc_goal_process_alloc(&heap,8320);thread=raw+4;
 CHECK(raw!=0);put(&rt,raw,0,type);put(&rt,thread,4,process);put(&rt,thread,32,8192);put(&rt,process,44,thread);
 stack=dc_goal_alloc(&rt,16416)+16;put(&rt,thread,28,stack+16384);
 CHECK(dc_goal_fiber_init(&fiber,&rt,process,12400,thread,stack,16384,entry));CHECK(fiber.phase==DC_FIBER_NEW);
 CHECK(fiber.image.valid==1 && fiber.image.saved==0);CHECK(fiber.native_thread.sp==stack+16384);
 used=rt.used;memcpy(before,memory,used);
 CHECK(!dc_goal_fiber_step(&fiber,7,&value));CHECK(rt.error==DC_GOAL_BAD_ARGUMENT);CHECK(value==0x1122334455667788ull);
 CHECK(fiber.phase==DC_FIBER_NEW);CHECK(!dc_goal_fiber_current());
 CHECK(!dc_goal_fiber_step(&fiber,0,NULL));
 CHECK(dc_goal_fiber_yield(&rt,5)==0);CHECK(rt.error==DC_GOAL_BAD_ARGUMENT);
 CHECK(!dc_goal_fiber_init(NULL,&rt,process,12400,thread,stack,16384,entry));
 CHECK(!dc_goal_fiber_init(&fiber,NULL,process,12400,thread,stack,16384,entry));
 CHECK(fiber.phase==DC_FIBER_EMPTY);CHECK(!fiber.image.valid);
 CHECK(!dc_goal_fiber_init(&fiber,&rt,process,12400,thread,stack+1,16384,entry));CHECK(fiber.phase==DC_FIBER_EMPTY);
 CHECK(!dc_goal_fiber_init(&fiber,&rt,process,12400,thread,stack,4080,entry));
 CHECK(!dc_goal_fiber_init(&fiber,&rt,process,12400,thread,stack,16384,entry+2));
 CHECK(!dc_goal_fiber_init(&fiber,&rt,process,12400,thread,stack,16384,0));
 CHECK(!dc_goal_fiber_init(&fiber,&rt,process,12401,thread,stack,16384,entry));
 CHECK(!dc_goal_fiber_init((dc_goal_fiber *)memory,&rt,process,12400,thread,stack,16384,entry));
 CHECK(!dc_goal_fiber_init((dc_goal_fiber *)&rt,&rt,process,12400,thread,stack,16384,entry));
 CHECK(rt.memory==memory);
 CHECK(memcmp(before,memory,used)==0);CHECK(rt.used==used);
 CHECK(dc_goal_fiber_init(&fiber,&rt,process,12400,thread,stack,16384,entry));fiber.phase=DC_FIBER_DONE;
 CHECK(!dc_goal_fiber_step(&fiber,0,&value));CHECK(fiber.phase==DC_FIBER_DONE);
 CHECK(dc_goal_fiber_clear(&fiber));CHECK(fiber.phase==DC_FIBER_EMPTY && !fiber.image.valid);
 CHECK(!dc_goal_fiber_step(&fiber,0,&value));CHECK(!dc_goal_fiber_clear(NULL));CHECK(value==0x1122334455667788ull);
 free(allocation);free(before);printf("{\"tests\":%u,\"passed\":%u}\n",total,passed);return total!=passed;
}
