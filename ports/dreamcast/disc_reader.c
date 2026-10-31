/* Fixed-storage, single-worker reader (AI-assisted), ISC. */
#include "disc_reader.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>

static int span(const void *p, uint32_t n) {
  return p && n && (uintptr_t)p <= UINTPTR_MAX - n;
}
static int overlap(const void *a, uint32_t an, const void *b, uint32_t bn) {
  return (uintptr_t)a < (uintptr_t)b + bn && (uintptr_t)b < (uintptr_t)a + an;
}
static dc_disc_request *lookup(dc_disc_reader *r, dc_disc_ticket t) {
  dc_disc_request *q;
  if (t.slot >= DC_DISC_REQUESTS || !t.generation) return NULL;
  q = &r->requests[t.slot];
  return q->state != DC_DISC_FREE && q->generation == t.generation ? q : NULL;
}
int dc_disc_reader_init(dc_disc_reader *r, const dc_disc_ops *ops, void *bounce,
                        uint32_t file_bytes) {
  if (!r || !ops || !ops->lock || !ops->unlock || !ops->wake || !ops->read ||
      !span(bounce, DC_DISC_BOUNCE_BYTES) || ((uintptr_t)bounce & 31u) ||
      overlap(r, sizeof(*r), bounce, DC_DISC_BOUNCE_BYTES) ||
      !file_bytes || file_bytes > INT32_MAX || file_bytes % DC_DISC_SECTOR) return 0;
  memset(r, 0, sizeof(*r));
  r->ops = *ops; r->bounce = bounce; r->file_bytes = file_bytes;
  return 1;
}
int dc_disc_reader_submit(dc_disc_reader *r, uint32_t offset, uint32_t length,
                          void *destination, uint32_t capacity, dc_disc_ticket *ticket) {
  uint32_t i, slot = DC_DISC_REQUESTS, padded;
  dc_disc_request *q;
  if (!r || !ticket || !span(destination, length) || length > capacity ||
      length > INT32_MAX || offset % DC_DISC_SECTOR ||
      overlap(destination, length, r->bounce, DC_DISC_BOUNCE_BYTES) ||
      overlap(destination, length, r, sizeof(*r))) return 0;
  padded = (length + DC_DISC_SECTOR - 1u) & ~(DC_DISC_SECTOR - 1u);
  if (offset > r->file_bytes || padded > r->file_bytes - offset) return 0;
  r->ops.lock(r->ops.context);
  if (r->closed) goto refuse;
  for (i = 0; i < DC_DISC_REQUESTS; ++i) {
    q = &r->requests[i];
    if (q->state == DC_DISC_FREE) {
      /* Retire a slot rather than permit a generation-wrap ABA. */
      if (q->generation != UINT32_MAX && slot == DC_DISC_REQUESTS) slot = i;
    } else if (overlap(destination, length, q->destination, q->length)) goto refuse;
  }
  if (slot == DC_DISC_REQUESTS) goto refuse;
  q = &r->requests[slot];
  ++q->generation; q->offset = offset; q->length = length;
  q->destination = destination; q->state = DC_DISC_QUEUED;
  r->queue[(r->head + r->count) % DC_DISC_REQUESTS] = slot;
  ++r->count;
  *ticket = (dc_disc_ticket){slot, q->generation};
  r->ops.unlock(r->ops.context);
  r->ops.wake(r->ops.context);
  return 1;
refuse:
  r->ops.unlock(r->ops.context);
  return 0;
}
dc_disc_state dc_disc_reader_poll(dc_disc_reader *r, dc_disc_ticket t) {
  dc_disc_request *q; dc_disc_state state;
  if (!r) return DC_DISC_INVALID;
  r->ops.lock(r->ops.context); q = lookup(r, t);
  state = q ? q->state : DC_DISC_INVALID;
  r->ops.unlock(r->ops.context); return state;
}
int dc_disc_reader_release(dc_disc_reader *r, dc_disc_ticket t) {
  dc_disc_request *q; int ok = 0;
  if (!r) return 0;
  r->ops.lock(r->ops.context); q = lookup(r, t);
  if (q && (q->state == DC_DISC_READY || q->state == DC_DISC_FAILED)) {
    q->state = DC_DISC_FREE; q->destination = NULL; ok = 1;
  }
  r->ops.unlock(r->ops.context); return ok;
}
int dc_disc_reader_work(dc_disc_reader *r) {
  dc_disc_request *q; uint32_t done = 0; int ok = 1;
  r->ops.lock(r->ops.context);
  if (!r->count) { r->ops.unlock(r->ops.context); return 0; }
  q = &r->requests[r->queue[r->head]];
  r->head = (r->head + 1u) % DC_DISC_REQUESTS; --r->count;
  q->state = DC_DISC_READING;
  r->ops.unlock(r->ops.context);
  while (done < q->length) {
    uint32_t copy = q->length - done, read_bytes;
    if (copy > DC_DISC_BOUNCE_BYTES) copy = DC_DISC_BOUNCE_BYTES;
    read_bytes = (copy + DC_DISC_SECTOR - 1u) & ~(DC_DISC_SECTOR - 1u);
    if (r->ops.read(r->ops.context, q->offset + done, r->bounce, read_bytes) != (int)read_bytes) {
      ok = 0; break;
    }
    memcpy(q->destination + done, r->bounce, copy); done += copy;
  }
  r->ops.lock(r->ops.context); q->state = ok ? DC_DISC_READY : DC_DISC_FAILED;
  r->ops.unlock(r->ops.context); return 1;
}
void dc_disc_reader_close(dc_disc_reader *r) {
  r->ops.lock(r->ops.context); r->closed = 1; r->ops.unlock(r->ops.context);
  r->ops.wake(r->ops.context);
}
int dc_disc_reader_drained(dc_disc_reader *r) {
  uint32_t i; int drained;
  r->ops.lock(r->ops.context); drained = r->closed && !r->count;
  for (i = 0; i < DC_DISC_REQUESTS; ++i)
    if (r->requests[i].state == DC_DISC_READING) drained = 0;
  r->ops.unlock(r->ops.context); return drained;
}
