/*
 * Space Invaders (Taito / Midway, 1978), after MAME's midw8080/mw8080bw.cpp.
 *
 * An 8080 at 1.9968 MHz (the 8085 core of Phoenix in 8080 mode), 33536
 * cycles per 59.54 Hz frame of 262 lines. RST 1 at line 96 and RST 2 at line
 * 224, held until the CPU enables interrupts. A 256x224 bitmap of one bit per
 * pixel at 2400-3fff, turned upright (MAME ROT270) into 224x256, drawn at y
 * 16..271; the colours are the cabinet's strips of film over the monitor. An
 * MB14241 shifter on ports 2, 3 and 4 helps the game draw.
 *
 * The sound board is discrete (MAME: a netlist and an SN76477); here it is
 * GalaginoPlus's approximation of it, with its recordings of the shot and of
 * an invader hit, and the fleet's four notes at the frequencies of MAME's 555.
 */
#include "gal_internal.h"

#include "i8085.h"

#include "data/spaceinvaders_samples.h"

#define CYCLES_PER_LINE 128
#define LINES 262
#define CYCLES_PER_FRAME (CYCLES_PER_LINE * LINES) /* 33536 */
#define VIEW_TOP 16

/* Galagino's RAM buffer holds 2000-3fff */
#define VRAM 0x0400

#define IM_IE 0x08 /* i8085.c: the interrupt enable in the mask register */

static struct
{
    i8085_t *cpu;
    uint8_t *rom;
    uint16_t shift;
    uint8_t shift_count;
    uint8_t pending; /* RST vector waiting for interrupts to be enabled, or 0 */
    uint8_t p3, p5;  /* the sound ports */
    uint16_t white, red, green;

    /* sound */
    gal_voice_t shot, hit;
    uint32_t ufo_phase, ufo_slf;
    int ufo_level;
    uint32_t fleet_phase;
    int32_t fleet_lp1, fleet_lp2;
    uint32_t noise_clock, noise_rng;
    int noise_out;
    int explo_env, explo_count;
    uint32_t bonus_phase, bonus_gate;
    int ufohit_freq;
    uint32_t ufohit_phase, ufohit_warble;
    int32_t dc_x, dc_y;
} si;


/* --- CPU ----------------------------------------------------------------------- */

static uint8_t si_read(i8085_t *cpu, uint16_t addr)
{
    (void)cpu;
    (void)addr;
    return 0;
}

static void si_write(i8085_t *cpu, uint16_t addr, uint8_t value)
{
    (void)cpu;
    (void)addr;
    (void)value;
}

/* the player's controls on bits 4-6: fire, left, right */
static uint8_t controls(void)
{
    uint8_t v = 0;
    if (gal.buttons & GAL_BTN_FIRE)
        v |= 0x10;
    if (gal.buttons & GAL_BTN_LEFT)
        v |= 0x20;
    if (gal.buttons & GAL_BTN_RIGHT)
        v |= 0x40;
    return v;
}

static uint8_t si_in(i8085_t *cpu, uint8_t port)
{
    (void)cpu;
    switch (port & 7)
    {
    case 0:
    case 4:
        return 0x08 | controls();
    case 1:
    case 5:
    {
        /* coin (active low), start 2, start 1 */
        uint8_t v = 0x08 | controls();
        if (!(gal.buttons & GAL_BTN_COIN))
            v |= 0x01;
        if (gal.buttons & GAL_BTN_START2)
            v |= 0x02;
        if (gal.buttons & GAL_BTN_START)
            v |= 0x04;
        return v;
    }
    case 2:
    case 6:
        /* MAME's defaults: 3 lives, bonus at 1500, coinage not shown */
        return 0x00 | controls();
    default:
        return (uint8_t)(si.shift >> (8 - si.shift_count));
    }
}

static void si_out(i8085_t *cpu, uint8_t port, uint8_t value)
{
    (void)cpu;
    switch (port & 7)
    {
    case 2:
        si.shift_count = value & 7;
        break;
    case 3:
        if ((value & 0x02) && !(si.p3 & 0x02))
            gal_voice_start(&si.shot, si_sample_shot, si_sample_shot_LEN, 12000, 0);
        if ((value & 0x08) && !(si.p3 & 0x08))
            gal_voice_start(&si.hit, si_sample_invhit, si_sample_invhit_LEN, 12000, 0);
        si.p3 = value;
        break;
    case 4:
        si.shift = (uint16_t)((si.shift >> 8) | (value << 8));
        break;
    case 5:
        si.p5 = value;
        break;
    default:
        break; /* 6: the watchdog */
    }
}

/* An RST n, as the interrupt acknowledge puts it on the bus */
static void interrupt(uint8_t vector)
{
    i8085_t *c = si.cpu;
    if (!(c->im & IM_IE))
    {
        si.pending = vector;
        return;
    }
    si.pending = 0;
    c->im &= ~IM_IE;
    if (c->halt)
    {
        c->halt = 0;
        c->pc.w++;
    }
    c->sp.w -= 2;
    uint8_t *lo = c->wr_page[c->sp.w >> 8], *hi = c->wr_page[(uint16_t)(c->sp.w + 1) >> 8];
    lo[c->sp.w & 0xff] = c->pc.b.l;
    hi[(c->sp.w + 1) & 0xff] = c->pc.b.h;
    c->pc.w = vector & 0x38;
    c->icount -= 11;
}

/* Runs to line `to` of the frame a line at a time, so a held interrupt is
 * taken soon after the game enables them. */
static void run_lines(int from, int to)
{
    for (int l = from; l < to; l++)
    {
        if (si.pending && (si.cpu->im & IM_IE))
            interrupt(si.pending);
        i8085_run(si.cpu, CYCLES_PER_LINE);
    }
}

/* --- Set-up --------------------------------------------------------------------- */

static int si_init(uint8_t *const *regions)
{
    memset(&si, 0, sizeof(si));
    si.rom = regions[0];
    si.cpu = (i8085_t *)gal_alloc(sizeof(i8085_t));
    if (!si.cpu)
        return 0;
    i8085_init(si.cpu, 0);
    i8085_t *c = si.cpu;
    /* A15 is not decoded: 0000-1fff ROM, 2000-3fff RAM, 4000-5fff no ROM,
     * 6000-7fff RAM again */
    for (int m = 0; m < 0x100; m += 0x80)
    {
        for (int p = 0; p < 0x20; p++)
        {
            c->rd_page[m + p] = si.rom + 256 * p;
            c->wr_page[m + p] = gal_ignored;
            c->rd_page[m + 0x40 + p] = gal_zeros;
            c->wr_page[m + 0x40 + p] = gal_ignored;
            c->rd_page[m + 0x20 + p] = c->rd_page[m + 0x60 + p] = gal.ram + 256 * p;
            c->wr_page[m + 0x20 + p] = c->wr_page[m + 0x60 + p] = gal.ram + 256 * p;
        }
    }
    c->read = si_read;
    c->write = si_write;
    c->in = si_in;
    c->out = si_out;
    si.white = gal_color(0xffff);
    si.red = gal_color(0xf800);
    si.green = gal_color(0x07e0);
    return 1;
}

static void si_reset(void)
{
    memset(gal.ram, 0, sizeof(gal.ram));
    si.shift = 0;
    si.shift_count = 0;
    si.pending = 0;
    si.p3 = si.p5 = 0;
    si.shot.data = si.hit.data = NULL;
    si.noise_rng = 1;
    si.explo_env = 0;
    si.ufohit_freq = 0;
    si.dc_x = si.dc_y = 0;
    si.fleet_lp1 = si.fleet_lp2 = 0;
    si.cpu->icount = 0;
    i8085_reset(si.cpu);
}

static void si_run_slice(int q)
{
    /* the quarters by lines: 0..65, 66..130, 131..196, 197..261 */
    const int from = q * LINES / 4, to = (q + 1) * LINES / 4;
    if (from <= 96 && 96 < to)
    {
        run_lines(from, 96);
        interrupt(0xcf); /* RST 1 */
        run_lines(96, to);
    }
    else if (from <= 224 && 224 < to)
    {
        run_lines(from, 224);
        interrupt(0xd7); /* RST 2 */
        run_lines(224, to);
    }
    else
        run_lines(from, to);
}

/* --- Sound ------------------------------------------------------------------------- */

/* MAME's fleet 555: R1 by the note, R2 75K, C 0.1 uF; f = 1.44 / ((R1 + 2 R2) C),
 * high for (R1 + R2) / (R1 + 2 R2) of the period */
static const uint16_t fleet_hz[4] = {76, 66, 62, 58};
static const uint8_t fleet_duty[4] = {147, 155, 157, 160}; /* of 256 */

static void si_sound(int16_t *out, int n)
{
    const int rate = gal.samplerate;
    int note = -1;
    for (int b = 3; b >= 0; b--)
        if (si.p5 & (1 << b))
            note = b;
    const uint32_t fleet_step = note >= 0 ? (uint32_t)(((uint64_t)fleet_hz[note] << 32) / rate) : 0;
    /* one-pole low-passes at about 338 Hz and 80 Hz (MAME's two RC filters) */
    const int32_t a1 = (int32_t)(65536 * 2 * 3.14159 * 338 / rate), a2 = (int32_t)(65536 * 2 * 3.14159 * 80 / rate);

    for (int i = 0; i < n; i++)
    {
        int32_t v = 0;

        /* the noise: 17-bit LFSR clocked at 7515 Hz */
        si.noise_clock += 7515;
        while (si.noise_clock >= (uint32_t)rate)
        {
            si.noise_clock -= rate;
            const uint32_t bit = ((si.noise_rng >> 4) ^ (si.noise_rng >> 16)) & 1;
            si.noise_rng = ((si.noise_rng << 1) | bit) & 0x1ffff;
            si.noise_out = (si.noise_rng >> 12) & 1;
        }

        /* the saucer: SN76477, its VCO 1220..3700 Hz swept by a 5.3 Hz triangle */
        if (si.p3 & 0x01)
        {
            si.ufo_slf += (uint32_t)(((uint64_t)53 << 32) / (10 * (uint64_t)rate));
            const uint32_t t = si.ufo_slf >> 16;
            const int tri = (t < 0x8000) ? (int)t : (int)(0xffff - t); /* 0..0x7fff */
            const int f = 1220 + (tri * 2480 >> 15);
            si.ufo_phase += (uint32_t)(((uint64_t)f << 32) / rate);
            v += (si.ufo_phase & 0x80000000u) ? 1440 : -1440;
        }
        else
            si.ufo_slf = 0;

        /* the player's explosion: noise, slowly fading */
        if (si.p3 & 0x04)
        {
            if (!si.explo_env)
                si.explo_env = 120;
            v += (si.noise_out ? 24 : -24) * si.explo_env;
            if (++si.explo_count >= 92)
            {
                si.explo_count = 0;
                if (si.explo_env > 15)
                    si.explo_env--;
            }
        }
        else
            si.explo_env = si.explo_count = 0;

        /* the bonus base: 480 Hz, gated by a 7.4 Hz 555 (high 102 ms of 135) */
        if (si.p3 & 0x10)
        {
            si.bonus_gate += (uint32_t)(((uint64_t)74 << 32) / (10 * (uint64_t)rate));
            si.bonus_phase += (uint32_t)(((uint64_t)480 << 32) / rate);
            if ((si.bonus_gate >> 24) < 193)
                v += (si.bonus_phase & 0x80000000u) ? 1800 : -1800;
        }
        else
            si.bonus_gate = 0;

        /* the fleet: the 555's pulses through the two filters */
        int32_t pulse = 0;
        if (fleet_step)
        {
            si.fleet_phase += fleet_step;
            pulse = ((si.fleet_phase >> 24) < fleet_duty[note]) ? 12000 : 0;
        }
        si.fleet_lp1 += (int32_t)(((int64_t)(pulse - si.fleet_lp1) * a1) >> 16);
        si.fleet_lp2 += (int32_t)(((int64_t)(si.fleet_lp1 - si.fleet_lp2) * a2) >> 16);
        v += si.fleet_lp2;

        /* the saucer hit: a falling tone, warbling at 15 Hz */
        if (si.p5 & 0x10)
        {
            if (!si.ufohit_freq)
                si.ufohit_freq = 2000 << 8;
            si.ufohit_warble += (uint32_t)(((uint64_t)15 << 32) / rate);
            const uint32_t t = si.ufohit_warble >> 16;
            const int tri = (t < 0x8000) ? (int)t : (int)(0xffff - t);
            int f = (si.ufohit_freq >> 8) + (tri * 400 >> 15) - 200;
            if (f < 100)
                f = 100;
            si.ufohit_phase += (uint32_t)(((uint64_t)f << 32) / rate);
            v += (si.ufohit_phase & 0x80000000u) ? 2400 : -2400;
            if (si.ufohit_freq > (300 << 8))
                si.ufohit_freq -= (1700 << 8) / (rate * 3 / 2);
        }
        else
            si.ufohit_freq = 0;

        /* DC blocker */
        const int32_t y = v - si.dc_x + ((si.dc_y * 255) >> 8);
        si.dc_x = v;
        si.dc_y = y;
        out[i] = (int16_t)(y > 32767 ? 32767 : (y < -32768 ? -32768 : y));
    }
    /* the recordings, while their bits stay set; bit 5 mutes it all */
    if (!(si.p3 & 0x02))
        si.shot.data = NULL;
    if (!(si.p3 & 0x08))
        si.hit.data = NULL;
    gal_voice_mix(&si.shot, out, n);
    gal_voice_mix(&si.hit, out, n);
    if (!(si.p3 & 0x20))
        memset(out, 0, sizeof(int16_t) * n);
}

/* --- Video ------------------------------------------------------------------------- */

static void si_prepare_frame(void)
{
    gal.active_sprites = 0;
}

/* The colour of an upright row y (0..255) and column x: red over the saucer,
 * green over the bases and the player, and over the bases left at the bottom. */
static uint16_t film(int x, int y)
{
    if (y >= 32 && y < 64)
        return si.red;
    if (y >= 184 && y < 240)
        return si.green;
    if (y >= 240 && x >= 16 && x < 134)
        return si.green;
    return si.white;
}

/* Upright pixel (X, Y) is raw pixel (255 - (Y - 16), X): bit (x & 7) of byte
 * x / 8 of line X. */
static void si_render_band(const gal_band_t *b)
{
    const uint8_t *vram = gal.ram + VRAM;
    for (int j = 0; j < b->h; j++)
    {
        uint16_t *dst = b->buf + j * b->w;
        const int y = b->y0 + j - VIEW_TOP;
        const int x = 255 - y;
        const int byte = x >> 3, bit = x & 7;
        for (int i = 0; i < b->w; i++)
        {
            const int X = b->x0 + i;
            dst[i] = ((vram[X * 32 + byte] >> bit) & 1) ? film(X, y) : 0;
        }
    }
}

/* High scores: MAME's hiscore.dat, as GalaginoPlus uses it. */
static uint8_t si_hs_read(uint16_t addr)
{
    return gal.ram[(addr - 0x2000) & 0x1fff];
}

static void si_hs_write(uint16_t addr, uint8_t value)
{
    gal.ram[(addr - 0x2000) & 0x1fff] = value;
}

static const gal_hiscore_region_t si_hiscore[] = {
    {0x20f4, 2, 0x00, 0x00},
};

const gal_game_ops_t gal_invaders_ops = {
    .init = si_init,
    .reset = si_reset,
    .run_slice = si_run_slice,
    .prepare_frame = si_prepare_frame,
    .render_band = si_render_band,
    .sound = si_sound,
    .view_top = VIEW_TOP,
    .view_h = 256,
    .hiscore = si_hiscore,
    .nhiscore = 1,
    .hs_read = si_hs_read,
    .hs_write = si_hs_write,
};
