/*
 * Build glue of the Phoenix core taken from pico-phoenix (i8085.c, phoenix.c,
 * phoenix_sound.c, tms36xx.c). pico-phoenix puts the hot paths in SRAM; here
 * they stay in flash, as the code of every game that runs alone: SRAM is kept
 * for the code all games share.
 */
#ifndef PHX_PORT_H
#define PHX_PORT_H

#define PHX_HOT(f) f

#endif
