/*
 * Sound, as upstream Galagino synthesises it (galagino.ino, snd_render_buffer
 * and audio_namco_waveregs_parse), at the board's sample rate instead of
 * Galagino's 24 kHz.
 *
 * Levels are Galagino's: the sum of the voices (about +/-512 at most) times
 * its MASTER_VOLUME of 64.
 */
#include "gal_internal.h"

#include <math.h>

#define MASTER_VOLUME 64

/* ---------------------------------------------------------------------------
 * Namco WSG: three voices of 32-step, 4-bit waveforms from the sound PROM.
 *
 * The chip runs at 96 kHz (3.072 MHz / 32): each step a voice adds its 20-bit
 * frequency to its accumulator and plays step (acc >> 15) & 31. Galagino at
 * 24 kHz adds the frequency once per sample and reads (cnt >> 13): four chip
 * steps per sample. Here the accumulator is kept in 1/256 chip steps, so any
 * sample rate works: acc += freq * (96000 * 256 / rate), step (acc >> 23) & 31.
 * ------------------------------------------------------------------------ */

static uint32_t wsg_acc[3];

void gal_wsg_reset(void)
{
    memset(wsg_acc, 0, sizeof(wsg_acc));
}

void GAL_HOT(gal_wsg_render)(const int8_t *waves, uint8_t wave_mask, int16_t *out, int n)
{
    const uint8_t *regs = gal.soundregs;
    const uint32_t step = (uint32_t)((96000u * 256u + gal.samplerate / 2) / gal.samplerate);

    uint32_t inc[3];
    int vol[3];
    const int8_t *wave[3];
    for (int ch = 0; ch < 3; ch++)
    {
        vol[ch] = regs[ch * 5 + 0x15];
        uint32_t freq = (ch == 0) ? regs[0x10] : 0;
        freq += regs[ch * 5 + 0x11] << 4;
        freq += regs[ch * 5 + 0x12] << 8;
        freq += regs[ch * 5 + 0x13] << 12;
        freq += regs[ch * 5 + 0x14] << 16;
        inc[ch] = freq * step;
        wave[ch] = waves + 32 * (regs[ch * 5 + 0x05] & wave_mask);
    }

    for (int i = 0; i < n; i++)
    {
        int v = 0;
        for (int ch = 0; ch < 3; ch++)
        {
            if (vol[ch])
                v += vol[ch] * wave[ch][(wsg_acc[ch] >> 23) & 0x1f];
            wsg_acc[ch] += inc[ch];
        }
        out[i] = (int16_t)(v * MASTER_VOLUME);
    }
}

/* ---------------------------------------------------------------------------
 * Namco 15XX (Mappy, Tower of Druaga, Gaplus): eight voices of eight
 * registers in the sound CPU's RAM, from MAME's namco.cpp: +3 the volume,
 * +4..+6 a 20-bit frequency, +6 bits 4-6 the waveform. The chip runs at
 * 24 kHz (18.432 MHz / 768) and plays step (acc >> 15) & 31; the
 * accumulator is kept in 1/256 chip steps as above. Halved, as GalaginoPlus
 * does, for the eight voices.
 * ------------------------------------------------------------------------ */

static uint32_t wsg15_acc[8];

void gal_wsg15_reset(void)
{
    memset(wsg15_acc, 0, sizeof(wsg15_acc));
}

void gal_wsg15_render(const uint8_t *regs, const int8_t *waves, int enabled, int16_t *out, int n)
{
    const uint32_t step = (uint32_t)((24000u * 256u + gal.samplerate / 2) / gal.samplerate);
    uint32_t inc[8];
    int vol[8];
    const int8_t *wave[8];
    for (int ch = 0; ch < 8; ch++)
    {
        const uint8_t *r = regs + 8 * ch;
        vol[ch] = enabled ? (r[3] & 0x0f) : 0;
        inc[ch] = (r[4] | (r[5] << 8) | ((uint32_t)(r[6] & 0x0f) << 16)) * step;
        wave[ch] = waves + 32 * ((r[6] >> 4) & 7);
    }
    for (int i = 0; i < n; i++)
    {
        int v = 0;
        for (int ch = 0; ch < 8; ch++)
        {
            if (vol[ch])
                v += vol[ch] * wave[ch][(wsg15_acc[ch] >> 23) & 0x1f];
            wsg15_acc[ch] += inc[ch];
        }
        out[i] = (int16_t)(v * (MASTER_VOLUME / 2));
    }
}

/* ---------------------------------------------------------------------------
 * Sample voices: Galaga's explosion and Donkey Kong's walk, jump and stomp,
 * recorded at their own rates and stepped through at the output rate.
 * Galagino adds them unscaled (Galaga) or shifted down (Donkey Kong) to the
 * voice sum before MASTER_VOLUME.
 * ------------------------------------------------------------------------ */

void gal_voice_start(gal_voice_t *v, const int8_t *data, uint32_t len, int rate, int shift)
{
    v->data = data;
    v->len = len << 16;
    v->pos = 0;
    v->step = (uint32_t)(((uint64_t)rate << 16) / (uint32_t)gal.samplerate);
    v->shift = (uint8_t)shift;
}

void GAL_HOT(gal_voice_mix)(gal_voice_t *v, int16_t *out, int n)
{
    if (!v->data)
        return;
    for (int i = 0; i < n; i++)
    {
        if (v->pos >= v->len)
        {
            v->data = NULL;
            return;
        }
        int s = out[i] + (v->data[v->pos >> 16] >> v->shift) * MASTER_VOLUME;
        out[i] = (int16_t)(s > 32767 ? 32767 : (s < -32768 ? -32768 : s));
        v->pos += v->step;
    }
}

/* ---------------------------------------------------------------------------
 * AY-3-8910 (Frogger: one, 1942: two, Bomb Jack: three; up to GAL_AY_MAX), as
 * Galagino synthesises it: tone and
 * noise, linear volume, no envelopes. The registers of chip k are
 * soundregs[16 * k ...].
 *
 * The tone counters run at clock / 8 and flip the output after `period`
 * counts. Galagino advances them by clock / 8 / 24000 per sample, rounded
 * (9 for Frogger, 8 for 1942); here the step is kept in 1/256 counts.
 *
 * A channel can have a one-pole low-pass, the RC filter a board puts on its
 * output (MAME's filter_rc): y += (x - y) * (1 - exp(-1 / (R C rate))). The
 * sum is saturated, not wrapped, when the channels together exceed 16 bits.
 * ------------------------------------------------------------------------ */

#define AY_LP_SHIFT 12 /* the coefficient's fraction bits */

static struct
{
    int32_t cnt[GAL_AY_MAX][4];
    int8_t toggle[GAL_AY_MAX][3];
    uint32_t rng[GAL_AY_MAX];
    int16_t lp_k[GAL_AY_MAX][3]; /* 0: no filter */
    int32_t lp_y[GAL_AY_MAX][3]; /* the filter's output, in 1/256 */
} ay;

void gal_ay_reset(void)
{
    memset(&ay, 0, sizeof(ay));
    for (int k = 0; k < GAL_AY_MAX; k++)
    {
        ay.rng[k] = 1;
        for (int c = 0; c < 3; c++)
            ay.toggle[k][c] = 1;
    }
}

int gal_ay_lowpass_k(double rc)
{
    if (rc <= 0)
        return 0;
    const int coef = (int)((1 << AY_LP_SHIFT) * (1.0 - exp(-1.0 / (rc * gal.samplerate))) + 0.5);
    return coef < 1 ? 1 : coef;
}

void gal_ay_lowpass(int k, int c, int coef)
{
    ay.lp_k[k][c] = (int16_t)coef;
}

/* The 17-bit noise LFSR stepped s times. A step shifts right and puts
 * bit 0 ^ bit 3 in at bit 16; for up to 14 steps the bits fed back all come
 * from the register as it was, so the steps can be taken together. */
static inline uint32_t ay_noise_steps(uint32_t rng, int32_t s)
{
    while (s > 14)
    {
        rng = (rng >> 14) | (((rng ^ (rng >> 3)) & 0x3fff) << 3);
        s -= 14;
    }
    return (rng >> s) | (((rng ^ (rng >> 3)) & ((1u << s) - 1)) << (17 - s));
}

void GAL_HOT(gal_ay_render)(int chips, int clock, int ay_vol, int16_t *out, int n)
{
    const int32_t inc = (int32_t)((((int64_t)clock / 8) * 256 + gal.samplerate / 2) / gal.samplerate);
    /* the channels that sound, and the filtered ones, which settle when
     * silent: a channel sounds if period != 0, volume != 0 and it is enabled
     * (bit 0 tone, bit 1 noise; the mixer register enables them with a 0) */
    struct
    {
        int32_t period, amp, lp_k;
        uint8_t k, c, enable;
    } ch[3 * GAL_AY_MAX];
    int nch = 0;
    int32_t noise_period[GAL_AY_MAX];
    for (int k = 0; k < chips; k++)
    {
        const uint8_t *r = gal.soundregs + 16 * k;
        for (int c = 0; c < 3; c++)
        {
            const int32_t period = (r[2 * c] + 256 * (r[2 * c + 1] & 15)) << 8;
            const uint8_t enable = (uint8_t)((((r[7] >> c) & 1) | ((r[7] >> (c + 2)) & 2)) ^ 3);
            const int volume = r[8 + c] & 0x0f;
            const int sounds = period && volume && enable;
            if (sounds || (ay.lp_k[k][c] && ay.lp_y[k][c]))
            {
                ch[nch].period = period;
                ch[nch].amp = sounds ? ay_vol * volume : 0;
                ch[nch].lp_k = ay.lp_k[k][c];
                ch[nch].k = (uint8_t)k;
                ch[nch].c = (uint8_t)c;
                ch[nch].enable = enable;
                nch++;
            }
        }
        noise_period[k] = (r[6] & 0x1f) << 8;
    }

    for (int i = 0; i < n; i++)
    {
        for (int k = 0; k < chips; k++)
        {
            const int32_t p = noise_period[k];
            if (p)
            {
                ay.cnt[k][3] += inc;
                if (ay.cnt[k][3] >= p)
                {
                    const int32_t steps = ay.cnt[k][3] / p;
                    ay.cnt[k][3] -= steps * p;
                    ay.rng[k] = ay_noise_steps(ay.rng[k], steps);
                }
            }
        }
        int32_t v = 0; /* in 1/256 */
        for (int j = 0; j < nch; j++)
        {
            const int k = ch[j].k, c = ch[j].c;
            int32_t x = 0;
            if (ch[j].amp)
            {
                int bit = 1;
                if (ch[j].enable & 1)
                    bit &= ay.toggle[k][c] > 0;
                if (ch[j].enable & 2)
                    bit &= ay.rng[k] & 1;
                x = (bit ? ch[j].amp : -ch[j].amp) << 8;

                ay.cnt[k][c] += inc;
                while (ay.cnt[k][c] >= ch[j].period)
                {
                    ay.cnt[k][c] -= ch[j].period;
                    ay.toggle[k][c] = (int8_t)-ay.toggle[k][c];
                }
            }
            if (ch[j].lp_k)
            {
                /* once the step rounds to 0 it has settled: no remainder
                 * is left behind as a DC offset */
                const int32_t step = ((x - ay.lp_y[k][c]) * ch[j].lp_k) >> AY_LP_SHIFT;
                ay.lp_y[k][c] = step ? ay.lp_y[k][c] + step : x;
                x = ay.lp_y[k][c];
            }
            v += x;
        }
        const int32_t s = (v * MASTER_VOLUME) >> 8;
        out[i] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
    }
}

/* ---------------------------------------------------------------------------
 * SN76489 / SN76496 (Van-Van Car, Lady Bug, Mr. Do!, Star Force, Circus
 * Charlie), after MAME's sn76496.cpp: three square-wave tones and a noise
 * LFSR, stepped at clock / 16 and averaged over each output sample, so high
 * tones do not alias. Each channel is at most 8191, as in MAME.
 * ------------------------------------------------------------------------ */

void gal_sn_init(gal_sn_t *c, int sn76489, uint32_t clock)
{
    memset(c, 0, sizeof(*c));
    c->feedback = sn76489 ? 0x4000 : 0x10000;
    c->tap1 = sn76489 ? 0x01 : 0x04;
    c->tap2 = sn76489 ? 0x02 : 0x08;
    c->negate = (uint8_t)(sn76489 != 0);
    double v = 32767.0 / 4;
    for (int i = 0; i < 15; i++, v /= 1.258925412) /* 2 dB a step */
        c->vol_table[i] = (int16_t)v;
    c->vol_table[15] = 0;
    for (int i = 0; i < 4; i++)
    {
        c->vol[i] = 0;
        c->period[i] = (i == 3) ? 0 : 0x400;
        c->count[i] = 0;
        c->out[i] = 0;
    }
    c->rng = c->feedback;
    c->out[3] = c->rng & 1;
    c->step = (uint32_t)(((uint64_t)clock << 16) / 16 / (uint32_t)gal.samplerate);
}

void gal_sn_write(gal_sn_t *c, uint8_t data)
{
    int r;
    if (data & 0x80)
    {
        r = (data & 0x70) >> 4;
        c->last = (uint8_t)r;
        c->reg[r] = (uint16_t)((c->reg[r] & 0x3f0) | (data & 0x0f));
    }
    else
        r = c->last;
    const int ch = r >> 1;
    switch (r)
    {
    case 0:
    case 2:
    case 4: /* tone frequency */
        if (!(data & 0x80))
            c->reg[r] = (uint16_t)((c->reg[r] & 0x0f) | ((data & 0x3f) << 4));
        c->period[ch] = c->reg[r] ? c->reg[r] : 0x400;
        if (r == 4 && (c->reg[6] & 3) == 3)
            c->period[3] = c->period[2] << 1;
        break;
    case 1:
    case 3:
    case 5:
    case 7: /* volume */
        c->vol[ch] = c->vol_table[data & 0x0f];
        if (!(data & 0x80))
            c->reg[r] = (uint16_t)((c->reg[r] & 0x3f0) | (data & 0x0f));
        break;
    default: /* 6: noise frequency and mode */
    {
        if (!(data & 0x80))
            c->reg[r] = (uint16_t)((c->reg[r] & 0x3f0) | (data & 0x0f));
        const int nf = c->reg[6];
        c->period[3] = ((nf & 3) == 3) ? (c->period[2] << 1) : (1 << (5 + (nf & 3)));
        c->rng = c->feedback;
        break;
    }
    }
}

static inline int sn_tick(gal_sn_t *c)
{
    for (int i = 0; i < 3; i++)
        if (--c->count[i] <= 0)
        {
            c->out[i] ^= 1;
            c->count[i] = c->period[i];
        }
    if (--c->count[3] <= 0)
    {
        /* white noise taps both bits, periodic noise only the first */
        const int t1 = (c->rng & c->tap1) != 0;
        const int t2 = ((c->rng & c->tap2) != 0) && (c->reg[6] & 4);
        c->rng >>= 1;
        if (t1 != t2)
            c->rng |= c->feedback;
        c->out[3] = c->rng & 1;
        c->count[3] = c->period[3];
    }
    return (c->out[0] ? c->vol[0] : 0) + (c->out[1] ? c->vol[1] : 0) + (c->out[2] ? c->vol[2] : 0) +
           (c->out[3] ? c->vol[3] : 0);
}

void GAL_HOT(gal_sn_render)(gal_sn_t *chips, int nchips, int gain_q8, int16_t *out, int n)
{
    for (int i = 0; i < n; i++)
    {
        int32_t sum = 0;
        for (int k = 0; k < nchips; k++)
        {
            gal_sn_t *c = &chips[k];
            c->phase += c->step;
            int ticks = (int)(c->phase >> 16);
            c->phase &= 0xffff;
            int32_t acc = 0;
            for (int t = 0; t < ticks; t++)
                acc += sn_tick(c);
            int32_t v = ticks ? acc / ticks
                              : (c->out[0] ? c->vol[0] : 0) + (c->out[1] ? c->vol[1] : 0) +
                                    (c->out[2] ? c->vol[2] : 0) + (c->out[3] ? c->vol[3] : 0);
            sum += c->negate ? -v : v;
        }
        /* the chips' output is one-sided: a DC blocker, y = x - x' + 0.996 y' */
        gal_sn_t *c0 = &chips[0];
        const int32_t y = sum - c0->dc_x + ((c0->dc_y * 255) >> 8);
        c0->dc_x = sum;
        c0->dc_y = y;
        sum = (y * gain_q8) >> 8;
        out[i] = (int16_t)(sum > 32767 ? 32767 : sum < -32768 ? -32768 : sum);
    }
}

/* ---------------------------------------------------------------------------
 * Z80 CTC (Pinball Action, Star Force), the parts the games use: a channel
 * counts the CPU's clock through a prescaler of 16 or 256 (timer) or pulses on
 * its trigger (counter) down from its time constant; at zero it reloads and,
 * with interrupts enabled, requests one. Control word bit 1 holds the channel
 * in reset until a time constant comes.
 * ------------------------------------------------------------------------ */

static int ctc_running(const gal_ctc_t *c, int ch)
{
    return c->constant[ch] && !(c->control[ch] & 0x02) && !c->want_tc[ch];
}

int32_t gal_ctc_period(const gal_ctc_t *c, int ch)
{
    if (!ctc_running(c, ch) || (c->control[ch] & 0x40))
        return 0;
    return c->constant[ch] * ((c->control[ch] & 0x20) ? 256 : 16);
}

void gal_ctc_write(gal_ctc_t *c, int ch, uint8_t value)
{
    if (c->want_tc[ch])
    {
        c->want_tc[ch] = 0;
        c->control[ch] &= (uint8_t)~0x02; /* the constant ends a reset */
        c->constant[ch] = value ? value : 256;
        c->count[ch] = (c->control[ch] & 0x40) ? c->constant[ch] : gal_ctc_period(c, ch);
        return;
    }
    if (value & 1)
    {
        c->control[ch] = value;
        c->want_tc[ch] = (value & 0x04) != 0;
        if (!(value & 0x80))
            c->pending &= (uint8_t)~(1u << ch);
    }
    else if (ch == 0)
        c->vector = value & 0xf8;
}

uint8_t gal_ctc_read(const gal_ctc_t *c, int ch)
{
    const int32_t v = (c->control[ch] & 0x40) ? c->count[ch] : c->count[ch] / ((c->control[ch] & 0x20) ? 256 : 16);
    return (uint8_t)v;
}

static void ctc_zero(gal_ctc_t *c, int ch)
{
    if (c->control[ch] & 0x80)
        c->pending |= (uint8_t)(1u << ch);
    if (ch < 3 && (c->chain & (1u << ch)))
        gal_ctc_trigger(c, ch + 1);
}

void gal_ctc_trigger(gal_ctc_t *c, int ch)
{
    if (!ctc_running(c, ch) || !(c->control[ch] & 0x40))
        return;
    if (--c->count[ch] <= 0)
    {
        c->count[ch] = c->constant[ch];
        ctc_zero(c, ch);
    }
}

void gal_ctc_clock(gal_ctc_t *c, int cycles)
{
    for (int ch = 0; ch < 4; ch++)
    {
        const int32_t period = gal_ctc_period(c, ch);
        if (!period)
            continue;
        c->count[ch] -= cycles;
        while (c->count[ch] <= 0)
        {
            c->count[ch] += period;
            ctc_zero(c, ch);
        }
    }
}

int gal_ctc_take(gal_ctc_t *c)
{
    for (int ch = 0; ch < 4; ch++)
        if (c->pending & (1u << ch))
        {
            c->pending &= (uint8_t)~(1u << ch);
            return c->vector | (ch << 1);
        }
    return -1;
}
