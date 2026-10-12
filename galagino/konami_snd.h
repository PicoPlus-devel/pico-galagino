/*
 * Konami's Z80 and two AY-3-8910 sound board (MAME's timeplt_audio); see
 * konami_snd.c.
 */
#ifndef KONAMI_SND_H
#define KONAMI_SND_H

#include <stdint.h>

#define KONAMI_SND_CLOCK 1789772
#define KONAMI_SND_CYCLES_PER_FRAME (KONAMI_SND_CLOCK / 60)

/* rom: the board's program, mapped from 0000; ram_ofs: its 1 KB in gal.ram */
void konami_snd_init(const uint8_t *rom, int rom_size, int ram_ofs);
void konami_snd_reset(void);
void konami_snd_command(uint8_t data);
/* the main CPU's latch bit that interrupts the sound CPU on a rising edge */
void konami_snd_trigger(int state);
void konami_snd_run(int cycles);
void konami_snd_render(int16_t *out, int n);

#endif
