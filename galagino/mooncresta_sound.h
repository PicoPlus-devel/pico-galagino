// license:BSD-3-Clause
// copyright-holders:Couriersud, K.Wilkins, Derrick Renaud
/*
 * Moon Cresta sound: the Galaxian discrete sound circuit with Moon Cresta's
 * mixing stage, as one mono stream.
 *
 * Ported from MAME's src/mame/galaxian/galaxian_a.cpp. The circuit is MAME's
 * mooncrst_discrete netlist translated node by node into plain C, using the
 * step functions of the MAME discrete modules it is built from (LFSR noise,
 * D flip-flop, resistor DAC, 555 current-controlled and astable oscillators,
 * note counter, RC discharge, band-pass op-amp filter, RC filter, resistor
 * mixer). It runs at the output rate, as MAME's discrete core does.
 *
 *   background  a 4-bit "LFO" DAC sets the charging current of a 555 whose
 *               ramp sweeps the control voltage of three 555 tone oscillators
 *               (FS1-FS3), the familiar Galaxian hum
 *   tone        a counter reloaded from the pitch register divides 1.536 MHz;
 *               its outputs are mixed in through VOL1/VOL2
 *   hit         noise gated by an RC discharge, through a band-pass filter
 *   fire        a 555 swept by an RC ramp plus noise, gated by an RC discharge
 */
#ifndef MOONCRESTA_SOUND_H
#define MOONCRESTA_SOUND_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 555 astable (DSD_555_ASTBL) with a control voltage input */
typedef struct
{
    float v_charge, v_out_high;
    int out_type;
    float cap_v;
    int flip_flop;
    float t_rc_charge, t_rc_discharge;
    float exp_charge, exp_discharge;
    float out;
} mcr_555_t;

typedef struct
{
    float sample_time;

    /* latches */
    uint8_t lfo;      /* a004-a007, 4 bits */
    uint8_t fs[3];    /* a800-a802 */
    uint8_t hit;      /* a803 */
    uint8_t fire;     /* a805 */
    uint8_t vol1;     /* a806 */
    uint8_t vol2;     /* a807 */
    uint8_t pitch;    /* b800 */

    /* noise: 17-bit LFSR at 122.88 kHz, latched on the rising edge of 2V */
    uint32_t lfsr;
    int lfsr_out;
    float lfsr_t_left, lfsr_t_clock;
    float sq_t_left, sq_t_on, sq_t_off;
    int sq_ff, dff_last_clk;
    int noise; /* NODE_152 */

    /* background */
    float dac_v[16]; /* NODE_100 per LFO value */
    float cc_cap_v;  /* 555 current-controlled oscillator, NODE_105 */
    int cc_flip_flop;
    float cc_exp_bleed;
    mcr_555_t vco[3]; /* NODE_115..117 */
    float vco_r1[3], vco_r2[3], vco_c[3];
    float bg_vcap, bg_exp; /* background mixer cF, NODE_120 */

    /* tone: note counter clocked at 1.536 MHz */
    float note_t_left, note_t_clock;
    int note_count1, note_count2;

    /* hit */
    float hit_vcap, hit_exp; /* RC discharge, NODE_155 */
    float bp_a1, bp_a2, bp_b0, bp_b1, bp_b2;
    float bp_x1, bp_x2, bp_y1, bp_y2;
    float bp_out; /* NODE_157 */

    /* fire */
    float fire_rc_v, fire_rc_exp; /* NODE_173 */
    mcr_555_t fire_vco;           /* NODE_181 */
    float fire_vcap, fire_exp;    /* RC discharge, NODE_182 */

    /* final mixer */
    float mix_vcap_fire, mix_exp_fire; /* input high-pass of the fire path */
    float mix_vcap_amp, mix_exp_amp;   /* output coupling capacitor */

    float gain;
} mcr_sound_t;

void mcr_sound_init(mcr_sound_t *s, int samplerate);
void mcr_sound_reset(mcr_sound_t *s);

/* a004-a007: one bit of the background LFO DAC each */
void mcr_sound_lfo_w(mcr_sound_t *s, int offset, uint8_t data);
/* a800-a807: FS1, FS2, FS3, HIT, n/c, FIRE, VOL1, VOL2 */
void mcr_sound_w(mcr_sound_t *s, int offset, uint8_t data);
/* b800 */
void mcr_sound_pitch_w(mcr_sound_t *s, uint8_t data);

void mcr_sound_render(mcr_sound_t *s, int16_t *out, int samples);

#ifdef __cplusplus
}
#endif

#endif
