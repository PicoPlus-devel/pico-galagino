// license:BSD-3-Clause
// copyright-holders:Juergen Buchmueller, Derrick Renaud
/*
 * Phoenix sound board: the custom noise generator, the two discrete effect
 * circuits and the MM6221AA melody chip, mixed to one mono stream.
 *
 * Ported from MAME's src/mame/phoenix/phoenix_a.cpp. The discrete part is
 * MAME's phoenix_discrete netlist translated node by node into plain C, using
 * the step functions of the MAME discrete modules it is built from (555
 * astable, note counter, RC discharge, RC filter, resistor mixers). MAME runs
 * that netlist at 120 kHz; here it runs at the output rate, which the
 * modules' anti-aliased outputs are designed to allow.
 */
#ifndef PHOENIX_SOUND_H
#define PHOENIX_SOUND_H

#include <stdint.h>

#include "tms36xx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 555 astable (DSD_555_ASTBL) */
typedef struct
{
    float r1, r2;
    float v_charge, v_out_high;
    float threshold, trigger; /* used when the control voltage is not a node */
    int out_type;
    int use_cv;
    float cap_v;
    int flip_flop;
    float last_c;
    float t_rc_charge, t_rc_discharge;
    float exp_charge, exp_discharge;
    float out;
} phx_555_t;

/* Note counter (DSS_NOTE), clocked by count, energy output */
typedef struct
{
    int count1, count2;
    int max1, max2;
} phx_note_t;

typedef struct
{
    int samplerate;
    float sample_time;

    /* latched register values */
    uint8_t latch_a;
    uint8_t latch_b;

    /* custom noise generator */
    int c24_counter, c24_level;
    int c25_counter, c25_level;
    int noise_counter, noise_polybit, noise_polyoffs;
    uint32_t noise_lfsr;
    int lowpass_counter, lowpass_polybit;

    /* discrete effect 1 */
    float rcdisc4_v[2], rcdisc4_exp[2], rcdisc4_vc, rcdisc4_max;
    phx_555_t ic20a; /* NODE_21 */
    phx_note_t note1;  /* NODE_22 */
    float n25, exp25;

    /* discrete effect 2 */
    phx_555_t ic44; /* NODE_33 */
    phx_555_t ic51; /* NODE_34 */
    float n37, exp37;
    phx_555_t ic50; /* NODE_39 */
    phx_note_t note2; /* NODE_40 */

    /* final mixer (NODE_90) */
    float mix_vcap[2], mix_exp[2], mix_vcap_amp, mix_exp_amp, mix_rtotal;

    /* melody */
    tms36xx_t tms;

    /* output stage */
    float dc_x, dc_y;
    float gain;
} phx_sound_t;

void phx_sound_init(phx_sound_t *s, int samplerate);
void phx_sound_control_a(phx_sound_t *s, uint8_t data);
void phx_sound_control_b(phx_sound_t *s, uint8_t data);
void phx_sound_render(phx_sound_t *s, int16_t *out, int samples);

#ifdef __cplusplus
}
#endif

#endif
