// license:BSD-3-Clause
// copyright-holders:Aaron Giles, Couriersud, Stephane Humbert, Robbbert
/*
 * The Galaxian hardware and Konami's variants of it, ported from MAME's
 * galaxian.cpp and galaxian_v.cpp, by way of pico-mooncresta's mooncresta.c:
 *
 *   Galaxian      Namco, 1979: the original board, discrete sound
 *   Moon Cresta   Nichibutsu, 1980: encrypted program, banked graphics
 *   Scramble      Konami, 1981: the Konami board, a sound CPU with two AYs,
 *                 a blue background, blinking stars, a protection device
 *   Super Cobra   Konami, 1981: the same, more program, no protection
 *   Anteater      Tago, 1982: Super Cobra's board, scrambled graphics, the
 *                 background blue only along the top
 *   Turtles       Konami/Stern, 1981, and Amidar, Konami, 1982: the Turtles
 *                 board, a background colour of three bits, no stars, no shots
 *   Bagman        Valadon, 1982, the bootleg on Moon Cresta hardware
 *                 (bagmanm2): more program and RAM elsewhere, a graphics bank,
 *                 INT instead of NMI, no stars
 *
 * The main CPU is a Z80 at 3.072 MHz, 50688 cycles per 60.6 Hz frame; VBLANK
 * raises NMI while it is enabled. The picture is MAME's unrotated raster, 256
 * x 224 (x 0-255, lines 16-239): a 32x32 map of 8x8 tiles whose columns
 * scroll each on their own, 8 sprites of 16x16, the shots ("bullets") and the
 * stars. Upright (the monitor is on its side, ROT90) that is 224 x 256: raw
 * column x is upright row 16 + x, raw line y upright column 239 - y. A band of
 * upright rows is drawn a raw column at a time, a band of upright columns (tate)
 * a raw line at a time, as pico-mooncresta does.
 *
 * Galagino's RAM buffer: main RAM at 0000 (up to 2 KB), video RAM at 0800,
 * object RAM at 0c00, the sound CPU's RAM at 1000.
 */
#include "gal_internal.h"

#include "mooncresta_sound.h"

#define MAIN_CYCLES_PER_FRAME 50688
#define KONAMI_SOUND_CLOCK 1789772 /* 14.318181 MHz / 8 */
#define SOUND_CYCLES_PER_FRAME 29531
#define STEPS_PER_QUARTER 16
#define AY_VOL 8

#define RAW_W 256
#define Y0 16  /* the first visible raw line */
#define Y1 240 /* one past the last */
#define SPRITE_MIN_X 17
#define STAR_RNG_PERIOD ((1 << 17) - 1)
#define MAX_STARS 320
/* Scramble's stars blink with a 555 period of 0.83 s, 50.4 frames */
#define BLINK_FRAMES_X10 504

typedef enum
{
    GX_GALAXIAN,
    GX_MOONCRST,
    GX_SCRAMBLE,
    GX_SCOBRA,
    GX_ANTEATER,
    GX_TURTLES,
    GX_AMIDAR,
    GX_BAGMAN,
} gx_var_t;

/* a minimal 8255: mode 0, the output latches */
typedef struct
{
    uint8_t a, b, c;
} ppi_t;

static struct
{
    gx_var_t var;
    int konami; /* the Konami sound board */

    /* graphics: 2bpp rows along raw x, columns along raw y, pixel i at bits 2i */
    uint16_t *tile_rows, *tile_cols; /* [ntiles][8] */
    uint32_t *spr_rows, *spr_cols;   /* [nsprites][16] */
    int ntiles, nsprites;
    uint16_t pal[32];
    uint16_t star_pal[64];
    uint16_t bullet[2]; /* shell, missile */
    uint16_t bg_blue, bg_turtles[8];
    uint32_t *star_ofs; /* the stars of one RNG period: offset, colour */
    uint8_t *star_col;
    int nstars;

    uint8_t *vram, *obj;
    uint8_t gfxbank[3];
    uint8_t irq_enable, stars_enabled, background, bg_rgb;
    uint32_t star_origin, frame, star_origin_frame;
    int blink_acc;
    uint8_t blink_state;
    uint8_t prot_state_lo, prot_result;
    uint16_t prot_state;
    ppi_t ppi[2];
    uint8_t in1_dsw, in2_dsw, in3;

    /* where the main CPU is in the frame */
    int run_base, run_len;

    /* Konami sound */
    uint8_t snd_command, snd_control, ay_addr[2];
    uint64_t snd_cycles;
    int32_t snd_run_start;

    /* Galaxian sound: rendered up to the CPU's position on every write */
    mcr_sound_t *dsnd;
    int16_t qbuf[256];
    int qpos, qsamples;
} gx;

typedef struct
{
    int16_t y, x;
    uint8_t missile;
} bullet_t;

/* the per-frame working set, in the game's arena */
typedef struct
{
    struct
    {
        int16_t sx, sy;
        uint16_t code;
        uint8_t color, flipx, flipy;
    } spr[8];
    bullet_t bul[16];
    int nbul;
    uint8_t sx[MAX_STARS], sy[MAX_STARS], sc[MAX_STARS];
    int16_t first_x[256], next_x[MAX_STARS], first_y[256], next_y[MAX_STARS];
    uint16_t tile_code[0x400];
    uint16_t line[RAW_W];
} gx_work_t;
static gx_work_t *work;
#define rs (*work)

/* --- Galaxian sound ---------------------------------------------------------------- */

static int frame_cycle(const z80_t *cpu)
{
    const int c = gx.run_base + (gx.run_len - cpu->icount);
    return c < 0 ? 0 : c;
}

/* the discrete sound up to the main CPU's position in this quarter */
static void GAL_HOT(dsnd_catchup)(const z80_t *cpu)
{
    const int q_cycles = MAIN_CYCLES_PER_FRAME / 4;
    int into = frame_cycle(cpu) % q_cycles;
    int target = (int)((int64_t)into * gx.qsamples / q_cycles);
    if (target > gx.qsamples)
        target = gx.qsamples;
    if (target > gx.qpos)
    {
        mcr_sound_render(gx.dsnd, gx.qbuf + gx.qpos, target - gx.qpos);
        gx.qpos = target;
    }
}

/* --- Inputs ---------------------------------------------------------------- */

/* Galaxian and Moon Cresta: active high */
static uint8_t gx_in0_namco(void)
{
    const uint16_t k = gal.buttons;
    uint8_t v = 0;
    if (k & GAL_BTN_COIN)
        v |= 0x01;
    if (k & GAL_BTN_LEFT)
        v |= 0x04;
    if (k & GAL_BTN_RIGHT)
        v |= 0x08;
    if (k & GAL_BTN_FIRE)
        v |= 0x10;
    return v; /* bit 5: upright */
}

static uint8_t gx_in1_namco(void)
{
    const uint16_t k = gal.buttons;
    uint8_t v = gx.in1_dsw;
    if (k & GAL_BTN_START)
        v |= 0x01;
    if (k & GAL_BTN_START2)
        v |= 0x02;
    /* the game reads player 2 from here on an upright cabinet too */
    if (k & GAL_BTN_LEFT)
        v |= 0x04;
    if (k & GAL_BTN_RIGHT)
        v |= 0x08;
    if (k & GAL_BTN_FIRE)
        v |= 0x10;
    return v;
}

/* Bagman (the bootleg on Moon Cresta hardware): active high. The coin goes
 * to the second slot, as in GalaginoPlus: the game ignores the first one. */
static uint8_t gx_in0_bagman(void)
{
    const uint16_t k = gal.buttons;
    uint8_t v = 0;
    if (k & GAL_BTN_COIN)
        v |= 0x02;
    if (k & GAL_BTN_START)
        v |= 0x04;
    if (k & GAL_BTN_LEFT)
        v |= 0x08;
    if (k & GAL_BTN_RIGHT)
        v |= 0x10;
    if (k & GAL_BTN_UP)
        v |= 0x20;
    if (k & GAL_BTN_DOWN)
        v |= 0x40;
    if (k & GAL_BTN_FIRE)
        v |= 0x80;
    return v;
}

/* the Konami boards: active low, IN0-IN2 on the first 8255 */
static uint8_t gx_konami_in(int port)
{
    const uint16_t k = gal.buttons;
    uint8_t v = 0xff;
    if (gx.var == GX_ANTEATER)
    {
        if (port == 0)
        {
            if (k & GAL_BTN_FIRE)
                v &= ~0x01;
            if (k & GAL_BTN_UP)
                v &= ~0x04;
            if (k & GAL_BTN_DOWN)
                v &= ~0x08;
            if (k & GAL_BTN_RIGHT)
                v &= ~0x10;
            if (k & GAL_BTN_LEFT)
                v &= ~0x20;
            if (k & GAL_BTN_COIN)
                v &= ~0x80;
        }
        else if (port == 1)
            v = gx.in1_dsw;
        else
        {
            v = gx.in2_dsw;
            if (k & GAL_BTN_START)
                v &= ~0x01;
            if (k & GAL_BTN_START2)
                v &= ~0x40;
        }
        return v;
    }
    if (port == 0)
    {
        if (k & GAL_BTN_EXTRA)
            v &= ~0x02; /* button 2: Scramble's and Super Cobra's bombs */
        if (k & GAL_BTN_FIRE)
            v &= ~0x08;
        if (k & GAL_BTN_RIGHT)
            v &= ~0x10;
        if (k & GAL_BTN_LEFT)
            v &= ~0x20;
        if (k & GAL_BTN_COIN)
            v &= ~0x80;
    }
    else if (port == 1)
    {
        v = gx.in1_dsw;
        if (k & GAL_BTN_START2)
            v &= ~0x40;
        if (k & GAL_BTN_START)
            v &= ~0x80;
    }
    else
    {
        v = gx.in2_dsw;
        if (k & GAL_BTN_UP)
            v &= ~0x10;
        if (k & GAL_BTN_DOWN)
            v &= ~0x40;
        if (gx.var == GX_SCRAMBLE)
        {
            /* the protection's two bits */
            v &= ~0xa0;
            if (gx.prot_result & 0x80)
                v |= 0xa0;
        }
    }
    return v;
}

/* --- Scramble's protection (MAME's theend_protection_w) ------------------------ */

static void prot_w(uint8_t data)
{
    gx.prot_state = (uint16_t)((gx.prot_state << 4) | (data & 0x0f));
    const int num1 = (gx.prot_state >> 8) & 0x0f, num2 = (gx.prot_state >> 4) & 0x0f, op = gx.prot_state & 0x0f;
    switch (op)
    {
    case 0x6:
        gx.prot_result ^= 0x80;
        break;
    case 0x9:
        gx.prot_result = (uint8_t)((num1 + 1 < 0xf ? num1 + 1 : 0xf) << 4);
        break;
    case 0xb:
        gx.prot_result = (uint8_t)((num2 - num1 > 0 ? num2 - num1 : 0) << 4);
        break;
    case 0xa:
        gx.prot_result = 0;
        break;
    case 0xf:
        gx.prot_result = (uint8_t)((num1 - num2 > 0 ? num1 - num2 : 0) << 4);
        break;
    default:
        break;
    }
}

/* --- The two 8255s of the Konami boards ----------------------------------------- */

static uint8_t ppi_read(int n, int port)
{
    if (n == 0)
        return port < 3 ? gx_konami_in(port) : 0xff;
    if (port == 2)
        return (gx.var == GX_SCRAMBLE) ? (uint8_t)((gx.ppi[1].c & 0x0f) | (gx.prot_result & 0xf0)) : gx.in3;
    return port == 0 ? gx.ppi[1].a : port == 1 ? gx.ppi[1].b : 0xff;
}

static void snd_control_w(uint8_t value)
{
    /* the falling edge of bit 3 interrupts the sound CPU until it acknowledges */
    if ((gx.snd_control & 0x08) && !(value & 0x08))
        z80_hold_irq(&gal.cpu[1], 0xff);
    gx.snd_control = value;
}

static void ppi_write(int n, int port, uint8_t value)
{
    ppi_t *p = &gx.ppi[n];
    if (port == 3)
    {
        if (value & 0x80)
        {
            /* a mode set clears the output latches */
            p->a = p->b = p->c = 0;
            if (n == 1)
            {
                gx.snd_command = 0;
                snd_control_w(0);
            }
            return;
        }
        /* port C bit set/reset */
        const int bit = (value >> 1) & 7;
        p->c = (uint8_t)((value & 1) ? (p->c | (1 << bit)) : (p->c & ~(1 << bit)));
        port = 2;
        value = p->c;
    }
    else if (port == 0)
        p->a = value;
    else if (port == 1)
        p->b = value;
    else
        p->c = value;
    if (n != 1)
        return;
    if (port == 0)
        gx.snd_command = value;
    else if (port == 1)
        snd_control_w(value);
    else if (gx.var == GX_SCRAMBLE)
        prot_w(value);
}

/* --- Main CPU ---------------------------------------------------------------- */

static void stars_enable_w(const z80_t *cpu, uint8_t data)
{
    if (!gx.stars_enabled && (data & 1))
    {
        /* the shift register is released from CLR: this frame's origin is 0
         * minus the clocks counted so far */
        const int c = frame_cycle(cpu);
        const int vpos = (Y0 + c / 192) % 264, hpos = (c % 192) * 2 * 3;
        gx.star_origin = STAR_RNG_PERIOD - (uint32_t)(vpos * 512 + hpos);
        gx.star_origin_frame = gx.frame;
    }
    gx.stars_enabled = data & 1;
}

static void irq_enable_w(z80_t *cpu, uint8_t data)
{
    /* the latched D0 goes to CLEAR on the interrupt flip-flop */
    gx.irq_enable = data & 1;
    if (!gx.irq_enable)
    {
        if (gx.var == GX_BAGMAN)
            z80_set_irq_line(cpu, 0);
        else
            z80_set_nmi_line(cpu, 0);
    }
}

static uint8_t GAL_HOT(gx_read)(z80_t *cpu, uint16_t addr)
{
    (void)cpu;
    switch (gx.var)
    {
    case GX_GALAXIAN:
        switch (addr & 0xf800)
        {
        case 0x6000:
            return gx_in0_namco();
        case 0x6800:
            return gx_in1_namco();
        case 0x7000:
            return gx.in2_dsw;
        default:
            return 0xff;
        }
    case GX_MOONCRST:
        switch (addr & 0xf800)
        {
        case 0xa000:
            return gx_in0_namco();
        case 0xa800:
            return gx_in1_namco();
        case 0xb000:
            return gx.in2_dsw;
        default:
            return 0xff;
        }
    case GX_BAGMAN:
        switch (addr & 0xf800)
        {
        case 0xa000:
            return gx_in0_bagman();
        case 0xa800:
            return (uint8_t)(0x80 | ((gal.buttons & GAL_BTN_START2) ? 0x01 : 0)); /* upright */
        case 0xb000:
            return gx.in2_dsw;
        default:
            return 0xff;
        }
    case GX_SCRAMBLE:
        if (addr >= 0x8000)
        {
            uint8_t v = 0xff;
            if (addr & 0x100)
                v &= ppi_read(0, addr & 3);
            if (addr & 0x200)
                v &= ppi_read(1, addr & 3);
            return v;
        }
        return 0xff;
    case GX_SCOBRA:
    case GX_ANTEATER:
        if ((addr & 0xb800) == 0x9800)
            return ppi_read(0, addr & 3);
        if ((addr & 0xb800) == 0xa000)
            return ppi_read(1, addr & 3);
        return 0xff;
    default: /* Turtles, Amidar */
        if ((addr & 0xb800) == 0xb000)
            return ppi_read(0, (addr >> 4) & 3);
        if ((addr & 0xb800) == 0xb800)
            return ppi_read(1, (addr >> 4) & 3);
        return 0xff;
    }
}

static void GAL_HOT(gx_write)(z80_t *cpu, uint16_t addr, uint8_t data)
{
    const int ofs = addr & 7;
    switch (gx.var)
    {
    case GX_GALAXIAN:
    case GX_MOONCRST:
    {
        /* Galaxian's latches at 6000/6800/7000/7800, Moon Cresta's at a000.. */
        const uint16_t base = (gx.var == GX_GALAXIAN) ? 0x6000 : 0xa000;
        switch ((addr & 0xf800) - base)
        {
        case 0x0000:
            if (ofs < 3 && gx.var == GX_MOONCRST)
                gx.gfxbank[ofs] = data;
            else if (ofs >= 4)
            {
                dsnd_catchup(cpu);
                mcr_sound_lfo_w(gx.dsnd, ofs - 4, data);
            }
            break;
        case 0x0800:
            dsnd_catchup(cpu);
            mcr_sound_w(gx.dsnd, ofs, data);
            break;
        case 0x1000:
            if (ofs == (gx.var == GX_GALAXIAN ? 1 : 0))
                irq_enable_w(cpu, data);
            else if (ofs == 4)
                stars_enable_w(cpu, data);
            break;
        case 0x1800:
            dsnd_catchup(cpu);
            mcr_sound_pitch_w(gx.dsnd, data);
            break;
        default:
            break;
        }
        return;
    }
    case GX_BAGMAN:
        switch (addr & 0xf800)
        {
        case 0xa000:
            if (ofs >= 4)
            {
                dsnd_catchup(cpu);
                mcr_sound_lfo_w(gx.dsnd, ofs - 4, data);
            }
            break;
        case 0xa800:
            dsnd_catchup(cpu);
            mcr_sound_w(gx.dsnd, ofs, data);
            break;
        case 0xb000:
            if (ofs == 1)
                irq_enable_w(cpu, data);
            else if (ofs == 2)
                gx.gfxbank[0] = data & 1;
            break;
        case 0xb800:
            dsnd_catchup(cpu);
            mcr_sound_pitch_w(gx.dsnd, data);
            break;
        default:
            break;
        }
        return;
    case GX_SCRAMBLE:
        if (addr >= 0x8000)
        {
            if (addr & 0x100)
                ppi_write(0, addr & 3, data);
            if (addr & 0x200)
                ppi_write(1, addr & 3, data);
            return;
        }
        if ((addr & 0xf800) != 0x6800)
            return;
        break; /* the latch, below */
    case GX_SCOBRA:
    case GX_ANTEATER:
        if ((addr & 0xb800) == 0x9800)
        {
            ppi_write(0, addr & 3, data);
            return;
        }
        if ((addr & 0xb800) == 0xa000)
        {
            ppi_write(1, addr & 3, data);
            return;
        }
        if ((addr & 0xb800) != 0xa800)
            return;
        break; /* the latch, below */
    default: /* Turtles, Amidar */
        if ((addr & 0xb800) == 0xb000)
            ppi_write(0, (addr >> 4) & 3, data);
        else if ((addr & 0xb800) == 0xb800)
            ppi_write(1, (addr >> 4) & 3, data);
        else if ((addr & 0xb800) == 0xa000)
        {
            const int bit = data & 1;
            switch (addr & 0x38)
            {
            case 0x00: /* background red */
                gx.bg_rgb = (uint8_t)((gx.bg_rgb & ~1) | bit);
                break;
            case 0x08:
                irq_enable_w(cpu, data);
                break;
            case 0x20: /* green */
                gx.bg_rgb = (uint8_t)((gx.bg_rgb & ~2) | (bit << 1));
                break;
            case 0x28: /* blue */
                gx.bg_rgb = (uint8_t)((gx.bg_rgb & ~4) | (bit << 2));
                break;
            default: /* flip, coin counters */
                break;
            }
        }
        return;
    }
    /* the latch of Scramble and Super Cobra */
    switch (ofs)
    {
    case 1:
        irq_enable_w(cpu, data);
        break;
    case 3:
        gx.background = data & 1;
        break;
    case 4:
        stars_enable_w(cpu, data);
        break;
    default: /* coin counter, flip */
        break;
    }
}

static uint8_t gx_main_in(z80_t *cpu, uint16_t port)
{
    (void)cpu;
    (void)port;
    return 0xff;
}

static void gx_main_out(z80_t *cpu, uint16_t port, uint8_t data)
{
    (void)cpu;
    (void)port;
    (void)data;
}

/* --- Konami sound CPU ------------------------------------------------------------ */

/* MAME's konami_sound_timer_r: a 40960-step counter at 8x the CPU clock */
static uint8_t snd_timer(const z80_t *cpu)
{
    const uint64_t now = gx.snd_cycles + (uint64_t)(gx.snd_run_start - cpu->icount);
    uint32_t cycles = (uint32_t)((now * 8) % (16 * 16 * 2 * 8 * 5 * 2));
    uint8_t hibit = 0;
    if (cycles >= 16 * 16 * 2 * 8 * 5)
    {
        hibit = 1;
        cycles -= 16 * 16 * 2 * 8 * 5;
    }
    return (uint8_t)((hibit << 7) | (((cycles >> 14) & 1) << 6) | (((cycles >> 13) & 1) << 5) |
                     (((cycles >> 11) & 1) << 4) | 0x0e);
}

static uint8_t ay_data_r(const z80_t *cpu, int chip)
{
    const int reg = gx.ay_addr[chip];
    if (chip == 0 && reg == 14)
        return gx.snd_command;
    if (chip == 0 && reg == 15)
        return snd_timer(cpu);
    return reg < 16 ? gal.soundregs[16 * chip + reg] : 0xff;
}

static uint8_t GAL_HOT(snd_read)(z80_t *cpu, uint16_t addr)
{
    (void)cpu;
    (void)addr;
    return 0xff;
}

static void GAL_HOT(snd_write)(z80_t *cpu, uint16_t addr, uint8_t data)
{
    (void)cpu;
    (void)addr;
    (void)data; /* 9000-9fff: the RC filters of the AY outputs, not emulated */
}

static uint8_t GAL_HOT(snd_in)(z80_t *cpu, uint16_t port)
{
    uint8_t v = 0xff;
    if (port & 0x20)
        v &= ay_data_r(cpu, 1);
    if (port & 0x80)
        v &= ay_data_r(cpu, 0);
    return v;
}

static void GAL_HOT(snd_out)(z80_t *cpu, uint16_t port, uint8_t data)
{
    (void)cpu;
    /* AV4, AV5: the second AY; AV6, AV7: the first */
    if (port & 0x10)
        gx.ay_addr[1] = data & 15;
    else if (port & 0x20)
        gal.soundregs[16 + gx.ay_addr[1]] = data;
    if (port & 0x40)
        gx.ay_addr[0] = data & 15;
    else if (port & 0x80)
        gal.soundregs[gx.ay_addr[0]] = data;
}

/* --- Graphics -------------------------------------------------------------------- */

/* MAME's compute_resistor_weights with autoscaling, as in pico-mooncresta */
static void resistor_weights(const double *r, int count, double pulldown, double *w)
{
    for (int n = 0; n < count; n++)
    {
        double r0 = 1.0 / pulldown, r1 = 1.0 / 1e12;
        for (int j = 0; j < count; j++)
        {
            if (j == n)
                r1 += 1.0 / r[j];
            else
                r0 += 1.0 / r[j];
        }
        r0 = 1.0 / r0;
        r1 = 1.0 / r1;
        const double v = 255.0 * r0 / (r1 + r0);
        w[n] = v < 0 ? 0 : (v > 255 ? 255 : v);
    }
}

static uint16_t rgb(int r, int g, int b)
{
    return gal_color((uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)));
}

/* galaxian_palette */
static void build_palette(const uint8_t *prom)
{
    static const double res[3] = {1000, 470, 220};
    double rw[3], gw[3], bw[2];
    resistor_weights(res, 3, 470, rw);
    resistor_weights(res, 3, 470, gw);
    resistor_weights(res + 1, 2, 470, bw);
    double max = rw[0] + rw[1] + rw[2];
    if (gw[0] + gw[1] + gw[2] > max)
        max = gw[0] + gw[1] + gw[2];
    if (bw[0] + bw[1] > max)
        max = bw[0] + bw[1];
    const double scale = 255.0 / max;
    for (int i = 0; i < 32; i++)
    {
        const uint8_t p = prom[i];
        const int r = (int)((rw[0] * (p & 1) + rw[1] * ((p >> 1) & 1) + rw[2] * ((p >> 2) & 1)) * scale + 0.5);
        const int g = (int)((gw[0] * ((p >> 3) & 1) + gw[1] * ((p >> 4) & 1) + gw[2] * ((p >> 5) & 1)) * scale + 0.5);
        const int b = (int)((bw[0] * ((p >> 6) & 1) + bw[1] * ((p >> 7) & 1)) * scale + 0.5);
        gx.pal[i] = rgb(r, g, b);
    }
    /* stars: 150 and 100 Ohm per component, compressed into 194..255 */
    const int minval = 255 * 130 / 150, midval = 255 * 130 / 100, maxval = 255 * 130 / 60;
    const int starmap[4] = {0, minval, minval + (255 - minval) * (midval - minval) / (maxval - minval), 255};
    for (int i = 0; i < 64; i++)
        gx.star_pal[i] = rgb(starmap[(((i >> 4) & 1) << 1) | ((i >> 5) & 1)],
                             starmap[(((i >> 2) & 1) << 1) | ((i >> 3) & 1)],
                             starmap[((i & 1) << 1) | ((i >> 1) & 1)]);
    gx.bullet[0] = rgb(0xff, 0xff, 0xff);
    gx.bullet[1] = rgb(0xff, 0xff, 0x00);
    gx.bg_blue = rgb(0, 0, 0x56);
    for (int i = 0; i < 8; i++)
        gx.bg_turtles[i] = rgb((i & 1) * 0x55, ((i >> 1) & 1) * 0x47, ((i >> 2) & 1) * 0x55);
}

/* galaxian_charlayout / spritelayout: two planes, the first (the high bit) in
 * the first half of the region, MSB first along x */
static int gfx_pixel(const uint8_t *gfx, int half, int byte, int x)
{
    const int mask = 0x80 >> x;
    return ((gfx[byte] & mask) ? 2 : 0) | ((gfx[half + byte] & mask) ? 1 : 0);
}

/* Bagman reaches only half of its graphics (bagmanmc_extend_tile_info and
 * _sprite_info): tiles 0-255 and 512-767, sprites 64-127 and 192-255, kept
 * as 512 and 128 */
static int tile_src(int code)
{
    return (gx.var == GX_BAGMAN) ? ((code & 0xff) | ((code >> 8) << 9)) : code;
}

static int sprite_src(int code)
{
    return (gx.var == GX_BAGMAN) ? ((code & 0x3f) | 0x40 | ((code >> 6) << 7)) : code;
}

static void decode_gfx(const uint8_t *gfx, int size)
{
    const int half = size / 2;
    for (int code = 0; code < gx.ntiles; code++)
    {
        const int src = tile_src(code);
        for (int i = 0; i < 8; i++)
        {
            uint16_t r = 0, c = 0;
            for (int j = 0; j < 8; j++)
            {
                r |= (uint16_t)(gfx_pixel(gfx, half, src * 8 + i, j) << (2 * j));
                c |= (uint16_t)(gfx_pixel(gfx, half, src * 8 + j, i) << (2 * j));
            }
            gx.tile_rows[code * 8 + i] = r;
            gx.tile_cols[code * 8 + i] = c;
        }
    }
    for (int code = 0; code < gx.nsprites; code++)
        for (int i = 0; i < 16; i++)
        {
            uint32_t r = 0, c = 0;
            const int src = sprite_src(code);
            for (int j = 0; j < 16; j++)
            {
                /* (x, y) at byte code*32 + (y&7) + (y&8 ? 16 : 0) + (x&8 ? 8 : 0) */
                const int rb = src * 32 + (i & 7) + ((i & 8) ? 16 : 0) + ((j & 8) ? 8 : 0);
                const int cb = src * 32 + (j & 7) + ((j & 8) ? 16 : 0) + ((i & 8) ? 8 : 0);
                r |= (uint32_t)gfx_pixel(gfx, half, rb, j & 7) << (2 * j);
                c |= (uint32_t)gfx_pixel(gfx, half, cb, i & 7) << (2 * j);
            }
            gx.spr_rows[code * 16 + i] = r;
            gx.spr_cols[code * 16 + i] = c;
        }
}

/* MAME's decode_anteater_gfx */
static void anteater_decode(uint8_t *gfx, int len, uint8_t *scratch)
{
    memcpy(scratch, gfx, len);
    for (int o = 0; o < len; o++)
    {
        int s = o & 0x9bf;
        s |= (((o >> 4) & 1) ^ ((o >> 9) & 1) ^ (((o >> 2) & 1) & ((o >> 10) & 1))) << 6;
        s |= (((o >> 2) & 1) ^ ((o >> 10) & 1)) << 9;
        s |= (((o >> 0) & 1) ^ ((o >> 6) & 1) ^ 1) << 10;
        gfx[o] = scratch[s];
    }
}

/* The starfield: a 17-bit LFSR clocked twice per pixel; a star where its top 8
 * bits are 1 and its low bit 0. Kept as a list of the ~256 of them. */
static void init_stars(void)
{
    uint32_t shiftreg = 0;
    gx.nstars = 0;
    for (uint32_t i = 0; i < STAR_RNG_PERIOD; i++)
    {
        if ((shiftreg & 0x1fe01) == 0x1fe00 && gx.nstars < MAX_STARS)
        {
            gx.star_ofs[gx.nstars] = i;
            gx.star_col[gx.nstars] = (uint8_t)((~shiftreg & 0x1f8) >> 3);
            gx.nstars++;
        }
        shiftreg = (shiftreg >> 1) | ((((shiftreg >> 12) ^ ~shiftreg) & 1) << 16);
    }
}

/* MAME's decode_mooncrst */
static void mooncrst_decrypt(uint8_t *rom, int length)
{
    for (int offs = 0; offs < length; offs++)
    {
        const uint8_t data = rom[offs];
        uint8_t res = data;
        if (data & 0x02)
            res ^= 0x40;
        if (data & 0x20)
            res ^= 0x04;
        if (!(offs & 1))
            res = (uint8_t)((res & 0xbb) | ((res & 0x04) << 4) | ((res & 0x40) >> 4));
        rom[offs] = res;
    }
}

/* --- Life cycle ---------------------------------------------------------------- */

static void map_pages(z80_t *cpu, int first, int count, uint8_t *mem, int mask, int writable)
{
    for (int p = 0; p < count; p++)
    {
        cpu->rd_page[first + p] = mem + (p & mask) * 256;
        if (writable)
            cpu->wr_page[first + p] = mem + (p & mask) * 256;
    }
}

/* regions: program, sound program or -1, graphics, palette */
static int gx_init_common(uint8_t *const *regions, gx_var_t var, int r_prog, int prog_len, int r_snd,
                          int r_gfx, int gfx_len, int r_pal)
{
    memset(&gx, 0, sizeof(gx));
    gx.var = var;
    gx.konami = r_snd >= 0;
    gx.ntiles = gfx_len / 16;
    gx.nsprites = gfx_len / 64;
    if (var == GX_BAGMAN)
    {
        gx.ntiles /= 2;
        gx.nsprites /= 2;
    }
    gx.tile_rows = (uint16_t *)gal_alloc(gx.ntiles * 8 * 2);
    gx.tile_cols = (uint16_t *)gal_alloc(gx.ntiles * 8 * 2);
    gx.spr_rows = (uint32_t *)gal_alloc(gx.nsprites * 16 * 4);
    gx.spr_cols = (uint32_t *)gal_alloc(gx.nsprites * 16 * 4);
    gx.star_ofs = (uint32_t *)gal_alloc(MAX_STARS * 4);
    gx.star_col = (uint8_t *)gal_alloc(MAX_STARS);
    if (!gx.tile_rows || !gx.tile_cols || !gx.spr_rows || !gx.spr_cols || !gx.star_ofs || !gx.star_col)
        return 0;
    if (!gx.konami && !(gx.dsnd = (mcr_sound_t *)gal_alloc(sizeof(mcr_sound_t))))
        return 0;
    if (!(work = (gx_work_t *)gal_alloc(sizeof(gx_work_t))))
        return 0;

    if (var == GX_ANTEATER)
    {
        uint8_t *scratch = (uint8_t *)gal_temp_alloc(gfx_len);
        if (!scratch)
            return 0;
        anteater_decode(regions[r_gfx], gfx_len, scratch);
    }
    if (var == GX_MOONCRST)
        mooncrst_decrypt(regions[r_prog], prog_len);
    decode_gfx(regions[r_gfx], gfx_len);
    build_palette(regions[r_pal]);
    init_stars();

    gx.vram = gal.ram + 0x800;
    gx.obj = gal.ram + 0xc00;

    z80_t *cpu = &gal.cpu[0];
    for (int p = 0; p < prog_len / 256; p++)
        cpu->rd_page[p] = regions[r_prog] + p * 256;
    switch (var)
    {
    case GX_GALAXIAN:
        map_pages(cpu, 0x40, 8, gal.ram, 3, 1);     /* 4000 RAM, mirrored at 4400 */
        map_pages(cpu, 0x50, 8, gx.vram, 3, 1);     /* 5000 video RAM, mirrored at 5400 */
        map_pages(cpu, 0x58, 8, gx.obj, 0, 1);      /* 5800 object RAM, mirrored to 5fff */
        break;
    case GX_MOONCRST:
        map_pages(cpu, 0x80, 8, gal.ram, 3, 1);
        map_pages(cpu, 0x90, 8, gx.vram, 3, 1);
        map_pages(cpu, 0x98, 8, gx.obj, 0, 1);
        break;
    case GX_BAGMAN:
        /* 6000-67ff RAM, 9000-93ff video RAM, 9800-9bff object RAM and
         * more RAM (MAME's ckongg_map_base) */
        gx.obj = gal.ram + 0x1000;
        map_pages(cpu, 0x60, 8, gal.ram, 7, 1);
        map_pages(cpu, 0x90, 4, gx.vram, 3, 1);
        map_pages(cpu, 0x98, 4, gx.obj, 3, 1);
        break;
    case GX_SCRAMBLE:
        map_pages(cpu, 0x40, 8, gal.ram, 7, 1);     /* 4000-47ff RAM */
        map_pages(cpu, 0x48, 8, gx.vram, 3, 1);     /* 4800 video RAM, mirrored at 4c00 */
        map_pages(cpu, 0x50, 8, gx.obj, 0, 1);      /* 5000 object RAM, mirrored to 57ff */
        break;
    case GX_SCOBRA:
    case GX_ANTEATER:
        for (int m = 0; m < 0x80; m += 0x40)        /* mirrored at c000 */
        {
            map_pages(cpu, 0x80 + m, 8, gal.ram, 7, 1);
            map_pages(cpu, 0x88 + m, 8, gx.vram, 3, 1);
            map_pages(cpu, 0x90 + m, 8, gx.obj, 0, 1);
        }
        break;
    default: /* Turtles, Amidar */
        for (int m = 0; m < 0x80; m += 0x40)
        {
            map_pages(cpu, 0x80 + m, 8, gal.ram, 7, 1);
            map_pages(cpu, 0x90 + m, 8, gx.vram, 3, 1);
            map_pages(cpu, 0x98 + m, 8, gx.obj, 0, 1);
        }
        break;
    }
    cpu->read = gx_read;
    cpu->write = gx_write;
    cpu->in = gx_main_in;
    cpu->out = gx_main_out;

    if (gx.konami)
    {
        cpu = &gal.cpu[1];
        for (int p = 0; p < 0x20; p++) /* 0000-1fff */
            cpu->rd_page[p] = regions[r_snd] + p * 256;
        for (int p = 0x80; p < 0x100; p++) /* 8000-83ff RAM, mirrored where A12 is 0 */
            if (!(p & 0x10))
                cpu->rd_page[p] = cpu->wr_page[p] = gal.ram + 0x1000 + (p & 3) * 256;
        cpu->read = snd_read;
        cpu->write = snd_write;
        cpu->in = snd_in;
        cpu->out = snd_out;
    }
    else
        mcr_sound_init(gx.dsnd, gal.samplerate);
    return 1;
}

/* IN1 bits 6-7 (Galaxian: coinage 1C1C; Moon Cresta: bonus at 30000,
 * English) and IN2 (Galaxian: bonus at 7000, 3 lives; Moon Cresta: coinage
 * 1C1C), MAME's defaults */
static int galaxian_init(uint8_t *const *regions)
{
    if (!gx_init_common(regions, GX_GALAXIAN, 0, 0x2800, -1, 1, 0x1000, 2))
        return 0;
    gx.in1_dsw = 0x00;
    gx.in2_dsw = 0x04;
    return 1;
}

static int mooncrst_init(uint8_t *const *regions)
{
    if (!gx_init_common(regions, GX_MOONCRST, 0, 0x4000, -1, 1, 0x2000, 2))
        return 0;
    gx.in1_dsw = 0x80;
    gx.in2_dsw = 0x00;
    return 1;
}

/* IN2, MAME's defaults: 3 lives, 1C1C, easy, English */
static int bagman_init(uint8_t *const *regions)
{
    if (!gx_init_common(regions, GX_BAGMAN, 0, 0x6000, -1, 1, 0x4000, 2))
        return 0;
    gx.in2_dsw = 0xfe;
    return 1;
}

/* IN1 and IN2 (and IN3) DIP switches, MAME's defaults: 3 lives (Turtles 4),
 * 1C1C, upright, demo sounds on */
static int scramble_init(uint8_t *const *regions)
{
    if (!gx_init_common(regions, GX_SCRAMBLE, 0, 0x4000, 1, 2, 0x1000, 3))
        return 0;
    gx.in1_dsw = 0xfc; /* lives 0 */
    gx.in2_dsw = 0xf1; /* coinage 0, upright */
    return 1;
}

static int scobra_init(uint8_t *const *regions)
{
    if (!gx_init_common(regions, GX_SCOBRA, 0, 0x6000, 1, 2, 0x1000, 3))
        return 0;
    gx.in1_dsw = 0xfd; /* allow continue, 3 lives */
    gx.in2_dsw = 0xf3; /* 1C1C, upright */
    return 1;
}

static int anteater_init(uint8_t *const *regions)
{
    if (!gx_init_common(regions, GX_ANTEATER, 0, 0x4000, 1, 2, 0x1000, 3))
        return 0;
    gx.in1_dsw = 0xfd; /* 3 lives, demo sounds on */
    gx.in2_dsw = 0xfb; /* 1C1C */
    return 1;
}

static int turtles_init(uint8_t *const *regions)
{
    if (!gx_init_common(regions, GX_TURTLES, 0, 0x5000, 1, 2, 0x1000, 3))
        return 0;
    gx.in1_dsw = 0xfd; /* 4 lives */
    gx.in2_dsw = 0xf1;
    gx.in3 = 0x00;
    return 1;
}

static int amidar_init(uint8_t *const *regions)
{
    if (!gx_init_common(regions, GX_AMIDAR, 0, 0x4000, 1, 2, 0x1000, 3))
        return 0;
    gx.in1_dsw = 0xff; /* 3 lives */
    gx.in2_dsw = 0xf1; /* demo sounds on, bonus at 30000 and 50000 */
    gx.in3 = 0xff;     /* 1C1C on both slots */
    return 1;
}

static void gx_reset(void)
{
    memset(gal.ram, 0, sizeof(gal.ram));
    memset(gal.soundregs, 0, sizeof(gal.soundregs));
    memset(gx.gfxbank, 0, sizeof(gx.gfxbank));
    memset(gx.ppi, 0, sizeof(gx.ppi));
    gx.irq_enable = gx.stars_enabled = gx.background = gx.bg_rgb = 0;
    gx.star_origin = 0;
    gx.star_origin_frame = gx.frame;
    gx.prot_state = 0;
    gx.prot_result = 0;
    gx.snd_command = gx.snd_control = 0;
    gx.ay_addr[0] = gx.ay_addr[1] = 0;
    gx.snd_cycles = 0;
    for (int c = 0; c < (gx.konami ? 2 : 1); c++)
    {
        z80_init(&gal.cpu[c]);
        z80_reset(&gal.cpu[c]);
    }
    if (gx.konami)
        gal_ay_reset();
    else
    {
        mcr_sound_reset(gx.dsnd);
        /* the circuit's capacitors start empty: the output coupling one would
         * pass the mixer's DC level as a full-scale pop. Let it settle. */
        for (int i = 0; i < gal.samplerate / 5; i += 256)
            mcr_sound_render(gx.dsnd, gx.qbuf, 256);
    }
}

static void GAL_HOT(gx_run_slice)(int q)
{
    const int spf = gal.samplerate / 60;
    gx.qsamples = spf * (q + 1) / 4 - spf * q / 4;
    gx.qpos = 0;
    if (!gx.konami)
    {
        gx.run_base = q * (MAIN_CYCLES_PER_FRAME / 4);
        gx.run_len = MAIN_CYCLES_PER_FRAME / 4;
        z80_run(&gal.cpu[0], gx.run_len);
    }
    else
    {
        for (int i = 0; i < STEPS_PER_QUARTER; i++)
        {
            const int step = q * STEPS_PER_QUARTER + i, steps = 4 * STEPS_PER_QUARTER;
            gx.run_base = step * MAIN_CYCLES_PER_FRAME / steps;
            gx.run_len = (step + 1) * MAIN_CYCLES_PER_FRAME / steps - gx.run_base;
            const int snd_cycles =
                (step + 1) * SOUND_CYCLES_PER_FRAME / steps - step * SOUND_CYCLES_PER_FRAME / steps;
            z80_run(&gal.cpu[0], gx.run_len);
            z80_t *snd = &gal.cpu[1];
            gx.snd_run_start = snd->icount + snd_cycles;
            z80_run(snd, snd_cycles);
            gx.snd_cycles += (uint32_t)(gx.snd_run_start - snd->icount);
        }
    }

    if (q == 3)
    {
        /* VBLANK: the stars move one RNG clock back per frame */
        gx.frame++;
        if (gx.frame != gx.star_origin_frame)
        {
            const uint32_t delta = (gx.frame - gx.star_origin_frame) % STAR_RNG_PERIOD;
            gx.star_origin = (gx.star_origin + STAR_RNG_PERIOD - delta) % STAR_RNG_PERIOD;
            gx.star_origin_frame = gx.frame;
        }
        /* Scramble's blinking stars */
        gx.blink_acc += 10;
        if (gx.blink_acc >= BLINK_FRAMES_X10)
        {
            gx.blink_acc -= BLINK_FRAMES_X10;
            gx.blink_state++;
        }
        /* the interrupt line is clocked here while it is enabled; Bagman
         * has it on INT */
        if (gx.irq_enable && gx.var == GX_BAGMAN)
            z80_set_irq_line(&gal.cpu[0], 1);
        else if (gx.irq_enable)
            z80_set_nmi_line(&gal.cpu[0], 1);
    }
}

static void GAL_HOT(gx_sound)(int16_t *out, int n)
{
    if (gx.konami)
    {
        gal_ay_render(2, KONAMI_SOUND_CLOCK, AY_VOL, out, n);
        return;
    }
    if (n > (int)(sizeof(gx.qbuf) / sizeof(gx.qbuf[0])))
        n = sizeof(gx.qbuf) / sizeof(gx.qbuf[0]);
    if (gx.qpos < n)
        mcr_sound_render(gx.dsnd, gx.qbuf + gx.qpos, n - gx.qpos);
    memcpy(out, gx.qbuf, n * sizeof(int16_t));
}

/* --- Video --------------------------------------------------------------------- */

static int has_stars(void)
{
    return gx.var != GX_TURTLES && gx.var != GX_AMIDAR;
}

static int scramble_stars(void)
{
    return gx.var == GX_SCRAMBLE || gx.var == GX_SCOBRA || gx.var == GX_ANTEATER;
}

static void GAL_HOT(gx_prepare_frame)(void)
{
    const uint8_t *bank = gx.gfxbank;
    const int mooncrst = gx.var == GX_MOONCRST;

    for (int i = 0; i < 0x400; i++)
    {
        uint16_t code = gx.vram[i];
        /* mooncrst_extend_tile_info */
        if (mooncrst && bank[2] && (code & 0xc0) == 0x80)
            code = (uint16_t)((code & 0x3f) | (bank[0] << 6) | (bank[1] << 7) | 0x100);
        if (gx.var == GX_BAGMAN)
            code |= (uint16_t)(bank[0] << 8);
        rs.tile_code[i] = code & (gx.ntiles - 1);
    }

    for (int n = 0; n < 8; n++)
    {
        const uint8_t *base = gx.obj + 0x40 + n * 4;
        /* the first three sprites match against y-1 */
        rs.spr[n].sy = (uint8_t)(240 - (base[0] - (n < 3)));
        uint16_t code = base[1] & 0x3f;
        rs.spr[n].flipx = (base[1] & 0x40) != 0;
        rs.spr[n].flipy = (base[1] & 0x80) != 0;
        rs.spr[n].color = base[2] & 7;
        rs.spr[n].sx = (uint8_t)(base[3] + 1);
        /* mooncrst_extend_sprite_info */
        if (mooncrst && bank[2] && (code & 0x30) == 0x20)
            code = (uint16_t)((code & 0x0f) | (bank[0] << 4) | (bank[1] << 5) | 0x40);
        if (gx.var == GX_BAGMAN)
            code |= (uint16_t)(bank[0] << 6);
        rs.spr[n].code = code & (gx.nsprites - 1);
    }

    /* bullets: per line one shell, the highest numbered that matches, and
     * the missile; Turtles and Amidar have none */
    rs.nbul = 0;
    if (gx.var != GX_TURTLES && gx.var != GX_AMIDAR)
    {
        const uint8_t *bb = gx.obj + 0x60;
        for (int y = Y0; y < Y1 && rs.nbul < 15; y++)
        {
            int shell = -1, missile = -1;
            for (int w = 0; w < 3; w++)
                if ((uint8_t)(bb[w * 4 + 1] + (uint8_t)(y - 1)) == 0xff)
                    shell = w;
            for (int w = 3; w < 8; w++)
                if ((uint8_t)(bb[w * 4 + 1] + (uint8_t)y) == 0xff)
                {
                    if (w != 7)
                        shell = w;
                    else
                        missile = w;
                }
            if (shell >= 0)
                rs.bul[rs.nbul++] = (bullet_t){(int16_t)y, (int16_t)(255 - bb[shell * 4 + 3]), 0};
            if (missile >= 0)
                rs.bul[rs.nbul++] = (bullet_t){(int16_t)y, (int16_t)(255 - bb[missile * 4 + 3]), 1};
        }
    }

    /* the stars of this frame, chained by line and by column */
    for (int i = 0; i < 256; i++)
        rs.first_x[i] = rs.first_y[i] = -1;
    if (!has_stars() || !gx.stars_enabled)
        return;
    const int scramble = scramble_stars();
    static const uint8_t colormask[4] = {0x20, 0x08, 0xff, 0xff};
    const int blink = gx.blink_state & 3;
    /* Scramble's stars stand still: the origin of every frame is 0 */
    const uint32_t base_ofs = scramble ? (uint32_t)(Y0 * 512) : (gx.star_origin + Y0 * 512) % STAR_RNG_PERIOD;
    int n = 0;
    for (int i = gx.nstars - 1; i >= 0; i--)
    {
        int32_t d = (int32_t)gx.star_ofs[i] - (int32_t)base_ofs;
        if (d < 0)
            d += STAR_RNG_PERIOD;
        if (d >= (Y1 - Y0) * 512)
            continue;
        const int y = Y0 + d / 512, x = (d % 512) >> 1;
        if (!((y ^ (x >> 3)) & 1)) /* stars only where V1 ^ H8 */
            continue;
        if (scramble)
        {
            if (blink == 2 && !(y & 2))
                continue;
            if (!((0x80 | gx.star_col[i]) & colormask[blink]))
                continue;
        }
        rs.sx[n] = (uint8_t)x;
        rs.sy[n] = (uint8_t)y;
        rs.sc[n] = gx.star_col[i];
        rs.next_x[n] = rs.first_x[x];
        rs.first_x[x] = (int16_t)n;
        rs.next_y[n] = rs.first_y[y];
        rs.first_y[y] = (int16_t)n;
        n++;
    }
}

/* the background of raw pixel x */
static inline uint16_t background(int x)
{
    switch (gx.var)
    {
    case GX_SCRAMBLE:
    case GX_SCOBRA:
        return gx.background ? gx.bg_blue : 0;
    case GX_ANTEATER:
        return (gx.background && x < 56) ? gx.bg_blue : 0;
    case GX_TURTLES:
    case GX_AMIDAR:
        return gx.bg_turtles[gx.bg_rgb];
    default:
        return 0;
    }
}

static inline void draw_bullet_line(uint16_t *line, int b)
{
    if (scramble_stars())
    {
        /* Scramble: only shells, two pixels, yellow */
        for (int x = rs.bul[b].x - 6; x <= rs.bul[b].x - 5; x++)
            if (x >= 0 && x < RAW_W)
                line[x] = gx.bullet[1];
        return;
    }
    const uint16_t c = gx.bullet[rs.bul[b].missile];
    for (int x = rs.bul[b].x - 4; x < rs.bul[b].x; x++)
        if (x >= 0 && x < RAW_W)
            line[x] = c;
}

/* raw line y, 256 pixels, into rs.line */
static void GAL_HOT(render_raw_line)(int y)
{
    uint16_t *line = rs.line;
    for (int x = 0; x < RAW_W; x++)
        line[x] = background(x);

    for (int s = rs.first_y[y]; s >= 0; s = rs.next_y[s])
        line[rs.sx[s]] = gx.star_pal[rs.sc[s]];

    /* tiles: every column scrolls on its own, pen 0 transparent */
    for (int col = 0; col < 32; col++)
    {
        const int ty = (y + gx.obj[col * 2]) & 0xff;
        const uint16_t code = rs.tile_code[(ty >> 3) * 32 + col];
        uint16_t w = gx.tile_rows[code * 8 + (ty & 7)];
        if (!w)
            continue;
        const uint16_t *pal = &gx.pal[(gx.obj[col * 2 + 1] & 7) << 2];
        uint16_t *dst = &line[col * 8];
        for (int px = 0; px < 8; px++, w >>= 2)
            if (w & 3)
                dst[px] = pal[w & 3];
    }

    for (int n = 7; n >= 0; n--)
    {
        int py = y - rs.spr[n].sy;
        if ((unsigned)py >= 16)
            continue;
        if (rs.spr[n].flipy)
            py = 15 - py;
        const uint32_t w = gx.spr_rows[rs.spr[n].code * 16 + py];
        if (!w)
            continue;
        const uint16_t *pal = &gx.pal[rs.spr[n].color << 2];
        for (int cx = 0; cx < 16; cx++)
        {
            const int x = rs.spr[n].sx + cx;
            if (x < SPRITE_MIN_X || x > 255)
                continue;
            const int pix = (w >> (2 * (rs.spr[n].flipx ? 15 - cx : cx))) & 3;
            if (pix)
                line[x] = pal[pix];
        }
    }

    for (int b = 0; b < rs.nbul; b++)
        if (rs.bul[b].y == y)
            draw_bullet_line(line, b);
}

/* raw column x, line 239 up to line 16, into dst (224 pixels): the upright
 * picture's row 16 + x */
static void GAL_HOT(render_raw_column)(int x, uint16_t *dst)
{
    const uint16_t bg = background(x);
    for (int i = 0; i < Y1 - Y0; i++)
        dst[i] = bg;

    for (int s = rs.first_x[x]; s >= 0; s = rs.next_x[s])
        dst[Y1 - 1 - rs.sy[s]] = gx.star_pal[rs.sc[s]];

    {
        const int col = x >> 3, px = x & 7;
        const int scroll = gx.obj[col * 2];
        const uint16_t *pal = &gx.pal[(gx.obj[col * 2 + 1] & 7) << 2];
        int i = 0;
        while (i < Y1 - Y0)
        {
            const int y = Y1 - 1 - i;
            const int ty = (y + scroll) & 0xff;
            const uint16_t code = rs.tile_code[(ty >> 3) * 32 + col];
            const uint16_t w = gx.tile_cols[code * 8 + px];
            int py = ty & 7; /* this tile covers rows ty&7 down to 0 */
            if (!w)
            {
                i += py + 1;
                continue;
            }
            for (; py >= 0 && i < Y1 - Y0; py--, i++)
            {
                const int pix = (w >> (2 * py)) & 3;
                if (pix)
                    dst[i] = pal[pix];
            }
        }
    }

    if (x >= SPRITE_MIN_X)
    {
        for (int n = 7; n >= 0; n--)
        {
            int cx = x - rs.spr[n].sx;
            if ((unsigned)cx >= 16)
                continue;
            if (rs.spr[n].flipx)
                cx = 15 - cx;
            const uint32_t w = gx.spr_cols[rs.spr[n].code * 16 + cx];
            if (!w)
                continue;
            const uint16_t *pal = &gx.pal[rs.spr[n].color << 2];
            for (int py = 0; py < 16; py++)
            {
                const int y = rs.spr[n].sy + py;
                if (y < Y0 || y >= Y1)
                    continue;
                const int pix = (w >> (2 * (rs.spr[n].flipy ? 15 - py : py))) & 3;
                if (pix)
                    dst[Y1 - 1 - y] = pal[pix];
            }
        }
    }

    for (int b = 0; b < rs.nbul; b++)
    {
        int x0, x1;
        uint16_t c;
        if (scramble_stars())
        {
            x0 = rs.bul[b].x - 6;
            x1 = rs.bul[b].x - 5;
            c = gx.bullet[1];
        }
        else
        {
            x0 = rs.bul[b].x - 4;
            x1 = rs.bul[b].x - 1;
            c = gx.bullet[rs.bul[b].missile];
        }
        if (x >= x0 && x <= x1)
            dst[Y1 - 1 - rs.bul[b].y] = c;
    }
}

static void GAL_HOT(gx_render_band)(const gal_band_t *b)
{
    if (b->w == GAL_W)
    {
        /* upright rows: raw columns */
        for (int r = 0; r < b->h; r++)
        {
            const int x = b->y0 + r - 16;
            uint16_t *dst = b->buf + r * b->w;
            if (x < 0 || x >= RAW_W)
                memset(dst, 0, GAL_W * sizeof(uint16_t));
            else
                render_raw_column(x, dst);
        }
        return;
    }
    /* tate: upright columns are raw lines */
    for (int c = 0; c < b->w; c++)
    {
        const int ux = b->x0 + c, y = Y1 - 1 - ux;
        render_raw_line(y);
        for (int r = 0; r < b->h; r++)
        {
            const int x = b->y0 + r - 16;
            b->buf[r * b->w + c] = (x >= 0 && x < RAW_W) ? rs.line[x] : 0;
        }
    }
}

/* --- High scores ------------------------------------------------------------------- */

static const gal_hiscore_region_t galaxian_hiscore[] = {{0x40a8, 0x03, 0x00, 0x00}};
static const gal_hiscore_region_t mooncrst_hiscore[] = {{0x8042, 0x54, 0x00, 0x24}};
static const gal_hiscore_region_t scramble_hiscore[] = {{0x4200, 0x1e, 0x00, 0x01}, {0x40a8, 0x03, 0x00, 0x01}};
static const gal_hiscore_region_t scobra_hiscore[] = {{0x8200, 0x1e, 0x00, 0x01}, {0x80a8, 0x03, 0x00, 0x01}};
static const gal_hiscore_region_t turtles_hiscore[] = {
    {0x8200, 0x1e, 0x00, 0x00}, /* the table */
    {0x80a8, 3, 0x00, 0x00},    /* the top score */
    {0x9340, 1, 0x01, 0x01},    /* video RAM: set up */
};
static const gal_hiscore_region_t amidar_hiscore[] = {{0x8200, 0x1e, 0x00, 0x01}, {0x80a8, 3, 0x00, 0x01}};
static const gal_hiscore_region_t anteater_hiscore[] = {{0x80ef, 0x3c, 0x01, 0x4c}};
static const gal_hiscore_region_t bagman_hiscore[] = {{0x6217, 0x50, 0x00, 0x10}};

/* Turtles and Amidar draw the top score only at certain points: after the
 * saved table is put in, the top bar is drawn from it (GalaginoPlus) */
static void turtles_hs_restored(void)
{
    static const uint16_t cells[6] = {0x9241, 0x9221, 0x9201, 0x91e1, 0x91c1, 0x91a1};
    int shown = 0;
    for (int i = 0; i < 6; i++)
    {
        const uint8_t b = gal.ram[0x80aa - 0x8000 - i / 2];
        const uint8_t d = (i & 1) ? (b & 0x0f) : (b >> 4);
        if (d || i >= 4)
            shown = 1;
        gx.vram[cells[i] - 0x9000] = shown ? d : 0x10;
    }
}

#define N(a) (sizeof(a) / sizeof((a)[0]))
#define GX_OPS(init_fn, hs, extra, restored)                                                            \
    {                                                                                                  \
        .init = init_fn, .reset = gx_reset, .run_slice = gx_run_slice, .prepare_frame = gx_prepare_frame, \
        .render_band = gx_render_band, .sound = gx_sound, .view_top = 16, .view_h = 256,               \
        .extra_button = extra, .hiscore = hs, .nhiscore = N(hs), .hs_restored = restored,              \
    }

const gal_game_ops_t gal_galaxian_ops = GX_OPS(galaxian_init, galaxian_hiscore, 0, NULL);
const gal_game_ops_t gal_mooncrst_ops = GX_OPS(mooncrst_init, mooncrst_hiscore, 0, NULL);
const gal_game_ops_t gal_scramble_ops = GX_OPS(scramble_init, scramble_hiscore, 1, NULL);
const gal_game_ops_t gal_scobra_ops = GX_OPS(scobra_init, scobra_hiscore, 1, NULL);
const gal_game_ops_t gal_anteater_ops = GX_OPS(anteater_init, anteater_hiscore, 0, NULL);
const gal_game_ops_t gal_turtles_ops = GX_OPS(turtles_init, turtles_hiscore, 0, turtles_hs_restored);
const gal_game_ops_t gal_amidar_ops = GX_OPS(amidar_init, amidar_hiscore, 0, turtles_hs_restored);
const gal_game_ops_t gal_bagman_ops = GX_OPS(bagman_init, bagman_hiscore, 0, NULL);
