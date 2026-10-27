/* Native cpad-open/cpad-get-data on the Dreamcast maple bus (AI-assisted), ISC. */
#ifndef DC_GOAL_PAD_H
#define DC_GOAL_PAD_H
#include <stdint.h>
/* KOS CONT_* bit values, repeated so host tests can build the mapping. */
enum {DC_PAD_B=1u<<1,DC_PAD_A=1u<<2,DC_PAD_START=1u<<3,DC_PAD_UP=1u<<4,DC_PAD_DOWN=1u<<5,
      DC_PAD_LEFT=1u<<6,DC_PAD_RIGHT=1u<<7,DC_PAD_Y=1u<<9,DC_PAD_X=1u<<10};
typedef struct {uint32_t buttons;int ltrig,rtrig,joyx,joyy;} dc_goal_pad_state;
/* 1 and the controller state when a maple controller is in that port. */
int dc_goal_pad_read_state(int port,dc_goal_pad_state *out);
/* Fill the original 32-byte hw-cpad part of a cpad-info. */
void dc_goal_pad_fill(uint8_t *cpad,const dc_goal_pad_state *state);
/* The original kernel functions, on a cpad-info object (132 bytes). */
void dc_goal_cpad_open(uint8_t *cpad,int32_t pad_number);
void dc_goal_cpad_get_data(uint8_t *cpad);
#endif
