/* Real loader, fixture code from built DCSH; never executed (AI-assisted), ISC. */
#include "arena_jak_exports.h"
#include "exports_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char memory[524288] __attribute__((aligned(16)));
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static void put(dc_goal_runtime *rt,uint32_t at,uint32_t v){memcpy(dc_goal_resolve(rt,at,4),&v,4);}
int main(void){
  dc_goal_runtime rt;dc_goal_module_result result;dc_goal_code_entry_info info;
  dc_goal_code_diagnostic diagnostic;dc_jak_exports_evidence receipt;
  uint32_t entries[4],head,record[4],used;unsigned i;
  CHECK(dc_goal_init(&rt,memory,sizeof memory,0x04000000));
  CHECK(dc_goal_load_module(&rt,fixture,4,NULL,NULL,&result));
  used=rt.used;head=rt.code_registry_head;memcpy(record,dc_goal_resolve(&rt,head,16),16);
  CHECK(dc_arena_jak_exports(&rt,0));
  CHECK(rt.used==used && dc_arena_jak_exports_evidence.completed==1);
  for(i=0;i<4;++i){
    dc_jak_export_row row=dc_arena_jak_exports_evidence.stages[0].exports[i];
    entries[i]=row.value;
    CHECK(row.value && row.bytes==fixture[i].bytes && !row.error && !row.diagnostic.reason);
  }
  /* A data-only allocation does not add code records. */
  CHECK(dc_goal_alloc(&rt,4096)!=0);
  CHECK(dc_arena_jak_exports(&rt,1));
  CHECK(dc_arena_jak_exports_evidence.completed==3 && !dc_arena_jak_exports_evidence.first_failed_stage);
  /* A bad unrelated registry record rejects EVERY export. This reproduces
   * error 9 without a changed fingerprint or any particular export mismatch. */
  put(&rt,head+4,3);
  CHECK(!dc_arena_jak_exports(&rt,1) && rt.error==DC_GOAL_BAD_MODULE);
  CHECK(dc_arena_jak_exports_evidence.first_failed_stage==2);
  CHECK(dc_arena_jak_exports_evidence.stages[1].first_failed==1);
  for(i=0;i<4;++i){
    dc_jak_export_row row=dc_arena_jak_exports_evidence.stages[1].exports[i];
    CHECK(row.value==entries[i] && row.error==DC_GOAL_BAD_MODULE);
    CHECK(row.diagnostic.reason==DC_CODE_RECORD_FIELDS && row.diagnostic.at==head);
    CHECK(row.diagnostic.bytes==3 && row.diagnostic.entry==record[0]);
    memset(&info,0x55,sizeof info);
    CHECK(!dc_goal_code_entry_diagnose(&rt,entries[i],&info,&diagnostic));
    CHECK(!info.entry && !info.bytes && !info.symbol);
  }
  memcpy(dc_goal_resolve(&rt,head,16),record,16);rt.error=DC_GOAL_OK;
  CHECK(dc_goal_set_symbol(&rt,dc_goal_find(&rt,"start"),0));
  CHECK(!dc_arena_jak_exports(&rt,1) && rt.error==DC_GOAL_BAD_FUNCTION);
  CHECK(dc_arena_jak_exports_evidence.stages[1].first_failed==1);
  for(i=1;i<4;++i)CHECK(!dc_arena_jak_exports_evidence.stages[1].exports[i].error);
  CHECK(dc_goal_set_symbol(&rt,dc_goal_find(&rt,"start"),entries[0]));
  /* Even an alias of a different valid export must fail owner authentication. */
  for(i=0;i<4;++i){
    uint32_t symbol=dc_goal_find(&rt,fixture[i].name);
    CHECK(dc_goal_set_symbol(&rt,symbol,entries[(i+1)%4]));
    CHECK(!dc_arena_jak_exports(&rt,1) && rt.error==DC_GOAL_BAD_FUNCTION);
    CHECK(dc_arena_jak_exports_evidence.stages[1].first_failed==i+1);
    CHECK(dc_arena_jak_exports_evidence.stages[1].exports[i].diagnostic.reason==DC_CODE_EXPORT_SYMBOL);
    CHECK(dc_arena_jak_exports_evidence.stages[1].exports[i].owner!=symbol);
    CHECK(dc_goal_set_symbol(&rt,symbol,entries[i]));
  }
  /* A prior original fault must survive without new queries or snapshot writes. */
  receipt=dc_arena_jak_exports_evidence;rt.error=DC_GOAL_BOUNDS;
  CHECK(!dc_arena_jak_exports(&rt,1) && rt.error==DC_GOAL_BOUNDS);
  CHECK(dc_arena_jak_exports_evidence.prior_error==DC_GOAL_BOUNDS);
  CHECK(!memcmp(receipt.stages,(const void *)dc_arena_jak_exports_evidence.stages,sizeof receipt.stages));
  printf("%u fixture-derived export/refusal assertions passed\n",checks);return 0;
}
