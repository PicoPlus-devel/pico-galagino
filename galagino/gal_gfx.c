/*
 * Graphics and sound PROM converters: C ports of upstream Galagino's
 * romconv/tileconv.py, spriteconv.py, cmapconv.py and audioconv.py. Upstream
 * runs them on a PC and compiles the output in; here they run once when a game
 * starts, on the ROMs read from the SD card.
 *
 * The output formats are upstream's, so the renderers can stay as they are,
 * with two differences: colours are in the canvas format (gal_color) instead of
 * byte-swapped RGB565 for an SPI display, and sprites are kept in two variants
 * (normal and flipped in x) instead of four, a flip in y being the same rows
 * read backwards.
 */
#include "gal_internal.h"

/* --- Colours --------------------------------------------------------------- */

uint16_t gal_color(uint16_t c)
{
    if (!c)
        return 0;
    const unsigned r = c >> 11, g = (c >> 5) & 63, b = c & 31;
    uint16_t v;
    switch (gal.fmt)
    {
    case GAL_FMT_RGB555:
        v = (uint16_t)((r << 10) | ((g >> 1) << 5) | b);
        break;
    case GAL_FMT_RGB444:
        v = (uint16_t)(((r >> 1) << 8) | ((g >> 2) << 4) | (b >> 1));
        break;
    default:
        v = c;
        break;
    }
    return v ? v : 1;
}

/* cmapconv.py parse_palette, the bbgggrrr mapping of Galaga, Pac-Man, Frogger
 * and Dig Dug (bit weights 4/7, 2/7, 1/7; see the comment there). */
uint16_t gal_prom_bgr233(uint8_t c)
{
    const unsigned b = 31 * ((c >> 6) & 0x3) / 3;
    const unsigned g = 63 * ((c >> 3) & 0x7) / 7;
    const unsigned r = 31 * ((c >> 0) & 0x7) / 7;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

/* --- Characters -------------------------------------------------------------- */

/* tileconv.py parse_chr + dump_chr */
void gal_conv_chars_namco(const uint8_t *rom, int count, uint16_t *out)
{
    for (int chr = 0; chr < count; chr++)
    {
        const uint8_t *data = rom + 16 * chr;
        for (int y = 0; y < 8; y++)
        {
            uint16_t val = 0;
            for (int x = 0; x < 8; x++)
            {
                const uint8_t byte = data[15 - x - 2 * (y & 4)];
                const int c0 = (byte & (0x08 >> (y & 3))) ? 1 : 0;
                const int c1 = (byte & (0x80 >> (y & 3))) ? 2 : 0;
                val = (uint16_t)((val >> 2) | ((c0 + c1) << 14));
            }
            *out++ = val;
        }
    }
}

static uint8_t bit01_swap(uint8_t a)
{
    return (uint8_t)((a & 0xfc) | ((a & 2) >> 1) | ((a & 1) << 1));
}

/* tileconv.py parse_charmap_2 + parse_chr_2: Frogger's 256 characters, one
 * bitplane per 2 KB ROM. "In frogger the second gfx rom has D0 and D1
 * swapped": if the first 16 bytes of rom0 are rom1 with D0/D1 swapped, rom0 is
 * swapped back. */
void gal_conv_chars_2(const uint8_t *rom0, const uint8_t *rom1, uint16_t *out)
{
    int swap = 1;
    for (int i = 0; i < 16; i++)
        if (bit01_swap(rom0[i]) != rom1[i])
            swap = 0;
    for (int chr = 0; chr < 256; chr++)
    {
        const uint8_t *d0 = rom0 + 8 * chr, *d1 = rom1 + 8 * chr;
        for (int y = 0; y < 8; y++)
        {
            uint16_t val = 0;
            for (int x = 0; x < 8; x++)
            {
                const uint8_t b0 = swap ? bit01_swap(d0[7 - x]) : d0[7 - x];
                const int c0 = (b0 & (0x80 >> y)) ? 1 : 0;
                const int c1 = (d1[7 - x] & (0x80 >> y)) ? 2 : 0;
                val = (uint16_t)((val >> 2) | ((c0 + c1) << 14));
            }
            *out++ = val;
        }
    }
}

/* --- Sprites ------------------------------------------------------------------- */

void gal_sprite_xflip(const uint32_t *in, uint32_t *out)
{
    for (int y = 0; y < 16; y++)
    {
        uint32_t v = in[y], f = 0;
        for (int x = 0; x < 16; x++, v >>= 2)
            f = (f << 2) | (v & 3);
        out[y] = f;
    }
}

/* spriteconv.py parse_sprite + dump_sprite */
void gal_conv_sprites_namco(const uint8_t *rom, int count, int pacman_fmt, uint32_t *out_normal, uint32_t *out_xflip)
{
    for (int spr = 0; spr < count; spr++)
    {
        const uint8_t *data = rom + 64 * spr;
        uint32_t *dst = out_normal + 16 * spr;
        for (int y = 0; y < 16; y++)
        {
            /* Pac-Man's top four rows are in fact its bottom four */
            const int sy = pacman_fmt ? (y + 4) & 15 : y;
            uint32_t val = 0;
            for (int x = 0; x < 16; x++)
            {
                const int idx = ((sy & 8) << 1) + (((x & 8) ^ 8) << 2) + (7 - (x & 7)) + 2 * (sy & 4);
                const int c0 = (data[idx] & (0x08 >> (sy & 3))) ? 1 : 0;
                const int c1 = (data[idx] & (0x80 >> (sy & 3))) ? 2 : 0;
                val = (val >> 2) | ((uint32_t)(c0 + c1) << 30);
            }
            dst[y] = val;
        }
        if (out_xflip)
            gal_sprite_xflip(dst, out_xflip + 16 * spr);
    }
}

/* spriteconv.py parse_sprite_frogger: 64 sprites spread over both 2 KB ROMs,
 * D0/D1 of the first one swapped. */
void gal_conv_sprites_frogger(const uint8_t *rom0, const uint8_t *rom1, uint32_t *out_normal, uint32_t *out_xflip)
{
    for (int spr = 0; spr < 64; spr++)
    {
        const uint8_t *d0 = rom0 + 32 * spr, *d1 = rom1 + 32 * spr;
        uint32_t *dst = out_normal + 16 * spr;
        for (int y = 0; y < 16; y++)
        {
            uint32_t val = 0;
            for (int x = 0; x < 16; x++)
            {
                const int ym = (y & 7) | ((x & 8) ^ 8);
                const int xm = (x & 7) | (y & 8);
                const int idx = (xm ^ 7) + ((ym & 8) << 1);
                const int c0 = (bit01_swap(d0[idx]) & (0x80 >> (ym & 7))) ? 1 : 0;
                const int c1 = (d1[idx] & (0x80 >> (ym & 7))) ? 2 : 0;
                val = (val >> 2) | ((uint32_t)(c0 + c1) << 30);
            }
            dst[y] = val;
        }
        gal_sprite_xflip(dst, out_xflip + 16 * spr);
    }
}

/* --- Sound ------------------------------------------------------------------------ */

/* audioconv.py: 8 waveforms of 32 four-bit steps per 256-byte PROM. */
void gal_conv_waves(const uint8_t *prom, int count, int8_t *out)
{
    for (int i = 0; i < count * 32; i++)
        out[i] = (int8_t)((prom[i] & 0x0f) - 7);
}

/* --- MAME gfx layouts ---------------------------------------------------------
 * The games after the first six are decoded straight from their MAME
 * gfx_layout, turned into the upright picture: the element's pixel (ux, uy) of
 * the upright picture is MAME's pixel
 *   ROT90:  (uy, s - 1 - ux)
 *   ROT270: (s - 1 - uy, ux)
 * of an s x s element. */

int gal_layout_pixel(const uint8_t *rom, const gal_layout_t *l, int n, int x, int y)
{
    const uint32_t base = (uint32_t)n * l->charincrement + l->yoffset[y] + l->xoffset[x];
    int v = 0;
    for (int p = 0; p < l->planes; p++)
    {
        const uint32_t bit = base + l->planeoffset[p];
        v = (v << 1) | ((rom[bit >> 3] >> (7 - (bit & 7))) & 1);
    }
    return v;
}

static inline int rot_pixel(const uint8_t *rom, const gal_layout_t *l, int n, int rot, int ux, int uy)
{
    const int s = l->w;
    return (rot == GAL_ROT90) ? gal_layout_pixel(rom, l, n, uy, s - 1 - ux)
                              : gal_layout_pixel(rom, l, n, s - 1 - uy, ux);
}

void gal_conv_tiles8(const uint8_t *rom, const gal_layout_t *l, int count, int rot, uint16_t *out)
{
    for (int n = 0; n < count; n++)
        for (int uy = 0; uy < 8; uy++)
        {
            uint16_t row = 0;
            for (int ux = 0; ux < 8; ux++)
                row |= (uint16_t)(rot_pixel(rom, l, n, rot, ux, uy) << (2 * ux));
            *out++ = row;
        }
}

void gal_conv_sprites16(const uint8_t *rom, const gal_layout_t *l, int count, int rot, uint32_t *out)
{
    for (int n = 0; n < count; n++)
        for (int uy = 0; uy < 16; uy++)
        {
            uint32_t row = 0;
            for (int ux = 0; ux < 16; ux++)
                row |= (uint32_t)rot_pixel(rom, l, n, rot, ux, uy) << (2 * ux);
            *out++ = row;
        }
}

/* Elements of up to four bits per pixel, upright: per row size / 8 words of
 * eight pixels, the leftmost in the low nibble. */
void gal_conv_4bpp(const uint8_t *rom, const gal_layout_t *l, int first, int count, int rot, uint32_t *out)
{
    const int s = l->w;
    for (int n = first; n < first + count; n++)
        for (int uy = 0; uy < s; uy++)
            for (int w = 0; w < s / 8; w++)
            {
                uint32_t word = 0;
                for (int k = 0; k < 8; k++)
                    word |= (uint32_t)rot_pixel(rom, l, n, rot, 8 * w + k, uy) << (4 * k);
                *out++ = word;
            }
}
