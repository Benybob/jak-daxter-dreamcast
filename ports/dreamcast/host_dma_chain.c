/* (AI-assisted), ISC license; see the repository LICENSE. */
#include "dma_chain.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
  fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
enum { BASE = 0x1000, CAPACITY = 1024 };

static void put32(uint8_t *p, uint32_t value) {
  unsigned i;
  for (i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (i * 8));
}
static uint64_t tag(uint8_t *mem, uint32_t offset, uint32_t id,
                    uint32_t qwc, uint32_t addr) {
  uint64_t raw = (uint64_t)addr << 32 | (uint64_t)id << 28 | qwc;
  put32(mem + offset, (uint32_t)raw);
  put32(mem + offset + 4, (uint32_t)(raw >> 32));
  put32(mem + offset + 8, UINT32_C(0x01020304));
  put32(mem + offset + 12, UINT32_C(0x50607080));
  return raw;
}
static int transfer(dc_dma_follower *f, uint8_t *mem, uint32_t offset,
                     uint32_t id, uint32_t qwc, uint32_t data_offset) {
  dc_dma_transfer t;
  uint64_t raw = 0;
  unsigned i;
  for (i = 0; i < 8; ++i) raw |= (uint64_t)mem[offset + i] << (i * 8);
  CHECK(dc_dma_follower_next(f, &t) == 1);
  CHECK(t.tag_addr == BASE + offset && t.raw_tag == raw);
  CHECK(t.id == id && t.qwc == qwc && t.size_bytes == qwc * 16);
  CHECK(t.vif0 == UINT32_C(0x01020304));
  CHECK(t.vif1 == UINT32_C(0x50607080));
  CHECK(t.data_addr == BASE + data_offset && t.data == mem + data_offset);
  CHECK(!memcmp(t.data, mem + data_offset, t.size_bytes));
  return 0;
}
static int chains(void) {
  uint8_t mem[CAPACITY];
  dc_dma_view view = {mem, BASE, sizeof(mem)};
  dc_dma_follower f;
  dc_dma_transfer t;
  memset(mem, 0xa5, sizeof(mem));
  tag(mem, 0, DC_DMA_CNT, 1, 0);
  tag(mem, 32, DC_DMA_NEXT, 1, BASE + 96);
  tag(mem, 96, DC_DMA_REF, 1, BASE + 512);
  tag(mem, 112, DC_DMA_REFS, 1, BASE + 528);
  tag(mem, 128, DC_DMA_END, 1, 0);
  CHECK(dc_dma_follower_init(&f, &view, BASE) == 0);
  CHECK(!transfer(&f, mem, 0, DC_DMA_CNT, 1, 16));
  CHECK(!transfer(&f, mem, 32, DC_DMA_NEXT, 1, 48));
  CHECK(!transfer(&f, mem, 96, DC_DMA_REF, 1, 512));
  CHECK(!transfer(&f, mem, 112, DC_DMA_REFS, 1, 528));
  CHECK(!transfer(&f, mem, 128, DC_DMA_END, 1, 144));
  CHECK(dc_dma_follower_next(&f, &t) == 0);
  CHECK(dc_dma_follower_next(&f, &t) == 0);
  tag(mem, 0, DC_DMA_REFE, 1, BASE + 512);
  CHECK(dc_dma_follower_init(&f, &view, BASE) == 0);
  CHECK(!transfer(&f, mem, 0, DC_DMA_REFE, 1, 512));
  CHECK(dc_dma_follower_next(&f, &t) == 0);
  /* CALL's return address skips its inline data, as does RET's data. */
  tag(mem, 0, DC_DMA_CALL, 1, BASE + 128);
  tag(mem, 32, DC_DMA_END, 0, 0);
  tag(mem, 128, DC_DMA_RET, 1, 0);
  CHECK(dc_dma_follower_init(&f, &view, BASE) == 0);
  CHECK(!transfer(&f, mem, 0, DC_DMA_CALL, 1, 16));
  CHECK(!transfer(&f, mem, 128, DC_DMA_RET, 1, 144));
  CHECK(!transfer(&f, mem, 32, DC_DMA_END, 0, 48));
  CHECK(dc_dma_follower_next(&f, &t) == 0);
  tag(mem, 128, DC_DMA_CALL, 1, BASE + 256);
  tag(mem, 160, DC_DMA_RET, 0, 0);
  tag(mem, 256, DC_DMA_RET, 1, 0);
  CHECK(dc_dma_follower_init(&f, &view, BASE) == 0);
  CHECK(!transfer(&f, mem, 0, DC_DMA_CALL, 1, 16));
  CHECK(!transfer(&f, mem, 128, DC_DMA_CALL, 1, 144));
  CHECK(f.depth == 2);
  CHECK(!transfer(&f, mem, 256, DC_DMA_RET, 1, 272));
  CHECK(!transfer(&f, mem, 160, DC_DMA_RET, 0, 176));
  CHECK(!transfer(&f, mem, 32, DC_DMA_END, 0, 48));
  CHECK(dc_dma_follower_next(&f, &t) == 0);
  return 0;
}
static int errors(void) {
  uint8_t mem[CAPACITY];
  dc_dma_view view = {mem, BASE, sizeof(mem)};
  dc_dma_follower f;
  dc_dma_transfer t, before;
  uint32_t id;
  memset(mem, 0, sizeof(mem));
  memset(&t, 0x5a, sizeof(t));
  memcpy(&before, &t, sizeof(t));
  CHECK(dc_dma_follower_init(&f, &view, BASE - 16) == DC_DMA_ADDRESS);
  CHECK(dc_dma_follower_init(&f, &view, BASE + CAPACITY) == DC_DMA_ADDRESS);
  CHECK(dc_dma_follower_init(&f, &view, BASE + 1) == DC_DMA_ALIGNMENT);
  tag(mem, 0, DC_DMA_REF, 2, BASE + CAPACITY - 16);
  CHECK(dc_dma_follower_init(&f, &view, BASE) == 0);
  CHECK(dc_dma_follower_next(&f, &t) == DC_DMA_ADDRESS);
  CHECK(!memcmp(&before, &t, sizeof(t)));
  CHECK(dc_dma_follower_next(&f, &t) == DC_DMA_ADDRESS);
  tag(mem, 0, DC_DMA_REF, 1, BASE + 1);
  CHECK(dc_dma_follower_init(&f, &view, BASE) == 0);
  CHECK(dc_dma_follower_next(&f, &t) == DC_DMA_ALIGNMENT);
  tag(mem, 0, DC_DMA_NEXT, 0, BASE + CAPACITY);
  CHECK(dc_dma_follower_init(&f, &view, BASE) == 0);
  CHECK(dc_dma_follower_next(&f, &t) == 1);
  CHECK(dc_dma_follower_next(&f, &t) == DC_DMA_ADDRESS);
  tag(mem, 0, DC_DMA_NEXT, 0, BASE + 1);
  CHECK(dc_dma_follower_init(&f, &view, BASE) == 0);
  CHECK(dc_dma_follower_next(&f, &t) == 1);
  CHECK(dc_dma_follower_next(&f, &t) == DC_DMA_ALIGNMENT);
  tag(mem, 0, DC_DMA_CALL, 0, BASE + 16);
  tag(mem, 16, DC_DMA_CALL, 0, BASE + 32);
  tag(mem, 32, DC_DMA_CALL, 0, BASE + 48);
  CHECK(dc_dma_follower_init(&f, &view, BASE) == 0);
  CHECK(dc_dma_follower_next(&f, &t) == 1);
  CHECK(dc_dma_follower_next(&f, &t) == 1);
  CHECK(dc_dma_follower_next(&f, &t) == DC_DMA_STACK_OVERFLOW);
  tag(mem, 0, DC_DMA_RET, 0, 0);
  CHECK(dc_dma_follower_init(&f, &view, BASE) == 0);
  CHECK(dc_dma_follower_next(&f, &t) == DC_DMA_STACK_EMPTY);
  /* Bits 28..30 exhaust the eight legal IDs. IRQ is not a fourth ID bit. */
  for (id = 0; id < 8; ++id) CHECK(dc_dma_tag_id_check(id) == 0);
  CHECK(dc_dma_tag_id_check(8) == DC_DMA_UNKNOWN_ID);
  tag(mem, 0, DC_DMA_NEXT, 0, BASE);
  CHECK(dc_dma_follower_init(&f, &view, BASE) == 0);
  f.limit = 2;
  CHECK(dc_dma_follower_next(&f, &t) == 1);
  CHECK(dc_dma_follower_next(&f, &t) == 1);
  CHECK(dc_dma_follower_next(&f, &t) == DC_DMA_LIMIT);
  tag(mem, 0, DC_DMA_END, 0, 0);
  mem[7] |= 0x80;
  CHECK(dc_dma_follower_init(&f, &view, BASE) == 0);
  CHECK(dc_dma_follower_next(&f, &t) == DC_DMA_SCRATCHPAD);
  tag(mem, 0, DC_DMA_CNT, 0, BASE);
  CHECK(dc_dma_follower_init(&f, &view, BASE) == 0);
  CHECK(dc_dma_follower_next(&f, &t) == DC_DMA_CNT_ADDRESS);
  CHECK(dc_dma_follower_init(NULL, &view, BASE) == DC_DMA_ARGUMENT);
  CHECK(dc_dma_follower_init(&f, NULL, BASE) == DC_DMA_ARGUMENT);
  CHECK(dc_dma_follower_next(NULL, &t) == DC_DMA_ARGUMENT);
  CHECK(dc_dma_follower_init(&f, &view, BASE) == 0);
  CHECK(dc_dma_follower_next(&f, NULL) == DC_DMA_ARGUMENT);
  view.addr = UINT32_C(0xfffffff0);
  CHECK(dc_dma_follower_init(&f, &view, view.addr) == DC_DMA_ADDRESS);
  view.addr = BASE; view.size = 15;
  CHECK(dc_dma_follower_init(&f, &view, BASE) == DC_DMA_ADDRESS);
  view.size = CAPACITY;
  /* Zero-byte END may point one-past the view; IRQ/PCE bits are preserved. */
  tag(mem, CAPACITY - 16, DC_DMA_END, 0, 0);
  mem[CAPACITY - 13] |= 0x8c;
  CHECK(dc_dma_follower_init(&f, &view, BASE + CAPACITY - 16) == 0);
  CHECK(!transfer(&f, mem, CAPACITY - 16, DC_DMA_END, 0, CAPACITY));
  CHECK(dc_dma_follower_next(&f, &t) == 0);
  return 0;
}
static int buckets(void) {
  uint8_t mem[CAPACITY];
  dc_dma_view view = {mem, BASE, sizeof(mem)};
  dc_dma_bucket_iter it;
  dc_dma_transfer t;
  uint32_t bucket;
  memset(mem, 0, sizeof(mem));
  tag(mem, 0, DC_DMA_CALL, 0, BASE + 128);
  put32(mem + 8, 0); put32(mem + 12, 0);
  tag(mem, 128, DC_DMA_CNT, 10, 0);
  tag(mem, 304, DC_DMA_RET, 0, 0);
  tag(mem, 16, DC_DMA_CALL, 0, BASE + 400);
  tag(mem, 400, DC_DMA_RET, 0, 0);
  tag(mem, 32, DC_DMA_CNT, 0, 0);
  CHECK(dc_dma_bucket_iter_init(&it, &view, BASE, 2) == 0);
  CHECK(dc_dma_bucket_iter_next(&it, &t, &bucket) == 1);
  CHECK(bucket == 0 && t.tag_addr == BASE + 16 && t.id == DC_DMA_CALL);
  CHECK(dc_dma_bucket_iter_next(&it, &t, &bucket) == 1);
  CHECK(bucket == 0 && t.tag_addr == BASE + 400 && t.id == DC_DMA_RET);
  CHECK(dc_dma_bucket_iter_next(&it, &t, &bucket) == 1);
  CHECK(bucket == 1 && t.tag_addr == BASE + 32 && t.id == DC_DMA_CNT);
  CHECK(dc_dma_bucket_iter_next(&it, &t, &bucket) == 0);
  CHECK(it.follower.current == BASE + 48);
  tag(mem, 0, DC_DMA_CNT, 0, 0);
  CHECK(dc_dma_bucket_iter_init(&it, &view, BASE, 2) == DC_DMA_BUCKET_LAYOUT);
  CHECK(dc_dma_bucket_iter_next(&it, &t, &bucket) == DC_DMA_BUCKET_LAYOUT);
  return 0;
}
int main(void) {
  if (chains() || errors() || buckets()) return 1;
  puts("dma_chain: chains, fields, stack, refusals and buckets passed");
  return 0;
}
