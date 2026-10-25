/* Asset-independent driver, private geometry or synthetic fixture (AI-assisted), ISC. */
#ifndef DC_SECTOR_CACHE_CASES_H
#define DC_SECTOR_CACHE_CASES_H
#include <stdint.h>
typedef struct {uint32_t total,passed,failed_case,windows,max_faces,peak_geometry_bytes;} dc_sector_oracle_result;
void dc_sector_cache_oracle(const char *,dc_sector_oracle_result *);
#endif
