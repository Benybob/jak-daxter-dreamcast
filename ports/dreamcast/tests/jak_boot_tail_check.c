/* Real allocator protects an overlapping staged image (AI-assisted), ISC. */
#include "arena_jak_memory.h"
#include "goal_runtime.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static unsigned char memory[13631488u+64] __attribute__((aligned(32)));
static unsigned char expected[1939456];
int main(void){
 dc_jak_memory_layout p;dc_goal_runtime rt;unsigned char *dest;uint32_t i,before;
 assert(!dc_jak_memory_plan(0,&p));assert(!dc_jak_memory_plan(2097153,&p));
 assert(dc_jak_memory_plan(sizeof expected,&p));
 assert(p.load_capacity==11692032 && p.borrowed==432064);
 dest=memory+p.boot_offset;
 for(i=0;i<sizeof expected;++i)expected[i]=(unsigned char)(i*37u+(i>>8));
 /* Destination starts within source: forward copying would corrupt this. */
 memcpy(memory+11000000,expected,sizeof expected);
 assert(dest>memory+11000000 && dest<memory+11000000+sizeof expected);
 memmove(dest,memory+11000000,sizeof expected);
 assert(!memcmp(dest,expected,sizeof expected));
 assert(dc_goal_init(&rt,memory+16,p.load_capacity,0x10000000));
 assert(dc_goal_alloc(&rt,p.load_capacity-rt.used));before=rt.used;
 assert(!dc_goal_alloc(&rt,16) && rt.error==DC_GOAL_OUT_OF_MEMORY && rt.used==before);
 assert(!memcmp(dest,expected,sizeof expected));
 /* After unmount, the borrowed tail can become normal GOAL memory. */
 rt.error=DC_GOAL_OK;rt.capacity=DC_JAK_ARENA_BYTES;
 assert(dc_goal_alloc(&rt,p.borrowed) && rt.used==rt.capacity);
 assert(!memcmp(memory+DC_JAK_ARENA_BYTES+32,expected+p.borrowed,sizeof expected-p.borrowed));
 puts("overlapping boot image intact through exact-capacity allocation/OOM; tail released without touching slab");
 return 0;
}
