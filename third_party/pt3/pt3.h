/* PT3PLAYER.H
 *
 * player functions for PT3
 *
 */

#ifndef PT3PLAYER_H

#define PT3PLAYER_H

#include <stdint.h>

//MAIN FUNCTION - instantly stops music
void func_mute(void);
//MAIN FUNCTION - runs 1 tick of music
void func_play_tick(int ch);
//MAIN FUNCTION - setup music for playing
int func_setup_music(uint8_t* music_ptr, int length, int ch, int first);
int func_restart_music(int ch);

/* Seek the sequencer for chip `ch` to order-list position `pos` (additive;
 * does not change normal tick/advance/loop behavior). `pos` is clamped to the
 * tune's valid position range [0, NumberOfPositions). */
void pt3_set_position(int ch, int pos);

/* Read the sequencer's current order-list position for chip `ch`. Returns -1
 * if no tune is loaded for that chip. Mirrors pt3_set_position (read-only). */
int pt3_get_position(int ch);


void func_getregs(uint8_t *dest, int ch);

extern int forced_notetable;

#endif
