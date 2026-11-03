/* Dreamcast ELF: load two synthetic AICA clips, play them, stop them (AI-assisted), ISC. */
#include <kos.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "aica_sfx.h"
#include "build/sound_fixture.h"

#define DC_SOUND_PROBE_CHECKS 59

KOS_INIT_FLAGS(INIT_DEFAULT);

typedef struct {
  uint32_t magic, version, state, total, passed, failed_case, system_ram, code_bytes;
  uint32_t observed_lo, observed_hi, expected_lo, expected_hi;
  char description[80];
} dc_sound_telemetry;
typedef char dc_sound_telemetry_size[sizeof(dc_sound_telemetry) == 128 ? 1 : -1];
typedef char dc_sound_square_layout[
    (sizeof dc_sound_square == DC_SOUND_SQUARE_BYTES &&
     DC_SOUND_SQUARE_SAMPLES / 2u == DC_SOUND_SQUARE_BYTES &&
     DC_SOUND_SQUARE_BYTES % 32u == 0u &&
     DC_SOUND_SQUARE_SAMPLES <= 65534u &&
     DC_SOUND_SQUARE_RATE == 48000u)
        ? 1
        : -1];
typedef char dc_sound_vag_layout[
    (sizeof dc_sound_vag == DC_SOUND_VAG_BYTES &&
     DC_SOUND_VAG_SAMPLES / 2u == DC_SOUND_VAG_BYTES &&
     DC_SOUND_VAG_BYTES % 32u == 0u &&
     DC_SOUND_VAG_SAMPLES <= 65534u &&
     DC_SOUND_VAG_RATE == 48000u &&
     DC_SOUND_SQUARE_BYTES != DC_SOUND_VAG_BYTES)
        ? 1
        : -1];

volatile dc_sound_telemetry dc_backend = {.magic = 0x34485347u, .version = 1};

static void check_bits(uint32_t actual, uint32_t expected, const char *name) {
  unsigned i;
  ++dc_backend.total;
  if (actual == expected) {
    ++dc_backend.passed;
    return;
  }
  if (dc_backend.failed_case) return;
  dc_backend.failed_case = dc_backend.total;
  dc_backend.observed_lo = actual;
  dc_backend.observed_hi = 0;
  dc_backend.expected_lo = expected;
  dc_backend.expected_hi = 0;
  for (i = 0; i < sizeof dc_backend.description - 1 && name[i]; ++i)
    dc_backend.description[i] = name[i];
  dc_backend.description[i] = 0;
}

static void check_u32(uint32_t actual, uint32_t expected, const char *name) {
  check_bits(actual, expected, name);
}

static void check_i32(int32_t actual, int32_t expected, const char *name) {
  check_bits((uint32_t)actual, (uint32_t)expected, name);
}

static uint32_t sum_bytes(const uint8_t *data, uint32_t count) {
  uint32_t sum = 0;
  uint32_t i;
  for (i = 0; i < count; ++i) sum += data[i];
  return sum;
}

static uint32_t pool(void) {
  uint32_t available = 0;
  if (dc_aica_sfx_init(&available) != 0) return 0;
  return available;
}

/* How long each clip sounds before the stop, so a capture holds a waveform. */
#ifndef DC_SOUND_PROBE_HOLD_MS
#define DC_SOUND_PROBE_HOLD_MS 500
#endif
static int wait_playing(const dc_aica_sfx *sfx, dc_aica_sfx_info *status) {
  uint64_t start = timer_ms_gettime64();
  uint32_t spins = 0;
  for (;;) {
    if (dc_aica_sfx_status(sfx, status) != 0) return -1;
    if (status->playing == 1u) return 1;
    if (timer_ms_gettime64() - start >= DC_AICA_STATUS_WAIT_MS) return (int)status->playing;
    if (++spins >= 1000000u) return (int)status->playing;
    thd_pass();
  }
}

static void set_description(const char *text) {
  unsigned i;
  for (i = 0; i < sizeof dc_backend.description - 1 && text[i]; ++i)
    dc_backend.description[i] = text[i];
  dc_backend.description[i] = 0;
}

int main(void) {
  dc_aica_sfx square;
  dc_aica_sfx vag;
  dc_aica_sfx_info st;
  uint32_t available = 0;
  dc_aica_sfx_reset(&square);
  dc_aica_sfx_reset(&vag);
  memset(&st, 0, sizeof st);
  dc_backend.state = 1;
  dc_backend.system_ram = _arch_mem_top - 0x8c000000u;
  check_u32(dc_backend.system_ram, 16777216u, "stock 16 MiB RAM");
  check_i32(dc_aica_sfx_init(&available), 0, "snd_init");
  check_i32(dc_aica_sfx_init(&available), 0, "snd_init again");
  check_u32(available, DC_AICA_SAMPLE_RAM_BYTES, "sample pool");
  check_i32(dc_aica_sfx_load(NULL, dc_sound_square, 32u, 1u, DC_SOUND_PROBE_RATE), -DC_AICA_ERR_ARG, "null voice");
  check_i32(dc_aica_sfx_load(&square, NULL, 32u, 1u, DC_SOUND_PROBE_RATE), -DC_AICA_ERR_ARG, "null adpcm");
  check_i32(dc_aica_sfx_load(&square, dc_sound_square, 0u, 1u, DC_SOUND_PROBE_RATE), -DC_AICA_ERR_SIZE, "zero bytes");
  check_i32(dc_aica_sfx_load(&square, dc_sound_square, 16u, 1u, DC_SOUND_PROBE_RATE), -DC_AICA_ERR_ALIGN, "unaligned bytes");
  check_i32(dc_aica_sfx_load(&square, dc_sound_square, 32u, 0u, DC_SOUND_PROBE_RATE), -DC_AICA_ERR_SAMPLES, "zero samples");
  check_i32(dc_aica_sfx_load(&square, dc_sound_square, 32u, 65535u, DC_SOUND_PROBE_RATE), -DC_AICA_ERR_SAMPLES, "samples above 65534");
  check_i32(dc_aica_sfx_load(&square, dc_sound_square, 32u, 65u, DC_SOUND_PROBE_RATE), -DC_AICA_ERR_SAMPLES, "samples past bytes");
  check_i32(dc_aica_sfx_load(&square, dc_sound_square, 2097184u, 1u, DC_SOUND_PROBE_RATE), -DC_AICA_ERR_SIZE, "bytes above 2 MiB");
  check_i32(dc_aica_sfx_load(&square, dc_sound_square, 1900576u, 1u, DC_SOUND_PROBE_RATE), -DC_AICA_ERR_SIZE, "bytes above sample pool");
  check_i32(dc_aica_sfx_load(&square, dc_sound_square, 32u, 1u, 0u), -DC_AICA_ERR_RANGE, "rate 0");
  check_i32(dc_aica_sfx_load(&square, dc_sound_square, 32u, 1u, 96001u), -DC_AICA_ERR_RANGE, "rate above guard");
  check_u32((uint32_t)sizeof dc_sound_square, DC_SOUND_SQUARE_BYTES, "square bytes");
  check_u32(DC_SOUND_SQUARE_SAMPLES, 8192u, "square samples");
  check_u32(DC_SOUND_SQUARE_RATE, 48000u, "square rate");
  check_u32(sum_bytes(dc_sound_square, DC_SOUND_SQUARE_BYTES), DC_SOUND_SQUARE_SUM, "square sum");
  check_u32((uint32_t)sizeof dc_sound_vag, DC_SOUND_VAG_BYTES, "vag bytes");
  check_u32(DC_SOUND_VAG_SAMPLES, 4480u, "vag samples");
  check_u32(DC_SOUND_VAG_RATE, 48000u, "vag rate");
  check_u32(sum_bytes(dc_sound_vag, DC_SOUND_VAG_BYTES), DC_SOUND_VAG_SUM, "vag sum");
  check_u32(DC_SOUND_SQUARE_BYTES == DC_SOUND_VAG_BYTES ? 1u : 0u, 0u, "clips differ");
  check_i32(dc_aica_sfx_load(&square, dc_sound_square, DC_SOUND_SQUARE_BYTES, DC_SOUND_SQUARE_SAMPLES, DC_SOUND_SQUARE_RATE), 0, "load square");
  check_u32(square.ram_offset, DC_AICA_SAMPLE_BASE, "square sound RAM");
  check_i32(dc_aica_sfx_readback(&square, dc_sound_square, DC_SOUND_SQUARE_BYTES), 0, "square readback");
  check_u32(pool(), DC_AICA_SAMPLE_RAM_BYTES - DC_SOUND_SQUARE_BYTES, "pool after square");
  check_i32(dc_aica_sfx_play(&square, 256, 128, 1), -DC_AICA_ERR_RANGE, "volume 256");
  check_i32(dc_aica_sfx_play(&square, 255, -1, 1), -DC_AICA_ERR_RANGE, "pan -1");
  check_i32(dc_aica_sfx_play(&square, 255, 128, 2), -DC_AICA_ERR_RANGE, "loop 2");
  check_i32(dc_aica_sfx_play(&square, 255, 128, 1), 0, "play square");
  check_i32(dc_aica_sfx_status(&square, &st), 0, "square status");
  check_i32(st.channel >= 0 && st.channel < 64, 1, "square channel");
  check_i32(wait_playing(&square, &st), 1, "square keyed");
  check_u32(st.position <= DC_SOUND_SQUARE_SAMPLES ? 1u : 0u, 1u, "square position");
  thd_sleep(DC_SOUND_PROBE_HOLD_MS); /* long enough to capture and hear */
  check_i32(dc_aica_sfx_stop(&square), 0, "stop square");
  check_i32(dc_aica_sfx_status(&square, &st), 0, "square status after stop");
  check_u32(st.playing, 0u, "square released");
  check_i32(st.channel, -1, "square channel freed");
  check_i32(dc_aica_sfx_load(&vag, dc_sound_vag, DC_SOUND_VAG_BYTES, DC_SOUND_VAG_SAMPLES, DC_SOUND_VAG_RATE), 0, "load vag");
  check_u32(vag.ram_offset, DC_AICA_SAMPLE_BASE + DC_SOUND_SQUARE_BYTES, "vag sound RAM");
  check_i32(dc_aica_sfx_readback(&vag, dc_sound_vag, DC_SOUND_VAG_BYTES), 0, "vag readback");
  check_u32(pool(), DC_AICA_SAMPLE_RAM_BYTES - DC_SOUND_SQUARE_BYTES - DC_SOUND_VAG_BYTES, "pool with both");
  check_i32(dc_aica_sfx_play(&vag, 255, 128, 1), 0, "play vag");
  check_i32(wait_playing(&vag, &st), 1, "vag keyed");
  check_i32(st.channel >= 0 && st.channel < 64, 1, "vag channel");
  check_u32(st.position <= DC_SOUND_VAG_SAMPLES ? 1u : 0u, 1u, "vag position");
  thd_sleep(DC_SOUND_PROBE_HOLD_MS); /* long enough to capture and hear */
  check_i32(dc_aica_sfx_stop(&vag), 0, "stop vag");
  check_i32(dc_aica_sfx_status(&vag, &st), 0, "vag status after stop");
  check_u32(st.playing, 0u, "vag released");
  check_i32(st.channel, -1, "vag channel freed");
  check_i32(dc_aica_sfx_unload(&vag), 0, "unload vag");
  check_u32(pool(), DC_AICA_SAMPLE_RAM_BYTES - DC_SOUND_SQUARE_BYTES, "pool after vag unload");
  check_i32(dc_aica_sfx_unload(&square), 0, "unload square");
  check_u32(pool(), DC_AICA_SAMPLE_RAM_BYTES, "pool restored");
  check_i32(dc_aica_sfx_play(&square, 255, 128, 1), -DC_AICA_ERR_STATE, "play unloaded");
  check_i32(dc_aica_sfx_unload(&square), -DC_AICA_ERR_STATE, "unload unloaded");
  check_i32(dc_aica_sfx_status(NULL, &st), -DC_AICA_ERR_ARG, "null status");
  if (!dc_backend.failed_case) {
    dc_backend.code_bytes = DC_SOUND_SQUARE_BYTES + DC_SOUND_VAG_BYTES;
    set_description("AICA square and vag keyed then stopped");
  }
  dc_backend.state = dc_backend.failed_case ? 3u : 2u;
  printf("SH4_SOUND %lu/%lu first_failure=%lu\n",
         (unsigned long)dc_backend.passed, (unsigned long)dc_backend.total,
         (unsigned long)dc_backend.failed_case);
  for (;;) thd_sleep(100);
  return 0;
}
