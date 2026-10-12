// license:BSD-3-Clause
// copyright-holders:Couriersud, K.Wilkins, Derrick Renaud
/*
 * Moon Cresta sound, ported from MAME's galaxian_a.cpp (mooncrst_discrete).
 * See mooncresta_sound.h.
 *
 * The per-sample maths is single precision: the RP2350's FPU has no double
 * support. The RC exponents use expm1 and log1p so that the long time
 * constants keep their accuracy at a 44.1 kHz step; the versions in
 * mcr_math.h are inlined so that the per-sample path never leaves SRAM.
 * Constants computed once at init use double.
 */
#include "mooncresta_sound.h"

#include <math.h>
#include <string.h>

#include "mcr_math.h"
#include "mcr_port.h"

#define RES_K(r) ((r) * 1e3)
#define CAP_U(c) ((c) * 1e-6)

#define TTL_OUT 4.0f
#define MASTER_CLOCK 18432000.0
#define SOUND_CLOCK (MASTER_CLOCK / 6 / 2) /* 1.536 MHz */
#define RNG_RATE (MASTER_CLOCK / 3 * 2)    /* 12.288 MHz */
#define OP_AMP_VP_RAIL_OFFSET 1.5

#define DISC_555_OUT_SQW 0x00
#define DISC_555_OUT_ENERGY 0x04
#define DEFAULT_555_BLEED_R 10e6

/* galaxian_a.cpp component values */
#define GAL_R15 RES_K(100)
#define GAL_R16 RES_K(220)
#define GAL_R17 RES_K(470)
#define GAL_R18 RES_K(1000)
#define GAL_R19 RES_K(330)
#define GAL_R20 RES_K(15)
#define GAL_R21 RES_K(100)
#define GAL_R22 RES_K(100)
#define GAL_R23 RES_K(470)
#define GAL_R24 RES_K(10)
#define GAL_R25 RES_K(100)
#define GAL_R26 RES_K(330)
#define GAL_R27 RES_K(10)
#define GAL_R28 RES_K(100)
#define GAL_R29 RES_K(220)
#define GAL_R30 RES_K(10)
#define GAL_R31 RES_K(47)
#define GAL_R32 RES_K(47)
#define GAL_R33 RES_K(10)
#define MCRST_R34 RES_K(15) /* Moon Cresta lists R34 once, as 15k */
#define GAL_R35 RES_K(150)
#define GAL_R36 RES_K(22)
#define GAL_R37 RES_K(470)
#define GAL_R38 RES_K(33)
#define GAL_R39 RES_K(22)
#define GAL_R40 (RES_K(2.2) * 0.6) /* volume adjust */
#define GAL_R41 RES_K(100)
#define GAL_R43 RES_K(2.2)
#define GAL_R44 RES_K(10)
#define GAL_R45 RES_K(22)
#define GAL_R46 RES_K(10)
#define GAL_R47 RES_K(2.2)
#define GAL_R48 RES_K(2.2)
#define GAL_R49 RES_K(10)
#define GAL_R50 RES_K(22)
#define GAL_R51 RES_K(33)
#define GAL_R52 RES_K(15)

#define GAL_C15 CAP_U(1)
#define GAL_C17 CAP_U(0.01)
#define GAL_C18 CAP_U(0.01)
#define GAL_C19 CAP_U(0.01)
#define GAL_C20 CAP_U(0.1)
#define GAL_C21 CAP_U(2.2)
#define GAL_C22 CAP_U(0.01)
#define GAL_C23 CAP_U(0.01)
#define GAL_C25 CAP_U(1)
#define GAL_C26 CAP_U(0.01)
#define GAL_C27 CAP_U(0.01)
#define GAL_C28 CAP_U(47)
#define GAL_C46 CAP_U(0.1)

/* the two thresholds of a 555 on 5 V */
#define V555_THRESHOLD (5.0f * 2.0f / 3.0f)
#define V555_TRIGGER (5.0f / 3.0f)

/* 1 - exp(-dt/rc), the per-step charge fraction (MAME RC_CHARGE_EXP) */
MCR_ALWAYS_INLINE float rc_charge_exp(float dt, float rc)
{
    return -mcr_expm1f(-dt / rc);
}

static float rc_exp_init(double dt, double rc)
{
    return (float)(1.0 - exp(-dt / rc));
}

static double par2(double a, double b)
{
    return 1.0 / (1.0 / a + 1.0 / b);
}

/* ---------------------------------------------------------------------------
 * DSD_555_ASTBL with a control voltage node (as every 555 here has one)
 * ------------------------------------------------------------------------ */

static void d555_init(mcr_555_t *d, double r1, double r2, double c, float v_out_high, int out_type, float sample_time)
{
    memset(d, 0, sizeof(*d));
    d->v_charge = 5.0f; /* DEFAULT_555_CHARGE: v_pos */
    d->v_out_high = v_out_high;
    d->out_type = out_type;
    d->flip_flop = 1;
    d->t_rc_charge = (float)((r1 + r2) * c);
    d->t_rc_discharge = (float)(r2 * c);
    d->exp_charge = rc_exp_init(sample_time, (r1 + r2) * c);
    d->exp_discharge = rc_exp_init(sample_time, r2 * c);
}

/* running = the inverted RESET input (0 holds the 555 in reset) */
static float MCR_HOT(d555_step)(mcr_555_t *d, int running, float cv, float sample_time)
{
    int count_f = 0;
    int count_r = 0;
    float dt;
    float x_time = 0;
    float v_cap = d->cap_v;
    float v_cap_next = 0;
    float exponent = 0;
    int flip_flop = d->flip_flop;
    int update_exponent = 0;
    float v_out = 0;

    if (!running)
    {
        d->out = 0;
        d->flip_flop = 1;
        d->cap_v = 0;
        return 0;
    }

    /* If CV is less then .25V, the circuit will oscillate way out of range.
     * MAME ignores it then, leaving the output as it was. */
    if (cv < 0.25f)
        return d->out;
    const float threshold = cv;
    const float trigger = cv / 2.0f;
    if (v_cap >= threshold)
    {
        flip_flop = 0;
        count_f++;
    }
    else if (v_cap <= trigger)
    {
        flip_flop = 1;
        count_r++;
    }

    dt = sample_time;
    /* Keep looping until all toggling in time sample is used up. The guard
     * only protects against a float pathology; MAME's double loop has none. */
    for (int guard = 0; guard < 64; guard++)
    {
        if (flip_flop)
        {
            /* Charging */
            exponent = update_exponent ? rc_charge_exp(dt, d->t_rc_charge) : d->exp_charge;
            v_cap_next = v_cap + ((d->v_charge - v_cap) * exponent);
            dt = 0;

            /* has it charged past upper limit? */
            if (v_cap_next >= threshold)
            {
                /* calculate the overshoot time */
                dt = -d->t_rc_charge * mcr_log1pf_neg((v_cap_next - threshold) / (d->v_charge - v_cap));
                x_time = dt;
                v_cap_next = threshold;
                flip_flop = 0;
                count_f++;
                update_exponent = 1;
            }
        }
        else
        {
            /* Discharging */
            exponent = update_exponent ? rc_charge_exp(dt, d->t_rc_discharge) : d->exp_discharge;
            v_cap_next = v_cap - (v_cap * exponent);
            dt = 0;

            /* has it discharged past lower limit? */
            if (v_cap_next <= trigger)
            {
                /* calculate the overshoot time */
                if (v_cap_next < trigger)
                    dt = -d->t_rc_discharge * mcr_log1pf_neg((trigger - v_cap_next) / v_cap);
                x_time = dt;
                v_cap_next = trigger;
                flip_flop = 1;
                count_r++;
                update_exponent = 1;
            }
        }
        v_cap = v_cap_next;
        if (dt <= 0)
            break;
    }
    d->cap_v = v_cap;

    /* Convert last switch time to a ratio */
    x_time = x_time / sample_time;

    if (d->out_type == DISC_555_OUT_ENERGY)
    {
        if (x_time == 0)
            x_time = 1.0f;
        v_out = d->v_out_high * (flip_flop ? x_time : (1.0f - x_time));
    }
    else /* DISC_555_OUT_SQW */
    {
        if (count_f + count_r >= 2)
            /* force at least 1 toggle */
            v_out = d->flip_flop ? 0 : d->v_out_high;
        else
            v_out = flip_flop * d->v_out_high;
    }
    d->out = v_out;
    d->flip_flop = flip_flop;
    return v_out;
}

/* ---------------------------------------------------------------------------
 * DSD_555_CC, as used for the background: no bias, ground or discharge
 * resistor (type 0), so the cap charges from a constant current set by the
 * LFO DAC and is discharged at once. Output: the cap voltage.
 * ------------------------------------------------------------------------ */

MCR_ALWAYS_INLINE float cc_step(mcr_sound_t *s, float vin)
{
    const float v_limit = vin + 0.7f; /* Q2 junction */
    float i = (5.0f - v_limit) * (float)(1.0 / GAL_R21);
    if (i < 0)
        i = 0;
    const float c_inv = (float)(1.0 / GAL_C15);
    float dt = s->sample_time;
    float v_cap = s->cc_cap_v;
    int flip_flop = s->cc_flip_flop;

    for (int guard = 0; guard < 64; guard++)
    {
        float v_next;
        if (flip_flop)
        {
            if (i == 0)
            {
                /* no charging current: the cap bleeds */
                v_next = v_cap - v_cap * s->cc_exp_bleed;
                dt = 0;
            }
            else
            {
                /* iC = C dv/dt */
                v_next = v_cap + i * dt * c_inv;
                if (v_next > v_limit)
                    v_next = v_limit;
                dt = 0;
                if (v_next >= V555_THRESHOLD)
                {
                    dt = (v_next - V555_THRESHOLD) / (i * c_inv);
                    v_next = V555_THRESHOLD;
                    flip_flop = 0;
                }
            }
        }
        else
        {
            /* immediate discharge, no change in dt */
            v_next = V555_TRIGGER;
            flip_flop = 1;
        }
        v_cap = v_next;
        if (dt <= 0)
            break;
    }
    s->cc_cap_v = v_cap;
    s->cc_flip_flop = flip_flop;
    return v_cap;
}

/* ---------------------------------------------------------------------------
 * Noise: DSS_LFSR_NOISE with galaxian_lfsr (17 bits, F0 = bit 4 XOR NOT
 * bit 16, shifted into bit 0, output F0), latched by a D flip-flop on the
 * rising edge of the 2V line.
 * ------------------------------------------------------------------------ */

MCR_ALWAYS_INLINE void lfsr_clock(mcr_sound_t *s)
{
    uint32_t reg = s->lfsr;
    uint32_t fb = (reg >> 17) & 1; /* the last feedback result */
    reg = ((reg << 1) & 0x1ffff) | fb;
    uint32_t f0 = ((reg >> 4) & 1) ^ (((reg >> 16) & 1) ^ 1);
    s->lfsr = reg | (f0 << 17);
    s->lfsr_out = (int)f0;
}

MCR_ALWAYS_INLINE int noise_step(mcr_sound_t *s)
{
    const float st = s->sample_time;

    float cycles = (s->lfsr_t_left + st) / s->lfsr_t_clock;
    int inc = (int)cycles;
    s->lfsr_t_left = (cycles - inc) * s->lfsr_t_clock;
    while (inc-- > 0)
        lfsr_clock(s);

    /* DSS_SQUAREWFIX: 2V, 60*264/2 Hz, 50% duty */
    s->sq_t_left -= st;
    while (s->sq_t_left <= 0)
    {
        s->sq_ff ^= 1;
        s->sq_t_left += s->sq_ff ? s->sq_t_on : s->sq_t_off;
    }

    /* DST_LOGIC_DFF, low to high */
    if (!s->dff_last_clk && s->sq_ff)
        s->noise = s->lfsr_out;
    s->dff_last_clk = s->sq_ff;
    return s->noise;
}

/* ---------------------------------------------------------------------------
 * Tone: DSS_NOTE (DISC_CLK_IS_FREQ, max1 255, max2 15). count1 counts up
 * from the pitch value; every time it passes 255 it is reloaded and the
 * 4-bit count2 advances. A pitch of 255 stops it.
 * ------------------------------------------------------------------------ */

MCR_ALWAYS_INLINE int note_step(mcr_sound_t *s)
{
    float cycles = (s->note_t_left + s->sample_time) / s->note_t_clock;
    int inc = (int)cycles;
    s->note_t_left = (cycles - inc) * s->note_t_clock;

    const int data = s->pitch;
    if (data != 255 && inc > 0)
    {
        int to_wrap = 256 - s->note_count1;
        if (inc < to_wrap)
        {
            s->note_count1 += inc;
        }
        else
        {
            const int period = 256 - data;
            inc -= to_wrap;
            s->note_count2 = (s->note_count2 + 1 + inc / period) & 15;
            s->note_count1 = data + inc % period;
        }
    }
    return s->note_count2;
}

/* ---------------------------------------------------------------------------
 * DST_RCDISC5: follows the input (minus a diode drop) at once while it
 * rises, decays through R*C while it falls; the output is the cap voltage
 * while enabled, else 0.
 * ------------------------------------------------------------------------ */

MCR_ALWAYS_INLINE float rcdisc5_step(float *v_cap, int enable, float in, float exponent)
{
    float u = in - 0.7f;
    if (u < 0)
        u = 0;
    float diff = u - *v_cap;
    if (enable)
    {
        if (diff < 0)
            diff = diff * exponent;
        *v_cap += diff;
        return *v_cap;
    }
    if (diff > 0)
        *v_cap = u;
    return 0;
}

/* ---------------------------------------------------------------------------
 * Public interface
 * ------------------------------------------------------------------------ */

/* galaxian_bandpass_desc */
#define BP_VREF (5.0 * GAL_R39 / (GAL_R38 + GAL_R39))
#define BP_VP (5.0 - OP_AMP_VP_RAIL_OFFSET)

void mcr_sound_init(mcr_sound_t *s, int samplerate)
{
    memset(s, 0, sizeof(*s));
    const double st = 1.0 / samplerate;
    s->sample_time = (float)st;

    /* DISCRETE_LFSR_NOISE(NODE_150, 1, 1, RNG_RATE/100, ...) */
    s->lfsr_t_clock = (float)(100.0 / RNG_RATE);
    /* DISCRETE_SQUAREWFIX(NODE_151, 1, 60*264/2, 1.0, 50, 0.5, 0) */
    s->sq_t_on = s->sq_t_off = (float)(0.5 / (60.0 * 264.0 / 2.0));

    /* DISCRETE_DAC_R1(NODE_100, GAL_INP_BG_DAC, TTL_OUT, &galaxian_bck_dac):
     * a 4-resistor ladder with a 4.4 V bias through R20 and R19 to ground */
    {
        const double r[4] = {GAL_R18, GAL_R17, GAL_R16, GAL_R15};
        const double v_bias = 4.4, r_bias = GAL_R20, r_gnd = GAL_R19;
        double r_total = 1.0 / r_bias + 1.0 / r_gnd;
        for (int bit = 0; bit < 4; bit++)
            r_total += 1.0 / r[bit];
        r_total = 1.0 / r_total;
        for (int i = 0; i < 16; i++)
        {
            double i_total = v_bias / r_bias;
            for (int bit = 0; bit < 4; bit++)
                if ((i >> bit) & 1)
                    i_total += TTL_OUT / r[bit];
            s->dac_v[i] = (float)(i_total * r_total);
        }
    }

    /* DISCRETE_555_CC(NODE_105, 1, NODE_100, GAL_R21, GAL_C15, 0, 0, 0, ...) */
    s->cc_exp_bleed = rc_exp_init(st, DEFAULT_555_BLEED_R * GAL_C15);

    /* DISCRETE_555_ASTABLE_CV(NODE_115..117, GAL_INP_FSn, ...,
     *                         &galaxian_555_vco_desc): energy output, 4.5 V */
    d555_init(&s->vco[0], GAL_R22, GAL_R23, GAL_C17, 5.0f - 0.5f, DISC_555_OUT_ENERGY, s->sample_time);
    d555_init(&s->vco[1], GAL_R25, GAL_R26, GAL_C18, 5.0f - 0.5f, DISC_555_OUT_ENERGY, s->sample_time);
    d555_init(&s->vco[2], GAL_R28, GAL_R29, GAL_C19, 5.0f - 0.5f, DISC_555_OUT_ENERGY, s->sample_time);

    /* DISCRETE_MIXER3(NODE_120, ..., &galaxian_bck_mixer_desc): three 10k, cF C20 */
    s->bg_exp = rc_exp_init(st, 1.0 / (1.0 / GAL_R24 + 1.0 / GAL_R27 + 1.0 / GAL_R30) * GAL_C20);

    /* DISCRETE_NOTE(NODE_132, 1, SOUND_CLOCK, GAL_INP_PITCH, 255, 15, ...) */
    s->note_t_clock = (float)(1.0 / SOUND_CLOCK);

    /* DISCRETE_RCDISC5(NODE_155, NODE_152, GAL_INP_HIT, R35 + R36, C21) */
    s->hit_exp = rc_exp_init(st, (GAL_R35 + GAL_R36) * GAL_C21);

    /* DISCRETE_OP_AMP_FILTER(NODE_157, 1, NODE_155, 0,
     *                        DISC_OP_AMP_FILTER_IS_BAND_PASS_1M, &galaxian_bandpass_desc) */
    {
        const double r_total = par2(GAL_R35, GAL_R36);
        const double rf = GAL_R37, c1 = GAL_C22, c2 = GAL_C23;
        const double fc = 1.0 / (2 * M_PI * sqrt(r_total * rf * c1 * c2));
        const double d = (c1 + c2) / sqrt(rf / r_total * c1 * c2);
        const double gain = -rf / r_total * c2 / (c1 + c2);
        /* calculate_filter2_coefficients(), band-pass, with pre-warping */
        const double sr = samplerate;
        const double two_over_t = 2 * sr;
        const double two_over_t_sq = two_over_t * two_over_t;
        const double wc = sr * 2.0 * tan(M_PI * fc / sr);
        const double wc_sq = wc * wc;
        const double den = two_over_t_sq + d * wc * two_over_t + wc_sq;
        s->bp_a1 = (float)(2.0 * (-two_over_t_sq + wc_sq) / den);
        s->bp_a2 = (float)((two_over_t_sq - d * wc * two_over_t + wc_sq) / den);
        s->bp_b0 = (float)(d * wc * two_over_t / den * gain);
        s->bp_b1 = 0;
        s->bp_b2 = -s->bp_b0;
    }

    /* DISCRETE_RCFILTER(NODE_173, NODE_172, GAL_R47, GAL_C28) */
    s->fire_rc_exp = rc_exp_init(st, GAL_R47 * GAL_C28);
    /* DISCRETE_555_ASTABLE_CV(NODE_181, 1, GAL_R44, GAL_R45, GAL_C27, NODE_178,
     *                         &galaxian_555_fire_vco_desc): square, 1 V */
    d555_init(&s->fire_vco, GAL_R44, GAL_R45, GAL_C27, 1.0f, DISC_555_OUT_SQW, s->sample_time);
    /* DISCRETE_RCDISC5(NODE_182, NODE_181, NODE_171, GAL_R41, GAL_C25) */
    s->fire_exp = rc_exp_init(st, GAL_R41 * GAL_C25);

    /* mooncrst_mixer_desc: the fire input has C26 in series; the output has
     * C46, against MAME's assumed 100k of amplifier input */
    s->mix_exp_fire = rc_exp_init(st, GAL_R43 * GAL_C26);
    s->mix_exp_amp = rc_exp_init(st, RES_K(100) * GAL_C46);

    s->gain = 1.0f;
    mcr_sound_reset(s);
}

void mcr_sound_reset(mcr_sound_t *s)
{
    s->lfo = 0;
    s->fs[0] = s->fs[1] = s->fs[2] = 0;
    s->hit = s->fire = s->vol1 = s->vol2 = 0;
    s->pitch = 0;
    s->note_count1 = 0;
    s->note_count2 = 0;
    s->bp_out = (float)BP_VREF;
}

void MCR_HOT(mcr_sound_lfo_w)(mcr_sound_t *s, int offset, uint8_t data)
{
    s->lfo = (uint8_t)((s->lfo & ~(1 << offset)) | ((data & 1) << offset));
}

void MCR_HOT(mcr_sound_w)(mcr_sound_t *s, int offset, uint8_t data)
{
    data &= 1;
    switch (offset & 7)
    {
    case 0: /* FS1 (555 timer at 8R) */
    case 1: /* FS2 (8S) */
    case 2: /* FS3 (8T) */
        s->fs[offset & 7] = data;
        break;
    case 3:
        s->hit = data;
        break;
    case 4: /* n/c */
        break;
    case 5:
        s->fire = data;
        break;
    case 6:
        s->vol1 = data;
        break;
    case 7:
        s->vol2 = data;
        break;
    }
}

void MCR_HOT(mcr_sound_pitch_w)(mcr_sound_t *s, uint8_t data)
{
    s->pitch = data;
}

/* One sample of mooncrst_discrete, in the node order of the MAME netlist.
 * Returns NODE_280 in volts. */
static float MCR_HOT(discrete_sample)(mcr_sound_t *s)
{
    const float st = s->sample_time;

    /* NOISE */
    const int noise = noise_step(s); /* NODE_152 */

    /* BACKGROUND */
    const float n100 = s->dac_v[s->lfo & 15];
    const float n105 = cc_step(s, n100);
    /* DISCRETE_MULTADD and DISCRETE_CLAMP: the op-amp around R31-R33 */
    float n111 = n105 * (float)(GAL_R33 / (1.0 / (1.0 / GAL_R31 + 1.0 / GAL_R32 + 1.0 / GAL_R33))) -
                 (float)(5.0 * GAL_R33 / GAL_R31);
    if (n111 < 0)
        n111 = 0;
    else if (n111 > 5.0f)
        n111 = 5.0f;
    const float n115 = d555_step(&s->vco[0], s->fs[0], n111, st);
    const float n116 = d555_step(&s->vco[1], s->fs[1], n111, st);
    const float n117 = d555_step(&s->vco[2], s->fs[2], n111, st);
    /* equal resistors: the average, then the cF low-pass */
    const float n120_in = (n115 + n116 + n117) * (1.0f / 3.0f);
    s->bg_vcap += (n120_in - s->bg_vcap) * s->bg_exp;
    const float n120 = s->bg_vcap;

    /* PITCH: QA, QC and QD of the 74393 */
    const int count = note_step(s);
    const float qa = (count & 1) ? TTL_OUT : 0;
    const float qc = (count & 4) ? TTL_OUT : 0;
    const float qd = (count & 8) ? TTL_OUT : 0;

    /* HIT */
    const float n155 = rcdisc5_step(&s->hit_vcap, noise, s->hit ? TTL_OUT : 0, s->hit_exp);
    {
        const float vref = (float)BP_VREF;
        /* Millman of R35 (from NODE_155) and R36 (from ground) */
        const float v = ((n155 - vref) * (float)(1.0 / GAL_R35) + (0 - vref) * (float)(1.0 / GAL_R36)) *
                        (float)par2(GAL_R35, GAL_R36);
        float v_out = -s->bp_a1 * s->bp_y1 - s->bp_a2 * s->bp_y2 + s->bp_b0 * v + s->bp_b1 * s->bp_x1 +
                      s->bp_b2 * s->bp_x2 + vref;
        s->bp_x2 = s->bp_x1;
        s->bp_x1 = v;
        s->bp_y2 = s->bp_y1;
        /* clip to the rails */
        if (v_out > (float)BP_VP)
            v_out = (float)BP_VP;
        if (v_out < 0)
            v_out = 0;
        s->bp_y1 = v_out - vref;
        s->bp_out = v_out;
    }
    const float n157 = s->bp_out;

    /* FIRE */
    const float n171 = s->fire ? TTL_OUT : 0;
    const float n172 = s->fire ? 0 : TTL_OUT;
    s->fire_rc_v += (n172 - s->fire_rc_v) * s->fire_rc_exp; /* NODE_173 */
    const float n178 = (float)par2(GAL_R46, GAL_R48) *
                       (noise * TTL_OUT * (float)(1.0 / GAL_R46) + s->fire_rc_v * (float)(1.0 / GAL_R48));
    const float n181 = d555_step(&s->fire_vco, 1, n178, st);
    const float n182 = rcdisc5_step(&s->fire_vcap, n181 != 0, n171, s->fire_exp);

    /* MIXER7, mooncrst_mixer_desc: VOL1 and VOL2 switch R49 and R52 in */
    float r_total = (float)(1.0 / GAL_R51 + 1.0 / GAL_R50 + 1.0 / MCRST_R34 + 1.0 / GAL_R40 + 1.0 / GAL_R43);
    float i = qa * (float)(1.0 / GAL_R51) + qc * (float)(1.0 / GAL_R50) + n120 * (float)(1.0 / MCRST_R34) +
              n157 * (float)(1.0 / GAL_R40);
    if (s->vol1)
    {
        r_total += (float)(1.0 / GAL_R49);
        i += qc * (float)(1.0 / GAL_R49);
    }
    if (s->vol2)
    {
        r_total += (float)(1.0 / GAL_R52);
        i += qd * (float)(1.0 / GAL_R52);
    }
    s->mix_vcap_fire += (n182 - s->mix_vcap_fire) * s->mix_exp_fire;
    i += (n182 - s->mix_vcap_fire) * (float)(1.0 / GAL_R43);
    float v = i / r_total;
    s->mix_vcap_amp += (v - s->mix_vcap_amp) * s->mix_exp_amp;
    return v - s->mix_vcap_amp;
}

void MCR_HOT(mcr_sound_render)(mcr_sound_t *s, int16_t *out, int samples)
{
    /* DISCRETE_OUTPUT(NODE_280, 32767.0/5.0*5): 1 V is full scale */
    const float scale = 32767.0f * s->gain;
    for (int n = 0; n < samples; n++)
    {
        float o = discrete_sample(s) * scale;
        if (o > 32767.0f)
            o = 32767.0f;
        else if (o < -32768.0f)
            o = -32768.0f;
        out[n] = (int16_t)o;
    }
}
