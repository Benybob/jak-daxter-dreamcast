/* Independent exact export-entry checks; never execute registered test code.
 * Native initializer rejection probes use deliberately unregistered objects.
 * (AI-assisted), ISC. */
#include "code_entry_cases.h"
#include "goal_module.h"
#include <stdlib.h>
#include <string.h>

typedef struct {dc_goal_runtime *rt;uint32_t head,count,calls;unsigned stable;} sync_state;
static void observe_sync(uint32_t allocation,uint32_t bytes,void *context) {
 sync_state *s=context;
 ++s->calls;
 if(s->rt->code_registry_head!=s->head || s->rt->code_registry_count!=s->count ||
    !dc_goal_resolve(s->rt,allocation,bytes)) s->stable=0;
}
static uint32_t word(dc_goal_runtime *rt,uint32_t at) {uint32_t v;memcpy(&v,dc_goal_resolve(rt,at,4),4);return v;}
static void put(dc_goal_runtime *rt,uint32_t at,uint32_t v) {memcpy(dc_goal_resolve(rt,at,4),&v,4);}
static int zero_info(const dc_goal_code_entry_info *p) {return !p->entry && !p->bytes && !p->symbol;}
void dc_code_entry_cases(dc_runtime_check check,void *context,unsigned *id) {
 const uint32_t capacity=512u*1024u;
 unsigned char *allocation=malloc(capacity+32),*memory,*snapshot=malloc(capacity);
 dc_goal_runtime rt;
 dc_goal_module_result result;
 dc_goal_code_entry_info info;
 sync_state sync;
 const unsigned char code[8]={0x0b,0,9,0,0x55,0xaa,0x55,0xaa};
 dc_goal_code_export functions[2]={{"dc-known-a",code,8,NULL,0},{"dc-known-b",code,4,NULL,0}};
 uint32_t a,b,fake,symbol,used,head,count,symbols,old_a,tag,i;
 uint32_t record[4];
 uint64_t value;
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
#define QUERY(address,ok) do { \
 used=rt.used;head=rt.code_registry_head;count=rt.code_registry_count;symbols=rt.symbol_count; \
 memcpy(snapshot,memory,used);memset(&info,0x6d,sizeof info); \
 CHECK(dc_goal_code_entry(&rt,address,&info),ok,"exact code-entry provenance result"); \
 CHECK(memcmp(snapshot,memory,used),0,"entry query writes no managed byte"); \
 CHECK(rt.used,used,"entry query never allocates"); \
 CHECK(rt.symbol_count,symbols,"entry query never interns a name"); \
 CHECK(rt.code_registry_head,head,"entry query preserves registry head"); \
 CHECK(rt.code_registry_count,count,"entry query preserves registry count"); \
 if(!(ok)) CHECK(zero_info(&info),1,"failed entry query clears all output fields"); \
} while(0)
 CHECK(allocation!=NULL && snapshot!=NULL,1,"separate entry fixture allocations");
 if(!allocation || !snapshot) {free(allocation);free(snapshot);return;}
 memory=(unsigned char *)(((uintptr_t)allocation+15u)&~(uintptr_t)15u);
 CHECK(dc_goal_init(&rt,memory,capacity,0x04000000u),1,"independent virtual-layout runtime initializes");
 QUERY(0,0);QUERY(rt.s7,0);QUERY(rt.base+1,0);
 fake=dc_goal_new_basic(&rt,rt.function_type,12);
 symbol=dc_goal_intern(&rt,"dc-unregistered-initializer");
 CHECK(fake!=0 && dc_goal_set_symbol(&rt,symbol,fake),1,"fabricated typed function object is installed as data only");
 QUERY(fake,0);CHECK(rt.error,DC_GOAL_BAD_FUNCTION,"function tag alone does not authorize entry");
 value=0x1122334455667788ull;
 CHECK(dc_goal_run_initializer(&rt,"dc-unregistered-initializer",&value),0,"scalar initializer rejects fabricated typed function before execution");
 CHECK(rt.error,DC_GOAL_BAD_MODULE,"scalar initializer preserves existing module-error contract");
 CHECK(value,0x1122334455667788ull,"rejected scalar initializer preserves output");
 CHECK(dc_goal_run_initializer_void(&rt,"dc-unregistered-initializer"),0,"void initializer rejects fabricated typed function before execution");
 CHECK(rt.error,DC_GOAL_BAD_MODULE,"void initializer preserves existing module-error contract");
 sync=(sync_state){&rt,rt.code_registry_head,rt.code_registry_count,0,1};
 CHECK(dc_goal_load_module(&rt,functions,2,observe_sync,&sync,&result),1,"trusted module installs two exact entries");
 CHECK(sync.calls,2,"both code buffers synchronized");CHECK(sync.stable,1,"registry is unpublished during cache synchronization");
 CHECK(rt.code_registry_count,2,"one compact record per installed function");
 a=dc_goal_symbol_value(&rt,dc_goal_find(&rt,"dc-known-a"));b=dc_goal_symbol_value(&rt,dc_goal_find(&rt,"dc-known-b"));
 QUERY(a,1);CHECK(info.entry,a,"first exact address returned");CHECK(info.bytes,8,"whole first code span recorded");
 CHECK(info.symbol,dc_goal_find(&rt,"dc-known-a"),"owning symbol recorded");
 QUERY(b,1);CHECK(info.bytes,4,"second code span exact");
 QUERY(a+1,0);QUERY(a+2,0);QUERY(a+4,0);QUERY(a+8,0);QUERY(a-4,0);
 old_a=a;head=rt.code_registry_head;memcpy(record,dc_goal_resolve(&rt,head,16),16);
 sync=(sync_state){&rt,head,rt.code_registry_count,0,1};
 CHECK(dc_goal_load_module(&rt,functions,1,observe_sync,&sync,&result),1,"function replacement installs a fresh exact entry");
 CHECK(memcmp(record,dc_goal_resolve(&rt,head,16),16),0,"replacement never edits older registry record");
 a=dc_goal_symbol_value(&rt,dc_goal_find(&rt,"dc-known-a"));
 CHECK(a!=old_a,1,"replacement code has a fresh allocation");
 QUERY(old_a,0);QUERY(a,1);
 symbol=dc_goal_find(&rt,"dc-known-a");
 CHECK(dc_goal_set_symbol(&rt,dc_goal_intern(&rt,"dc-known-alias"),a),1,"alias cell may reference registered code");
 CHECK(dc_goal_set_symbol(&rt,symbol,fake),1,"owning symbol deliberately rebound");QUERY(a,0);
 CHECK(dc_goal_set_symbol(&rt,symbol,a),1,"owning symbol restored explicitly");QUERY(a,1);
 tag=word(&rt,a-4);put(&rt,a-4,rt.object_type);QUERY(a,0);
 CHECK(rt.error,DC_GOAL_BAD_FUNCTION,"queried function header mismatch is a function error");
 QUERY(b,1); /* Unqueried code tags are not list structure or byte-integrity checks. */
 put(&rt,a-4,tag);QUERY(a,1);
 /* Each structural corruption is independently restored after a no-write query. */
 for(i=0;i<12;++i) {
  uint32_t original_head=rt.code_registry_head,original_count=rt.code_registry_count;
  memcpy(record,dc_goal_resolve(&rt,original_head,16),16);
  if(i==0) put(&rt,original_head+12,original_head);
  if(i==1) put(&rt,original_head+12,original_head+16);
  if(i==2) put(&rt,original_head,record[0]+2);
  if(i==3) put(&rt,original_head+4,3);
  if(i==4) put(&rt,original_head+4,0xffffffffu);
  if(i==5) put(&rt,original_head+8,0);
  if(i==6) ++rt.code_registry_count;
  if(i==7) rt.code_registry_count=0;
  if(i==8) rt.code_registry_head+=4;
  if(i==9) rt.code_registry_head=rt.base+rt.used;
  if(i==10) --rt.code_registry_count;
  if(i==11) {
   put(&rt,original_head,b);put(&rt,original_head+4,4);
   put(&rt,original_head+8,dc_goal_find(&rt,"dc-known-b"));
  }
  QUERY(i==11?b:a,0);CHECK(rt.error,DC_GOAL_BAD_MODULE,"malformed or duplicate registry fails closed without following an unbounded chain");
  rt.code_registry_head=original_head;rt.code_registry_count=original_count;
  memcpy(dc_goal_resolve(&rt,original_head,16),record,16);
 }
 QUERY(a,1);
 /* Preintern names: exactly32 code bytes then one16-byte record fit. */
 functions[0].name="dc-oom-a";functions[1].name="dc-oom-b";
 CHECK(dc_goal_intern(&rt,functions[0].name)!=0 && dc_goal_intern(&rt,functions[1].name)!=0,1,"OOM names preinterned independently of loader");
 used=rt.used;head=rt.code_registry_head;count=rt.code_registry_count;symbols=rt.symbol_count;
 memcpy(snapshot,memory,used);rt.capacity=used+48;
 sync=(sync_state){&rt,head,count,0,1};
 CHECK(dc_goal_load_module(&rt,functions,2,observe_sync,&sync,&result),0,"second provenance record OOM rolls whole module back after cache synchronization");
 CHECK(rt.error,DC_GOAL_OUT_OF_MEMORY,"metadata allocation failure is explicit");
 CHECK(sync.calls,2,"post-sync rollback path actually reached");
 CHECK(rt.used,used,"failed registration rolls arena cursor back");
 CHECK(rt.code_registry_head,head,"failed partial registration rolls head back");CHECK(rt.code_registry_count,count,"failed partial registration rolls count back");
 CHECK(rt.symbol_count,symbols,"failed registration preserves namespace count");
 CHECK(memcmp(snapshot,memory,used),0,"failed registration preserves old used prefix and symbol bindings");
 CHECK(result.functions|result.code_bytes|result.arena_bytes,0,"failed registration publishes no successful module result");
 rt.capacity=capacity;QUERY(a,1);
 CHECK(dc_goal_init(&rt,memory,capacity,0x04000000u),1,"runtime reset rebuilds without stale provenance");
 CHECK(rt.code_registry_head|rt.code_registry_count,0,"reset clears registry roots");QUERY(a,0);
 free(allocation);free(snapshot);
#undef QUERY
#undef CHECK
}
