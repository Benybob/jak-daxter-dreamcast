/* Layout, mutation, hierarchy and bounded-failure checks (AI-assisted), ISC. */
#include "runtime_cases.h"
#include "goal_runtime.h"
#include <string.h>

static uint32_t word(dc_goal_runtime *rt,uint32_t address) {
  uint32_t result=0;
  void *p=dc_goal_resolve(rt,address,4);
  if (p) memcpy(&result,p,4);
  return result;
}
static void put(dc_goal_runtime *rt,uint32_t address,uint32_t value) {
  void *p=dc_goal_resolve(rt,address,4);
  if (p) memcpy(p,&value,4);
}
static int zeroed(const void *memory,unsigned bytes) {
  const unsigned char *p=memory;
  unsigned i;
  if (!p) return 0;
  for (i=0;i<bytes;++i) if (p[i]) return 0;
  return 1;
}
void dc_runtime_cases(void *memory,uint32_t capacity,uint32_t base,
                       dc_runtime_check check,void *context,dc_runtime_stats *stats) {
  dc_goal_runtime rt,invalid;
  unsigned id=0,i;
  uint32_t symbol,string,pair,child,object,before,count,name_ref,length;
  dc_goal_type_fields *fields;
  unsigned char *arena=memory;
#define CHECK(a,b,n) check((uint64_t)(a),(uint64_t)(b),++id,n,context)
  memset(stats,0,sizeof(*stats)); stats->arena_bytes=capacity;
  memset(arena-16,0xa5,16); memset(arena+capacity,0x5a,16);
  CHECK(dc_goal_hash("_empty_"),0x8454b6e6u,"original empty-pair hash");
  CHECK(dc_goal_hash("#f"),0xffffdc99u,"original symbol CRC");
  i=(unsigned)dc_goal_init(&rt,memory,capacity,base);
  CHECK(i,1,"runtime bootstrap");
  if (!i) return;
  stats->boot_bytes=rt.used;
  CHECK(rt.s7-rt.table,DC_GOAL_MAX_SYMBOLS*4u+4u,"s7 at original symbol midpoint");
  CHECK(DC_GOAL_SYM_TO_STRING_OFFSET,0x20000u,"original symbol-to-string offset");
  CHECK(dc_goal_type_of(&rt,rt.s7),rt.symbol_type,"false symbol has symbol type");
  CHECK(dc_goal_symbol_value(&rt,rt.s7),rt.s7,"false is its own symbol value");
  CHECK(dc_goal_symbol_value(&rt,rt.s7+8),rt.s7+8,"true fixed-symbol value");
  CHECK(dc_goal_find(&rt,"_empty_"),rt.s7-10u,"empty-pair fixed reference");
  CHECK(word(&rt,rt.s7-12),rt.s7-10u,"empty-pair car points to itself");
  CHECK(word(&rt,rt.s7-8),rt.s7-10u,"empty-pair cdr points to itself");
  CHECK(dc_goal_type_of(&rt,rt.type_type),rt.type_type,"metatype tags itself");
  CHECK(dc_goal_is_subtype(&rt,rt.string_type,rt.basic_type),1,"string inherits basic");
  CHECK(dc_goal_is_subtype(&rt,rt.string_type,rt.structure_type),1,"string inherits structure");
  CHECK(dc_goal_is_subtype(&rt,rt.pair_type,rt.basic_type),0,"pair is not a basic");
  CHECK(dc_goal_type_of(&rt,42u<<3),rt.binteger_type,"boxed integer tag");
  CHECK(dc_goal_find(&rt,"dc-missing-symbol"),0,"missing symbol lookup");
  count=rt.symbol_count;
  symbol=dc_goal_intern(&rt,"dc-runtime-value");
  CHECK(symbol!=0,1,"intern allocates a symbol");
  if (!symbol) return;
  CHECK(symbol&7u,4,"symbol basic tag");
  before=rt.used;
  CHECK(dc_goal_intern(&rt,"dc-runtime-value"),symbol,"intern identity");
  CHECK(rt.used,before,"reinterning does not allocate");
  CHECK(rt.symbol_count,count+1,"reinterning does not duplicate a slot");
  CHECK(dc_goal_symbol_value(&rt,symbol),0,"new symbol begins unbound");
  CHECK(dc_goal_set_symbol(&rt,symbol,0xdeadbeefu),1,"mutable symbol assignment");
  CHECK(dc_goal_symbol_value(&rt,symbol),0xdeadbeefu,"mutable symbol read");
  { const char *name=dc_goal_symbol_name(&rt,symbol);
    CHECK(name && !strcmp(name,"dc-runtime-value"),1,"symbol name through original metadata layout"); }
  string=dc_goal_string(&rt,"Jak and Daxter");
  CHECK(string!=0,1,"native GOAL string allocation");
  if (!string) return;
  CHECK(word(&rt,string),14,"GOAL string length field");
  CHECK(dc_goal_type_of(&rt,string),rt.string_type,"GOAL string header type");
  { const char *text=dc_goal_resolve(&rt,string+4,15);
    CHECK(text && !memcmp(text,"Jak and Daxter",15),1,"GOAL string payload and terminator"); }
  pair=dc_goal_new_pair(&rt,symbol,rt.s7-10u);
  CHECK(pair!=0,1,"pair allocation");
  if (!pair) return;
  CHECK(pair&7u,2,"pair tag");
  CHECK(word(&rt,pair-2),symbol,"pair car at minus two");
  CHECK(word(&rt,pair+2),rt.s7-10u,"pair cdr at plus two");
  CHECK(dc_goal_type_of(&rt,pair),rt.pair_type,"pair type lookup");
  CHECK(dc_goal_set_method(&rt,rt.basic_type,3,0x8c00f004u),1,"raw method-slot installation");
  child=dc_goal_define_type(&rt,"dc-runtime-child",rt.basic_type,32,0x1234,12);
  CHECK(child!=0,1,"derived type definition");
  if (!child) return;
  fields=dc_goal_resolve(&rt,child,sizeof(*fields));
  CHECK(fields->parent,rt.basic_type,"type parent layout");
  CHECK(fields->allocated_size,32,"type allocation size");
  CHECK(fields->padded_size,32,"type padded size");
  CHECK(fields->heap_base,0x1234,"type heap-base layout");
  CHECK(fields->num_methods,12,"type method count layout");
  CHECK(dc_goal_get_method(&rt,child,3),0x8c00f004u,"inherited method slot");
  CHECK(dc_goal_get_method(&rt,child,11),0,"new method slots remain unbound");
  CHECK(dc_goal_symbol_value(&rt,fields->symbol),child,"type symbol resolves its type");
  object=dc_goal_new_basic(&rt,child,32);
  CHECK(object!=0,1,"derived basic allocation");
  if (!object) return;
  CHECK(dc_goal_type_of(&rt,object),child,"basic type tag at minus four");
  CHECK(zeroed(dc_goal_resolve(&rt,object,28),28),1,"new basic fields are zeroed");
  before=rt.used;
  CHECK(dc_goal_new_basic(&rt,child,4),0,"undersized object rejected");
  CHECK(rt.used,before,"rejected object does not consume heap");
  CHECK(dc_goal_define_type(&rt,"dc-runtime-child",rt.basic_type,32,0x1234,10),child,"compatible type reinterning");
  CHECK(rt.used,before,"compatible type reinterning does not allocate");
  CHECK(dc_goal_define_type(&rt,"dc-runtime-child",rt.basic_type,32,0x1234,13),0,"method-table growth rejected");
  CHECK(dc_goal_set_method(&rt,child,12,0xffffffffu),0,"method write bounds");
  CHECK(dc_goal_get_method(&rt,child,12),0,"method read bounds");
  CHECK(rt.error,DC_GOAL_METHOD_CAPACITY,"method bounds error is explicit");
  fields->parent=child;
  CHECK(dc_goal_is_subtype(&rt,child,rt.pair_type),0,"cyclic type chain terminates");
  CHECK(rt.error,DC_GOAL_BAD_TYPE,"cyclic type chain reports invalid type");
  fields->parent=rt.basic_type;
  CHECK(dc_goal_type_of(&rt,1),0,"invalid object tag rejected");
  CHECK(dc_goal_resolve(&rt,0,4)==NULL,1,"address underflow rejected");
  CHECK(dc_goal_resolve(&rt,UINT32_MAX,4)==NULL,1,"address overflow rejected");
  name_ref=word(&rt,symbol+DC_GOAL_SYM_TO_STRING_OFFSET); length=word(&rt,name_ref);
  put(&rt,name_ref,UINT32_MAX);
  CHECK(dc_goal_symbol_name(&rt,symbol)==NULL,1,"corrupt string length rejected");
  put(&rt,name_ref,length);
  put(&rt,symbol+DC_GOAL_SYM_TO_STRING_OFFSET,4);
  CHECK(dc_goal_symbol_name(&rt,symbol)==NULL,1,"corrupt name pointer rejected");
  put(&rt,symbol+DC_GOAL_SYM_TO_STRING_OFFSET,name_ref);
  CHECK(dc_goal_set_symbol(&rt,rt.s7+1,0),0,"unaligned symbol mutation rejected");
  CHECK(dc_goal_set_symbol(&rt,rt.s7-10u,0),0,"empty pair is not a symbol cell");
  CHECK(dc_goal_alloc(&rt,0),0,"zero-size allocation rejected");
  CHECK(dc_goal_alloc(&rt,UINT32_MAX),0,"allocation size overflow rejected");
  stats->live_before_stress=rt.used; stats->symbols=rt.symbol_count;
  before=rt.used;
  CHECK(dc_goal_alloc(&rt,capacity),0,"oversized allocation fails");
  CHECK(rt.used,before,"failed allocation leaves bump pointer unchanged");
  CHECK(dc_goal_alloc(&rt,capacity-before)!=0,1,"exact remaining heap allocation");
  CHECK(dc_goal_alloc(&rt,1),0,"exhausted heap rejected");
  count=rt.symbol_count;
  CHECK(dc_goal_intern(&rt,"dc-oom-symbol"),0,"intern handles heap exhaustion");
  CHECK(rt.symbol_count,count,"failed intern does not publish a slot");
  CHECK(rt.error,DC_GOAL_OUT_OF_MEMORY,"intern propagates allocation failure");
  /* Fill hash metadata without allocating names. Probe hash intentionally
     differs, so a full-table search must terminate without touching them. */
  for (i=0;i<DC_GOAL_MAX_SYMBOLS;++i)
    put(&rt,rt.table+4+i*8u+DC_GOAL_SYM_INFO_OFFSET,1);
  CHECK(dc_goal_intern(&rt,"dc-full-table"),0,"full symbol probe terminates");
  CHECK(rt.error,DC_GOAL_SYMBOL_TABLE_FULL,"full symbol table error");
  CHECK(dc_goal_init(NULL,memory,capacity,base),0,"null runtime rejected");
  CHECK(dc_goal_init(&invalid,arena+1,capacity,base),0,"misaligned arena rejected");
  CHECK(dc_goal_init(&invalid,memory,capacity,UINT32_MAX-15u),0,"32-bit address wrap rejected");
  CHECK(dc_goal_init(&invalid,memory,16,base),0,"undersized arena fails bootstrap");
  for (i=0;i<16;++i) if (arena[-16+(int)i]!=0xa5 || arena[capacity+i]!=0x5a) break;
  CHECK(i,16,"arena boundary canaries preserved");
#undef CHECK
}
