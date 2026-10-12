/*
 * The Pac-Man hardware: Pac-Man (Midway, 1980), from upstream Galagino's
 * pacman.h, and the games of GalaginoPlus on the same board, after MAME's
 * pacman.cpp and pengo.cpp: Ms. Pac-Man, Crush Roller, Eyes, Mr. TNT, Lizard
 * Wizard, The Glob, Ali Baba and 40 Thieves, and Sega's Pengo.
 *
 * One Z80 at 3.072 MHz, 50688 cycles per 60.6 Hz frame. Galagino's RAM buffer
 * holds 4000-4fff (video, colour, work and sprite RAM) at 0000-0fff and the
 * sprite coordinates written to 5060-506f at 1060-106f. A15 and A13 are not
 * decoded for RAM and I/O, so 4000-4fff also appears at 6000, c000 and e000.
 *
 * What the games add:
 *   Ms. Pac-Man   the Pac-Man ROMs, and a daughterboard with three encrypted
 *                 ROMs that is switched in and out by reading or writing a few
 *                 trap addresses (MAME's mspacman_map, init_mspacman)
 *   Crush Roller  a protection device at 5004 and 5080-50ff (maketrax)
 *   Eyes, Mr. TNT data lines swapped on the program and graphics ROMs
 *   Lizard Wizard 8 KB more program at 8000
 *   The Glob      an EPOS daughterboard: four decryptions of the program, one
 *                 of which the CPU sees, chosen by a counter that IN steps
 *   Ali Baba      more program at 8000 and a000, more RAM at 9000, the sprite
 *                 RAM and the sound registers elsewhere, its own latches, a
 *                 "mystery" clock
 *   Van-Van Car   the picture upside down (MAME ROT270), RAM at 4800, two
 *                 SN76496 on OUT ports 1 and 2 instead of the WSG, NMI
 *   Pengo         32 KB of program, encrypted (Sega 315-5010: opcodes and data
 *                 differently), RAM at 8000 and I/O at 9000; two banks of
 *                 graphics, of palettes and of colour lookup
 *
 * The picture is MAME's: an opaque tile layer, sprites clipped to the rows
 * 16..271 of the upright picture (Ali Baba: 0..239), each also drawn 256 rows
 * up (the tunnel of Crush Roller), the first three a pixel to the left (not
 * in Pengo); colours from five bits of the attribute and the two bank bits.
 */
#include "gal_internal.h"

#include "dip_switches.h"

#define CYCLES_PER_FRAME 50688

typedef enum
{
    PM_PACMAN,
    PM_MSPACMAN,
    PM_CRUSH,
    PM_EYES,
    PM_MRTNT,
    PM_LIZWIZ,
    PM_THEGLOB,
    PM_ALIBABA,
    PM_PENGO,
    PM_VANVAN,
} pm_var_t;

static struct
{
    pm_var_t var;
    uint8_t *rom; /* the program, the first 16 KB as the CPU sees it at 0000 */
    uint16_t *tiles;    /* [banks][256][8] */
    uint32_t *sprites;  /* [banks][64][16] */
    uint16_t *colormap; /* [128][4]: palette bank, lookup bank, 5 bits */
    int8_t *waves;      /* [16][32] */
    uint8_t irq_enable;
    uint8_t irq_vector;
    uint8_t dsw1, dsw2;
    uint8_t gfx_bank, palette_bank, lookup_bank; /* Pengo */
    gal_sn_t sn[2];                              /* Van-Van Car */
    uint16_t spr_code, spr_pos; /* sprite RAM in gal.ram: codes, coordinates */

    /* Ms. Pac-Man: the decrypted daughterboard program at 0000-3fff and
     * 8000-97ff, and which of the two the CPU sees */
    uint8_t *ms_low, *ms_high;
    uint8_t ms_decode;
    /* The Glob: the four decryptions, and the counter that picks one */
    uint8_t *glob[4];
    uint8_t glob_counter;
    /* Crush Roller */
    uint8_t mk_counter, mk_offset, mk_disable;
    /* Ali Baba */
    uint8_t mystery_control, mystery_clock, mystery_prescaler;
} pm;

/* The page of 4800-4bff: nothing answers there, and the bus reads bf (MAME's
 * pacman_read_nop; Ms. Pac-Man reads it). Writes go nowhere. */
static uint8_t open_bus[256];

static uint8_t bitswap8(uint8_t v, int b7, int b6, int b5, int b4, int b3, int b2, int b1, int b0)
{
    return (uint8_t)((((v >> b7) & 1) << 7) | (((v >> b6) & 1) << 6) | (((v >> b5) & 1) << 5) |
                     (((v >> b4) & 1) << 4) | (((v >> b3) & 1) << 3) | (((v >> b2) & 1) << 2) |
                     (((v >> b1) & 1) << 1) | ((v >> b0) & 1));
}

/* --- Ms. Pac-Man ------------------------------------------------------------- */

/* any access to these addresses switches the daughterboard in or out */
static int ms_trap(uint16_t a)
{
    if (a >= 0x3ff8 && a <= 0x3fff)
        return 1; /* in */
    if ((a >= 0x0038 && a <= 0x003f) || (a >= 0x03b0 && a <= 0x03b7) || (a >= 0x1600 && a <= 0x1607) ||
        (a >= 0x2120 && a <= 0x2127) || (a >= 0x3ff0 && a <= 0x3ff7) || (a >= 0x8000 && a <= 0x8007) ||
        (a >= 0x97f0 && a <= 0x97f7))
        return -1; /* out */
    return 0;
}

static int ms_trap_page(int p)
{
    return p == 0x00 || p == 0x03 || p == 0x16 || p == 0x21 || p == 0x3f || p == 0x80 || p == 0x97;
}

/* ROM at 0000-3fff and 8000-bfff for the current state; the pages with trap
 * addresses go through the callbacks */
static void ms_map(void)
{
    z80_t *cpu = &gal.cpu[0];
    for (int p = 0; p < 0x40; p++)
    {
        const uint8_t *lo = pm.ms_decode ? pm.ms_low + p * 256 : pm.rom + p * 256;
        /* Pac-Man mode: Pac-Man, mirrored. Ms. Pac-Man: 8000-97ff u5 and u6,
         * 9800-9fff the upper half of 6f, a000-bfff 6h and 6j, as Pac-Man */
        const uint8_t *hi = (pm.ms_decode && p < 0x18) ? pm.ms_high + p * 256 : pm.rom + p * 256;
        cpu->rd_page[p] = ms_trap_page(p) ? NULL : lo;
        cpu->rd_page[0x80 + p] = ms_trap_page(0x80 + p) ? NULL : hi;
    }
}

static uint8_t ms_rom_read(uint16_t a)
{
    const int t = ms_trap(a);
    if (t)
    {
        const uint8_t decode = t > 0;
        if (decode != pm.ms_decode)
        {
            pm.ms_decode = decode;
            ms_map();
        }
    }
    const int p = a >> 8;
    if (p < 0x40)
        return pm.ms_decode ? pm.ms_low[a] : pm.rom[a];
    if (!pm.ms_decode)
        return pm.rom[a & 0x3fff];
    a &= 0x3fff;
    return a < 0x1800 ? pm.ms_high[a] : pm.rom[a];
}

/* MAME's init_mspacman: the daughterboard's program decrypted, and forty
 * 8-byte patches of it copied into the Pac-Man code */
static void ms_decrypt(const uint8_t *pac, const uint8_t *u, uint8_t *low, uint8_t *high)
{
    const uint8_t *u5 = u, *u6 = u + 0x800, *u7 = u + 0x1800;
    memcpy(low, pac, 0x3000);
    for (int i = 0; i < 0x1000; i++)
    {
        /* bitswap<12>(i,11,3,7,9,10,8,6,5,4,2,1,0) */
        const int a = (((i >> 11) & 1) << 11) | (((i >> 3) & 1) << 10) | (((i >> 7) & 1) << 9) |
                      (((i >> 9) & 1) << 8) | (((i >> 10) & 1) << 7) | (((i >> 8) & 1) << 6) |
                      (((i >> 6) & 1) << 5) | (((i >> 5) & 1) << 4) | (((i >> 4) & 1) << 3) |
                      (((i >> 2) & 1) << 2) | (((i >> 1) & 1) << 1) | (i & 1);
        low[0x3000 + i] = bitswap8(u7[a], 0, 4, 5, 7, 6, 3, 2, 1);
    }
    for (int i = 0; i < 0x800; i++)
    {
        /* bitswap<11>(i,8,7,5,9,10,6,3,4,2,1,0) */
        const int a5 = (((i >> 8) & 1) << 10) | (((i >> 7) & 1) << 9) | (((i >> 5) & 1) << 8) |
                       (((i >> 9) & 1) << 7) | (((i >> 10) & 1) << 6) | (((i >> 6) & 1) << 5) |
                       (((i >> 3) & 1) << 4) | (((i >> 4) & 1) << 3) | (((i >> 2) & 1) << 2) |
                       (((i >> 1) & 1) << 1) | (i & 1);
        /* bitswap<11>(i,3,7,9,10,8,6,5,4,2,1,0) */
        const int a6 = (((i >> 3) & 1) << 10) | (((i >> 7) & 1) << 9) | (((i >> 9) & 1) << 8) |
                       (((i >> 10) & 1) << 7) | (((i >> 8) & 1) << 6) | (((i >> 6) & 1) << 5) |
                       (((i >> 5) & 1) << 4) | (((i >> 4) & 1) << 3) | (((i >> 2) & 1) << 2) |
                       (((i >> 1) & 1) << 1) | (i & 1);
        high[0x0000 + i] = bitswap8(u5[a5], 0, 4, 5, 7, 6, 3, 2, 1);        /* 8000 */
        high[0x0800 + i] = bitswap8(u6[0x800 + a6], 0, 4, 5, 7, 6, 3, 2, 1); /* 8800: u6, upper half */
        high[0x1000 + i] = bitswap8(u6[a6], 0, 4, 5, 7, 6, 3, 2, 1);         /* 9000: u6, lower half */
    }

    /* mspacman_install_patches: (destination, source in 8000-81ff) */
    static const uint16_t patch[40][2] = {
        {0x0410, 0x8008}, {0x08e0, 0x81d8}, {0x0a30, 0x8118}, {0x0bd0, 0x80d8}, {0x0c20, 0x8120},
        {0x0e58, 0x8168}, {0x0ea8, 0x8198}, {0x1000, 0x8020}, {0x1008, 0x8010}, {0x1288, 0x8098},
        {0x1348, 0x8048}, {0x1688, 0x8088}, {0x16b0, 0x8188}, {0x16d8, 0x80c8}, {0x16f8, 0x81c8},
        {0x19a8, 0x80a8}, {0x19b8, 0x81a8}, {0x2060, 0x8148}, {0x2108, 0x8018}, {0x21a0, 0x81a0},
        {0x2298, 0x80a0}, {0x23e0, 0x80e8}, {0x2418, 0x8000}, {0x2448, 0x8058}, {0x2470, 0x8140},
        {0x2488, 0x8080}, {0x24b0, 0x8180}, {0x24d8, 0x80c0}, {0x24f8, 0x81c0}, {0x2748, 0x8050},
        {0x2780, 0x8090}, {0x27b8, 0x8190}, {0x2800, 0x8028}, {0x2b20, 0x8100}, {0x2b30, 0x8110},
        {0x2bf0, 0x81d0}, {0x2cc0, 0x80d0}, {0x2cd8, 0x80e0}, {0x2cf0, 0x81e0}, {0x2d60, 0x8160},
    };
    for (int k = 0; k < 40; k++)
        memcpy(low + patch[k][0], high + (patch[k][1] - 0x8000), 8);
}

/* --- The Glob ------------------------------------------------------------------ */

/* epos_decrypt_rom: the decryption for counter 8 + k */
static uint8_t glob_decrypt(int k, uint8_t v)
{
    static const uint8_t invert[4] = {0xfc, 0xf6, 0x7d, 0x77};
    static const uint8_t bs[4][8] = {
        {3, 7, 0, 6, 4, 1, 2, 5},
        {1, 7, 0, 3, 4, 6, 2, 5},
        {3, 0, 4, 6, 7, 1, 2, 5},
        {1, 0, 4, 3, 7, 6, 2, 5},
    };
    const uint8_t *b = bs[k];
    return bitswap8(v ^ invert[k], b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
}

static void glob_map(void)
{
    z80_t *cpu = &gal.cpu[0];
    const uint8_t *bank = pm.glob[pm.glob_counter & 3];
    for (int p = 0; p < 0x40; p++)
        cpu->rd_page[p] = cpu->rd_page[0x80 + p] = bank + p * 256;
}

/* --- Inputs -------------------------------------------------------------------- */

static uint8_t pengo_in0(void)
{
    const uint16_t keys = gal.buttons;
    uint8_t v = 0xff;
    if (keys & GAL_BTN_UP)
        v &= ~0x01;
    if (keys & GAL_BTN_DOWN)
        v &= ~0x02;
    if (keys & GAL_BTN_LEFT)
        v &= ~0x04;
    if (keys & GAL_BTN_RIGHT)
        v &= ~0x08;
    if (keys & GAL_BTN_COIN)
        v &= ~0x10;
    if (keys & GAL_BTN_FIRE)
        v &= ~0x80;
    return v;
}

static uint8_t pm_in0(void)
{
    const uint16_t keys = gal.buttons;
    /* bit 4: Pac-Man's rack test DIP (off), Crush Roller's cabinet (upright) */
    uint8_t v = (pm.var == PM_CRUSH) ? 0xef : 0xff;
    if (keys & GAL_BTN_UP)
        v &= ~0x01;
    if (keys & GAL_BTN_LEFT)
        v &= ~0x02;
    if (keys & GAL_BTN_RIGHT)
        v &= ~0x04;
    if (keys & GAL_BTN_DOWN)
        v &= ~0x08;
    if (keys & GAL_BTN_COIN)
        v &= ~0x20;
    if (pm.var == PM_ALIBABA && (keys & GAL_BTN_FIRE))
        v &= ~0x40;
    if (pm.var == PM_VANVAN && (keys & GAL_BTN_FIRE))
        v &= ~0x10;
    return v;
}

static uint8_t pm_in1(void)
{
    const uint16_t keys = gal.buttons;
    uint8_t v = 0xff; /* 0xef for service; The Glob, Ali Baba: bit 7 upright */
    if (keys & GAL_BTN_START)
        v &= ~0x20;
    if (keys & GAL_BTN_START2)
        v &= ~0x40;
    switch (pm.var)
    {
    case PM_EYES:
    case PM_MRTNT:
    case PM_LIZWIZ:
        if (keys & GAL_BTN_FIRE)
            v &= ~0x10;
        break;
    case PM_THEGLOB:
        /* button 1 (with start 1) calls the elevator, button 2 acts */
        if (keys & GAL_BTN_EXTRA)
            v &= ~0x20;
        if (keys & GAL_BTN_FIRE)
            v &= ~0x40;
        break;
    default:
        break;
    }
    return v;
}

/* --- Crush Roller's protection (MAME's maketrax_*) --------------------------- */

static void mk_protection_w(uint8_t data)
{
    if (data == 0)
    {
        pm.mk_counter = pm.mk_offset = 0;
        pm.mk_disable = 1;
    }
    else if (data == 1)
    {
        pm.mk_disable = 0;
        if (++pm.mk_counter == 0x3c)
        {
            pm.mk_counter = 0;
            if (++pm.mk_offset == 0x1e)
                pm.mk_offset = 0;
        }
    }
}

static uint8_t mk_port2_r(int offset)
{
    static const uint8_t prot[0x1e] = {0x00, 0xc0, 0x00, 0x40, 0xc0, 0x40, 0x00, 0xc0, 0x00, 0x40,
                                       0x00, 0xc0, 0x00, 0x40, 0xc0, 0x40, 0x00, 0xc0, 0x00, 0x40,
                                       0x00, 0xc0, 0x00, 0x40, 0xc0, 0x40, 0x00, 0xc0, 0x00, 0x40};
    uint8_t data = pm.dsw1 & 0x3f;
    if (!pm.mk_disable)
        return prot[pm.mk_offset] | data;
    switch (offset)
    {
    case 0x01:
    case 0x04:
        return data | 0x40;
    case 0x05:
    case 0x0e:
    case 0x10:
        return data | 0xc0;
    default:
        return data;
    }
}

static uint8_t mk_port3_r(int offset)
{
    static const uint8_t prot[0x1e] = {0x1f, 0x3f, 0x2f, 0x2f, 0x0f, 0x0f, 0x0f, 0x3f, 0x0f, 0x0f,
                                       0x1c, 0x3c, 0x2c, 0x2c, 0x0c, 0x0c, 0x0c, 0x3c, 0x0c, 0x0c,
                                       0x11, 0x31, 0x21, 0x21, 0x01, 0x01, 0x01, 0x31, 0x01, 0x01};
    if (!pm.mk_disable)
        return prot[pm.mk_offset];
    switch (offset)
    {
    case 0x00:
        return 0x1f;
    case 0x09:
        return 0x30;
    case 0x0c:
        return 0x00;
    default:
        return 0x20;
    }
}

/* --- Memory map ------------------------------------------------------------ */

static uint8_t GAL_HOT(pm_read)(z80_t *cpu, uint16_t addr)
{
    (void)cpu;
    if (pm.var == PM_MSPACMAN && (addr & 0x4000) == 0)
        return ms_rom_read(addr);
    if ((addr & 0x5000) != 0x5000)
        return 0xff; /* unpopulated ROM sockets */
    addr = (addr & 0x0fff) | 0x5000;
    if ((addr & 0xff00) != 0x5000)
        return 0xff;
    if (pm.var == PM_CRUSH && addr >= 0x5080)
        return addr < 0x50c0 ? mk_port2_r(addr - 0x5080) : mk_port3_r(addr - 0x50c0);
    if (pm.var == PM_ALIBABA && addr >= 0x50c0)
    {
        if (addr == 0x50c0)
        {
            /* what the mystery item is */
            static uint32_t rng = 0x12345678;
            rng = rng * 1664525u + 1013904223u;
            return (rng >> 24) & 0x0f;
        }
        if (addr == 0x50c1)
            return pm.mystery_clock >= 24; /* the mystery is lit */
        return 0xbf;
    }
    switch (addr & 0xc0)
    {
    case 0x00:
        return pm_in0();
    case 0x40:
        return pm_in1();
    case 0x80:
        return pm.dsw1;
    default:
        return pm.dsw2;
    }
}

static void GAL_HOT(pm_write)(z80_t *cpu, uint16_t addr, uint8_t value)
{
    if (pm.var == PM_MSPACMAN && (addr & 0x4000) == 0)
    {
        const int t = ms_trap(addr);
        if (t && (t > 0) != pm.ms_decode)
        {
            pm.ms_decode = t > 0;
            ms_map();
        }
        return;
    }
    if ((addr & 0x5000) != 0x5000)
        return;
    addr = (addr & 0x0fff) | 0x5000;
    if ((addr & 0xff00) != 0x5000)
        return;

    if (pm.var == PM_ALIBABA)
    {
        if (addr >= 0x5050 && addr <= 0x505f)
            gal.ram[pm.spr_pos + (addr - 0x5050)] = value; /* sprite coordinates */
        else if (addr >= 0x5040 && addr <= 0x506f)
        {
            /* the sound registers are not contiguous */
            const int ofs = addr - 0x5040;
            gal.soundregs[((ofs >> 1) & 0x10) | (ofs & 0x0f)] = value & 0x0f;
        }
        else if (addr == 0x5080)
        {
            /* d0 starts and stops the mystery clock, d1 shows it */
            if ((pm.mystery_control ^ value) & 1)
                pm.mystery_prescaler = pm.mystery_clock = 0;
            pm.mystery_control = value;
        }
        else if (addr == 0x50c2) /* latch 2, Q2: interrupt enable */
        {
            pm.irq_enable = value & 1;
            if (!pm.irq_enable)
                z80_set_irq_line(cpu, 0);
        }
        return;
    }

    if ((addr & 0xfff0) == 0x5060) /* sprite coordinates */
        gal.ram[addr - 0x4000] = value;
    else if (addr == 0x5000)
    {
        pm.irq_enable = value & 1;
        if (!pm.irq_enable)
            z80_set_irq_line(cpu, 0);
    }
    else if (addr == 0x5004 && pm.var == PM_CRUSH)
        mk_protection_w(value);
    else if ((addr & 0xffe0) == 0x5040)
        gal.soundregs[addr - 0x5040] = value & 0x0f;
}

/* Pengo: RAM at 8000-8fff, I/O at 9000-90ff */
static uint8_t GAL_HOT(pengo_read)(z80_t *cpu, uint16_t addr)
{
    (void)cpu;
    if ((addr & 0xff00) != 0x9000)
        return 0xff;
    switch (addr & 0xc0)
    {
    case 0x00:
        return pm.dsw2; /* coinage */
    case 0x40:
        return pm.dsw1;
    case 0x80:
        return pm_in1();
    default:
        return pengo_in0();
    }
}

static void GAL_HOT(pengo_write)(z80_t *cpu, uint16_t addr, uint8_t value)
{
    if ((addr & 0xff00) != 0x9000)
        return;
    if (addr < 0x9020)
        gal.soundregs[addr & 0x1f] = value & 0x0f;
    else if (addr < 0x9030)
        gal.ram[0x1060 + (addr & 0x0f)] = value; /* sprite coordinates */
    else if (addr >= 0x9040 && addr <= 0x9047)
    {
        const int bit = value & 1;
        switch (addr & 7)
        {
        case 0:
            pm.irq_enable = bit;
            if (!bit)
                z80_set_irq_line(cpu, 0);
            break;
        case 2:
            pm.palette_bank = bit;
            break;
        case 6:
            pm.lookup_bank = bit;
            break;
        case 7:
            pm.gfx_bank = bit;
            break;
        default: /* sound enable, flip screen, coin counters */
            break;
        }
    }
}

static void pm_out(z80_t *cpu, uint16_t port, uint8_t value)
{
    if (pm.var == PM_VANVAN)
    {
        /* the two SN76496 */
        if ((port & 0xff) == 1 || (port & 0xff) == 2)
            gal_sn_write(&pm.sn[(port & 0xff) - 1], value);
        return;
    }
    /* the interrupt vector; writing it acknowledges a pending interrupt */
    pm.irq_vector = value;
    z80_set_irq_line(cpu, 0);
}

static uint8_t pm_in(z80_t *cpu, uint16_t port)
{
    (void)cpu;
    if (pm.var != PM_THEGLOB)
        return 0;
    /* The Glob: an odd port counts down, an even one up; the decryption is
     * that of counter 8..b (MAME's epos_decryption_w) */
    pm.glob_counter = (pm.glob_counter + ((port & 1) ? -1 : 1)) & 0x0f;
    if (pm.glob_counter >= 0x08 && pm.glob_counter <= 0x0b)
        glob_map();
    return 0;
}

/* --- Life cycle ---------------------------------------------------------------- */

/* MAME's eyes_decode: in every 8 bytes of graphics, address lines A0 and A2
 * and data lines D4 and D6 are swapped */
static void eyes_decode_gfx(uint8_t *data, int len)
{
    for (int i = 0; i < len; i += 8)
    {
        uint8_t s[8];
        for (int j = 0; j < 8; j++)
            s[j] = data[i + ((j & 2) | ((j >> 2) & 1) | ((j & 1) << 2))];
        for (int j = 0; j < 8; j++)
            data[i + j] = bitswap8(s[j], 7, 4, 5, 6, 3, 2, 1, 0);
    }
}

/* The parts all the games share: the region indices of the characters,
 * sprites, palette, colour lookup and sound waveforms; the program at 0000-3fff
 * (mirrored at 8000), RAM at 4000-4fff (mirrored at 6000, c000 and e000). */
static int pm_common_init(uint8_t *const *regions, int r_chars, int r_sprites, int r_pal, int r_lookup, int r_waves)
{
    const int banks = (pm.var == PM_PENGO) ? 2 : 1;
    pm.tiles = (uint16_t *)gal_alloc(banks * 256 * 8 * 2);
    pm.sprites = (uint32_t *)gal_alloc(banks * 64 * 16 * 4);
    pm.colormap = (uint16_t *)gal_alloc(128 * 4 * 2);
    pm.waves = (int8_t *)gal_alloc(16 * 32);
    if (!pm.tiles || !pm.sprites || !pm.colormap || !pm.waves)
        return 0;

    if (pm.var == PM_EYES || pm.var == PM_MRTNT)
    {
        eyes_decode_gfx(regions[r_chars], 0x1000);
        eyes_decode_gfx(regions[r_sprites], 0x1000);
    }
    if (pm.var == PM_PENGO)
    {
        /* one ROM per bank: its characters, then its sprites */
        for (int k = 0; k < 2; k++)
        {
            gal_conv_chars_namco(regions[r_chars] + 0x2000 * k, 256, pm.tiles + 256 * 8 * k);
            gal_conv_sprites_namco(regions[r_chars] + 0x2000 * k + 0x1000, 64, 1, pm.sprites + 64 * 16 * k, NULL);
        }
    }
    else
    {
        gal_conv_chars_namco(regions[r_chars], 256, pm.tiles);
        gal_conv_sprites_namco(regions[r_sprites], 64, 1, pm.sprites, NULL);
    }

    /* MAME's pacman_palette: 64 groups of four lookup entries of the first 16
     * palette colours, and again of the second 16 (Pengo's palette bank) */
    uint16_t palette[32];
    for (int i = 0; i < 32; i++)
        palette[i] = gal_color(gal_prom_bgr233(regions[r_pal][i]));
    for (int i = 0; i < 64 * 4; i++)
    {
        pm.colormap[i] = palette[regions[r_lookup][i] & 0x0f];
        pm.colormap[64 * 4 + i] = palette[0x10 | (regions[r_lookup][i] & 0x0f)];
    }

    if (r_waves >= 0)
        gal_conv_waves(regions[r_waves], 16, pm.waves);

    for (int i = 0; i < 256; i++)
        open_bus[i] = 0xbf;

    z80_t *cpu = &gal.cpu[0];
    for (int p = 0; p < 0x40; p++)
        cpu->rd_page[p] = cpu->rd_page[p | 0x80] = pm.rom + p * 256;
    for (int m = 0; m < 4; m++)
    {
        /* 4000, 6000, c000, e000 */
        const int base = 0x40 + (m & 1) * 0x20 + (m >> 1) * 0x80;
        for (int p = 0; p < 0x10; p++)
        {
            const int empty = (p >= 0x08 && p < 0x0c);
            cpu->rd_page[base + p] = empty ? open_bus : gal.ram + p * 256;
            cpu->wr_page[base + p] = empty ? gal_ignored : gal.ram + p * 256;
        }
    }
    cpu->read = pm_read;
    cpu->write = pm_write;
    cpu->in = pm_in;
    cpu->out = pm_out;
    pm.spr_code = 0x0ff0;
    pm.spr_pos = 0x1060;
    pm.dsw2 = 0xff;
    return 1;
}

static int pm_init(uint8_t *const *regions)
{
    pm.var = PM_PACMAN;
    pm.rom = regions[0];
    pm.dsw1 = PACMAN_DIP;
    return pm_common_init(regions, 1, 2, 3, 4, 5);
}

static int ms_init(uint8_t *const *regions)
{
    pm.var = PM_MSPACMAN;
    pm.rom = regions[0];
    pm.dsw1 = PACMAN_DIP;
    pm.ms_low = (uint8_t *)gal_alloc(0x4000);
    pm.ms_high = (uint8_t *)gal_alloc(0x1800);
    if (!pm.ms_low || !pm.ms_high || !pm_common_init(regions, 2, 3, 4, 5, 6))
        return 0;
    ms_decrypt(pm.rom, regions[1], pm.ms_low, pm.ms_high);
    return 1;
}

static int crush_init(uint8_t *const *regions)
{
    pm.var = PM_CRUSH;
    pm.rom = regions[0];
    pm.dsw1 = CRUSH_DIP;
    return pm_common_init(regions, 1, 2, 3, 4, 5);
}

static int eyes_init_var(uint8_t *const *regions, pm_var_t var, uint8_t dsw1)
{
    pm.var = var;
    pm.rom = regions[0];
    pm.dsw1 = dsw1;
    /* data lines D3 and D5 of the program are swapped */
    for (int i = 0; i < 0x4000; i++)
        pm.rom[i] = bitswap8(pm.rom[i], 7, 6, 3, 4, 5, 2, 1, 0);
    return pm_common_init(regions, 1, 2, 3, 4, 5);
}

static int eyes_init(uint8_t *const *regions)
{
    return eyes_init_var(regions, PM_EYES, EYES_DIP);
}

static int mrtnt_init(uint8_t *const *regions)
{
    return eyes_init_var(regions, PM_MRTNT, MRTNT_DIP);
}

static int lizwiz_init(uint8_t *const *regions)
{
    pm.var = PM_LIZWIZ;
    pm.rom = regions[0];
    pm.dsw1 = LIZWIZ_DIP;
    if (!pm_common_init(regions, 1, 2, 3, 4, 5))
        return 0;
    /* 8000-9fff: wiza, wizb; A15 is decoded, nothing at a000-bfff */
    z80_t *cpu = &gal.cpu[0];
    for (int p = 0; p < 0x40; p++)
        cpu->rd_page[0x80 + p] = (p < 0x20) ? pm.rom + 0x4000 + p * 256 : NULL;
    return 1;
}

static int glob_init(uint8_t *const *regions)
{
    pm.var = PM_THEGLOB;
    pm.dsw1 = THEGLOB_DIP;
    /* The decryptions: the program region becomes the first, the others are
     * made from it through the inverse of the first */
    uint8_t inv0[256];
    for (int v = 0; v < 256; v++)
        inv0[glob_decrypt(0, (uint8_t)v)] = (uint8_t)v;
    pm.glob[0] = regions[0];
    for (int i = 0; i < 0x4000; i++)
        pm.glob[0][i] = glob_decrypt(0, pm.glob[0][i]);
    for (int k = 1; k < 4; k++)
    {
        if (!(pm.glob[k] = (uint8_t *)gal_alloc(0x4000)))
            return 0;
        for (int i = 0; i < 0x4000; i++)
            pm.glob[k][i] = glob_decrypt(k, inv0[pm.glob[0][i]]);
    }
    pm.rom = pm.glob[0];
    return pm_common_init(regions, 1, 2, 3, 4, 5);
}

static int alibaba_init(uint8_t *const *regions)
{
    pm.var = PM_ALIBABA;
    pm.rom = regions[0];
    pm.dsw1 = ALIBABA_DIP;
    if (!pm_common_init(regions, 2, 3, 4, 5, 6))
        return 0;
    z80_t *cpu = &gal.cpu[0];
    /* A15 is decoded: 8000-8fff 6l, 9000-93ff RAM (mirrored to 9fff), a000-a7ff
     * 6m (mirrored to bfff) */
    for (int p = 0; p < 0x40; p++)
    {
        const uint8_t *page = NULL;
        uint8_t *ram = NULL;
        if (p < 0x10)
            page = pm.rom + 0x4000 + p * 256;
        else if (p < 0x20)
            page = ram = gal.ram + 0x1400 + (p & 3) * 256;
        else
            page = pm.rom + 0x5000 + (p & 7) * 256;
        cpu->rd_page[0x80 + p] = page;
        cpu->wr_page[0x80 + p] = ram;
    }
    pm.spr_code = 0x0ef0;
    pm.spr_pos = 0x1050;
    return 1;
}

/* MAME's sega_315_5010 decrypt: bits 7, 5 and 3 of every byte, by the address
 * bits 12, 8, 4 and 0 and the byte's bits 7, 5 and 3, differently for opcodes
 * and data */
static void sega_315_5010_decrypt(uint8_t *data, uint8_t *opcodes, int size)
{
    static const uint8_t convtable[32][4] = {
        /* opcode                   data                     address bits 12, 8, 4, 0 */
        {0xa0, 0x80, 0xa8, 0x88}, {0x28, 0xa8, 0x08, 0x88}, /* 0000 */
        {0x28, 0xa8, 0x08, 0x88}, {0xa0, 0x80, 0xa8, 0x88}, /* 0001 */
        {0xa0, 0x80, 0x20, 0x00}, {0xa0, 0x80, 0x20, 0x00}, /* 0010 */
        {0x08, 0x28, 0x88, 0xa8}, {0xa0, 0x80, 0xa8, 0x88}, /* 0011 */
        {0x08, 0x00, 0x88, 0x80}, {0x28, 0xa8, 0x08, 0x88}, /* 0100 */
        {0xa0, 0x80, 0x20, 0x00}, {0x08, 0x00, 0x88, 0x80}, /* 0101 */
        {0xa0, 0x80, 0x20, 0x00}, {0xa0, 0x80, 0x20, 0x00}, /* 0110 */
        {0xa0, 0x80, 0x20, 0x00}, {0x00, 0x08, 0x20, 0x28}, /* 0111 */
        {0x88, 0x80, 0x08, 0x00}, {0xa0, 0x80, 0x20, 0x00}, /* 1000 */
        {0x88, 0x80, 0x08, 0x00}, {0x00, 0x08, 0x20, 0x28}, /* 1001 */
        {0x08, 0x28, 0x88, 0xa8}, {0x08, 0x28, 0x88, 0xa8}, /* 1010 */
        {0xa0, 0x80, 0xa8, 0x88}, {0xa0, 0x80, 0x20, 0x00}, /* 1011 */
        {0x08, 0x00, 0x88, 0x80}, {0x88, 0x80, 0x08, 0x00}, /* 1100 */
        {0x00, 0x08, 0x20, 0x28}, {0x88, 0x80, 0x08, 0x00}, /* 1101 */
        {0x08, 0x28, 0x88, 0xa8}, {0x08, 0x28, 0x88, 0xa8}, /* 1110 */
        {0x08, 0x00, 0x88, 0x80}, {0xa0, 0x80, 0x20, 0x00}, /* 1111 */
    };
    for (int a = 0; a < size; a++)
    {
        const uint8_t src = data[a];
        const int row = (a & 1) | ((a >> 4) & 1) << 1 | ((a >> 8) & 1) << 2 | ((a >> 12) & 1) << 3;
        int col = ((src >> 3) & 1) | ((src >> 5) & 1) << 1;
        uint8_t x = 0;
        if (src & 0x80)
        {
            col = 3 - col;
            x = 0xa8;
        }
        opcodes[a] = (src & ~0xa8) | (convtable[2 * row][col] ^ x);
        data[a] = (src & ~0xa8) | (convtable[2 * row + 1][col] ^ x);
    }
}

static int pengo_init(uint8_t *const *regions)
{
    pm.var = PM_PENGO;
    pm.rom = regions[0];
    pm.dsw1 = PENGO_DSW1;
    pm.dsw2 = PENGO_DSW2;
    uint8_t *opcodes = (uint8_t *)gal_alloc(0x8000);
    /* the opcode page table: the second CPU's read table, which Pengo does
     * not use (the arena has no kilobyte to spare) */
    const uint8_t **op_page = gal.cpu[1].rd_page;
    if (!opcodes || !pm_common_init(regions, 1, 1, 2, 3, 4))
        return 0;
    sega_315_5010_decrypt(pm.rom, opcodes, 0x8000);

    /* 0000-7fff program; 8000-8fff RAM, opcodes run from 8800-8fff as well */
    z80_t *cpu = &gal.cpu[0];
    for (int p = 0; p < 256; p++)
        cpu->rd_page[p] = cpu->wr_page[p] = NULL;
    memset(op_page, 0, 256 * sizeof(uint8_t *));
    for (int p = 0; p < 0x80; p++)
    {
        cpu->rd_page[p] = pm.rom + p * 256;
        op_page[p] = opcodes + p * 256;
    }
    for (int p = 0; p < 0x10; p++)
        cpu->rd_page[0x80 + p] = cpu->wr_page[0x80 + p] = gal.ram + p * 256;
    for (int p = 0x08; p < 0x10; p++)
        op_page[0x80 + p] = gal.ram + p * 256;
    cpu->op_page = op_page;
    cpu->read = pengo_read;
    cpu->write = pengo_write;
    return 1;
}

static int vanvan_init(uint8_t *const *regions)
{
    pm.var = PM_VANVAN;
    pm.rom = regions[0];
    if (!pm_common_init(regions, 1, 2, 3, 4, -1))
        return 0;
    pm.dsw1 = 0xfe; /* MAME's defaults: upright, not flipped, 3 lives, 1C1C */
    pm.dsw2 = 0x00;
    /* 0000-3fff program; 8000-8fff van-5; A15 is decoded. 4800-4bff is RAM */
    z80_t *cpu = &gal.cpu[0];
    for (int p = 0; p < 0x40; p++)
        cpu->rd_page[0x80 + p] = (p < 0x10) ? pm.rom + 0x4000 + p * 256 : NULL;
    for (int m = 0; m < 4; m++)
    {
        const int base = 0x40 + (m & 1) * 0x20 + (m >> 1) * 0x80;
        for (int p = 0x08; p < 0x0c; p++)
            cpu->rd_page[base + p] = cpu->wr_page[base + p] = gal.ram + p * 256;
    }
    for (int k = 0; k < 2; k++)
        gal_sn_init(&pm.sn[k], 0, 1789750);
    return 1;
}

static void pm_reset(void)
{
    memset(gal.ram, 0, sizeof(gal.ram));
    memset(gal.soundregs, 0, sizeof(gal.soundregs));
    pm.irq_enable = 0;
    pm.irq_vector = 0;
    pm.mk_counter = pm.mk_offset = pm.mk_disable = 0;
    pm.mystery_control = pm.mystery_clock = pm.mystery_prescaler = 0;
    pm.gfx_bank = pm.palette_bank = pm.lookup_bank = 0;
    if (pm.var == PM_MSPACMAN)
    {
        /* MAME starts with the daughterboard in, the code leaves it at once */
        pm.ms_decode = 1;
        ms_map();
    }
    if (pm.var == PM_THEGLOB)
    {
        pm.glob_counter = 0x0a; /* set by jumpers */
        glob_map();
    }
    z80_init(&gal.cpu[0]);
    z80_reset(&gal.cpu[0]);
    gal_wsg_reset();
}

static void GAL_HOT(pm_run_slice)(int q)
{
    z80_run(&gal.cpu[0], CYCLES_PER_FRAME / 4);
    if (q == 3)
    {
        if (pm.var == PM_ALIBABA)
        {
            /* the mystery clock ticks every 64 frames */
            pm.mystery_prescaler = (pm.mystery_prescaler + 1) & 0x3f;
            if (pm.mystery_prescaler == 0 && (pm.mystery_control & 1))
                pm.mystery_clock = (pm.mystery_clock + 1) & 0x1f;
        }
        if (pm.irq_enable && pm.var == PM_VANVAN)
        {
            /* Van-Van Car: NMI */
            z80_set_nmi_line(&gal.cpu[0], 1);
            z80_set_nmi_line(&gal.cpu[0], 0);
        }
        else if (pm.irq_enable)
            z80_hold_irq(&gal.cpu[0], pm.var == PM_PENGO ? 0xff : pm.irq_vector); /* Pengo: IM 1 */
    }
}

/* --- Video --------------------------------------------------------------------- */

static void GAL_HOT(pm_prepare_frame)(void)
{
    gal.active_sprites = 0;
    for (int idx = 0; idx < 8; idx++)
    {
        const int n = 7 - idx; /* drawn from the last to the first */
        const uint8_t *code = gal.ram + pm.spr_code + 2 * n;
        const uint8_t *pos = gal.ram + pm.spr_pos + 2 * n;
        gal_sprite_t spr;
        spr.code = (code[0] >> 2) | (pm.gfx_bank << 6);
        spr.color = (code[1] & 0x1f) | (pm.lookup_bank << 5) | (pm.palette_bank << 6);
        spr.flags = code[0] & 3;
        /* the position on the upright screen; the first three a pixel to the
         * left (MAME's xoffsethack), but in Pengo */
        spr.x = 255 - 16 - pos[0] - (n < 3 && pm.var != PM_PENGO);
        spr.y = 16 + 256 - pos[1];
        if (spr.y > -16 && spr.y < GAL_H && spr.x > -16 && spr.x < GAL_W)
            gal.sprite[gal.active_sprites++] = spr;
    }
}

static void GAL_HOT(pm_render_band)(const gal_band_t *b)
{
    const int tile_bank = pm.gfx_bank << 8;
    const int color_bank = (pm.lookup_bank << 5) | (pm.palette_bank << 6);
    for (int row = b->y0 / 8; row <= (b->y0 + b->h - 1) / 8; row++)
    {
        for (int col = b->x0 / 8; col <= (b->x0 + b->w - 1) / 8; col++)
        {
            const unsigned addr = tileaddr[row][col];
            gal_draw_tile_opaque(b, 8 * col, 8 * row, pm.tiles + 8 * (tile_bank | gal.ram[addr]),
                                 pm.colormap + 4 * (color_bank | (gal.ram[0x400 + addr] & 0x1f)));
        }
    }

    /* the sprites only in the rows MAME clips them to */
    const int top = (pm.var == PM_ALIBABA) ? 0 : 16, bottom = (pm.var == PM_ALIBABA) ? 240 : 272;
    const int ys = b->y0 > top ? b->y0 : top, ye = b->y0 + b->h < bottom ? b->y0 + b->h : bottom;
    if (ys >= ye)
        return;
    const gal_band_t sb = {b->buf + (ys - b->y0) * b->w, b->x0, ys, b->w, ye - ys};
    for (int s = 0; s < gal.active_sprites; s++)
    {
        const gal_sprite_t *spr = &gal.sprite[s];
        const uint32_t *rows = pm.sprites + 16 * spr->code;
        const uint16_t *colors = pm.colormap + 4 * spr->color;
        gal_draw_sprite_fx(&sb, spr->x, spr->y, rows, spr->flags & 2, spr->flags & 1, colors, 0);
        /* again 256 rows up: the tunnel of Crush Roller */
        gal_draw_sprite_fx(&sb, spr->x, spr->y - 256, rows, spr->flags & 2, spr->flags & 1, colors, 0);
    }
}

static void GAL_HOT(pm_sound)(int16_t *out, int n)
{
    if (pm.var == PM_VANVAN)
        gal_sn_render(pm.sn, 2, 256, out, n);
    else
        gal_wsg_render(pm.waves, 0x0f, out, n);
}

/* --- The games --------------------------------------------------------------------- */

/* High scores: MAME's hiscore.dat, as GalaginoPlus uses it. */
static const gal_hiscore_region_t pm_hiscore[] = {
    {0x4e88, 4, 0x00, 0x00}, /* high score (BCD) */
    {0x43ed, 6, 0x40, 0x40}, /* video RAM: the high score shown */
    {0x43d1, 1, 0x48, 0x48}, /* video RAM */
};
static const gal_hiscore_region_t crush_hiscore[] = {
    {0x4c80, 3, 0x00, 0x00},    /* high score (BCD) */
    {0x4e40, 0x1e, 0x4b, 0x00}, /* the table */
};
static const gal_hiscore_region_t eyes_hiscore[] = {
    {0x4cf7, 0x3c, 0x4c, 0x00}, /* the table */
    {0x43ed, 6, 0x00, 0x40},    /* video RAM: the high score shown */
};
static const gal_hiscore_region_t mrtnt_hiscore[] = {
    {0x4cb3, 0x3c, 0x4c, 0x01},
    {0x43ed, 0x06, 0x00, 0x40},
};
static const gal_hiscore_region_t lizwiz_hiscore[] = {
    {0x4daf, 0x3c, 0x4d, 0x01},
    {0x43ed, 0x06, 0x00, 0x40},
};
static const gal_hiscore_region_t glob_hiscore[] = {
    {0x4c48, 0x3c, 0x4d, 0x00}, /* the table (hiscore.dat "theglobp") */
    {0x4c93, 1, 0x01, 0x01},
    {0x4cb9, 1, 0x15, 0x15},
};

static const gal_hiscore_region_t vanvan_hiscore[] = {
    {0x4809, 6, 0x00, 0x00},    /* the top score */
    {0x4c60, 0xf0, 0x00, 0x00}, /* the table */
};
static const gal_hiscore_region_t pengo_hiscore[] = {
    {0x8840, 0x1e, 0xd0, 0x41}, /* the table, with the initials */
    {0x880c, 2, 0xd0, 0x07},    /* the top score */
};

#define N(a) (sizeof(a) / sizeof((a)[0]))
#define PM_OPS(init_fn, hs, extra)                                                                  \
    {                                                                                              \
        .init = init_fn, .reset = pm_reset, .run_slice = pm_run_slice, .prepare_frame = pm_prepare_frame, \
        .render_band = pm_render_band, .sound = pm_sound, .view_top = 0, .view_h = GAL_H,          \
        .extra_button = extra, .hiscore = hs, .nhiscore = N(hs),                                   \
    }

const gal_game_ops_t gal_pacman_ops = PM_OPS(pm_init, pm_hiscore, 0);
const gal_game_ops_t gal_mspacman_ops = PM_OPS(ms_init, pm_hiscore, 0);
const gal_game_ops_t gal_crush_ops = PM_OPS(crush_init, crush_hiscore, 0);
const gal_game_ops_t gal_eyes_ops = PM_OPS(eyes_init, eyes_hiscore, 0);
const gal_game_ops_t gal_mrtnt_ops = PM_OPS(mrtnt_init, mrtnt_hiscore, 0);
const gal_game_ops_t gal_lizwiz_ops = PM_OPS(lizwiz_init, lizwiz_hiscore, 0);
const gal_game_ops_t gal_theglob_ops = PM_OPS(glob_init, glob_hiscore, 1);
const gal_game_ops_t gal_alibaba_ops = PM_OPS(alibaba_init, pm_hiscore, 0);
const gal_game_ops_t gal_pengo_ops = PM_OPS(pengo_init, pengo_hiscore, 0);
const gal_game_ops_t gal_vanvan_ops = {
    .init = vanvan_init, .reset = pm_reset, .run_slice = pm_run_slice, .prepare_frame = pm_prepare_frame,
    .render_band = pm_render_band, .sound = pm_sound, .view_top = 16, .view_h = 256, .flip180 = 1,
    .hiscore = vanvan_hiscore, .nhiscore = N(vanvan_hiscore),
};

#ifdef GAL_CONV_CHECK
int gal_tables_pacman(gal_table_t *t)
{
    int n = 0;
    t[n++] = (gal_table_t){"pacman_rom", pm.rom, 0x4000};
    t[n++] = (gal_table_t){"pacman_5e", pm.tiles, 256 * 8 * 2};
    t[n++] = (gal_table_t){"pacman_sprites", pm.sprites, 64 * 16 * 4};
    t[n++] = (gal_table_t){"pacman_colormap", pm.colormap, 64 * 4 * 2};
    t[n++] = (gal_table_t){"pacman_wavetable", pm.waves, 16 * 32};
    return n;
}
#endif
