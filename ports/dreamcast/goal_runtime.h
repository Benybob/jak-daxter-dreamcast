/* Bounded Jak 1 native runtime foundation (AI-assisted), ISC license.
 * This is not a full kernel, process runtime, allocator or object linker.
 * On SH-4 references use the native cached P0 alias of the arena, requiring
 * MMU-off operation. Host tests can use an explicit 32-bit base for the layout.
 */
#ifndef DC_GOAL_RUNTIME_H
#define DC_GOAL_RUNTIME_H
#include <stddef.h>
#include <stdint.h>
#include "goal_layout.h"

enum dc_goal_error {
  DC_GOAL_OK=0,DC_GOAL_BAD_ARGUMENT,DC_GOAL_OUT_OF_MEMORY,DC_GOAL_BOUNDS,
  DC_GOAL_BAD_OBJECT,DC_GOAL_SYMBOL_TABLE_FULL,DC_GOAL_BAD_HASH,
  DC_GOAL_BAD_TYPE,DC_GOAL_METHOD_CAPACITY,DC_GOAL_BAD_MODULE,
  DC_GOAL_DIVIDE_BY_ZERO,DC_GOAL_INTEGER_OVERFLOW,DC_GOAL_BAD_FUNCTION,
  /* Original control transfer with no verified native continuation yet. */
  DC_GOAL_UNSUPPORTED_TRANSFER
};
typedef struct {
  unsigned char *memory;
  uint32_t base,capacity,used,table,s7,symbol_count;
  uint32_t object_type,structure_type,basic_type,type_type,symbol_type;
  uint32_t string_type,function_type,pair_type,binteger_type;
  uint32_t number_type,integer_type;
  /* Private plain-arena records of exact loader-installed code entries. */
  uint32_t code_registry_head,code_registry_count;
  enum dc_goal_error error;
} dc_goal_runtime;

int dc_goal_init(dc_goal_runtime *rt,void *memory,uint32_t capacity,uint32_t virtual_base);
#ifdef __DREAMCAST__
int dc_goal_init_native(dc_goal_runtime *rt,void *memory,uint32_t capacity);
#endif
void *dc_goal_resolve(dc_goal_runtime *rt,uint32_t address,uint32_t size);
uint32_t dc_goal_alloc(dc_goal_runtime *rt,uint32_t bytes);
/* Size of the most recent arena request that did not fit (diagnostics only). */
extern uint32_t dc_goal_last_failed_alloc;
/* First failure of a GOAL-called type bridge (type new, method-set!) since the
 * loader last cleared it. Every runtime call resets rt->error, so without this a
 * failed deftype inside a startup would be hidden by the next call. */
extern enum dc_goal_error dc_goal_sticky_error;
uint32_t dc_goal_hash(const char *name);
uint32_t dc_goal_find(dc_goal_runtime *rt,const char *name);
uint32_t dc_goal_intern(dc_goal_runtime *rt,const char *name);
int dc_goal_bind_fixed(dc_goal_runtime *rt,uint32_t offset,const char *name,uint32_t value);
uint32_t dc_goal_symbol_value(dc_goal_runtime *rt,uint32_t symbol);
int dc_goal_set_symbol(dc_goal_runtime *rt,uint32_t symbol,uint32_t value);
const char *dc_goal_symbol_name(dc_goal_runtime *rt,uint32_t symbol);
uint32_t dc_goal_string(dc_goal_runtime *rt,const char *text);
uint32_t dc_goal_new_basic(dc_goal_runtime *rt,uint32_t type,uint32_t bytes);
uint32_t dc_goal_new_pair(dc_goal_runtime *rt,uint32_t car,uint32_t cdr);
uint32_t dc_goal_type_of(dc_goal_runtime *rt,uint32_t value);
uint32_t dc_goal_define_type(dc_goal_runtime *rt,const char *name,uint32_t parent,
                             uint16_t bytes,uint16_t heap_base,uint16_t methods);
int dc_goal_set_method(dc_goal_runtime *rt,uint32_t type,uint16_t index,uint32_t function);
uint32_t dc_goal_get_method(dc_goal_runtime *rt,uint32_t type,uint16_t index);
int dc_goal_is_subtype(dc_goal_runtime *rt,uint32_t type,uint32_t ancestor);
/* Original deftype flags: size/heap-base/method-count in successive 16-bit fields.
 * Existing method capacity may not grow; parent reads stay within its table. */
uint32_t dc_goal_new_type(dc_goal_runtime *rt,uint32_t symbol,uint32_t parent,uint64_t flags);
/* Original forward type interning: 0 defaults to 12 slots, 1 to 44. Existing
 * types retain identity/capacity. New forward types have no invented parent. */
uint32_t dc_goal_intern_type(dc_goal_runtime *rt,const char *name,uint16_t methods);
/* Original method-set! sentinels 0=no-op, 1=clear, 2=inherit. Optional child
 * propagation updates descendants still containing the old method value. */
uint32_t dc_goal_method_set(dc_goal_runtime *rt,uint32_t type,uint32_t index,
                            uint32_t method,int propagate);
#endif
