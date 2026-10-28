/* (AI-assisted), ISC license; see the repository LICENSE. */
#include "dma_chain.h"
#include <stddef.h>

static uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
         (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Use wide arithmetic before pointer formation; zero bytes may be one-past. */
static int range(const dc_dma_view *v, uint64_t addr, uint32_t bytes) {
  uint64_t end = (uint64_t)v->addr + v->size;
  if (addr & 15) return DC_DMA_ALIGNMENT;
  if (addr < v->addr || addr > end || bytes > end - addr)
    return DC_DMA_ADDRESS;
  return 0;
}

int dc_dma_tag_id_check(uint32_t id) {
  return id <= DC_DMA_END ? 0 : DC_DMA_UNKNOWN_ID;
}

int dc_dma_follower_init(dc_dma_follower *f, const dc_dma_view *v,
                         uint32_t start) {
  dc_dma_follower fresh = {0};
  int rc;
  if (!f) return DC_DMA_ARGUMENT;
  *f = fresh;
  if (!v || !v->base || !v->size) return f->error = DC_DMA_ARGUMENT;
  if ((uint64_t)v->addr + v->size > UINT64_C(0x100000000))
    return f->error = DC_DMA_ADDRESS;
  f->view = *v;
  f->current = start;
  f->limit = DC_DMA_DEFAULT_LIMIT;
  rc = range(v, start, 16);
  return f->error = rc;
}

int dc_dma_follower_next(dc_dma_follower *f, dc_dma_transfer *out) {
  dc_dma_transfer t = {0};
  const uint8_t *tag;
  uint32_t addr, depth;
  uint64_t data, next, resume;
  int ended = 0, rc;
  if (!f) return DC_DMA_ARGUMENT;
  if (f->error) return f->error;
  if (!out) return f->error = DC_DMA_ARGUMENT;
  if (f->ended) return 0;
  if (f->count >= f->limit) return f->error = DC_DMA_LIMIT;
  rc = range(&f->view, f->current, 16);
  if (rc) return f->error = rc;
  tag = f->view.base + (f->current - f->view.addr);
  t.tag_addr = f->current;
  t.raw_tag = (uint64_t)le32(tag) | (uint64_t)le32(tag + 4) << 32;
  t.id = (uint32_t)(t.raw_tag >> 28) & 7;
  t.qwc = (uint32_t)t.raw_tag & 65535;
  t.vif0 = le32(tag + 8);
  t.vif1 = le32(tag + 12);
  t.size_bytes = t.qwc * 16;
  rc = dc_dma_tag_id_check(t.id);
  if (rc) return f->error = rc;
  if (t.raw_tag >> 63) return f->error = DC_DMA_SCRATCHPAD;
  addr = (uint32_t)(t.raw_tag >> 32) & UINT32_C(0x7fffffff);
  data = (uint64_t)f->current + 16;
  resume = data + t.size_bytes;
  next = data;
  depth = f->depth;
  switch (t.id) {
    case DC_DMA_CNT:
      if (addr) return f->error = DC_DMA_CNT_ADDRESS;
      next = resume;
      break;
    case DC_DMA_NEXT: next = addr; break;
    case DC_DMA_REF:
    case DC_DMA_REFS: data = addr; break;
    case DC_DMA_REFE: data = addr; ended = 1; break;
    case DC_DMA_CALL:
      if (depth >= 2) return f->error = DC_DMA_STACK_OVERFLOW;
      if (resume > UINT32_MAX) return f->error = DC_DMA_ADDRESS;
      next = addr;
      break;
    case DC_DMA_RET:
      if (!depth) return f->error = DC_DMA_STACK_EMPTY;
      next = f->stack[depth - 1];
      break;
    case DC_DMA_END: next = f->current; ended = 1; break;
    default: return f->error = DC_DMA_UNKNOWN_ID;
  }
  rc = range(&f->view, data, t.size_bytes);
  if (rc) return f->error = rc;
  if (data > UINT32_MAX || next > UINT32_MAX)
    return f->error = DC_DMA_ADDRESS;
  /* Check the successor when it is read, not before yielding this transfer. */
  t.data_addr = (uint32_t)data;
  t.data = f->view.base + (t.data_addr - f->view.addr);
  if (t.id == DC_DMA_CALL) f->stack[f->depth++] = (uint32_t)resume;
  if (t.id == DC_DMA_RET) --f->depth;
  f->current = (uint32_t)next;
  f->ended = ended;
  ++f->count;
  *out = t;
  return 1;
}

int dc_dma_bucket_iter_init(dc_dma_bucket_iter *it, const dc_dma_view *view,
                            uint32_t start, uint32_t count) {
  dc_dma_bucket_iter fresh = {0};
  dc_dma_transfer t;
  uint64_t last = (uint64_t)start + 16 + (uint64_t)count * 16;
  int rc;
  if (!it) return DC_DMA_ARGUMENT;
  *it = fresh;
  if (last > UINT32_MAX) return it->error = DC_DMA_ADDRESS;
  it->base = start + 16;
  it->bucket_count = count;
  rc = dc_dma_follower_init(&it->follower, view, start);
  if (rc) return it->error = rc;
  rc = dc_dma_follower_next(&it->follower, &t);
  if (rc < 0) return it->error = rc;
  if (t.id != DC_DMA_CALL || t.vif0 || t.vif1)
    return it->error = DC_DMA_BUCKET_LAYOUT;
  rc = dc_dma_follower_next(&it->follower, &t);
  if (rc < 0) return it->error = rc;
  if (t.id != DC_DMA_CNT || t.qwc != 10)
    return it->error = DC_DMA_BUCKET_LAYOUT;
  rc = dc_dma_follower_next(&it->follower, &t);
  if (rc < 0) return it->error = rc;
  if (t.id != DC_DMA_RET || t.qwc || it->follower.current != it->base)
    return it->error = DC_DMA_BUCKET_LAYOUT;
  return 0;
}

int dc_dma_bucket_iter_next(dc_dma_bucket_iter *it, dc_dma_transfer *out,
                            uint32_t *bucket) {
  int rc;
  if (!it || !out || !bucket) return DC_DMA_ARGUMENT;
  if (it->error) return it->error;
  if (it->bucket >= it->bucket_count) return 0;
  if (it->follower.current == it->base + (it->bucket + 1) * 16)
    ++it->bucket;
  if (it->bucket >= it->bucket_count) return 0;
  if (it->follower.ended) return it->error = DC_DMA_BUCKET_LAYOUT;
  rc = dc_dma_follower_next(&it->follower, out);
  if (rc < 0) return it->error = rc;
  *bucket = it->bucket;
  return rc;
}
