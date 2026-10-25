/* Shared original-free decompression oracle (AI-assisted), ISC. */
#ifndef DC_SECTOR_INFLATE_CASES_H
#define DC_SECTOR_INFLATE_CASES_H
#include <stdint.h>
typedef struct{uint32_t total,passed,failed_line,peak_decode_bytes;} dc_inflate_report;
void dc_sector_inflate_cases(dc_inflate_report *);
#endif
