/* One allocation owns arena then slab; only Jak's main (AI-assisted), ISC. */
#include <kos.h>
#include <malloc.h>
#include <stdlib.h>
void *dc_arena_jak_root_alloc(size_t,size_t);
void dc_arena_jak_root_free(void *);
#define memalign dc_arena_jak_root_alloc
#define free dc_arena_jak_root_free
#include "runtime_main.c"
