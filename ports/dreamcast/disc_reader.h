/* Fixed-storage, single-worker reader (AI-assisted), ISC. */
#ifndef DC_DISC_READER_H
#define DC_DISC_READER_H
#include <stdint.h>

#define DC_DISC_SECTOR 2048u
#define DC_DISC_BOUNCE_BYTES 32768u
#define DC_DISC_REQUESTS 8u

typedef enum {
  DC_DISC_INVALID = -1, DC_DISC_FREE, DC_DISC_QUEUED, DC_DISC_READING,
  DC_DISC_READY, DC_DISC_FAILED
} dc_disc_state;
typedef struct { uint32_t slot, generation; } dc_disc_ticket;
typedef struct {
  /* lock/unlock must provide mutex acquire/release ordering. No IO under lock.
   * wake must retain notifications (e.g. a semaphore); it must not run work().
   * read runs ONLY on the worker, returns bytes read or a negative error.
   * It must finish all DMA before returning; short reads fail the request. */
  void (*lock)(void *);
  void (*unlock)(void *);
  void (*wake)(void *);
  int (*read)(void *, uint32_t offset, void *aligned, uint32_t bytes);
  void *context;
} dc_disc_ops;
typedef struct {
  dc_disc_state state;
  uint32_t generation, offset, length;
  unsigned char *destination;
} dc_disc_request;
typedef struct {
  dc_disc_ops ops;
  unsigned char *bounce;
  uint32_t file_bytes, head, count, closed;
  uint32_t queue[DC_DISC_REQUESTS];
  dc_disc_request requests[DC_DISC_REQUESTS];
} dc_disc_reader;

/* Init before either thread exists; never reinitialize a live reader. No heap
 * operations anywhere in this core. Caller supplies one 32-byte-aligned 32 KiB
 * buffer outside the renderer slab. File size is sector padded, <= INT32_MAX.
 * Exactly one producer/frame thread and one worker. Metadata locks are short,
 * but can briefly wait for each other; these are not lock-free frame APIs. */
int dc_disc_reader_init(dc_disc_reader *, const dc_disc_ops *, void *bounce,
                        uint32_t file_bytes);
/* offset must be sector aligned; length is exact payload length, nonzero.
 * Each read rounds UP to whole sectors, but copies only length bytes.
 * No overlap with bounce, reader, or any live destination is accepted.
 * Caller owns/pins destination until READY/FAILED and successful release.
 * Do not read destination while QUEUED/READING, including after partial copies.
 * Failure leaves ticket unchanged. Full queue returns 0 immediately. */
int dc_disc_reader_submit(dc_disc_reader *, uint32_t offset, uint32_t length,
                          void *destination, uint32_t capacity, dc_disc_ticket *);
dc_disc_state dc_disc_reader_poll(dc_disc_reader *, dc_disc_ticket);
int dc_disc_reader_release(dc_disc_reader *, dc_disc_ticket);
/* Worker only: processes one whole request, may block in read. Returns 1 for
 * a processed request (even FAILED), 0 for no queued work. NEVER call on frame.
 * Poll publishes READY only after all copies, through the metadata mutex. */
int dc_disc_reader_work(dc_disc_reader *);
/* Close rejects submits but drains queued reads. Wake worker, then join it
 * before freeing ANY storage/closing file. Closing/joining is a level teardown
 * operation, never a retarget. No cancellation or eviction of pending slots. */
void dc_disc_reader_close(dc_disc_reader *);
int dc_disc_reader_drained(dc_disc_reader *);
#endif
