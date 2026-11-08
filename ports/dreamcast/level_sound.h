/* Streamed dialogue for the level ELF (AI-assisted), ISC. */
#ifndef DC_LEVEL_SOUND_H
#define DC_LEVEL_SOUND_H
#include "goal_runtime.h"
/* Serves rpc-call ports 0 and 1 (the IOP sound player and loader) ahead of the
 * handler already installed, which still gets every other port. */
void dc_level_sound_install(dc_goal_runtime *rt);
/* Once a frame: *sound-iop-info* (frame, strpos, stream id) and the AICA stream. */
void dc_level_sound_frame(dc_goal_runtime *rt);
typedef struct {unsigned irx_calls,streams,ended,missing,pcm_bytes,errors;} dc_level_sound_counts;
void dc_level_sound_summary(dc_level_sound_counts *out);
#endif
