/*
 * Donkey Kong (Nintendo, 1981), from upstream Galagino's dkong.h.
 *
 * A Z80 at 3.072 MHz and an MB8884 (an 8048) sound CPU at 6 MHz / 15 =
 * 400 kHz machine cycles, writing an 8-bit DAC; walk, jump and stomp were
 * discrete circuits and are Galagino's recorded samples. The raster is 256
 * lines: the game uses the upright rows 16..271 (tile rows 2..33).
 *
 * Galagino's RAM buffer holds 6000-77ff at 0000: work RAM, the sprite RAM the
 * DMA copies to at 7000 (1000), the video RAM at 7400 (1400).
 *
 * Donkey Kong Jr. (Nintendo, 1982), after MAME's nintendo/dkong.cpp and
 * dkong_a.cpp, runs on the same board: 24 KB of program (the set's ROMs in
 * MAME's order of 2 KB pieces), 512 characters in two banks, a sprite bank.
 * Its sound CPU reads the music command as external data and three more
 * signals on P2; walking, jumping, climbing and falling are discrete circuits,
 * here GalaginoPlus's recorded Donkey Kong samples.
 *
 * Donkey Kong 3 (Nintendo, 1983), after the same driver: a Z80 at 4 MHz, 256
 * sprites, one palette of 256 colours; its sound is two RP2A03, the NES's
 * 6502 with its APU, each fed by latches and woken by the vertical blank. The
 * APU (two pulses, a triangle, noise) is GalaginoPlus's.
 *
 * Galagino runs the 8048 as fast as its audio queue drains and plays the DAC at
 * 11765 Hz (6 MHz / 15 / 34 cycles per sample). Here it runs by its clock in
 * step with the Z80, and the DAC is sampled as the level it holds: each write
 * is stamped with its cycle within the quarter frame, and the output samples
 * read the level at their time.
 */
#include "gal_internal.h"

#include "dip_switches.h"
#include "i8048.h"
#include "m6502.h"

#include "data/dkong_sample_fall.h"
#include "data/dkong_sample_jump.h"
#include "data/dkong_sample_stomp.h"
#include "data/dkong_sample_walk0.h"
#include "data/dkong_sample_walk1.h"
#include "data/dkong_sample_walk2.h"

#define MAIN_CYCLES_PER_FRAME 50688
#define SOUND_CYCLES_PER_FRAME 6600 /* 400 kHz / 60.606 Hz */
#define STEPS_PER_QUARTER 16
#define SAMPLE_RATE 11765 /* of the recorded samples */
#define MAX_DAC_EVENTS 512

const unsigned char *dkong_rom_cpu2; /* i8048.h reads its ROM through this */

/* --- The NES APU of Donkey Kong 3's RP2A03s (GalaginoPlus's dk3_apu_t) --------- */

typedef struct
{
    uint8_t p_ctrl[2], p_sweep[2], p_seq[2], p_env_vol[2], p_env_cnt[2], p_len[2], p_sweep_cnt[2];
    uint8_t p_en[2], p_env_rst[2], p_sweep_rst[2];
    uint16_t p_timer[2];
    int32_t p_cnt[2];
    uint16_t t_timer;
    uint8_t t_en, t_seq, t_lin_ctrl, t_lin, t_lin_reload, t_len;
    int32_t t_cnt;
    uint8_t n_ctrl, n_period_idx, n_mode, n_en, n_env_vol, n_env_cnt, n_env_rst, n_len;
    uint16_t n_lfsr;
    int32_t n_cnt;
} apu_t;

static const uint8_t apu_length[32] = {10, 254, 20, 2,  40, 4,  80, 6,  160, 8,  60, 10, 14, 12, 26, 14,
                                       12, 16,  24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30};
static const uint8_t apu_duty[4][8] = {
    {0, 1, 0, 0, 0, 0, 0, 0}, {0, 1, 1, 0, 0, 0, 0, 0}, {0, 1, 1, 1, 1, 0, 0, 0}, {1, 0, 0, 1, 1, 1, 1, 1}};
static const uint8_t apu_tri[32] = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2,  1,  0,
                                    0,  1,  2,  3,  4,  5,  6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
static const uint16_t apu_noise_period[16] = {4,   8,   16,  32,  64,  96,   128,  160,
                                              202, 254, 380, 508, 762, 1016, 2034, 4068};

static void apu_reset(apu_t *a)
{
    memset(a, 0, sizeof(*a));
    a->n_lfsr = 1;
}

static void apu_write(apu_t *a, uint16_t addr, uint8_t v)
{
    const int ch = (addr >> 2) & 1;
    switch (addr)
    {
    case 0x4000:
    case 0x4004:
        a->p_ctrl[ch] = v;
        break;
    case 0x4001:
    case 0x4005:
        a->p_sweep[ch] = v;
        a->p_sweep_rst[ch] = 1;
        break;
    case 0x4002:
    case 0x4006:
        a->p_timer[ch] = (uint16_t)((a->p_timer[ch] & 0x700) | v);
        break;
    case 0x4003:
    case 0x4007:
        a->p_timer[ch] = (uint16_t)((a->p_timer[ch] & 0xff) | ((v & 7) << 8));
        a->p_seq[ch] = 0;
        a->p_cnt[ch] = 0;
        a->p_env_rst[ch] = 1;
        if (a->p_en[ch])
            a->p_len[ch] = apu_length[v >> 3];
        break;
    case 0x4008:
        a->t_lin_ctrl = v;
        break;
    case 0x400a:
        a->t_timer = (uint16_t)((a->t_timer & 0x700) | v);
        break;
    case 0x400b:
        a->t_timer = (uint16_t)((a->t_timer & 0xff) | ((v & 7) << 8));
        a->t_cnt = 0;
        a->t_lin_reload = 1;
        if (a->t_en)
            a->t_len = apu_length[v >> 3];
        break;
    case 0x400c:
        a->n_ctrl = v;
        break;
    case 0x400e:
        a->n_period_idx = v & 0x0f;
        a->n_mode = (v >> 7) & 1;
        break;
    case 0x400f:
        a->n_env_rst = 1;
        if (a->n_en)
            a->n_len = apu_length[v >> 3];
        break;
    case 0x4015:
        a->p_en[0] = v & 1;
        a->p_en[1] = (v >> 1) & 1;
        a->t_en = (v >> 2) & 1;
        a->n_en = (v >> 3) & 1;
        if (!a->p_en[0])
            a->p_len[0] = 0;
        if (!a->p_en[1])
            a->p_len[1] = 0;
        if (!a->t_en)
            a->t_len = 0;
        if (!a->n_en)
            a->n_len = 0;
        break;
    default:
        break;
    }
}

static uint8_t apu_status(const apu_t *a)
{
    return (uint8_t)((a->p_len[0] ? 1 : 0) | (a->p_len[1] ? 2 : 0) | (a->t_len ? 4 : 0) | (a->n_len ? 8 : 0));
}

static void env_clock(uint8_t ctrl, uint8_t *rst, uint8_t *vol, uint8_t *cnt)
{
    if (*rst)
    {
        *rst = 0;
        *vol = 15;
        *cnt = ctrl & 0x0f;
    }
    else if (!(ctrl & 0x10))
    {
        if (*cnt > 0)
            (*cnt)--;
        else
        {
            *cnt = ctrl & 0x0f;
            if (*vol > 0)
                (*vol)--;
            else if (ctrl & 0x20)
                *vol = 15;
        }
    }
}

/* the frame sequencer, 240 Hz: envelopes and the linear counter, and on
 * every second step the length counters and the sweeps */
static void apu_frame_step(apu_t *a, int clock_length)
{
    for (int ch = 0; ch < 2; ch++)
        env_clock(a->p_ctrl[ch], &a->p_env_rst[ch], &a->p_env_vol[ch], &a->p_env_cnt[ch]);
    env_clock(a->n_ctrl, &a->n_env_rst, &a->n_env_vol, &a->n_env_cnt);
    if (a->t_lin_reload)
        a->t_lin = a->t_lin_ctrl & 0x7f;
    else if (a->t_lin > 0)
        a->t_lin--;
    if (!(a->t_lin_ctrl & 0x80))
        a->t_lin_reload = 0;
    if (!clock_length)
        return;
    for (int ch = 0; ch < 2; ch++)
    {
        if (!(a->p_ctrl[ch] & 0x20) && a->p_len[ch] > 0)
            a->p_len[ch]--;
        const uint8_t sw = a->p_sweep[ch];
        if (a->p_sweep_rst[ch])
        {
            a->p_sweep_cnt[ch] = (sw >> 4) & 7;
            a->p_sweep_rst[ch] = 0;
        }
        else if (a->p_sweep_cnt[ch] > 0)
            a->p_sweep_cnt[ch]--;
        else
        {
            a->p_sweep_cnt[ch] = (sw >> 4) & 7;
            const uint16_t cur = a->p_timer[ch] & 0x7ff;
            if ((sw & 0x80) && (sw & 7) && cur >= 8)
            {
                const uint16_t delta = cur >> (sw & 7);
                /* pulse 1 negates in ones' complement */
                const uint16_t target = (sw & 8) ? (uint16_t)(cur - delta - (ch == 0)) : (uint16_t)(cur + delta);
                if (target <= 0x7ff)
                    a->p_timer[ch] = target;
            }
        }
    }
    if (!(a->t_lin_ctrl & 0x80) && a->t_len > 0)
        a->t_len--;
    if (!(a->n_ctrl & 0x20) && a->n_len > 0)
        a->n_len--;
}

/* `ticks` CPU cycles on, and the mix of the moment (about +-300) */
static int apu_sample(apu_t *a, int ticks)
{
    int val = 0;
    for (int ch = 0; ch < 2; ch++)
    {
        if (!a->p_en[ch] || !a->p_len[ch])
            continue;
        const uint16_t per = a->p_timer[ch] & 0x7ff;
        if (per < 8)
            continue;
        const uint8_t sh = a->p_sweep[ch] & 7;
        if (sh && !(a->p_sweep[ch] & 8) && per + (per >> sh) > 0x7ff)
            continue;
        const int vol = (a->p_ctrl[ch] & 0x10) ? (a->p_ctrl[ch] & 0x0f) : a->p_env_vol[ch];
        if (!vol)
            continue;
        const int period = (per + 1) * 2;
        a->p_cnt[ch] -= ticks;
        while (a->p_cnt[ch] <= 0)
        {
            a->p_cnt[ch] += period;
            a->p_seq[ch] = (a->p_seq[ch] + 1) & 7;
        }
        val += apu_duty[(a->p_ctrl[ch] >> 6) & 3][a->p_seq[ch]] ? vol * 6 : -vol * 6;
    }
    if (a->t_en && a->t_len && a->t_lin)
    {
        const int period = (a->t_timer & 0x7ff) + 1;
        if (period >= 2)
        {
            a->t_cnt -= ticks;
            while (a->t_cnt <= 0)
            {
                a->t_cnt += period;
                a->t_seq = (a->t_seq + 1) & 31;
            }
            val += (apu_tri[a->t_seq] - 7) * 6;
        }
    }
    if (a->n_en && a->n_len)
    {
        const int vol = (a->n_ctrl & 0x10) ? (a->n_ctrl & 0x0f) : a->n_env_vol;
        if (vol)
        {
            a->n_cnt -= ticks;
            while (a->n_cnt <= 0)
            {
                a->n_cnt += apu_noise_period[a->n_period_idx];
                const uint16_t bit = (a->n_lfsr ^ (a->n_mode ? (a->n_lfsr >> 6) : (a->n_lfsr >> 1))) & 1;
                a->n_lfsr = (uint16_t)((a->n_lfsr >> 1) | (bit << 14));
            }
            val += (a->n_lfsr & 1) ? vol * 3 : -vol * 3;
        }
    }
    return val;
}

typedef enum
{
    DK_DKONG,
    DK_DKONGJR,
    DK_DKONG3,
} dk_var_t;

static struct
{
    dk_var_t var;
    uint8_t jr; /* Donkey Kong Jr. (and Donkey Kong 3: the jr parts they share) */
    uint8_t gfx_bank, sprite_bank;
    uint8_t jr_p2;   /* Donkey Kong Jr.: P2 bits 4-6 from the main CPU's latches */
    uint8_t jr_fall; /* the falling sound's latch */
    const uint8_t *rom;
    uint16_t *tiles;     /* [256][8], Donkey Kong Jr. [512][8] */
    uint32_t *sprites;   /* [128][16] */
    uint32_t *sprites_x; /* the same flipped in x */
    uint16_t *cmap;      /* [4][256][4]: per screen colour table and tile position */
    uint16_t cmap_sprite[4][16][4];

    uint8_t nmi_enable;
    uint8_t colortable_select;
    uint8_t sfx_index;

    struct i8048_state_S snd;
    unsigned long quarter_start; /* snd.cycles at the start of the quarter */
    struct
    {
        uint16_t t;
        uint8_t v;
    } dac[MAX_DAC_EVENTS];
    int ndac;
    uint8_t dac_level;
    int32_t dc_x, dc_y; /* DC blocker state */

    gal_voice_t voice[4]; /* walk, jump, stomp; Donkey Kong Jr.'s fall */
    uint32_t rng;

    /* Donkey Kong 3 */
    uint16_t *palette; /* [256], in the arena */
    const uint8_t *color_codes;
    m6502_t *snd3[2];
    uint8_t *snd3_ram;
    apu_t *apu;
    uint8_t latch3[3];
    uint8_t snd3_run;
    uint32_t apu_phase;
    int32_t apu_lpf;
} dk;

/* --- The 8048's ports, called by i8048.c --------------------------------------- */

const gal_i8048_io_t *gal_i8048_io;

void i8048_port_write(struct i8048_state_S *state, unsigned char port, unsigned char pos)
{
    if (gal_i8048_io)
    {
        gal_i8048_io->port_write(state, port, pos);
        return;
    }
    if (port == 1)
    {
        if (dk.ndac < MAX_DAC_EVENTS)
        {
            dk.dac[dk.ndac].t = (uint16_t)(state->cycles - dk.quarter_start);
            dk.dac[dk.ndac].v = pos;
            dk.ndac++;
        }
    }
    else if (port == 2)
        state->p2_state = pos;
}

unsigned char i8048_port_read(struct i8048_state_S *state, unsigned char port)
{
    if (gal_i8048_io)
        return gal_i8048_io->port_read(state, port);
    if (port == 2 && dk.jr)
        return (unsigned char)((state->p2_state & 0x8f) | dk.jr_p2);
    if (port == 2)
        return (unsigned char)state->p2_state;
    return 0;
}

void i8048_xdm_write(struct i8048_state_S *state, unsigned char addr, unsigned char data)
{
    (void)state;
    (void)addr;
    (void)data;
}

unsigned char i8048_xdm_read(struct i8048_state_S *state, unsigned char addr)
{
    if (gal_i8048_io)
        return gal_i8048_io->xdm_read(state, addr);
    /* Donkey Kong Jr.: the command latch, five bits */
    if (dk.jr)
        return dk.sfx_index & 0x1f;
    /* inverted Z80 MUS register */
    if (state->p2_state & 0x40)
        return dk.sfx_index ^ 0x0f;
    return dkong_rom_cpu2[2048 + addr + 256 * (state->p2_state & 7)];
}

/* --- Main CPU ---------------------------------------------------------------- */

static void dk_trigger_sound(int snd)
{
    static const struct
    {
        const unsigned char *data;
        uint16_t length;
    } samples[] = {
        {dkong_sample_walk0, sizeof(dkong_sample_walk0)}, {dkong_sample_walk1, sizeof(dkong_sample_walk1)},
        {dkong_sample_walk2, sizeof(dkong_sample_walk2)}, {dkong_sample_jump, sizeof(dkong_sample_jump)},
        {dkong_sample_stomp, sizeof(dkong_sample_stomp)},
    };
    /* 0 walk (one of three variants), 1 jump, 2 stomp; at 6.25, 12.5 and 25% volume */
    int idx = snd + 2;
    if (snd == 0)
    {
        dk.rng = dk.rng * 1664525u + 1013904223u;
        idx = (int)((dk.rng >> 16) % 3);
    }
    gal_voice_start(&dk.voice[snd], (const int8_t *)samples[idx].data, samples[idx].length, SAMPLE_RATE, 2 - snd);
}

static uint8_t GAL_HOT(dk_read)(z80_t *cpu, uint16_t addr)
{
    (void)cpu;
    const uint16_t keys = gal.buttons;
    switch (addr & 0xfff0)
    {
    case 0x7c00: /* IN0 */
    {
        uint8_t v = 0;
        if (keys & GAL_BTN_RIGHT)
            v |= 0x01;
        if (keys & GAL_BTN_LEFT)
            v |= 0x02;
        if (keys & GAL_BTN_UP)
            v |= 0x04;
        if (keys & GAL_BTN_DOWN)
            v |= 0x08;
        if (keys & GAL_BTN_FIRE)
            v |= 0x10;
        return v;
    }
    case 0x7c80: /* IN1 */
        return 0x00;
    case 0x7d00: /* IN2 */
    {
        uint8_t v = 0;
        if (keys & GAL_BTN_COIN)
            v |= 0x80;
        if (keys & GAL_BTN_START)
            v |= 0x04;
        if (keys & GAL_BTN_START2)
            v |= 0x08;
        return v;
    }
    case 0x7d80:
        return DKONG_DIP;
    }
    return 0xff;
}

/* Donkey Kong Jr.'s latches (MAME's dkongjr_map and dkongjr_audio); kept
 * out of the SRAM functions that call it, as the other parts of Donkey Kong
 * Jr. and Donkey Kong 3 */
#define DK_FLASH __attribute__((noinline))

static DK_FLASH void jr_write(z80_t *cpu, uint16_t addr, uint8_t value)
{
    const int bit = value & 1;
    switch (addr)
    {
    case 0x7c00:
        dk.sfx_index = value; /* the music command */
        return;
    case 0x7c80:
        dk.gfx_bank = (uint8_t)bit;
        return;
    case 0x7c81: /* P2 bit 6, inverted */
        dk.jr_p2 = (uint8_t)((dk.jr_p2 & ~0x40) | (bit ? 0 : 0x40));
        return;
    case 0x7d00: /* walking */
    case 0x7d02: /* climbing */
        if (bit)
            dk_trigger_sound(0);
        return;
    case 0x7d01: /* jumping */
        if (bit)
            dk_trigger_sound(1);
        return;
    case 0x7d03: /* P2 bit 5, inverted */
        dk.jr_p2 = (uint8_t)((dk.jr_p2 & ~0x20) | (bit ? 0 : 0x20));
        return;
    case 0x7d04:
        dk.snd.T1 = !bit;
        return;
    case 0x7d05:
        dk.snd.T0 = !bit;
        return;
    case 0x7d06: /* P2 bit 4, inverted */
        dk.jr_p2 = (uint8_t)((dk.jr_p2 & ~0x10) | (bit ? 0 : 0x10));
        return;
    case 0x7d80:
        dk.snd.notINT = !bit;
        return;
    case 0x7d81: /* falling */
        if (bit && !dk.jr_fall)
            gal_voice_start(&dk.voice[3], (const int8_t *)dkong_sample_fall, sizeof(dkong_sample_fall), SAMPLE_RATE, 1);
        dk.jr_fall = (uint8_t)bit;
        return;
    case 0x7d83:
        dk.sprite_bank = (uint8_t)bit;
        return;
    case 0x7d84:
        dk.nmi_enable = (uint8_t)bit;
        if (!dk.nmi_enable)
            z80_set_nmi_line(cpu, 0);
        return;
    case 0x7d85:
        if (bit) /* the DMA: the sprites from 6900 to 7000 */
            memcpy(gal.ram + 0x1000, gal.ram + 0x900, 384);
        return;
    case 0x7d86:
    case 0x7d87:
        dk.colortable_select =
            (uint8_t)((dk.colortable_select & ~(1 << (addr & 1))) | (bit << (addr & 1)));
        return;
    default:
        return; /* 7d82: flip, for the cocktail cabinet */
    }
}

static void GAL_HOT(dk_write)(z80_t *cpu, uint16_t addr, uint8_t value)
{
    if (dk.jr)
    {
        jr_write(cpu, addr, value);
        return;
    }
    if ((addr & 0xfe00) != 0x7c00) /* 7800 DMA registers and the rest */
        return;

    if (addr == 0x7c00) /* music */
        dk.sfx_index = value;

    if ((addr & 0xfff0) == 0x7d00)
    {
        const int reg = addr & 0x0f;
        if (reg <= 2 && value) /* samples: walk, jump, stomp */
            dk_trigger_sound(reg);
        else if (reg == 3)
        {
            if (value & 1)
                dk.snd.p2_state &= ~0x20;
            else
                dk.snd.p2_state |= 0x20;
        }
        else if (reg == 4)
            dk.snd.T1 = !(value & 1);
        else if (reg == 5)
            dk.snd.T0 = !(value & 1);
    }

    if (addr == 0x7d80)
        dk.snd.notINT = !(value & 1);
    else if (addr == 0x7d84)
    {
        dk.nmi_enable = value & 1;
        if (!dk.nmi_enable)
            z80_set_nmi_line(cpu, 0);
    }
    else if (addr == 0x7d85 && (value & 1))
    {
        /* the DMA: Donkey Kong only uses it to copy the sprites from 6900 to 7000 */
        memcpy(gal.ram + 0x1000, gal.ram + 0x900, 384);
    }
    else if (addr == 0x7d86)
        dk.colortable_select = (uint8_t)((dk.colortable_select & ~1) | (value & 1));
    else if (addr == 0x7d87)
        dk.colortable_select = (uint8_t)((dk.colortable_select & ~2) | ((value << 1) & 2));
}

static uint8_t dk_in(z80_t *cpu, uint16_t port)
{
    (void)cpu;
    (void)port;
    return 0xff;
}

static void dk_out(z80_t *cpu, uint16_t port, uint8_t value)
{
    (void)cpu;
    (void)port;
    (void)value;
}

/* --- Life cycle ---------------------------------------------------------------- */

/* cmapconv.py parse_palette with two PROMs: inverted rrrgggbb */
static uint16_t dk_palette(uint8_t lo, uint8_t hi)
{
    const unsigned c = ((unsigned)hi << 4) + lo;
    const unsigned r = 31 - 31 * ((c >> 5) & 0x7) / 7;
    const unsigned g = 63 - 63 * ((c >> 2) & 0x7) / 7;
    const unsigned b = 31 - 31 * ((c >> 0) & 0x3) / 3;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

/* spriteconv.py parse_sprite_dkong: each sprite spread over all four ROMs
 * (Donkey Kong 3: 256 of them, in ROMs twice as large); no flipped copy
 * when out_xflip is NULL */
static void dk_conv_sprites(const uint8_t *roms, int quarter, int count, uint32_t *out_normal, uint32_t *out_xflip)
{
    for (int spr = 0; spr < count; spr++)
    {
        uint32_t *dst = out_normal + 16 * spr;
        for (int y = 0; y < 16; y++)
        {
            const uint8_t *d0 = roms + quarter * (y / 8) + 16 * spr;
            const uint8_t *d1 = roms + quarter * (y / 8 + 2) + 16 * spr;
            uint32_t val = 0;
            for (int x = 0; x < 16; x++)
            {
                const int c0 = (d0[15 - x] & (0x80 >> (y & 7))) ? 1 : 0;
                const int c1 = (d1[15 - x] & (0x80 >> (y & 7))) ? 2 : 0;
                val = (val >> 2) | ((uint32_t)(c0 + c1) << 30);
            }
            dst[y] = val;
        }
        if (out_xflip)
            gal_sprite_xflip(dst, out_xflip + 16 * spr);
    }
}

static int dk_init_common(uint8_t *const *regions)
{
    dk.rom = regions[0];
    dkong_rom_cpu2 = regions[1];
    gal_i8048_io = NULL;
    dk.tiles = (uint16_t *)gal_alloc((dk.jr ? 512 : 256) * 8 * 2);
    dk.sprites = (uint32_t *)gal_alloc(128 * 16 * 4);
    dk.sprites_x = (uint32_t *)gal_alloc(128 * 16 * 4);
    dk.cmap = (uint16_t *)gal_alloc(4 * 256 * 4 * 2);
    if (!dk.tiles || !dk.sprites || !dk.sprites_x || !dk.cmap)
        return 0;

    if (dk.jr)
    {
        /* two banks: the planes are the two halves of the 8 KB */
        gal_conv_chars_2(regions[2], regions[2] + 0x1000, dk.tiles);
        gal_conv_chars_2(regions[2] + 0x800, regions[2] + 0x1800, dk.tiles + 256 * 8);
    }
    else
        gal_conv_chars_2(regions[2], regions[2] + 0x800, dk.tiles);
    dk_conv_sprites(regions[3], 0x800, 128, dk.sprites, dk.sprites_x);

    /* cmapconv.py parse_colormap_dkong: four screen colour tables */
    /* Donkey Kong Jr.: both palette PROMs in one region */
    const uint8_t *pal_lo = regions[4], *pal_hi = dk.jr ? regions[4] + 0x100 : regions[5];
    const uint8_t *codes = dk.jr ? regions[5] : regions[6];
    uint16_t palette[256];
    for (int i = 0; i < 256; i++)
        palette[i] = gal_color(dk_palette(pal_lo[i] & 0x0f, pal_hi[i] & 0x0f));
    for (int s = 0; s < 4; s++)
    {
        for (int i = 0; i < 256; i++)
        {
            const int idx = (codes[i] & 0x0f) + 16 * s;
            for (int k = 0; k < 4; k++)
                dk.cmap[(s * 256 + i) * 4 + k] = palette[4 * idx + k];
        }
        for (int i = 0; i < 16; i++)
            for (int k = 0; k < 4; k++)
                dk.cmap_sprite[s][i][k] = palette[4 * (i + 16 * s) + k];
    }

    z80_t *cpu = &gal.cpu[0];
    for (int p = 0; p < (dk.jr ? 0x60 : 0x40); p++)
        cpu->rd_page[p] = dk.rom + p * 256;
    for (int p = 0; p < 0x18; p++) /* 6000-77ff RAM */
        cpu->rd_page[0x60 + p] = cpu->wr_page[0x60 + p] = gal.ram + p * 256;
    cpu->read = dk_read;
    cpu->write = dk_write;
    cpu->in = dk_in;
    cpu->out = dk_out;
    return 1;
}

static int dk_init(uint8_t *const *regions)
{
    dk.var = DK_DKONG;
    dk.jr = 0;
    return dk_init_common(regions);
}

/* MAME's dkongjrj: the three 8 KB ROMs, in 2 KB pieces, go to these
 * addresses (ROM_LOAD and ROM_CONTINUE) */
static const uint16_t jr_piece[12] = {0x0000, 0x0800, 0x3000, 0x3800, 0x2000, 0x4800,
                                      0x1000, 0x5800, 0x4000, 0x2800, 0x5000, 0x1800};

static int jr_init(uint8_t *const *regions)
{
    dk.var = DK_DKONGJR;
    dk.jr = 1;
    /* piece k goes to jr_piece[k]: moved in place along the permutation's
     * cycles, with one piece of room */
    uint8_t *rom = regions[0];
    uint8_t *tmp = (uint8_t *)gal_temp_alloc(0x800);
    if (!tmp)
        return 0;
    int from[12]; /* the piece that ends up at position j */
    for (int k = 0; k < 12; k++)
        from[jr_piece[k] / 0x800] = k;
    uint16_t done = 0;
    for (int start = 0; start < 12; start++)
    {
        if (done & (1u << start))
            continue;
        memcpy(tmp, rom + 0x800 * start, 0x800);
        int j = start;
        while (from[j] != start)
        {
            memcpy(rom + 0x800 * j, rom + 0x800 * from[j], 0x800);
            done |= (uint16_t)(1u << j);
            j = from[j];
        }
        memcpy(rom + 0x800 * j, tmp, 0x800);
        done |= (uint16_t)(1u << j);
    }
    return dk_init_common(regions);
}

/* --- Donkey Kong 3 ---------------------------------------------------------------- */

static uint8_t dk3_read(z80_t *cpu, uint16_t addr)
{
    (void)cpu;
    const uint16_t keys = gal.buttons;
    uint8_t v = 0;
    switch (addr)
    {
    case 0x7c00: /* IN0, active high */
        if (keys & GAL_BTN_RIGHT)
            v |= 0x01;
        if (keys & GAL_BTN_LEFT)
            v |= 0x02;
        if (keys & GAL_BTN_UP)
            v |= 0x04;
        if (keys & GAL_BTN_DOWN)
            v |= 0x08;
        if (keys & GAL_BTN_FIRE)
            v |= 0x10;
        if (keys & GAL_BTN_START)
            v |= 0x20;
        if (keys & GAL_BTN_START2)
            v |= 0x40;
        return v;
    case 0x7c80: /* IN1: the coin */
        return (keys & GAL_BTN_COIN) ? 0x20 : 0;
    default:
        return 0; /* the DIP switches: MAME's defaults, 1C1C, 3 lives, upright */
    }
}

static void dk3_write(z80_t *cpu, uint16_t addr, uint8_t value)
{
    const int bit = value & 1;
    switch (addr)
    {
    case 0x7c00:
    case 0x7c80:
    case 0x7d00:
        dk.latch3[(addr - 0x7c00) >> 7] = value;
        break;
    case 0x7d80: /* the RP2A03s out of reset */
        if (bit && !dk.snd3_run)
            for (int k = 0; k < 2; k++)
            {
                m6502_reset(dk.snd3[k]);
                dk.snd3[k]->icount = 0;
            }
        dk.snd3_run = (uint8_t)bit;
        break;
    case 0x7e81:
        dk.gfx_bank = (uint8_t)!bit;
        break;
    case 0x7e83:
        dk.sprite_bank = (uint8_t)bit;
        break;
    case 0x7e84:
        dk.nmi_enable = (uint8_t)bit;
        if (!dk.nmi_enable)
            z80_set_nmi_line(cpu, 0);
        break;
    case 0x7e85: /* the DMA: the sprites from 6900 to 7000 */
        if (bit)
            memcpy(gal.ram + 0x1000, gal.ram + 0x900, 0x200);
        break;
    case 0x7e86:
    case 0x7e87:
        dk.colortable_select =
            (uint8_t)((dk.colortable_select & ~(1 << (addr & 1))) | (bit << (addr & 1)));
        break;
    default:
        break; /* 7e80 the coin counter, 7e82 the flip of a cocktail cabinet */
    }
}

/* the RP2A03s: 2 KB of RAM to 7fff, the APU at 4000, the latches at 4016 and
 * 4017, 8 KB of program to ffff */
static uint8_t dk3_snd_read(m6502_t *cpu, uint16_t addr)
{
    const int k = cpu == dk.snd3[1];
    if (addr == 0x4015)
        return apu_status(&dk.apu[k]);
    if (addr == 0x4016)
        return dk.latch3[k ? 2 : 0];
    if (addr == 0x4017)
        return k ? 0 : dk.latch3[1];
    return dk.snd3_ram[0x800 * k + (addr & 0x7ff)];
}

static void dk3_snd_write(m6502_t *cpu, uint16_t addr, uint8_t value)
{
    const int k = cpu == dk.snd3[1];
    if (addr >= 0x4000 && addr < 0x4018)
        apu_write(&dk.apu[k], addr, value);
    else
        dk.snd3_ram[0x800 * k + (addr & 0x7ff)] = value;
}

/* the tables the flash cache keeps: 256 characters per 4 KB (a bank), 64
 * sprites */
static void dk3_derive(int i, const uint8_t *src, uint32_t ofs, uint8_t *out, uint32_t len)
{
    for (uint32_t o = ofs; o < ofs + len; o += 0x1000, out += 0x1000)
    {
        if (i == 0)
        {
            const int bank = (int)(o / 0x1000);
            gal_conv_chars_2(src + 0x800 * bank, src + 0x1000 + 0x800 * bank, (uint16_t *)out);
        }
        else
            dk_conv_sprites(src + 16 * (o / 64), 0x1000, 64, (uint32_t *)out, NULL);
    }
}

static int dk3_init(uint8_t *const *regions)
{
    dk.var = DK_DKONG3;
    dk.jr = 0;
    /* the programs and what the CPUs use all the time to SRAM, the graphics
     * to PSRAM or from the flash cache */
    uint8_t *rom = (uint8_t *)gal_fast_alloc(0x8000);
    uint8_t *snd_rom = (uint8_t *)gal_fast_alloc(0x4000);
    uint8_t *codes = (uint8_t *)gal_fast_alloc(0x100);
    dk.snd3[0] = (m6502_t *)gal_fast_alloc(sizeof(m6502_t));
    dk.snd3[1] = (m6502_t *)gal_fast_alloc(sizeof(m6502_t));
    dk.snd3_ram = (uint8_t *)gal_fast_alloc(0x1000);
    dk.apu = (apu_t *)gal_fast_alloc(2 * sizeof(apu_t));
    dk.palette = (uint16_t *)gal_fast_alloc(256 * 2);
    if (!rom || !snd_rom || !codes || !dk.snd3[0] || !dk.snd3[1] || !dk.snd3_ram || !dk.apu || !dk.palette)
        return 0;
    memcpy(rom, regions[0], 0x8000);
    memcpy(snd_rom, regions[1], 0x2000);
    memcpy(snd_rom + 0x2000, regions[2], 0x2000);
    memcpy(codes, regions[6], 0x100);
    dk.rom = rom;
    dk.color_codes = codes;
    dk.sprites_x = NULL;
    dk.cmap = NULL;
    if (regions[GAL_MAX_REGIONS])
    {
        dk.tiles = (uint16_t *)regions[GAL_MAX_REGIONS];
        dk.sprites = (uint32_t *)regions[GAL_MAX_REGIONS + 1];
    }
    else
    {
        dk.tiles = (uint16_t *)gal_alloc(512 * 8 * 2);
        dk.sprites = (uint32_t *)gal_alloc(256 * 16 * 4);
        if (!dk.tiles || !dk.sprites)
            return 0;
        dk3_derive(0, regions[3], 0, (uint8_t *)dk.tiles, 512 * 8 * 2);
        dk3_derive(1, regions[4], 0, (uint8_t *)dk.sprites, 256 * 16 * 4);
    }

    /* MAME's dkong3_palette: R in the high and G in the low nibble of the
     * first PROM, B in the second, through inverting drivers */
    const uint8_t *prom = regions[5];
    for (int i = 0; i < 256; i++)
    {
        const int r = 15 - (prom[i] >> 4), g = 15 - (prom[i] & 15), b = 15 - (prom[0x200 + i] & 15);
        dk.palette[i] = gal_color((uint16_t)(((r * 31 / 15) << 11) | ((g * 63 / 15) << 5) | (b * 31 / 15)));
    }
    dk.color_codes = regions[6];

    z80_t *cpu = &gal.cpu[0];
    for (int p = 0; p < 0x60; p++)
        cpu->rd_page[p] = dk.rom + p * 256;
    for (int p = 0; p < 0x20; p++)
        cpu->rd_page[0x80 + p] = dk.rom + 0x6000 + p * 256;
    for (int p = 0; p < 0x18; p++) /* 6000-77ff RAM */
        cpu->rd_page[0x60 + p] = cpu->wr_page[0x60 + p] = gal.ram + p * 256;
    cpu->read = dk3_read;
    cpu->write = dk3_write;
    cpu->in = dk_in;
    cpu->out = dk_out;

    for (int k = 0; k < 2; k++)
    {
        m6502_t *c = dk.snd3[k];
        memset(c, 0, sizeof(*c));
        for (int p = 0; p < 0x80; p++)
            if (p != 0x40)
                c->rd_page[p] = c->wr_page[p] = dk.snd3_ram + 0x800 * k + 256 * (p & 7);
        for (int p = 0; p < 0x80; p++)
            c->rd_page[0x80 + p] = snd_rom + 0x2000 * k + 256 * (p & 0x1f);
        c->read = dk3_snd_read;
        c->write = dk3_snd_write;
        c->decimal_disabled = 1; /* the RP2A03 has no decimal mode */
    }
    return 1;
}

static void dk_reset(void)
{
    memset(gal.ram, 0, sizeof(gal.ram));
    dk.nmi_enable = 0;
    dk.colortable_select = 0;
    dk.sfx_index = 0;
    dk.gfx_bank = dk.sprite_bank = 0;
    dk.jr_p2 = 0x70; /* the latches' bits are 0, inverted */
    dk.jr_fall = 0;
    dk.ndac = 0;
    dk.dac_level = 0;
    dk.dc_x = dk.dc_y = 0;
    for (int i = 0; i < 4; i++)
        dk.voice[i].data = NULL;
    if (!dk.rng)
        dk.rng = 12345;
    z80_init(&gal.cpu[0]);
    z80_reset(&gal.cpu[0]);
    if (dk.var == DK_DKONG3)
    {
        memset(dk.snd3_ram, 0, 0x1000);
        memset(dk.latch3, 0, sizeof(dk.latch3));
        for (int k = 0; k < 2; k++)
        {
            apu_reset(&dk.apu[k]);
            m6502_reset(dk.snd3[k]);
            dk.snd3[k]->icount = 0;
        }
        dk.snd3_run = 0;
        dk.apu_phase = 0;
        dk.apu_lpf = 0;
        gal.game_started = 1;
        return;
    }
    i8048_reset(&dk.snd);
    dk.quarter_start = 0;
    gal.game_started = 1; /* as in Galagino */
}

#define DK3_CYCLES_PER_FRAME 66000 /* 4 MHz at 60.6 Hz */
#define APU_CYCLES_PER_FRAME 29537 /* 1.79 MHz */

static DK_FLASH void dk3_run_slice(int q)
{
    for (int i = 0; i < STEPS_PER_QUARTER; i++)
    {
        const int step = q * STEPS_PER_QUARTER + i, steps = 4 * STEPS_PER_QUARTER;
        z80_run(&gal.cpu[0], (step + 1) * DK3_CYCLES_PER_FRAME / steps - step * DK3_CYCLES_PER_FRAME / steps);
        if (dk.snd3_run)
            for (int k = 0; k < 2; k++)
                m6502_run(dk.snd3[k],
                          (step + 1) * APU_CYCLES_PER_FRAME / steps - step * APU_CYCLES_PER_FRAME / steps);
    }
    /* the APUs' frame sequencer, 240 Hz */
    for (int k = 0; k < 2; k++)
        apu_frame_step(&dk.apu[k], q & 1);
    if (q == 3)
    {
        /* the vertical blank: the main CPU's NMI while enabled, the RP2A03s'
         * always */
        if (dk.nmi_enable)
            z80_set_nmi_line(&gal.cpu[0], 1);
        if (dk.snd3_run)
            dk.snd3[0]->nmi = dk.snd3[1]->nmi = 1;
    }
}

static void GAL_HOT(dk_run_slice)(int q)
{
    if (dk.var == DK_DKONG3)
    {
        dk3_run_slice(q);
        return;
    }
    dk.quarter_start = dk.snd.cycles;
    dk.ndac = 0;
    for (int i = 0; i < STEPS_PER_QUARTER; i++)
    {
        const int step = q * STEPS_PER_QUARTER + i, steps = 4 * STEPS_PER_QUARTER;
        const int main_cycles =
            (step + 1) * MAIN_CYCLES_PER_FRAME / steps - step * MAIN_CYCLES_PER_FRAME / steps;
        const unsigned long snd_end =
            dk.quarter_start + (unsigned long)((i + 1) * (SOUND_CYCLES_PER_FRAME / 4) / STEPS_PER_QUARTER);

        z80_run(&gal.cpu[0], main_cycles);
        while ((long)(dk.snd.cycles - snd_end) < 0)
            i8048_step(&dk.snd);
    }

    /* VBLANK: NMI while enabled */
    if (q == 3 && dk.nmi_enable)
    {
        z80_set_nmi_line(&gal.cpu[0], 1);
        z80_set_nmi_line(&gal.cpu[0], 0);
    }
}

/* --- Video --------------------------------------------------------------------- */

static void GAL_HOT(dk_prepare_frame)(void)
{
    gal.active_sprites = 0;
    for (int idx = 0; idx < 96 && gal.active_sprites < GAL_MAX_SPRITES; idx++)
    {
        const uint8_t *base = gal.ram + 0x1000 + (dk.var != DK_DKONG ? 0x200 * dk.sprite_bank : 0) + 4 * idx;
        gal_sprite_t spr;
        /* the position on the upright screen */
        spr.x = base[0] - 23;
        spr.y = base[3] + 8;
        spr.code = (uint8_t)((base[1] & 0x7f) | (dk.var == DK_DKONG3 ? (base[2] & 0x40) << 1 : 0));
        spr.color = base[2] & 0x0f;
        spr.flags = (uint8_t)(((base[2] & 0x80) ? 1 : 0) | ((base[1] & 0x80) ? 2 : 0));
        if (spr.y > -16 && spr.y < GAL_H && spr.x > -16 && spr.x < GAL_W)
            gal.sprite[gal.active_sprites++] = spr;
    }
}

static void GAL_HOT(dk_render_band)(const gal_band_t *b)
{
    gal_fill_band(b, 0);

    const int row_first = b->y0 / 8 < 2 ? 2 : b->y0 / 8;
    const int row_last = (b->y0 + b->h - 1) / 8 > 33 ? 33 : (b->y0 + b->h - 1) / 8;
    const uint16_t *cmap = dk.cmap ? dk.cmap + dk.colortable_select * 256 * 4 : NULL;
    for (int row = row_first; row <= row_last; row++)
    {
        for (int col = b->x0 / 8; col <= (b->x0 + b->w - 1) / 8; col++)
        {
            const unsigned addr = tileaddr[row][col];
            const uint8_t code = gal.ram[0x1400 + addr];
            if (code == 0x10 && dk.var == DK_DKONG) /* blank */
                continue;
            /* a colour per tile position: 32 rows by 8 groups of four columns */
            const uint16_t *colors =
                (dk.var == DK_DKONG3)
                    ? dk.palette + 4 * ((dk.color_codes[row - 2 + 32 * (col / 4)] & 0x0f) + 16 * dk.colortable_select)
                    : cmap + 4 * (row - 2 + 32 * (col / 4));
            gal_draw_tile(b, 8 * col, 8 * row, dk.tiles + 8 * (code + 256 * dk.gfx_bank), colors);
        }
    }

    /* black sprite pixels are not transparent here: they mask */
    for (int s = 0; s < gal.active_sprites; s++)
    {
        const gal_sprite_t *spr = &gal.sprite[s];
        if (dk.var == DK_DKONG3)
        {
            gal_draw_sprite_fx(b, spr->x, spr->y, dk.sprites + 16 * spr->code, spr->flags & 2, spr->flags & 1,
                               dk.palette + 4 * (spr->color + 16 * dk.colortable_select), 1);
            continue;
        }
        const uint32_t *rows = ((spr->flags & 2) ? dk.sprites_x : dk.sprites) + 16 * spr->code;
        gal_draw_sprite_ex(b, spr->x, spr->y, rows, spr->flags & 1, dk.cmap_sprite[dk.colortable_select][spr->color],
                           1);
    }
}

/* --- Sound --------------------------------------------------------------------- */

/* the two APUs: stepped by the RP2A03's clock per output sample, through
 * GalaginoPlus's low-pass */
static DK_FLASH void dk3_sound(int16_t *out, int n)
{
    const uint32_t step = (uint32_t)(((uint64_t)1789773 << 16) / (uint32_t)gal.samplerate);
    for (int i = 0; i < n; i++)
    {
        dk.apu_phase += step;
        const int ticks = (int)(dk.apu_phase >> 16);
        dk.apu_phase &= 0xffff;
        const int v = apu_sample(&dk.apu[0], ticks) + apu_sample(&dk.apu[1], ticks);
        dk.apu_lpf = (v * 3 + dk.apu_lpf) >> 2;
        int x = dk.apu_lpf * 40;
        out[i] = (int16_t)(x > 32767 ? 32767 : (x < -32768 ? -32768 : x));
    }
}

static void GAL_HOT(dk_sound)(int16_t *out, int n)
{
    if (dk.var == DK_DKONG3)
    {
        dk3_sound(out, n);
        return;
    }
    /* the DAC level at each output sample's time within the quarter */
    const uint32_t quarter = SOUND_CYCLES_PER_FRAME / 4;
    int e = 0;
    for (int i = 0; i < n; i++)
    {
        const uint32_t t = (uint32_t)i * quarter / (uint32_t)n;
        while (e < dk.ndac && dk.dac[e].t <= t)
            dk.dac_level = dk.dac[e++].v;
        /* Galagino plays the DAC at half the range; the DC blocker (a one-pole
         * high-pass at about 30 Hz) stands in for the output capacitor */
        const int32_t x = ((int32_t)dk.dac_level - 128) * 64;
        dk.dc_y = x - dk.dc_x + ((dk.dc_y * 255) >> 8);
        dk.dc_x = x;
        out[i] = (int16_t)(dk.dc_y > 32767 ? 32767 : (dk.dc_y < -32768 ? -32768 : dk.dc_y));
    }
    while (e < dk.ndac)
        dk.dac_level = dk.dac[e++].v;

    for (int v = 0; v < 4; v++)
        gal_voice_mix(&dk.voice[v], out, n);
}


/* High scores: MAME's hiscore.dat, as GalaginoPlus uses it. */
static const gal_hiscore_region_t dk_hiscore[] = {
    {0x6100, 0xaa, 0x94, 0x76},
    {0x60b8, 0x03, 0x50, 0x00},
    {0x7641, 0x01, 0x00, 0x00},
    {0x7621, 0x01, 0x00, 0x00},
    {0x7601, 0x01, 0x07, 0x07},
    {0x75e1, 0x01, 0x06, 0x06},
    {0x75c1, 0x01, 0x05, 0x05},
    {0x75a1, 0x01, 0x00, 0x00},
};

const gal_game_ops_t gal_dkong_ops = {
    .init = dk_init,
    .reset = dk_reset,
    .run_slice = dk_run_slice,
    .prepare_frame = dk_prepare_frame,
    .render_band = dk_render_band,
    .sound = dk_sound,
    .view_top = 16,
    .view_h = 256,
    .hiscore = dk_hiscore,
    .nhiscore = sizeof(dk_hiscore) / sizeof(dk_hiscore[0]),
};

static const gal_hiscore_region_t jr_hiscore[] = {
    {0x6105, 0x9e, 0x10, 0x10}, {0x60b8, 0x03, 0x00, 0x01}, {0x7661, 0x01, 0x10, 0x10},
    {0x7641, 0x01, 0x00, 0x00}, {0x7621, 0x01, 0x01, 0x01}, {0x7601, 0x01, 0x01, 0x01},
    {0x75e1, 0x01, 0x08, 0x08}, {0x75c1, 0x01, 0x00, 0x00}, {0x75a1, 0x01, 0x00, 0x00},
};

const gal_game_ops_t gal_dkongjr_ops = {
    .init = jr_init,
    .reset = dk_reset,
    .run_slice = dk_run_slice,
    .prepare_frame = dk_prepare_frame,
    .render_band = dk_render_band,
    .sound = dk_sound,
    .view_top = 16,
    .view_h = 256,
    .hiscore = jr_hiscore,
    .nhiscore = sizeof(jr_hiscore) / sizeof(jr_hiscore[0]),
};

static const gal_hiscore_region_t dk3_hiscore[] = {
    {0x6b00, 0xaa, 0xf3, 0x76},
    {0x6c20, 0x40, 0x00, 0x00},
    {0x6c16, 0x04, 0x00, 0x00},
    {0x68f3, 0x03, 0x01, 0x00},
};

const gal_game_ops_t gal_dkong3_ops = {
    .init = dk3_init,
    .reset = dk_reset,
    .run_slice = dk_run_slice,
    .prepare_frame = dk_prepare_frame,
    .render_band = dk_render_band,
    .sound = dk_sound,
    .view_top = 16,
    .view_h = 256,
    .large = 1,
    .nderived = 2,
    .derived_src = {3, 4},
    .derived_size = {512 * 8 * 2, 256 * 16 * 4},
    .derive = dk3_derive,
    .hiscore = dk3_hiscore,
    .nhiscore = sizeof(dk3_hiscore) / sizeof(dk3_hiscore[0]),
};

#ifdef GAL_CONV_CHECK
int gal_tables_dkong(gal_table_t *t)
{
    int n = 0;
    t[n++] = (gal_table_t){"dkong_rom_cpu1", dk.rom, 0x4000};
    t[n++] = (gal_table_t){"dkong_rom_cpu2", dkong_rom_cpu2, 0x1000};
    t[n++] = (gal_table_t){"v_5h_b_bin", dk.tiles, 256 * 8 * 2};
    t[n++] = (gal_table_t){"dkong_sprites", dk.sprites, 128 * 16 * 4};
    t[n++] = (gal_table_t){"dkong_sprites_x", dk.sprites_x, 128 * 16 * 4};
    t[n++] = (gal_table_t){"dkong_colormap", dk.cmap, 4 * 256 * 4 * 2};
    t[n++] = (gal_table_t){"dkong_colormap_sprite", dk.cmap_sprite, sizeof(dk.cmap_sprite)};
    return n;
}
#endif
