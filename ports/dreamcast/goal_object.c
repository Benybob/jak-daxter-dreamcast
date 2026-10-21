/* Original V2/V4 retail relocation tables, bounded and read-only (AI-assisted), ISC.
 * Format contract: decompiler/ObjectFile/LinkedObjectFileCreation.cpp and
 * game/kernel/common/klink.cpp c_symlink2, with Jak 1 data placeholders only. */
#include "goal_object.h"
#include <stdlib.h>
#include <string.h>
typedef struct {
  const unsigned char *data;
  unsigned char *seen;
  uint32_t bytes,position,end;
  dc_goal_object_layout layout;
  dc_goal_object_visitor visit;
  void *context;
} scanner;
static uint32_t le32(const unsigned char *p) {
  return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static int byte(scanner *s,uint32_t *value) {
  if (s->position>=s->end) return 0;
  *value=s->data[s->position++]; return 1;
}
static int peek(scanner *s,uint32_t *value) {
  if (s->position>=s->end) return 0;
  *value=s->data[s->position]; return 1;
}
static const char *relocation(scanner *s,dc_goal_object_relocation *r) {
  uint32_t word;
  if ((r->offset&3u) || r->offset>s->layout.data_bytes-4u) return "object patch bounds";
  word=r->offset/4u;
  if (s->seen[word/8u]&(1u<<(word%8u))) return "duplicate object patch";
  s->seen[word/8u]|=(unsigned char)(1u<<(word%8u));
  if (s->visit && !s->visit(r,s->context)) return "object relocation visitor rejected";
  if (r->kind==DC_OBJECT_INTERNAL) ++s->layout.internal_relocations;
  else if (r->kind==DC_OBJECT_TYPE) ++s->layout.type_relocations;
  else if (r->kind==DC_OBJECT_EMPTY) ++s->layout.empty_relocations;
  else ++s->layout.symbol_relocations;
  return NULL;
}
static const char *pointers(scanner *s) {
  uint32_t next,count,word=0,i;
  int fixing=0;
  if (!peek(s,&next)) return "short object pointer table";
  if (!next) { ++s->position; return NULL; }
  for (;;) {
    for (;;) {
      if (!byte(s,&count)) return "short object pointer run";
      if (count>s->layout.data_bytes/4u-word) return "object pointer run bounds";
      if (fixing) for (i=0;i<count;++i) {
        dc_goal_object_relocation r={DC_OBJECT_INTERNAL,(word+i)*4u,0,0,NULL,0};
        const char *error;
        r.target=le32(s->data+s->layout.data_offset+r.offset);
        /* Empty texture-page segments point exactly one past the data. This
         * preserves an address, not permission to dereference that boundary. */
        if (r.target>s->layout.data_bytes) return "object pointer target bounds";
        error=relocation(s,&r); if (error) return error;
      }
      word+=count;
      if (count!=255) break;
      if (!peek(s,&next)) return "short object pointer continuation";
      if (!next) { ++s->position; fixing=!fixing; }
    }
    fixing=!fixing;
    if (!peek(s,&next)) return "short object pointer terminator";
    if (!next) { ++s->position; return NULL; }
  }
}
static const char *imports(scanner *s) {
  uint32_t marker,next;
  if (!peek(s,&marker)) return "short object import table";
  while (marker) {
    dc_goal_object_relocation r={DC_OBJECT_SYMBOL,0,0,0,NULL,0};
    const unsigned char *end;
    if (marker&128u) {
      r.kind=DC_OBJECT_TYPE; r.method_hint=(uint8_t)(marker&127u); ++s->position;
      if (r.method_hint==127u) return "unsupported object method hint";
    } else if (marker<=9u) return "unsupported object symbol encoding";
    end=memchr(s->data+s->position,0,s->end-s->position);
    if (!end || end==s->data+s->position) return "invalid object import name";
    r.name=(const char*)s->data+s->position;
    r.name_bytes=(uint32_t)(end-(s->data+s->position));
    if (r.kind==DC_OBJECT_SYMBOL && r.name_bytes==7u && !memcmp(r.name,"_empty_",7)) r.kind=DC_OBJECT_EMPTY;
    s->position+=r.name_bytes+1u;
    ++s->layout.imports;
    for (;;) {
      uint32_t encoded,part;
      unsigned length=1,j;
      const char *error;
      if (!byte(s,&encoded)) return "short object import delta";
      if (encoded&3u) length=(encoded&2u)?((encoded&1u)?4u:3u):2u;
      for (j=1;j<length;++j) {
        if (!byte(s,&part)) return "short object import delta";
        encoded|=part<<(j*8u);
      }
      encoded&=0xfffffffcu;
      if (encoded>s->layout.data_bytes-4u-r.offset) return "object import delta bounds";
      r.offset+=encoded;
      if (le32(s->data+s->layout.data_offset+r.offset)!=UINT32_MAX) return "object import placeholder";
      error=relocation(s,&r); if (error) return error;
      if (!peek(s,&next)) return "short object import terminator";
      if (!next) { ++s->position; break; }
    }
    if (!peek(s,&marker)) return "short object import table terminator";
  }
  return NULL;
}
const char *dc_goal_object_scan(const void *data,uint32_t bytes,dc_goal_object_visitor visit,
                                void *context,dc_goal_object_layout *layout) {
  scanner s;
  const char *error;
  uint32_t outer_length,version;
  if (!layout) return "invalid object scan arguments";
  memset(layout,0,sizeof(*layout)); memset(&s,0,sizeof(s));
  if (!data || bytes<12 || bytes>DC_GOAL_OBJECT_MAX_BYTES) return "object size bounds";
  s.data=data; s.bytes=bytes; s.visit=visit; s.context=context;
  outer_length=le32(s.data+4); version=le32(s.data+8);
  if (le32(s.data)!=UINT32_MAX || (version!=2 && version!=4)) return "unsupported retail object version";
  s.layout.version=version;
  if (version==4) {
    if (bytes<16) return "short object V4 header";
    s.layout.data_offset=16; s.layout.data_bytes=le32(s.data+12);
    if (s.layout.data_bytes>bytes-16) return "object V4 data bounds";
    s.layout.link_offset=16+s.layout.data_bytes;
  } else {
    if (outer_length>bytes) return "object V2 data bounds";
    s.layout.data_offset=outer_length; s.layout.data_bytes=bytes-outer_length;
  }
  if (s.layout.data_bytes<4 || (s.layout.data_bytes&3u)) return "object data alignment/size";
  if (s.layout.link_offset>bytes-12) return "short object inner header";
  s.layout.link_bytes=le32(s.data+s.layout.link_offset+4);
  if (le32(s.data+s.layout.link_offset)!=UINT32_MAX ||
      le32(s.data+s.layout.link_offset+8)!=2 || s.layout.link_bytes!=outer_length ||
      s.layout.link_bytes<64 || (s.layout.link_bytes&63u)) return "invalid object inner header";
  if (s.layout.link_bytes>bytes-s.layout.link_offset) return "object link bounds";
  s.end=s.layout.link_offset+s.layout.link_bytes;
  if (version==4 && s.end!=bytes) return "object trailing data";
  s.position=s.layout.link_offset+12;
  s.layout.bitmap_bytes=(s.layout.data_bytes/4u+7u)/8u;
  s.seen=calloc(s.layout.bitmap_bytes,1);
  if (!s.seen) return "object scan allocation failed";
  error=pointers(&s);
  if (!error) error=imports(&s);
  if (!error && ((s.position-s.layout.link_offset+1u+63u)&~63u)!=s.layout.link_bytes)
    error="object link padding length";
  if (!error) for (;s.position<s.end;++s.position) if (s.data[s.position]) {
    error="nonzero object link padding"; break;
  }
  free(s.seen);
  if (!error) *layout=s.layout;
  return error;
}
