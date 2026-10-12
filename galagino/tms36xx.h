// license:BSD-3-Clause
// copyright-holders:Juergen Buchmueller
/*
 * MM6221AA melody generator (TMS36XX family), as used by Phoenix.
 *
 * A port of MAME's src/devices/sound/tms36xx.cpp, reduced to the MM6221AA
 * subtype: three fixed tunes selected by a 2-bit value. The algorithm counts in
 * samples per second, so it runs directly at the output rate instead of at
 * MAME's clock * 64.
 */
#ifndef TMS36XX_H
#define TMS36XX_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    int samplerate;
    int basefreq;
    int speed;
    int tune_counter;
    int note_counter;
    int voices;
    int shift;
    int vol[12];
    int vol_counter[12];
    int decay[12];
    int counter[12];
    int frequency[12];
    int output;
    int enable;
    int tune_num;
    const int *tune; /* tunes[tune_num], so the sample loop never reads the flash table */
    int tune_ofs;
    int tune_max;
} tms36xx_t;

/* decay[6]: decay times in seconds of the six harmonics, 0 = voice unused.
 * speed: seconds per note. */
void tms36xx_init(tms36xx_t *t, int clock, int samplerate, const float decay[6], float speed);
void mm6221aa_tune_w(tms36xx_t *t, int tune);

/* One output sample, 0 .. 32767 * voices. Divide by 32768 * voices for the
 * 0..1 level MAME's stream carries. */
int tms36xx_sample(tms36xx_t *t);

#ifdef __cplusplus
}
#endif

#endif
