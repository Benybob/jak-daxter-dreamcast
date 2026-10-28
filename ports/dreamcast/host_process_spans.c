/* Independent ownership/lifetime oracles; no native execution (AI-assisted), ISC. */
#include "process_spans.h"
#include "goal_fiber.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static dc_goal_fiber *current;
dc_goal_fiber *dc_goal_fiber_current(void){return current;}
static uint32_t owner_id=77;
static int quiet=1;
static uint32_t owner(void){return owner_id;}
static int quiescent(const dc_goal_runtime *r){return r && quiet && !current;}
static unsigned passed,total;
#define CHECK(x) do{++total;if(x)++passed;else fprintf(stderr,"FAIL %u line %u\n",total,(unsigned)__LINE__);}while(0)
typedef struct {
 dc_goal_runtime r;dc_goal_process_spans table;dc_goal_process_span rows[4];
 void *memory;uint32_t raw,p[4];dc_goal_process_span_hooks hooks;
} fixture;
static int setup(fixture *f){
 unsigned i;memset(f,0,sizeof *f);f->memory=malloc(1048576);
 if(!f->memory||!dc_goal_init(&f->r,f->memory,1048576,0x01000000))return 0;
 f->raw=dc_goal_alloc(&f->r,1024);if(!f->raw)return 0;
 for(i=0;i<4;++i)f->p[i]=f->raw+i*256+4;
 f->hooks.owner=owner;f->hooks.quiescent=quiescent;return 1;
}
static int init(fixture *f){return dc_goal_process_spans_init(&f->table,&f->r,f->rows,4,&f->hooks);}
static void sorted(fixture *f){
 dc_goal_process_span a,b;unsigned char before[1024];uint32_t used=f->r.used;unsigned i;
 memcpy(before,dc_goal_resolve(&f->r,f->raw,1024),1024);
 CHECK(dc_goal_process_spans_registered(&f->r)==0);CHECK(init(f));
 CHECK(dc_goal_process_span_note(&f->r,f->p[2],240,&a));CHECK(a.bytes==240&&a.generation!=0);
 CHECK(dc_goal_process_span_note(&f->r,f->p[0],112,NULL));
 CHECK(dc_goal_process_span_note(&f->r,f->p[1],128,NULL));
 CHECK(f->table.count==3&&f->rows[0].process==f->p[0]&&f->rows[1].process==f->p[1]&&f->rows[2].process==f->p[2]);
 CHECK(dc_goal_process_span_note(&f->r,f->p[2],240,&b));CHECK(memcmp(&a,&b,sizeof a)==0&&f->table.count==3);
 CHECK(dc_goal_process_span_lookup(&f->r,f->p[2],&b)&&b.bytes==240&&b.generation==a.generation);
 CHECK(f->r.used==used&&memcmp(before,dc_goal_resolve(&f->r,f->raw,1024),1024)==0);
 /* Ownership must survive arbitrary changes to guest type/PID/heap metadata. */
 memset(dc_goal_resolve(&f->r,f->p[2]-4,240),0xa5,240);
 CHECK(dc_goal_process_span_lookup(&f->r,f->p[2],&b)&&b.bytes==240&&b.generation==a.generation);
 CHECK(dc_goal_process_span_check(&f->r,&a));
 for(i=0;i<6;++i){
  uint32_t p=i==0?f->p[2]:i==1?f->p[1]+16:i==2?f->p[3]+1:i==3?f->p[3]:i==4?f->r.base-12:f->r.base+f->r.used+4;
  uint32_t bytes=i==0?241:i==1?240:i==3?111:112;
  f->r.error=DC_GOAL_OK;b=a;
  CHECK(!dc_goal_process_span_note(&f->r,p,bytes,&b));
  CHECK(f->r.error==DC_GOAL_BAD_ARGUMENT&&f->table.count==3&&memcmp(&a,&b,sizeof a)==0);
 }
 CHECK(!dc_goal_process_span_lookup(&f->r,f->p[3],&b));CHECK(memcmp(&a,&b,sizeof a)==0);
 CHECK(dc_goal_process_spans_detach(&f->r));
}
static void lifetime(fixture *f){
 dc_goal_process_span a,b,old;unsigned i;CHECK(init(f));
 for(i=0;i<4;++i)CHECK(dc_goal_process_span_note(&f->r,f->p[i],240,NULL));
 CHECK(dc_goal_process_span_lookup(&f->r,f->p[0],&a));old=a;
 CHECK(dc_goal_process_span_room(&f->r)==-1&&f->r.error==DC_GOAL_OUT_OF_MEMORY);
 CHECK(dc_goal_process_span_note(&f->r,f->p[0],240,&b)&&b.generation==a.generation);
 quiet=0;f->r.error=DC_GOAL_DIVIDE_BY_ZERO;
 CHECK(!dc_goal_process_span_retire(&f->r,&a));CHECK(!dc_goal_process_spans_detach(&f->r));
 CHECK(f->table.count==4&&f->r.error==DC_GOAL_DIVIDE_BY_ZERO);
 quiet=1;f->r.error=DC_GOAL_OK;b=a;++b.generation;
 CHECK(!dc_goal_process_span_retire(&f->r,&b)&&f->table.count==4);
 f->r.error=DC_GOAL_OK;CHECK(dc_goal_process_span_retire(&f->r,&a));CHECK(f->table.count==3);
 CHECK(dc_goal_process_span_note(&f->r,f->p[0],240,&b)&&b.generation!=old.generation);
 CHECK(!dc_goal_process_span_check(&f->r,&old));CHECK(!dc_goal_process_span_retire(&f->r,&old));
 CHECK(dc_goal_process_span_check(&f->r,&b));CHECK(dc_goal_process_spans_detach(&f->r));
 CHECK(f->table.magic==0&&f->rows[0].generation==0);CHECK(init(f));
 CHECK(dc_goal_process_span_note(&f->r,f->p[0],240,&a)&&a.generation!=b.generation);
 CHECK(!dc_goal_process_span_check(&f->r,&b));CHECK(dc_goal_process_spans_detach(&f->r));
}
static void metadata(fixture *f){
 dc_goal_runtime foreign=f->r;dc_goal_process_spans saved;dc_goal_process_span out;unsigned i;
 CHECK(!dc_goal_process_spans_init(&f->table,&f->r,f->rows,0,&f->hooks));
 CHECK(!dc_goal_process_spans_init((dc_goal_process_spans*)f->memory,&f->r,f->rows,4,&f->hooks));
 CHECK(!dc_goal_process_spans_init(&f->table,&f->r,(dc_goal_process_span*)&f->table,1,&f->hooks));
 f->table.hooks=f->hooks;CHECK(dc_goal_process_spans_init(&f->table,&f->r,f->rows,4,&f->table.hooks));
 CHECK(f->table.hooks.owner==owner);CHECK(dc_goal_process_span_note(&f->r,f->p[0],240,NULL));
 foreign.memory=(unsigned char*)&f->table;foreign.capacity=sizeof f->table;
 CHECK(dc_goal_process_spans_registered(&foreign)==0);CHECK(dc_goal_process_spans_detach(&foreign));
 CHECK(dc_goal_process_span_lookup(&f->r,f->p[0],&out));
 saved=f->table;
 for(i=0;i<7;++i){
  f->table=saved;
  if(i==0)f->table.runtime=&foreign;else if(i==1)f->table.rows=NULL;
  else if(i==2)f->table.capacity=0;else if(i==3)f->table.count=5;
  else if(i==4)f->table.hooks.owner=NULL;else if(i==5)f->table.owner=999;else f->table.magic=0;
  f->r.error=DC_GOAL_OK;CHECK(dc_goal_process_spans_registered(&f->r)==-1);
  CHECK(!dc_goal_process_span_lookup(&f->r,f->p[0],&out));CHECK(f->r.error==DC_GOAL_BAD_ARGUMENT);
 }
 f->table=saved;owner_id=78;CHECK(!dc_goal_process_span_lookup(&f->r,f->p[0],&out));
 CHECK(!dc_goal_process_spans_detach(&f->r));owner_id=77;
 CHECK(dc_goal_process_span_lookup(&f->r,f->p[0],&out));CHECK(dc_goal_process_spans_detach(&f->r));
}
static void outputs(fixture *f){
 dc_goal_process_span a,saved;dc_goal_fiber fiber;void *scratch;unsigned char before[16];
 CHECK(init(f));CHECK(dc_goal_process_span_note(&f->r,f->p[0],240,&a));
 CHECK(!dc_goal_process_span_lookup(&f->r,f->p[0],f->rows));
 CHECK(!dc_goal_process_span_lookup(&f->r,f->p[0],(dc_goal_process_span*)&f->table));
 CHECK(!dc_goal_process_span_lookup(&f->r,f->p[0],(dc_goal_process_span*)&f->r));
 scratch=dc_goal_resolve(&f->r,f->raw+768,16);memset(scratch,0xa5,16);memset(before,0xa5,16);
 CHECK(!dc_goal_process_span_lookup(&f->r,f->p[0],scratch));CHECK(memcmp(scratch,before,16)==0);
 memset(&fiber,0,sizeof fiber);fiber.runtime=&f->r;fiber.phase=DC_FIBER_RUNNING;fiber.root_live=1;
 fiber.stack_base=f->raw+768;fiber.stack_bytes=256;fiber.platform_owner=77;current=&fiber;
 CHECK(dc_goal_process_span_lookup(&f->r,f->p[0],scratch));memcpy(&saved,scratch,sizeof saved);
 CHECK(saved.process==a.process&&saved.bytes==240&&saved.generation==a.generation);
 CHECK(!dc_goal_process_span_lookup(&f->r,f->p[0],(dc_goal_process_span*)&fiber));
 fiber.phase=DC_FIBER_SUSPENDED;CHECK(!dc_goal_process_span_lookup(&f->r,f->p[0],scratch));
 fiber.phase=DC_FIBER_RUNNING;fiber.root_live=0;CHECK(!dc_goal_process_span_lookup(&f->r,f->p[0],scratch));
 fiber.root_live=1;CHECK(!dc_goal_process_span_lookup(&f->r,f->p[0],(dc_goal_process_span*)((unsigned char*)scratch+248)));
 CHECK(!dc_goal_process_spans_detach(&f->r));current=NULL;CHECK(dc_goal_process_spans_detach(&f->r));
}
/* shrink-heap: a row keeps its process with fewer bytes and a fresh generation. */
static void shrinking(fixture *f){
 dc_goal_process_span a,b,c;
 CHECK(init(f));CHECK(dc_goal_process_span_note(&f->r,f->p[0],240,&a));
 CHECK(!dc_goal_process_span_shrink(&f->r,&a,241,NULL));f->r.error=DC_GOAL_OK;
 CHECK(!dc_goal_process_span_shrink(&f->r,&a,111,NULL));f->r.error=DC_GOAL_OK;
 CHECK(dc_goal_process_span_lookup(&f->r,f->p[0],&b)&&memcmp(&a,&b,sizeof a)==0);
 CHECK(dc_goal_process_span_shrink(&f->r,&a,160,&b));
 CHECK(b.process==f->p[0]&&b.bytes==160&&b.generation!=a.generation&&f->table.count==1);
 CHECK(!dc_goal_process_span_check(&f->r,&a));f->r.error=DC_GOAL_OK;
 CHECK(dc_goal_process_span_check(&f->r,&b));
 CHECK(!dc_goal_process_span_shrink(&f->r,&a,128,NULL));f->r.error=DC_GOAL_OK;
 /* The next pool member right after the shrunk extent no longer overlaps. */
 CHECK(dc_goal_process_span_note(&f->r,f->p[0]+160,96+16,&c)&&f->table.count==2);
 CHECK(!dc_goal_process_span_note(&f->r,f->p[0],240,NULL));f->r.error=DC_GOAL_OK;
 CHECK(dc_goal_process_span_note(&f->r,f->p[0],160,&c)&&memcmp(&b,&c,sizeof b)==0);
 CHECK(dc_goal_process_span_shrink(&f->r,&b,160,&c)&&c.bytes==160&&c.generation!=b.generation);
 CHECK(dc_goal_process_spans_detach(&f->r));
}
int main(int argc,char **argv){
 fixture f;int group=argc>1?atoi(argv[1]):0;if(!setup(&f))return 2;
 if(group==0)sorted(&f);else if(group==1)lifetime(&f);else if(group==2)metadata(&f);else if(group==3)outputs(&f);else if(group==4)shrinking(&f);else return 3;
 printf("process spans %u/%u checks group=%d\n",passed,total,group);free(f.memory);return passed==total?0:1;
}
