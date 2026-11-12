/* res-lump lookup-tag-idx in C for the level ELF (AI-assisted), ISC.
 *
 * Every res lookup (res-lump-data, -struct, -value, entity-by-name's name of
 * each actor, ambient and camera) runs engine/entity/res.gc's lookup-tag-idx:
 * a binary search on the first eight characters of each tag's name as a
 * 64-bit integer, then a scan of the tags of that name for the key frame. The
 * SH-4 backend keeps GOAL integers as register pairs, so in GOAL the search is
 * pair arithmetic; it was about 14% of a level GOAL frame with its callers.
 * This is the same method, statement for statement, installed in res-lump's
 * method 19 (and descendants that still hold it). The first CHECKED calls
 * also run the original GOAL method and compare the 64-bit results.
 *
 * GOAL addresses are C addresses (mips2c_dc.h). res-lump fields from its
 * pointer: length +0, tag +24; a res-tag is 16 bytes: name +0, key-frame +4. */
#include <stdint.h>
#include <string.h>
#include "goal_runtime.h"
#include "goal_layout.h"
#include "goal_module.h"
#include "res_native.h"

enum {LOOKUP_METHOD=19,CHECKED=4096};
typedef uint64_t (*lookup_fn)(uint64_t,uint64_t,uint64_t,float);
static uint32_t s7,sym_base,sym_interp,sym_exact;
static lookup_fn original;
volatile dc_res_native_counts dc_res_native;

static inline uint64_t chars8(uint32_t symbol){
 uint32_t string;uint64_t v;
 memcpy(&string,(const void *)(uintptr_t)(symbol+DC_GOAL_SYM_TO_STRING_OFFSET),4);
 memcpy(&v,(const void *)(uintptr_t)(string+4u),8);
 return v;
}
static inline uint32_t tag_name(uint32_t tags,int32_t i){
 uint32_t v;memcpy(&v,(const void *)(uintptr_t)(tags+16u*(uint32_t)i),4);return v;
}
static inline float tag_time(uint32_t tags,int32_t i){
 float v;memcpy(&v,(const void *)(uintptr_t)(tags+16u*(uint32_t)i+4u),4);return v;
}
static uint64_t pair(int32_t lo,int32_t hi){return (uint64_t)(uint32_t)lo|(uint64_t)(uint32_t)hi<<32;}

static uint64_t lookup(uint32_t lump,uint32_t name,uint32_t mode,float time){
 int32_t length,tag_idx=-1,lo=-1,hi=-1,invalid=-1,max,min=0;uint32_t tags;uint64_t key;
 /* The original crash!es for id, aid, trans, rot, nav-mesh, process-type and
  * task; no caller asks for them (they would stop the game). */
 if(lump==s7 || lump==0)return UINT64_MAX;
 memcpy(&length,(const void *)(uintptr_t)lump,4);
 if(length<=0)return UINT64_MAX;
 memcpy(&tags,(const void *)(uintptr_t)(lump+24u),4);
 key=chars8(name);
 for(max=length-1;max>=min;){
  int32_t check=min+(max-min)/2;int64_t diff=(int64_t)(key-chars8(tag_name(tags,check)));
  if(diff==0){tag_idx=check;break;}
  if(diff<0)max=check-1;else min=check+1;
 }
 if(tag_idx<0)return (uint64_t)(int64_t)tag_idx;
 while(tag_idx>0 && key==chars8(tag_name(tags,tag_idx-1)))--tag_idx;
 if(mode==sym_base)return pair(tag_idx,tag_idx);
 for(int32_t i=tag_idx;!(i>=length || key<chars8(tag_name(tags,i)));++i){
  float frame;
  if(name!=tag_name(tags,i))continue;
  frame=tag_time(tags,i);
  if(frame==time){lo=hi=i;break;}
  if(time>=frame && mode!=sym_exact){lo=hi=i;if(frame==-1000000000.0f)invalid=i;}
  else if(time<frame){if(lo!=invalid && mode==sym_interp)hi=i;break;}
 }
 return pair(lo,hi);
}

static uint64_t lookup_native(uint64_t lump,uint64_t name,uint64_t mode,float time){
 uint64_t got=lookup((uint32_t)lump,(uint32_t)name,(uint32_t)mode,time);
 ++dc_res_native.calls;
 if(dc_res_native.checked<CHECKED){
  uint64_t want=original(lump,name,mode,time);
  ++dc_res_native.checked;
  if(got!=want){
   if(!dc_res_native.mismatches){dc_res_native.got=got;dc_res_native.want=want;dc_res_native.name=(uint32_t)name;}
   ++dc_res_native.mismatches;
   return want;
  }
 }
 return got;
}

int dc_res_native_install(dc_goal_runtime *rt,dc_goal_cache_sync sync){
 static unsigned char code[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
 dc_goal_code_export function={"dc-native-lookup-tag-idx",code,12,NULL,0};
 dc_goal_module_result result;dc_goal_code_entry_info info;
 uint32_t target=(uint32_t)(uintptr_t)lookup_native,type,old,native,symbol;
 enum dc_goal_error prior=rt->error;
 type=dc_goal_symbol_value(rt,dc_goal_find(rt,"res-lump"));
 sym_base=dc_goal_find(rt,"base");sym_interp=dc_goal_find(rt,"interp");sym_exact=dc_goal_find(rt,"exact");
 old=type?dc_goal_get_method(rt,type,LOOKUP_METHOD):0;
 if(rt->error!=DC_GOAL_OK || !type || !sym_base || !sym_interp || !sym_exact || !old ||
    !dc_goal_code_entry(rt,old,&info) || old==dc_goal_get_method(rt,type,LOOKUP_METHOD+1)){rt->error=prior;return 0;}
 s7=rt->s7;original=(lookup_fn)(uintptr_t)old;
 memcpy(code+8,&target,4);
 if(!dc_goal_load_module(rt,&function,1,sync,NULL,&result))return 0;
 symbol=dc_goal_find(rt,"dc-native-lookup-tag-idx");native=symbol?dc_goal_symbol_value(rt,symbol):0;
 if(!native || !dc_goal_method_set(rt,type,LOOKUP_METHOD,native,1))return 0;
 return dc_goal_get_method(rt,type,LOOKUP_METHOD)==native;
}

/* entity-by-name (entity.gc) remembered. Its result depends only on the name
 * and on the levels it scans: *level*'s length, and each level's status and
 * bsp (whose actor, ambient and camera arrays and their res 'name tags are
 * fixed once the bsp is loaded). A miss runs the original GOAL function and
 * remembers the answer under that signature; any change to a level's status
 * or bsp clears the table. The first CHECKED_HITS hits also run the original.
 * The level's 370 calls each scanned about 475 entities, a 'name lookup each:
 * 175,755 of the run's 196,858 res lookups.
 * *level*: length +0, levels from +96, 2608 bytes apart; a level's status
 * +16 and bsp +44 (level_logic.c's offsets). Names up to 31 characters. */
enum {NAMES=64,NAME_CHARS=32,LEVELS=3,LEVEL0=96,LEVEL_STRIDE=2608,CHECKED_HITS=64};
typedef uint64_t (*one_fn)(uint64_t);
static one_fn original_by_name;
static uint32_t level_group_symbol,string_type,signature[1+2*LEVELS];
static struct {uint32_t length,result;char chars[NAME_CHARS];} names[NAMES];
static unsigned name_count;

static int same_levels(void){
 uint32_t group,now[1+2*LEVELS],i,n;
 memcpy(&group,(const void *)(uintptr_t)level_group_symbol,4);
 memcpy(&now[0],(const void *)(uintptr_t)group,4);
 n=now[0]<=LEVELS?now[0]:LEVELS;
 memset(now+1,0,sizeof now-4);
 for(i=0;i<n;++i){uint32_t level=group+LEVEL0+LEVEL_STRIDE*i;
  memcpy(&now[1+2*i],(const void *)(uintptr_t)(level+16u),4);memcpy(&now[2+2*i],(const void *)(uintptr_t)(level+44u),4);}
 if(!memcmp(now,signature,sizeof now))return 1;
 memcpy(signature,now,sizeof now);name_count=0;++dc_res_native.by_name_flushes;
 return 0;
}
static uint64_t by_name_native(uint64_t arg){
 uint32_t s=(uint32_t)arg,type,length,i;const char *chars;uint64_t want;
 ++dc_res_native.by_name_calls;
 if(!s || s==s7 || (s&3u))return original_by_name(arg);
 memcpy(&type,(const void *)(uintptr_t)(s-4u),4);
 memcpy(&length,(const void *)(uintptr_t)s,4);
 if(type!=string_type || length>=NAME_CHARS)return original_by_name(arg);
 chars=(const char *)(uintptr_t)(s+4u);
 if(same_levels())
  for(i=0;i<name_count;++i)
   if(names[i].length==length && !memcmp(names[i].chars,chars,length)){
    ++dc_res_native.by_name_hits;
    if(dc_res_native.by_name_checked<CHECKED_HITS){
     want=original_by_name(arg);++dc_res_native.by_name_checked;
     if((uint32_t)want!=names[i].result){++dc_res_native.by_name_mismatches;return want;}
    }
    return names[i].result;
   }
 want=original_by_name(arg);
 if(name_count<NAMES && same_levels()){
  names[name_count].length=length;memcpy(names[name_count].chars,chars,length);
  names[name_count].result=(uint32_t)want;++name_count;}
 return want;
}

int dc_res_native_install_by_name(dc_goal_runtime *rt,dc_goal_cache_sync sync){
 static unsigned char code[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
 dc_goal_code_export function={"dc-native-entity-by-name",code,12,NULL,0};
 dc_goal_module_result result;dc_goal_code_entry_info info;
 uint32_t target=(uint32_t)(uintptr_t)by_name_native,symbol,old,native;
 enum dc_goal_error prior=rt->error;
 symbol=dc_goal_find(rt,"entity-by-name");level_group_symbol=dc_goal_find(rt,"*level*");
 old=symbol?dc_goal_symbol_value(rt,symbol):0;
 if(rt->error!=DC_GOAL_OK || !old || !level_group_symbol || !dc_goal_code_entry(rt,old,&info) || info.symbol!=symbol)
  {rt->error=prior;return 0;}
 s7=rt->s7;string_type=rt->string_type;original_by_name=(one_fn)(uintptr_t)old;
 memset(signature,0xff,sizeof signature);
 memcpy(code+8,&target,4);
 if(!dc_goal_load_module(rt,&function,1,sync,NULL,&result))return 0;
 native=dc_goal_symbol_value(rt,dc_goal_find(rt,"dc-native-entity-by-name"));
 return native && dc_goal_set_symbol(rt,symbol,native);
}
