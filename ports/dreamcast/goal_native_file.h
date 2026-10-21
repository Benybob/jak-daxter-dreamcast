/* Buffered native SH-4 module files (AI-assisted), ISC. */
#ifndef DC_GOAL_NATIVE_FILE_H
#define DC_GOAL_NATIVE_FILE_H
#include "goal_module.h"
#include <stdio.h>
enum { DC_NATIVE_NO_INITIALIZER=0,DC_NATIVE_UINT64_INITIALIZER=1,DC_NATIVE_VOID_INITIALIZER=2 };
typedef struct {
  dc_goal_module_result module;
  uint32_t initializer_kind,decoder_bytes;
  char initializer[256];
} dc_native_file_result;
/* DCSH v1 is a native-only compiler format, distinct from retail MIPS files.
 * Source is borrowed for this call; decoded descriptors are freed after link.
 * Initializer name is copied and execution remains an explicit caller action.
 * CRC detects corruption, not malicious code. Only load trusted native builds. */
int dc_goal_load_native_module(dc_goal_runtime *,const void *source,uint32_t bytes,
                                dc_goal_cache_sync,void *context,dc_native_file_result *);
/* Read exactly one bounded span from the current position. Caller owns FILE;
 * trailing bytes are allowed for archive embedding. Short reads never link. */
int dc_goal_load_native_stream(dc_goal_runtime *,FILE *,uint32_t bytes,
                              dc_goal_cache_sync,void *context,dc_native_file_result *);
#endif
