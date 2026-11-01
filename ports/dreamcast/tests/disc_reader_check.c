/* Deterministic late IO on a real host worker thread (AI-assisted), ISC. */
#include "disc_reader.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Fail unattended on Windows too: libc abort may wait in a system dialog. */
#undef assert
#define assert(expr) do { if (!(expr)) { \
  fprintf(stderr, "Assertion failed: %s (%s:%d)\n", #expr, __FILE__, __LINE__); \
  exit(EXIT_FAILURE); } } while (0)

typedef struct {
  dc_disc_reader reader;
  pthread_mutex_t metadata, event;
  pthread_cond_t changed;
  pthread_t worker, frame;
  unsigned signals, calls, completed, permits, unlimited, fail_call;
  int fail_result;
  uint32_t offsets[64], sizes[64];
  unsigned char bounce[DC_DISC_BOUNCE_BYTES] __attribute__((aligned(32)));
} fixture;
static void lock(void *p) { assert(!pthread_mutex_lock(&((fixture *)p)->metadata)); }
static void unlock(void *p) { assert(!pthread_mutex_unlock(&((fixture *)p)->metadata)); }
static void wake(void *p) {
  fixture *f = p;
  assert(!pthread_mutex_lock(&f->event)); ++f->signals;
  assert(!pthread_cond_broadcast(&f->changed)); assert(!pthread_mutex_unlock(&f->event));
}
static unsigned char pattern(uint32_t off) { return (unsigned char)(off*17u + off/251u); }
static int read_chunk(void *p, uint32_t offset, void *buffer, uint32_t bytes) {
  fixture *f = p; unsigned call; int result; uint32_t i;
  assert(!pthread_equal(pthread_self(), f->frame));
  assert(!((uintptr_t)buffer & 31u) && buffer == f->bounce);
  assert(!(offset % DC_DISC_SECTOR) && !(bytes % DC_DISC_SECTOR));
  assert(bytes && bytes <= DC_DISC_BOUNCE_BYTES);
  /* The worker must not hold the metadata lock while blocked in read. */
  assert(!pthread_mutex_lock(&f->metadata)); assert(!pthread_mutex_unlock(&f->metadata));
  assert(!pthread_mutex_lock(&f->event));
  call = ++f->calls; assert(call <= 64);
  f->offsets[call-1] = offset; f->sizes[call-1] = bytes;
  assert(!pthread_cond_broadcast(&f->changed));
  while (!f->permits && !f->unlimited) assert(!pthread_cond_wait(&f->changed, &f->event));
  if (!f->unlimited) --f->permits;
  result = call == f->fail_call ? f->fail_result : (int)bytes;
  assert(!pthread_mutex_unlock(&f->event));
  for (i = 0; i < bytes; ++i) ((unsigned char *)buffer)[i] = pattern(offset+i);
  return result;
}
static void *worker(void *p) {
  fixture *f = p;
  for (;;) {
    assert(!pthread_mutex_lock(&f->event));
    while (!f->signals) assert(!pthread_cond_wait(&f->changed, &f->event));
    --f->signals; assert(!pthread_mutex_unlock(&f->event));
    while (dc_disc_reader_work(&f->reader)) {
      assert(!pthread_mutex_lock(&f->event)); ++f->completed;
      assert(!pthread_cond_broadcast(&f->changed)); assert(!pthread_mutex_unlock(&f->event));
    }
    if (dc_disc_reader_drained(&f->reader)) return NULL;
  }
}
static void init(fixture *f) {
  dc_disc_ops ops = {lock, unlock, wake, read_chunk, f};
  memset(f, 0, sizeof(*f)); f->frame = pthread_self();
  assert(!pthread_mutex_init(&f->metadata, NULL));
  assert(!pthread_mutex_init(&f->event, NULL)); assert(!pthread_cond_init(&f->changed, NULL));
  assert(dc_disc_reader_init(&f->reader, &ops, f->bounce, 262144));
}
static void start(fixture *f) { assert(!pthread_create(&f->worker, NULL, worker, f)); }
static void wait_calls(fixture *f, unsigned count) {
  assert(!pthread_mutex_lock(&f->event));
  while (f->calls < count) assert(!pthread_cond_wait(&f->changed, &f->event));
  assert(!pthread_mutex_unlock(&f->event));
}
static void allow(fixture *f, int all) {
  assert(!pthread_mutex_lock(&f->event));
  if (all) f->unlimited = 1; else ++f->permits;
  assert(!pthread_cond_broadcast(&f->changed)); assert(!pthread_mutex_unlock(&f->event));
}
static void finish(fixture *f) {
  dc_disc_reader_close(&f->reader); allow(f, 1);
  assert(!pthread_join(f->worker, NULL)); assert(dc_disc_reader_drained(&f->reader));
}
static void cleanup(fixture *f) {
  assert(!pthread_cond_destroy(&f->changed));
  assert(!pthread_mutex_destroy(&f->event)); assert(!pthread_mutex_destroy(&f->metadata));
}
/* Minimal frame consumer: publish only a complete ticket, never a partial or
 * failed destination. This is a host model, not a renderer integration. */
static int select_window(fixture *f, dc_disc_ticket t, const unsigned char *candidate,
                         const unsigned char **displayed) {
  if (dc_disc_reader_poll(&f->reader, t) != DC_DISC_READY) return 0;
  *displayed = candidate;
  return 1;
}
static dc_disc_ticket complete_previous(fixture *f, unsigned char previous[16]) {
  dc_disc_ticket t;
  assert(dc_disc_reader_submit(&f->reader, 131072, 16, previous, 16, &t));
  start(f); allow(f, 0);
  assert(!pthread_mutex_lock(&f->event));
  while (f->completed < 1) assert(!pthread_cond_wait(&f->changed, &f->event));
  assert(!pthread_mutex_unlock(&f->event));
  assert(dc_disc_reader_poll(&f->reader, t) == DC_DISC_READY);
  return t; /* Keep the completed ticket live while the next window loads. */
}
static void previous_matches(const unsigned char *displayed, const unsigned char *previous) {
  unsigned i;
  assert(displayed == previous);
  for (i = 0; i < 16; ++i) assert(displayed[i] == pattern(131072+i));
}
static void late(void) {
  fixture f; unsigned char destination[70002], previous[16];
  const unsigned char *displayed = NULL;
  dc_disc_ticket ticket, old; unsigned frame; uint32_t i;
  init(&f); memset(destination, 0xa5, sizeof(destination));
  old = complete_previous(&f, previous);
  assert(select_window(&f, old, previous, &displayed));
  assert(dc_disc_reader_submit(&f.reader, 2048, 70000, destination+1, 70000, &ticket));
  assert(dc_disc_reader_poll(&f.reader, ticket) == DC_DISC_QUEUED);
  assert(!dc_disc_reader_release(&f.reader, ticket)); wait_calls(&f, 2);
  {
    dc_disc_ticket refused = {77, 88};
    assert(!dc_disc_reader_submit(&f.reader, 0, 1, destination+2, 1, &refused));
    assert(refused.slot == 77 && refused.generation == 88);
  }
  /* Simulated frame submission, not PVR or a real renderer integration. */
  for (frame = 0; frame < 64; ++frame) {
    assert(dc_disc_reader_poll(&f.reader, ticket) == DC_DISC_READING);
    assert(!dc_disc_reader_release(&f.reader, ticket));
    assert(!select_window(&f, ticket, destination+1, &displayed));
    previous_matches(displayed, previous);
  }
  allow(&f, 0); wait_calls(&f, 3);
  assert(dc_disc_reader_poll(&f.reader, ticket) == DC_DISC_READING);
  assert(!select_window(&f, ticket, destination+1, &displayed));
  previous_matches(displayed, previous);
  allow(&f, 0); wait_calls(&f, 4);
  assert(dc_disc_reader_poll(&f.reader, ticket) == DC_DISC_READING);
  assert(!select_window(&f, ticket, destination+1, &displayed));
  previous_matches(displayed, previous);
  finish(&f); /* join is teardown; it is not part of any simulated frame */
  assert(dc_disc_reader_poll(&f.reader, ticket) == DC_DISC_READY);
  for (i = 0; i < 70000; ++i) assert(destination[i+1] == pattern(2048+i));
  assert(destination[0] == 0xa5 && destination[70001] == 0xa5);
  assert(select_window(&f, ticket, destination+1, &displayed) && displayed == destination+1);
  assert(f.calls == 4 && f.sizes[1] == 32768 && f.sizes[2] == 32768 && f.sizes[3] == 6144);
  assert(f.offsets[1] == 2048 && f.offsets[2] == 34816 && f.offsets[3] == 67584);
  assert(dc_disc_reader_release(&f.reader, old));
  assert(dc_disc_reader_release(&f.reader, ticket));
  assert(dc_disc_reader_poll(&f.reader, ticket) == DC_DISC_INVALID);
  cleanup(&f);
  puts("late: 64 simulated frames, three aligned reads, exact bytes, canaries");
}
static void queue(void) {
  fixture f; unsigned char destination[DC_DISC_REQUESTS+1][2050];
  dc_disc_ticket tickets[DC_DISC_REQUESTS], extra = {77, 88}, stale;
  unsigned i, j;
  init(&f); memset(destination, 0xa5, sizeof destination);
  for (i = 0; i < DC_DISC_REQUESTS; ++i) {
    assert(dc_disc_reader_submit(&f.reader, i*2048, 2048, destination[i]+1, 2048, &tickets[i]));
    if (!i) assert(!dc_disc_reader_submit(&f.reader, 0, 1, destination[0]+2, 1, &extra));
  }
  assert(!dc_disc_reader_submit(&f.reader, 0, 2048, destination[8], 2048, &extra));
  assert(extra.slot == 77 && extra.generation == 88);
  assert(!dc_disc_reader_submit(&f.reader, 0, 1, destination[0]+1, 1, &extra));
  start(&f); allow(&f, 1);
  assert(!pthread_mutex_lock(&f.event));
  while (f.completed < DC_DISC_REQUESTS) assert(!pthread_cond_wait(&f.changed, &f.event));
  assert(!pthread_mutex_unlock(&f.event));
  for (i = 0; i < DC_DISC_REQUESTS; ++i) {
    assert(dc_disc_reader_poll(&f.reader, tickets[i]) == DC_DISC_READY);
    assert(f.offsets[i] == i*2048);
    for (j = 0; j < 2048; ++j) assert(destination[i][j+1] == pattern(i*2048+j));
    assert(destination[i][0] == 0xa5 && destination[i][2049] == 0xa5);
    assert(dc_disc_reader_release(&f.reader, tickets[i]));
  }
  stale = tickets[0];
  assert(!dc_disc_reader_release(&f.reader, stale));
  assert(dc_disc_reader_submit(&f.reader, 0, 1, destination[0], 1, &extra));
  assert(extra.slot == stale.slot && extra.generation != stale.generation);
  assert(dc_disc_reader_poll(&f.reader, stale) == DC_DISC_INVALID);
  assert(!dc_disc_reader_release(&f.reader, stale));
  finish(&f);
  assert(!dc_disc_reader_submit(&f.reader, 0, 1, destination[0], 1, &extra));
  assert(f.calls == DC_DISC_REQUESTS+1);
  cleanup(&f);
  puts("queue: bounded FIFO, overlap, full, drain, close refusal, stale release");
}
static void errors(void) {
  int mode;
  for (mode = 0; mode < 3; ++mode) {
    fixture f; unsigned char destination[40002], previous[16];
    const unsigned char *displayed = NULL; uint32_t i; dc_disc_ticket t, old;
    init(&f); old = complete_previous(&f, previous);
    assert(select_window(&f, old, previous, &displayed));
    f.fail_call = 3; f.fail_result = mode == 0 ? -1 : (mode == 1 ? 1 : 32769);
    memset(destination, 0xac, sizeof(destination));
    assert(dc_disc_reader_submit(&f.reader, 0, 40000, destination+1, 40000, &t));
    finish(&f);
    assert(dc_disc_reader_poll(&f.reader, t) == DC_DISC_FAILED);
    for (i = 0; i < 32768; ++i) assert(destination[i+1] == pattern(i));
    for (; i < 40000; ++i) assert(destination[i+1] == 0xac);
    assert(destination[0] == 0xac && destination[40001] == 0xac);
    assert(!select_window(&f, t, destination+1, &displayed));
    previous_matches(displayed, previous);
    assert(dc_disc_reader_release(&f.reader, t));
    assert(dc_disc_reader_release(&f.reader, old));
    cleanup(&f);
  }
  puts("errors: negative, short and oversized read results never publish READY");
}
static void invalid(void) {
  fixture f; unsigned char destination[4096]; dc_disc_ticket t = {77, 88}; dc_disc_ops bad;
  init(&f); bad = f.reader.ops; bad.read = NULL;
  assert(!dc_disc_reader_init(&f.reader, &bad, f.bounce, 262144));
  assert(!dc_disc_reader_init(&f.reader, &f.reader.ops, f.bounce+1, 262144));
  assert(!dc_disc_reader_init(&f.reader, &f.reader.ops, f.bounce, UINT32_MAX));
  assert(!dc_disc_reader_submit(&f.reader, 1, 1, destination, sizeof(destination), &t));
  assert(!dc_disc_reader_submit(&f.reader, 0, 0, destination, sizeof(destination), &t));
  assert(!dc_disc_reader_submit(&f.reader, 0, 4097, destination, sizeof(destination), &t));
  assert(!dc_disc_reader_submit(&f.reader, 262144, 1, destination, sizeof(destination), &t));
  assert(!dc_disc_reader_submit(&f.reader, 0, UINT32_MAX, destination, UINT32_MAX, &t));
  assert(!dc_disc_reader_submit(&f.reader, 0, 1, NULL, 1, &t));
  assert(!dc_disc_reader_submit(&f.reader, 0, 1, f.bounce, 1, &t));
  assert(!dc_disc_reader_submit(&f.reader, 0, 1, &f.reader, 1, &t));
  assert(!dc_disc_reader_submit(&f.reader, 0, 32, (void *)(UINTPTR_MAX-16u), 32, &t));
  assert(t.slot == 77 && t.generation == 88);
  assert(dc_disc_reader_poll(&f.reader, t) == DC_DISC_INVALID);
  /* ABA rejection and generation exhaustion on an otherwise free slot. */
  f.reader.requests[0].generation = UINT32_MAX;
  assert(dc_disc_reader_submit(&f.reader, 0, 1, destination, sizeof(destination), &t));
  assert(t.slot == 1); start(&f); finish(&f);
  cleanup(&f);
  puts("invalid: spans, alignment, lengths, bounds, ticket atomicity, generation exhaustion");
}
int main(int argc, char **argv) {
  int group = argc == 2 ? atoi(argv[1]) : -1;
  if (group == 0) late(); else if (group == 1) queue();
  else if (group == 2) errors(); else if (group == 3) invalid(); else return 2;
  return 0;
}
