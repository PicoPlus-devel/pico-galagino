// license:BSD-3-Clause
// copyright-holders:Juergen Buchmueller, Derrick Renaud
/*
 * Phoenix sound board, ported from MAME's phoenix_a.cpp. See phoenix_sound.h.
 *
 * All analog maths is single precision: the RP2350's FPU has no double
 * support. The RC exponents use expm1 and log1p so that the long time
 * constants (up to a second) keep their accuracy at a 44.1 kHz step; the
 * versions in phx_math.h are inlined so that the per-sample path never leaves
 * SRAM.
 */
#include "phoenix_sound.h"

#include <math.h>
#include <string.h>

#include "phx_math.h"
#include "phx_port.h"

#define VMIN 0
#define VMAX 32767

#define RES_K(r) ((float)(r) * 1e3f)
#define CAP_U(c) ((float)(c) * 1e-6f)

#define DEFAULT_TTL_V_LOGIC_1 3.4f
#define OP_AMP_VP_RAIL_OFFSET 1.5f

#define DISC_555_OUT_ENERGY 0x04
#define DISC_555_OUT_COUNT_F_X 0x06

/* 1 - exp(-dt/rc), the per-step charge fraction (MAME RC_CHARGE_EXP) */
PHX_ALWAYS_INLINE float rc_charge_exp(float dt, float rc)
{
    return -phx_expm1f(-dt / rc);
}

static float par2(float a, float b)
{
    return 1.0f / (1.0f / a + 1.0f / b);
}

/* ---------------------------------------------------------------------------
 * Custom noise generator (phoenix_sound_device)
 * ------------------------------------------------------------------------ */

PHX_ALWAYS_INLINE int update_c24(phx_sound_t *s)
{
    /* Bit 6 lo charges C24 (6.8u) via R51 (330) and when bit 6 is hi, C24 is
     * discharged through R52 (20k) in approx. 20000 * 6.8e-6 = 0.136 s */
    /* 1/(R*C) for discharge (R52) and charge (R51 + R49) */
    const float k_dis = 1.0f / (20000.0f * 6.8e-6f);
    const float k_chg = 1.0f / ((330.0f + 1000.0f) * 6.8e-6f);
    const int samplerate = s->samplerate;

    if (s->latch_a & 0x40)
    {
        if (s->c24_level > VMIN)
        {
            s->c24_counter -= (int)((s->c24_level - VMIN) * k_dis);
            if (s->c24_counter <= 0)
            {
                int n = -s->c24_counter / samplerate + 1;
                s->c24_counter += n * samplerate;
                if ((s->c24_level -= n) < VMIN)
                    s->c24_level = VMIN;
            }
        }
    }
    else
    {
        if (s->c24_level < VMAX)
        {
            s->c24_counter -= (int)((VMAX - s->c24_level) * k_chg);
            if (s->c24_counter <= 0)
            {
                int n = -s->c24_counter / samplerate + 1;
                s->c24_counter += n * samplerate;
                if ((s->c24_level += n) > VMAX)
                    s->c24_level = VMAX;
            }
        }
    }
    return VMAX - s->c24_level;
}

PHX_ALWAYS_INLINE int update_c25(phx_sound_t *s)
{
    /* Bit 7 hi charges C25 (6.8u) over R50 (1k) and R53 (330) and when bit 7
     * is lo, C25 is discharged through R54 (47k) in about 0.3196 s */
    /* 1/(R*C) for charge (R50 + R53) and discharge (R54) */
    const float k_chg = 1.0f / ((1000.0f + 330.0f) * 6.8e-6f);
    const float k_dis = 1.0f / (47000.0f * 6.8e-6f);
    const int samplerate = s->samplerate;

    if (s->latch_a & 0x80)
    {
        if (s->c25_level < VMAX)
        {
            s->c25_counter -= (int)((VMAX - s->c25_level) * k_chg);
            if (s->c25_counter <= 0)
            {
                int n = -s->c25_counter / samplerate + 1;
                s->c25_counter += n * samplerate;
                if ((s->c25_level += n) > VMAX)
                    s->c25_level = VMAX;
            }
        }
    }
    else
    {
        if (s->c25_level > VMIN)
        {
            s->c25_counter -= (int)((s->c25_level - VMIN) * k_dis);
            if (s->c25_counter <= 0)
            {
                int n = -s->c25_counter / samplerate + 1;
                s->c25_counter += n * samplerate;
                if ((s->c25_level -= n) < VMIN)
                    s->c25_level = VMIN;
            }
        }
    }
    return s->c25_level;
}

/* MAME precomputes 2^18 bits of an 18-bit shift register (4006 pair with
 * EXNOR feedback) into a 32 KB table and indexes it with polyoffs. Stepping
 * the register itself gives the same bit for every offset, without the table:
 * bit p of the table is bit 0 of the register after p steps from zero. */
PHX_ALWAYS_INLINE void noise_advance(phx_sound_t *s, int n)
{
    while (n-- > 0)
    {
        s->noise_polyoffs = (s->noise_polyoffs + 1) & 0x3ffff;
        if (s->noise_polyoffs == 0)
            s->noise_lfsr = 0;
        else if (((s->noise_lfsr >> 16) & 1) == ((s->noise_lfsr >> 17) & 1))
            s->noise_lfsr = (s->noise_lfsr << 1) | 1;
        else
            s->noise_lfsr <<= 1;
    }
    s->noise_polybit = s->noise_lfsr & 1;
}

PHX_ALWAYS_INLINE int noise(phx_sound_t *s)
{
    const int samplerate = s->samplerate;
    int vc24 = update_c24(s);
    int vc25 = update_c25(s);
    int sum = 0, level, frequency;

    /* The voltage levels are added and control I(CE) of transistor TR1 (NPN)
     * which then controls the noise clock frequency (linearily?). */
    if (vc24 < vc25)
        level = vc24 + (vc25 - vc24) / 2;
    else
        level = vc25 + (vc24 - vc25) / 2;

    /* NE555: Ra=47k, Rb=1k, C=0.05uF: 588 Hz .. 6325 Hz */
    frequency = 588 + 6325 * level / 32768;

    s->noise_counter -= frequency;
    if (s->noise_counter <= 0)
    {
        int n = (-s->noise_counter / samplerate) + 1;
        s->noise_counter += n * samplerate;
        noise_advance(s, n);
    }
    if (!s->noise_polybit)
        sum += vc24;

    /* 400Hz crude low pass filter: this is only a guess!! */
    s->lowpass_counter -= 400;
    if (s->lowpass_counter <= 0)
    {
        s->lowpass_counter += samplerate;
        s->lowpass_polybit = s->noise_polybit;
    }
    if (!s->lowpass_polybit)
        sum += vc25;

    return sum;
}

/* ---------------------------------------------------------------------------
 * Discrete modules
 * ------------------------------------------------------------------------ */

static void d555_init(phx_555_t *d, float r1, float r2, float v_pos, float v_out_high, int out_type, int use_cv)
{
    memset(d, 0, sizeof(*d));
    d->r1 = r1;
    d->r2 = r2;
    d->v_charge = v_pos;
    d->v_out_high = (v_out_high < 0) ? v_pos - 1.2f : v_out_high;
    d->threshold = v_pos * 2.0f / 3.0f;
    d->trigger = v_pos / 3.0f;
    d->out_type = out_type;
    d->use_cv = use_cv;
    d->flip_flop = 1;
    d->cap_v = 0;
    d->last_c = -1;
}

/* DSD_555_ASTBL step. c is the timing capacitor (it can be a node), cv the
 * control voltage node when use_cv is set. */
static float PHX_HOT(d555_step)(phx_555_t *d, float c, float cv, float sample_time)
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
    float threshold = d->threshold;
    float trigger = d->trigger;
    float v_out = 0;

    if (d->use_cv)
    {
        /* If CV is less then .25V, the circuit will oscillate way out of
         * range. MAME ignores it then, leaving the output as it was. */
        if (cv < 0.25f)
            return d->out;
        threshold = cv;
        trigger = cv / 2.0f;
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
    }

    if (c != d->last_c)
    {
        d->t_rc_charge = (d->r1 + d->r2) * c;
        d->t_rc_discharge = d->r2 * c;
        d->exp_charge = rc_charge_exp(sample_time, d->t_rc_charge);
        d->exp_discharge = rc_charge_exp(sample_time, d->t_rc_discharge);
        d->last_c = c;
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
                dt = -d->t_rc_charge * phx_log1pf_neg((v_cap_next - threshold) / (d->v_charge - v_cap));
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
                    dt = -d->t_rc_discharge * phx_log1pf_neg((trigger - v_cap_next) / v_cap);
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

    switch (d->out_type)
    {
    case DISC_555_OUT_ENERGY:
        if (x_time == 0)
            x_time = 1.0f;
        v_out = d->v_out_high * (flip_flop ? x_time : (1.0f - x_time));
        break;
    case DISC_555_OUT_COUNT_F_X:
        v_out = count_f ? count_f + x_time : count_f;
        break;
    }
    (void)count_r;
    d->out = v_out;
    d->flip_flop = flip_flop;
    return v_out;
}

/* DSS_NOTE step, DISC_CLK_BY_COUNT | DISC_OUT_IS_ENERGY */
PHX_ALWAYS_INLINE float note_step(phx_note_t *n, float clk, int data)
{
    int clock = (int)clk;
    float x_time = clk - clock;
    int last_count2 = n->count2;

    /* Count output as long as the data loaded is not already equal to max 1
     * count. */
    if (data != n->max1)
    {
        for (int i = 0; i < clock; i++)
        {
            n->count1++;
            if (n->count1 > n->max1)
            {
                /* Max 1 count reached.  Load Data into counter. */
                n->count1 = data;
                n->count2 += 1;
                if (n->count2 > n->max2)
                    n->count2 = 0;
            }
        }
    }

    float v_out = n->count2;
    if (n->count2 != last_count2)
    {
        /* the x_time is only output if the output changed. */
        if (x_time == 0)
            x_time = 1.0f;
        v_out = last_count2;
        if (n->count2 > last_count2)
            v_out += (n->count2 - last_count2) * x_time;
        else
            v_out -= (last_count2 - n->count2) * x_time;
    }
    return v_out;
}

/* ---------------------------------------------------------------------------
 * Public interface
 * ------------------------------------------------------------------------ */

void phx_sound_init(phx_sound_t *s, int samplerate)
{
    memset(s, 0, sizeof(*s));
    s->samplerate = samplerate;
    s->sample_time = 1.0f / samplerate;
    const float st = s->sample_time;

    /* --- Effect 1 ---
     * DISCRETE_RCDISC4(NODE_20, 1, EFFECT_1_FREQ, 470 (R22), 100k (R23),
     *                  33k (R24), 6.8u (C7), 12V, type 1) */
    {
        const float r1 = 470, r2 = RES_K(100), r3 = RES_K(33), c1 = CAP_U(6.8), vp = 12;
        float v = vp - 0.5f; /* diode drop */
        float r, rt, i;
        /* input 1: R1 and R3 basically in parallel */
        r = par2(r1, r3);
        rt = r2 + r;
        i = v / rt;
        s->rcdisc4_v[1] = i * r + 0.5f;
        s->rcdisc4_exp[1] = rc_charge_exp(st, par2(r2, r) * c1);
        /* input 0: R1 out of circuit */
        rt = r2 + r3;
        i = v / rt;
        s->rcdisc4_v[0] = i * r3 + 0.5f;
        s->rcdisc4_exp[0] = rc_charge_exp(st, par2(r2, r3) * c1);
        s->rcdisc4_max = vp - OP_AMP_VP_RAIL_OFFSET;
    }
    /* DISCRETE_555_ASTABLE_CV(NODE_21, 1, 47k (R25), 47k (R26), .001u (C8),
     *                         NODE_20, phoenix_effect1_555) */
    d555_init(&s->ic20a, RES_K(47), RES_K(47), 5, -1, DISC_555_OUT_COUNT_F_X, 1);
    /* DISCRETE_NOTE(NODE_22, 1, NODE_21, EFFECT_1_DATA, 0x0f, 1, ...) */
    s->note1.max1 = 0x0f;
    s->note1.max2 = 1;
    /* DISCRETE_RCFILTER(NODE_25, NODE_24, 10k || 100k (R19, R20), .047u (C5)) */
    s->exp25 = rc_charge_exp(st, par2(RES_K(10), RES_K(100)) * CAP_U(0.047));

    /* --- Effect 2 ---
     * DISCRETE_555_ASTABLE(NODE_33, 1, 47k (R40), 100k (R41), NODE_30,
     *                      phoenix_effect2_555) */
    d555_init(&s->ic44, RES_K(47), RES_K(100), 5, 4.0f, DISC_555_OUT_ENERGY, 0);
    /* DISCRETE_555_ASTABLE(NODE_34, 1, 510k, 510k, 1u (C20), ...) */
    d555_init(&s->ic51, RES_K(510), RES_K(510), 5, 4.0f, DISC_555_OUT_ENERGY, 0);
    /* DISCRETE_RCFILTER(NODE_37, NODE_36, R45 // (R46 + R42 // 555 // R42), 100u (C22)) */
    {
        float r = 1.0f / (1.0f / RES_K(5.1) + (1.0f / (RES_K(5.1) + 1.0f / (1.0f / RES_K(10) + 1.0f / RES_K(5) + 1.0f / RES_K(10)))));
        s->exp37 = rc_charge_exp(st, r * CAP_U(100));
    }
    /* DISCRETE_555_ASTABLE_CV(NODE_39, 1, 20k (R47), 20k (R48), .001u (C23),
     *                         NODE_38, phoenix_effect1_555) */
    d555_init(&s->ic50, RES_K(20), RES_K(20), 5, -1, DISC_555_OUT_COUNT_F_X, 1);
    /* DISCRETE_NOTE(NODE_40, 1, NODE_39, EFFECT_2_DATA, 0x0f, 1, ...) */
    s->note2.max1 = 0x0f;
    s->note2.max2 = 1;

    /* --- Final mixer ---
     * DISCRETE_MIXER4(NODE_90, ..., phoenix_mixer): resistor mixer,
     * r = {57k, 30k, 20k, 20k}, c = {10u, 10u, .1u, 10u}, rF = 10k (VR1),
     * cAmp = 10u (C32), gain 40000. Inputs 3 and 4 are constant 0. */
    {
        const float rF = RES_K(10);
        s->mix_exp[0] = rc_charge_exp(st, par2(RES_K(57), rF) * CAP_U(10));
        s->mix_exp[1] = rc_charge_exp(st, par2(RES_K(30), rF) * CAP_U(10));
        s->mix_rtotal = 1.0f / (1.0f / RES_K(57) + 1.0f / RES_K(30) + 1.0f / RES_K(20) + 1.0f / RES_K(20) + 1.0f / rF);
        s->mix_exp_amp = rc_charge_exp(st, RES_K(100) * CAP_U(10));
    }

    /* tms36xx_device &tms(TMS36XX(config, "tms", 372));
     * tms.set_decays(0.50, 0, 0, 1.05, 0, 0); tms.set_tune_speed(0.21); */
    static const float decays[6] = {0.50f, 0, 0, 1.05f, 0, 0};
    tms36xx_init(&s->tms, 372, samplerate, decays, 0.21f);

    s->gain = 1.0f;
}

void phx_sound_control_a(phx_sound_t *s, uint8_t data)
{
    /* EFFECT_2_DATA = data & 0x0f, EFFECT_2_FREQ = (data & 0x30) >> 4;
     * bits 6 and 7 drive the custom noise generator */
    s->latch_a = data;
}

void phx_sound_control_b(phx_sound_t *s, uint8_t data)
{
    /* EFFECT_1_DATA = data & 0x0f, EFFECT_1_FREQ = data & 0x10,
     * EFFECT_1_FILT = data & 0x20; bits 6 and 7 select the MM6221AA tune */
    s->latch_b = data;
    mm6221aa_tune_w(&s->tms, data >> 6);
}

/* One sample of phoenix_discrete, in the node order of the MAME netlist.
 * Returns NODE_90 * (1 / 32768), the level MAME's stream carries. */
static float PHX_HOT(discrete_sample)(phx_sound_t *s)
{
    const float st = s->sample_time;
    const int data1 = s->latch_b & 0x0f;
    const int freq1 = (s->latch_b & 0x10) ? 1 : 0;
    const int filt1 = (s->latch_b & 0x20) ? 1 : 0;
    const int data2 = s->latch_a & 0x0f;
    const int freq2 = (s->latch_a & 0x30) >> 4;

    /* Effect 1 - shield, bird explode, level 3&4 siren, level 5 spaceship */
    s->rcdisc4_vc += (s->rcdisc4_v[freq1] - s->rcdisc4_vc) * s->rcdisc4_exp[freq1];
    float n20 = s->rcdisc4_vc;
    if (n20 > s->rcdisc4_max)
        n20 = s->rcdisc4_max;
    if (n20 < 0)
        n20 = 0;
    float n21 = d555_step(&s->ic20a, CAP_U(0.001), n20, st);
    float n22 = note_step(&s->note1, n21, data1);
    float n23 = filt1 ? DEFAULT_TTL_V_LOGIC_1 * RES_K(100) / (RES_K(10) + RES_K(100)) : DEFAULT_TTL_V_LOGIC_1;
    float n24 = n22 * n23;
    s->n25 += (n24 - s->n25) * s->exp25;
    float e1 = filt1 ? s->n25 : n24;

    /* Effect 2 - bird flying, bird/phoenix/spaceship hit, phoenix wing hit */
    float n30 = CAP_U(0.01) + ((freq2 & 1) ? CAP_U(0.47) : 0) + ((freq2 & 2) ? CAP_U(1) : 0);
    float n32 = (freq2 & 2) ? DEFAULT_TTL_V_LOGIC_1 / 2 : DEFAULT_TTL_V_LOGIC_1;
    float n33 = d555_step(&s->ic44, n30, 0, st);
    float n34 = d555_step(&s->ic51, CAP_U(1), 0, st);
    /* MIXER3: R42 = 10k, R45+R46 = 10.2k, 555 internal 5k to 5V, rF = 10k */
    const float g35 = 1.0f / (1.0f / RES_K(10) + 1.0f / RES_K(10.2) + 1.0f / RES_K(5) + 1.0f / RES_K(10));
    float n35 = (n33 / RES_K(10) + n34 / RES_K(10.2) + 5.0f / RES_K(5)) * g35;
    /* MIXER2: R45 = R46 = 5.1k */
    float n36 = (n34 + n35) * 0.5f;
    s->n37 += (n36 - s->n37) * s->exp37;
    /* MIXER3: R42 = 10k, R46 = 5.1k, 555 internal 5k to 5V, rF = 10k */
    const float g38 = 1.0f / (1.0f / RES_K(10) + 1.0f / RES_K(5.1) + 1.0f / RES_K(5) + 1.0f / RES_K(10));
    float n38 = (n33 / RES_K(10) + s->n37 / RES_K(5.1) + 5.0f / RES_K(5)) * g38;
    float n39 = d555_step(&s->ic50, CAP_U(0.001), n38, st);
    float n40 = note_step(&s->note2, n39, data2);
    float e2 = n40 * n32;

    /* MIXER4 with input high-pass caps and output cap C32 */
    float v1 = e1, v2 = e2;
    s->mix_vcap[0] += (v1 - s->mix_vcap[0]) * s->mix_exp[0];
    v1 -= s->mix_vcap[0];
    s->mix_vcap[1] += (v2 - s->mix_vcap[1]) * s->mix_exp[1];
    v2 -= s->mix_vcap[1];
    float v = (v1 / RES_K(57) + v2 / RES_K(30)) * s->mix_rtotal;
    s->mix_vcap_amp += (v - s->mix_vcap_amp) * s->mix_exp_amp;
    v -= s->mix_vcap_amp;

    return v * 40000.0f * (1.0f / 32768.0f);
}

void PHX_HOT(phx_sound_render)(phx_sound_t *s, int16_t *out, int samples)
{
    const float tms_scale = 1.0f / (32768.0f * (s->tms.voices ? s->tms.voices : 1));

    for (int i = 0; i < samples; i++)
    {
        /* the three routes into the mono speaker, with MAME's gains */
        float tms = tms36xx_sample(&s->tms) * tms_scale;
        int cust_i = noise(s) / 2;
        float cust = cust_i * (1.0f / 32768.0f);
        if (cust > 1.0f)
            cust = 1.0f;
        float disc = discrete_sample(s);
        float mix = 0.5f * tms + 0.4f * cust + 0.6f * disc;

        /* The melody and noise routes are unipolar; block their DC so the
         * stream sits around zero (one pole at ~5 Hz). */
        float y = mix - s->dc_x + 0.9993f * s->dc_y;
        s->dc_x = mix;
        s->dc_y = y;

        float o = y * s->gain * 32767.0f;
        if (o > 32767.0f)
            o = 32767.0f;
        else if (o < -32768.0f)
            o = -32768.0f;
        out[i] = (int16_t)o;
    }
}
