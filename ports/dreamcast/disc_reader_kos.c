/* Optional KOS adapter; no existing ELF links this (AI-assisted), ISC. */
#include "disc_reader_kos.h"
#include <fcntl.h>
#include <limits.h>
#include <malloc.h>
#include <stdlib.h>

static void lock(void *p) { mutex_lock(&((dc_disc_reader_kos *)p)->mutex); }
static void unlock(void *p) { mutex_unlock(&((dc_disc_reader_kos *)p)->mutex); }
static void wake(void *p) { sem_signal(&((dc_disc_reader_kos *)p)->wake); }
static int read_chunk(void *p, uint32_t offset, void *buffer, uint32_t bytes) {
  dc_disc_reader_kos *r = p;
  if (fs_seek(r->file, (off_t)offset, SEEK_SET) != (off_t)offset) return -1;
  return (int)fs_read(r->file, buffer, bytes);
}
static void *worker(void *p) {
  dc_disc_reader_kos *r = p;
  for (;;) {
    sem_wait(&r->wake);
    while (dc_disc_reader_work(&r->reader)) { }
    if (dc_disc_reader_drained(&r->reader)) return NULL;
  }
}
int dc_disc_reader_kos_open(dc_disc_reader_kos *r, const char *path) {
  uint64_t bytes;
  kthread_attr_t attr = { .create_detached = false, .stack_size = 8192,
                         .prio = PRIO_DEFAULT + 1, .label = "level reader" };
  dc_disc_ops ops = {lock, unlock, wake, read_chunk, r};
  if (!r || !path || r->thread || r->bounce) return 0;
  r->file = fs_open(path, O_RDONLY);
  if (r->file < 0) return 0;
  bytes = fs_total64(r->file);
  if (!bytes || bytes > INT32_MAX || bytes % DC_DISC_SECTOR) goto file_fail;
  r->bounce = memalign(32, DC_DISC_BOUNCE_BYTES);
  if (!r->bounce) goto file_fail;
  if (mutex_init(&r->mutex, MUTEX_TYPE_NORMAL)) goto bounce_fail;
  if (sem_init(&r->wake, 0)) goto mutex_fail;
  if (!dc_disc_reader_init(&r->reader, &ops, r->bounce, (uint32_t)bytes)) goto sem_fail;
  r->thread = thd_create_ex(&attr, worker, r);
  if (r->thread) return 1;
sem_fail:
  sem_destroy(&r->wake);
mutex_fail:
  mutex_destroy(&r->mutex);
bounce_fail:
  free(r->bounce); r->bounce = NULL;
file_fail:
  fs_close(r->file); r->file = -1; return 0;
}
void dc_disc_reader_kos_close(dc_disc_reader_kos *r) {
  if (!r || !r->thread) return;
  dc_disc_reader_close(&r->reader); thd_join(r->thread, NULL); r->thread = NULL;
  fs_close(r->file); r->file = -1;
  sem_destroy(&r->wake); mutex_destroy(&r->mutex);
  free(r->bounce); r->bounce = NULL;
}
