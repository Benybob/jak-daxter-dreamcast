/* Disc files read ahead on a thread for the level draw (AI-assisted), ISC. level_prefetch.c */
#ifndef DC_LEVEL_PREFETCH_H
#define DC_LEVEL_PREFETCH_H
#include <stdint.h>
/* requested: files queued; staged: read whole; taken: opened from memory;
 * missed: opened from the disc (not asked for, or not begun); waited: 1 ms
 * waits for a read in progress; dropped: too big or over the budget; stale:
 * staged and not opened or asked for again in 240 frames.
 * --dump dc_level_prefetch 28 */
typedef struct {uint32_t requested,staged,taken,missed,waited,dropped,stale;} dc_level_prefetch_counts;
extern volatile dc_level_prefetch_counts dc_level_prefetch;
/* Once a draw frame: ages the staged files. */
void dc_level_prefetch_frame(void);
/* Ask for a file (a path under 64 bytes) to be read ahead; again each frame
 * it is still wanted. At most 24 files and 160 KB are staged. */
void dc_level_prefetch_request(const char *path);
/* 1 and the file's bytes, now the caller's (dc_level_prefetch_free), when it
 * is staged; waits for it if it is being read; else 0. */
int dc_level_prefetch_take(const char *path,unsigned char **data,uint32_t *bytes);
void dc_level_prefetch_free(unsigned char *data);
#endif
