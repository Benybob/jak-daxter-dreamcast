/* Test-only allocator/IO instrumentation (AI-assisted), ISC. */
#ifndef DC_SECTOR_CACHE_BRIDGE_H
#define DC_SECTOR_CACHE_BRIDGE_H
#include "../sector_cache.h"
void dc_test_reset(unsigned);
void dc_test_io_reset(unsigned,unsigned);
unsigned dc_test_calls(void);
unsigned dc_test_live(void);
unsigned dc_test_open_files(void);
size_t dc_test_bytes(void);
size_t dc_test_peak(void);
uint32_t dc_test_cache_crc(const dc_sector_cache *);
#endif
