/* Copy vag_adpcm bytes into sound RAM and key one AICA channel (AI-assisted), ISC. */
#include "aica_sfx.h"
#include <kos.h>
#include <dc/sound/aica_comm.h>
#include <dc/sound/sfxmgr.h>
#include <dc/sound/sound.h>
#include <dc/spu.h>
#include <malloc.h>
#include <string.h>

typedef char dc_aica_cmd_words[
    ((uint32_t)AICA_CMDSTR_CHANNEL_SIZE < (uint32_t)AICA_CMD_MAX_SIZE &&
     (sizeof(aica_cmd_t) + sizeof(aica_channel_t)) % 4u == 0u &&
     sizeof(aica_channel_t) == 16u * sizeof(uint32))
        ? 1
        : -1];

static int ready;

static int wait_key(int channel, int want) {
  uint64_t start = timer_ms_gettime64();
  uint32_t spins = 0;
  for (;;) {
    int playing = snd_is_playing((unsigned)channel) ? 1 : 0;
    if (playing == want) return 0;
    if (timer_ms_gettime64() - start >= DC_AICA_STATUS_WAIT_MS) return -DC_AICA_ERR_STATE;
    if (++spins >= 1000000u) return -DC_AICA_ERR_STATE;
    thd_pass();
  }
}

static int submit_start(int channel, const dc_aica_sfx *sfx, int volume, int pan, int looping) {
  AICA_CMDSTR_CHANNEL(tmp, cmd, chan);
  memset(tmp, 0, sizeof tmp);
  cmd->cmd = AICA_CMD_CHAN;
  cmd->timestamp = 0;
  cmd->size = AICA_CMDSTR_CHANNEL_SIZE;
  cmd->cmd_id = (uint32)channel;
  chan->cmd = AICA_CH_CMD_START;
  chan->base = sfx->ram_offset;
  chan->type = AICA_SM_ADPCM;
  chan->length = sfx->sample_count;
  chan->loop = looping ? 1u : 0u;
  chan->loopstart = 0;
  chan->loopend = sfx->sample_count;
  chan->freq = sfx->rate;
  chan->vol = (uint32)volume;
  chan->pan = (uint32)pan;
  return snd_sh4_to_aica(tmp, cmd->size);
}

/* Same stop packet snd_sfx_stop sends, including its 44100 placeholder. */
static int submit_stop(int channel) {
  AICA_CMDSTR_CHANNEL(tmp, cmd, chan);
  memset(tmp, 0, sizeof tmp);
  cmd->cmd = AICA_CMD_CHAN;
  cmd->timestamp = 0;
  cmd->size = AICA_CMDSTR_CHANNEL_SIZE;
  cmd->cmd_id = (uint32)channel;
  chan->cmd = AICA_CH_CMD_STOP;
  chan->base = 0;
  chan->type = 0;
  chan->length = 0;
  chan->loop = 0;
  chan->loopstart = 0;
  chan->loopend = 0;
  chan->freq = 44100;
  chan->vol = 0;
  chan->pan = 0;
  return snd_sh4_to_aica(tmp, cmd->size);
}

int dc_aica_sfx_init(uint32_t *available) {
  if (!ready) {
    if (snd_init() != 0) return -DC_AICA_ERR_INIT;
    ready = 1;
  }
  if (available) *available = snd_mem_available();
  return 0;
}

void dc_aica_sfx_reset(dc_aica_sfx *sfx) {
  if (!sfx) return;
  memset(sfx, 0, sizeof *sfx);
  sfx->channel = -1;
}

int dc_aica_sfx_readback(const dc_aica_sfx *sfx, const uint8_t *adpcm, uint32_t byte_count) {
  void *got;
  int same;
  if (!sfx || !adpcm) return -DC_AICA_ERR_ARG;
  if (!sfx->loaded || sfx->ram_offset == 0) return -DC_AICA_ERR_STATE;
  if (byte_count == 0 || byte_count != sfx->byte_count) return -DC_AICA_ERR_SIZE;
  if ((byte_count % DC_AICA_ALIGN_BYTES) != 0) return -DC_AICA_ERR_ALIGN;
  if (sfx->ram_offset < DC_AICA_SAMPLE_BASE ||
      sfx->ram_offset > DC_AICA_SOUND_RAM_BYTES - byte_count)
    return -DC_AICA_ERR_RANGE;
  got = memalign(32, byte_count);
  if (!got) return -DC_AICA_ERR_NOMEM;
  spu_memread(got, sfx->ram_offset, byte_count);
  same = memcmp(got, adpcm, byte_count) == 0;
  free(got);
  return same ? 0 : -DC_AICA_ERR_READBACK;
}

int dc_aica_sfx_load(dc_aica_sfx *sfx, const uint8_t *adpcm, uint32_t byte_count,
                     uint32_t sample_count, uint32_t rate) {
  uint32_t available = 0;
  uint32_t offset;
  void *aligned;
  if (!sfx) return -DC_AICA_ERR_ARG;
  if (sfx->loaded) return -DC_AICA_ERR_STATE;
  dc_aica_sfx_reset(sfx);
  if (!adpcm) return -DC_AICA_ERR_ARG;
  /* Size before alignment, then sample count, then rate. Same order as
   * classify_load in make_sound_fixture.py. */
  if (byte_count == 0 || byte_count > DC_AICA_SOUND_RAM_BYTES ||
      byte_count > DC_AICA_SAMPLE_RAM_BYTES)
    return -DC_AICA_ERR_SIZE;
  if ((byte_count % DC_AICA_ALIGN_BYTES) != 0) return -DC_AICA_ERR_ALIGN;
  if (sample_count == 0 || sample_count > DC_AICA_MAX_SAMPLES ||
      sample_count > byte_count * 2u)
    return -DC_AICA_ERR_SAMPLES;
  if (rate == 0 || rate > DC_AICA_MAX_RATE) return -DC_AICA_ERR_RANGE;
  if (dc_aica_sfx_init(&available) != 0) return -DC_AICA_ERR_INIT;
  if (byte_count > available) return -DC_AICA_ERR_NOMEM;
  offset = snd_mem_malloc(byte_count);
  if (offset == 0) return -DC_AICA_ERR_NOMEM;
  if (offset < DC_AICA_SAMPLE_BASE || offset > DC_AICA_SOUND_RAM_BYTES - byte_count) {
    snd_mem_free(offset);
    return -DC_AICA_ERR_RANGE;
  }
  aligned = memalign(32, byte_count);
  if (!aligned) {
    snd_mem_free(offset);
    return -DC_AICA_ERR_NOMEM;
  }
  memcpy(aligned, adpcm, byte_count);
  spu_memload_sq(offset, aligned, byte_count);
  free(aligned);
  sfx->ram_offset = offset;
  sfx->byte_count = byte_count;
  sfx->sample_count = sample_count;
  sfx->rate = rate;
  sfx->loaded = 1;
  sfx->channel = -1;
  if (dc_aica_sfx_readback(sfx, adpcm, byte_count) != 0) {
    snd_mem_free(offset);
    dc_aica_sfx_reset(sfx);
    return -DC_AICA_ERR_READBACK;
  }
  return 0;
}

int dc_aica_sfx_play(dc_aica_sfx *sfx, int volume, int pan, int looping) {
  int channel;
  if (!sfx) return -DC_AICA_ERR_ARG;
  if (volume < 0 || volume > 255 || pan < 0 || pan > 255 || (looping != 0 && looping != 1))
    return -DC_AICA_ERR_RANGE;
  if (!sfx->loaded || sfx->ram_offset == 0) return -DC_AICA_ERR_STATE;
  if (sfx->channel >= 0) return -DC_AICA_ERR_STATE;
  if (sfx->byte_count == 0 || (sfx->byte_count % DC_AICA_ALIGN_BYTES) != 0 ||
      sfx->byte_count > DC_AICA_SAMPLE_RAM_BYTES)
    return -DC_AICA_ERR_SIZE;
  if (sfx->sample_count == 0 || sfx->sample_count > DC_AICA_MAX_SAMPLES ||
      sfx->sample_count > sfx->byte_count * 2u)
    return -DC_AICA_ERR_SAMPLES;
  if (sfx->rate == 0 || sfx->rate > DC_AICA_MAX_RATE) return -DC_AICA_ERR_RANGE;
  channel = snd_sfx_chn_alloc();
  if (channel < 0 || channel >= 64) return -DC_AICA_ERR_CHANNEL;
  if (submit_start(channel, sfx, volume, pan, looping) != 0) {
    snd_sfx_chn_free(channel);
    return -DC_AICA_ERR_CHANNEL;
  }
  sfx->channel = channel;
  return 0;
}

int dc_aica_sfx_stop(dc_aica_sfx *sfx) {
  int channel;
  int rc;
  if (!sfx || sfx->channel < 0 || sfx->channel >= 64) return -DC_AICA_ERR_STATE;
  channel = sfx->channel;
  rc = submit_stop(channel);
  if (rc == 0) rc = wait_key(channel, 0);
  snd_sfx_chn_free(channel);
  sfx->channel = -1;
  return rc == 0 ? 0 : (rc < 0 ? rc : -DC_AICA_ERR_CHANNEL);
}

int dc_aica_sfx_status(const dc_aica_sfx *sfx, dc_aica_sfx_info *status) {
  if (!sfx || !status) return -DC_AICA_ERR_ARG;
  memset(status, 0, sizeof *status);
  status->channel = -1;
  status->loaded = sfx->loaded;
  status->ram_offset = sfx->ram_offset;
  status->byte_count = sfx->byte_count;
  status->sample_count = sfx->sample_count;
  status->rate = sfx->rate;
  status->channel = sfx->channel;
  if (sfx->channel >= 0 && sfx->channel < 64) {
    status->playing = snd_is_playing((unsigned)sfx->channel) ? 1u : 0u;
    status->position = snd_get_pos((unsigned)sfx->channel);
  }
  return 0;
}

int dc_aica_sfx_unload(dc_aica_sfx *sfx) {
  int rc = 0;
  if (!sfx) return -DC_AICA_ERR_ARG;
  if (!sfx->loaded) return -DC_AICA_ERR_STATE;
  if (sfx->channel >= 0) rc = dc_aica_sfx_stop(sfx);
  if (sfx->ram_offset != 0) snd_mem_free(sfx->ram_offset);
  dc_aica_sfx_reset(sfx);
  return rc;
}
