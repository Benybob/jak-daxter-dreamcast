/* Bounded original retail data-object parser (AI-assisted), ISC. */
#ifndef DC_GOAL_OBJECT_H
#define DC_GOAL_OBJECT_H
#include <stdint.h>
#define DC_GOAL_OBJECT_MAX_BYTES (8u*1024u*1024u)
enum dc_goal_object_relocation_kind {
  DC_OBJECT_INTERNAL,DC_OBJECT_SYMBOL,DC_OBJECT_TYPE,DC_OBJECT_EMPTY
};
typedef struct {
  enum dc_goal_object_relocation_kind kind;
  uint32_t offset,target,name_bytes;
  const char *name;
  uint8_t method_hint;
} dc_goal_object_relocation;
typedef int (*dc_goal_object_visitor)(const dc_goal_object_relocation *,void *);
typedef struct {
  uint32_t version,data_offset,data_bytes,link_offset,link_bytes,bitmap_bytes;
  uint32_t internal_relocations,symbol_relocations,type_relocations,empty_relocations,imports;
} dc_goal_object_layout;
/* V2/V4 single data segments only. Never executes or interprets retail code.
 * Source is immutable; names borrow source memory. Visitors can observe a
 * valid prefix before a later error, so mutation needs a preceding full scan.
 * Duplicate patch locations, bad placeholders and beyond-end targets fail.
 * Exact one-past pointers are valid for original zero-length texture segments.
 * Up to data_bytes/32 temporary bitmap bytes; no native runtime is required. */
const char *dc_goal_object_scan(const void *data,uint32_t bytes,dc_goal_object_visitor visit,
                                void *context,dc_goal_object_layout *layout);
#endif
