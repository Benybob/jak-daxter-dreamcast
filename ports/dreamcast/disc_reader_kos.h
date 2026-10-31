/* Optional KOS adapter; no existing ELF links this (AI-assisted), ISC. */
#ifndef DC_DISC_READER_KOS_H
#define DC_DISC_READER_KOS_H
#include "disc_reader.h"
#include <kos/fs.h>
#include <kos/mutex.h>
#include <kos/sem.h>
#include <kos/thread.h>
typedef struct {
  dc_disc_reader reader;
  mutex_t mutex;
  semaphore_t wake;
  file_t file;
  kthread_t *thread;
  void *bounce;
} dc_disc_reader_kos;
/* Boot/level entry ONLY. Zeroed, unowned output. Open one level file, allocate
 * bounce on KOS heap before renderer slab, launch a lower-priority worker.
 * Failure cleans up. Caller uses core submit/poll/release on .reader. */
int dc_disc_reader_kos_open(dc_disc_reader_kos *, const char *path);
/* Level teardown ONLY: drain/join before closing/freeing. May wait for drive.
 * No producer API calls concurrently with open/close. */
void dc_disc_reader_kos_close(dc_disc_reader_kos *);
#endif
