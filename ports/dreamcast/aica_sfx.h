/* One mono AICA ADPCM voice: load, play, stop, status (AI-assisted), ISC. */
#ifndef DC_AICA_SFX_H
#define DC_AICA_SFX_H
#include <stdint.h>

/* 2 MiB sound RAM. Usable sample pool starts at 0x030000 (196608): the KOS
 * stream driver occupies the bytes below that. 2097152 - 196608 = 1900544.
 * Caps below are local guards. The chip's own limits are not re-measured. */
#define DC_AICA_SOUND_RAM_BYTES 2097152u
#define DC_AICA_SAMPLE_BASE 0x030000u
#define DC_AICA_SAMPLE_RAM_BYTES 1900544u
#define DC_AICA_ALIGN_BYTES 32u
#define DC_AICA_MAX_SAMPLES 65534u
#define DC_AICA_MAX_RATE 96000u
#define DC_AICA_STATUS_WAIT_MS 200u

enum {
  DC_AICA_OK = 0,
  DC_AICA_ERR_ARG = 1,
  DC_AICA_ERR_SIZE = 2,
  DC_AICA_ERR_ALIGN = 3,
  DC_AICA_ERR_SAMPLES = 4,
  DC_AICA_ERR_RANGE = 5,
  DC_AICA_ERR_INIT = 6,
  DC_AICA_ERR_NOMEM = 7,
  DC_AICA_ERR_READBACK = 8,
  DC_AICA_ERR_CHANNEL = 9,
  DC_AICA_ERR_STATE = 10
};

typedef struct dc_aica_sfx {
  uint32_t ram_offset;
  uint32_t byte_count;
  uint32_t sample_count;
  uint32_t rate;
  int32_t channel;
  uint32_t loaded;
} dc_aica_sfx;

/* Snapshot only. The query function is dc_aica_sfx_status; a typedef and a
 * function cannot share one ordinary identifier. */
typedef struct dc_aica_sfx_info {
  uint32_t loaded;
  uint32_t ram_offset;
  uint32_t byte_count;
  uint32_t sample_count;
  uint32_t rate;
  int32_t channel;
  uint32_t playing;
  uint32_t position;
} dc_aica_sfx_info;

/* Bytes are mono 4-bit ADPCM, low nibble first, as vag_adpcm.py writes them.
 * byte_count is the sound-RAM image (multiple of 32). sample_count is how many
 * of those nibbles to play and is at most byte_count * 2. Returns 0 or a
 * negative DC_AICA_ERR_* value. (AI-assisted), ISC. */
int dc_aica_sfx_init(uint32_t *available);
void dc_aica_sfx_reset(dc_aica_sfx *sfx);
int dc_aica_sfx_load(dc_aica_sfx *sfx, const uint8_t *adpcm, uint32_t byte_count,
                     uint32_t sample_count, uint32_t rate);
int dc_aica_sfx_readback(const dc_aica_sfx *sfx, const uint8_t *adpcm, uint32_t byte_count);
int dc_aica_sfx_play(dc_aica_sfx *sfx, int volume, int pan, int looping);
int dc_aica_sfx_stop(dc_aica_sfx *sfx);
int dc_aica_sfx_status(const dc_aica_sfx *sfx, dc_aica_sfx_info *status);
int dc_aica_sfx_unload(dc_aica_sfx *sfx);
#endif
