/*
 * The DIP switches of a game as a text file on the SD card: a line per port
 * that has a row, "dsw0=c9". Kept apart from main.cpp to be tested on the host
 * against a simulated FatFs (hosttest/dip_test.c). FIL comes from the caller,
 * being too large for the stack of the settings menu.
 */
#ifndef DIPFILE_H
#define DIPFILE_H

#include <stdint.h>

#include "ff.h"
#include "galagino.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A longer file is ignored: this one's was not written by dipFileSave. */
#define DIPFILE_MAX 63

/* Puts the choices saved in the file into the ports. Only a row's own bits are
 * taken, and only when they are one of its choices; a row without them keeps
 * what the ports hold, as does every switch that is not offered. Returns true
 * when the file was read. */
int dipFileLoad(FIL *fil, const char *path, const gal_dip_t *rows, int nrows, uint8_t *ports);

/* Writes the ports that have rows. Returns true when all of it was written. */
int dipFileSave(FIL *fil, const char *path, const gal_dip_t *rows, int nrows, const uint8_t *ports);

#ifdef __cplusplus
}
#endif

#endif
