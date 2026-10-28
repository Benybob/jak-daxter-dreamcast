/* (AI-assisted), ISC license; see the repository LICENSE. */
#ifndef DC_DMA_CHAIN_H
#define DC_DMA_CHAIN_H
#include <stdint.h>

typedef struct {
  const uint8_t *base;
  uint32_t addr, size;
} dc_dma_view;

enum dc_dma_id {
  DC_DMA_REFE = 0, DC_DMA_CNT, DC_DMA_NEXT, DC_DMA_REF,
  DC_DMA_REFS, DC_DMA_CALL, DC_DMA_RET, DC_DMA_END
};
enum dc_dma_error {
  DC_DMA_ADDRESS = -1, DC_DMA_ALIGNMENT = -2, DC_DMA_STACK_OVERFLOW = -3,
  DC_DMA_STACK_EMPTY = -4, DC_DMA_UNKNOWN_ID = -5, DC_DMA_LIMIT = -6,
  DC_DMA_SCRATCHPAD = -7, DC_DMA_CNT_ADDRESS = -8,
  DC_DMA_ARGUMENT = -9, DC_DMA_BUCKET_LAYOUT = -10
};
enum { DC_DMA_DEFAULT_LIMIT = 65536 };

typedef struct {
  uint32_t tag_addr;
  uint64_t raw_tag;
  uint32_t id, qwc, vif0, vif1, data_addr, size_bytes;
  const uint8_t *data;
} dc_dma_transfer;

/* Caller-owned state; private fields, except limit may be set after init. */
typedef struct {
  dc_dma_view view;
  uint32_t current, stack[2], depth, count, limit;
  int ended, error;
} dc_dma_follower;

/* Init: 0 on success. Next: 1 transfer, 0 ended, negative sticky error.
 * On error, next leaves out unchanged. Reset by calling init again.
 * The view and its bytes must remain valid and unchanged while following.
 */
int dc_dma_follower_init(dc_dma_follower *, const dc_dma_view *, uint32_t start);
int dc_dma_follower_next(dc_dma_follower *, dc_dma_transfer *out);
/* All encoded three-bit IDs are valid; checks external decoded IDs too. */
int dc_dma_tag_id_check(uint32_t id);

/* Jak 1 only: init consumes CALL / CNT(qwc=10) / RET default-reg prologue.
 * Next returns transfers with zero-based bucket IDs, stopping at count slots.
 * It includes structural CALL/RET transfers; it does not interpret VIF/GIF.
 */
typedef struct {
  dc_dma_follower follower;
  uint32_t base, bucket, bucket_count;
  int error;
} dc_dma_bucket_iter;
int dc_dma_bucket_iter_init(dc_dma_bucket_iter *, const dc_dma_view *,
                            uint32_t start, uint32_t bucket_count);
int dc_dma_bucket_iter_next(dc_dma_bucket_iter *, dc_dma_transfer *,
                            uint32_t *bucket);
#endif
