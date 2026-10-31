/* Per-object CPU ownership, including file handles (AI-assisted), ISC. */
#ifndef DC_ARENA_SLAB_H
#define DC_ARENA_SLAB_H
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#define DC_ARENA_FLY_VERTEX_BYTES (512u*1024u)
void *dc_arena_malloc(size_t);
void *dc_arena_calloc(size_t,size_t);
void *dc_arena_memalign(size_t,size_t);
void dc_arena_free(void *);
FILE *dc_arena_fopen(const char *,const char *);
int dc_arena_fclose(FILE *);
size_t dc_arena_fread(void *,size_t,size_t,FILE *);
int dc_arena_fgetc(FILE *);
int dc_arena_fseek(FILE *,long,int);
long dc_arena_ftell(FILE *);
int dc_arena_ferror(FILE *);
#ifdef DC_ARENA_SLAB_REDIRECT
#define malloc dc_arena_malloc
#define calloc dc_arena_calloc
#define memalign dc_arena_memalign
#define free dc_arena_free
#define fopen dc_arena_fopen
#define fclose dc_arena_fclose
#define fread dc_arena_fread
#define fgetc dc_arena_fgetc
#define fseek dc_arena_fseek
#define ftell dc_arena_ftell
/* Newlib can inline this in stdio.h. That header was included above. */
#undef ferror
#define ferror dc_arena_ferror
#endif
#endif
