// license:BSD-3-Clause
// copyright-holders:Richard Davies
/*
 * Phoenix (Amstar, 1980) machine: memory map, video registers, inputs, frame
 * timing, palette and tilemap renderer.
 *
 * Ported from MAME's src/mame/phoenix/phoenix.cpp and phoenix_v.cpp, for the
 * `phoenix` set only (upright cabinet, 8085 CPU, Phoenix sound board).
 *
 * Hardware summary:
 *   8085A at 2.75 MHz, no interrupts: the game polls VBLANK on DSW0 bit 7.
 *   352 x 256 frame at a 5.5 MHz pixel clock (61.04 Hz), 256 x 208 visible.
 *   Two 32x32 tilemaps of 8x8 2bpp tiles, no sprites. The monitor is mounted
 *   on its side (MAME ROT90), so the upright picture is 208 x 256.
 */
#ifndef PHOENIX_H
#define PHOENIX_H

#include <stdint.h>

#include "i8085.h"
#include "phoenix_sound.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PHX_CYCLES_PER_LINE 176 /* 352 pixel clocks at half the CPU input clock */
#define PHX_LINES 256
#define PHX_VISIBLE_LINES 208
#define PHX_CYCLES_PER_FRAME (PHX_CYCLES_PER_LINE * PHX_LINES) /* 45056 */

#define PHX_RAW_WIDTH 256
#define PHX_RAW_HEIGHT 208

/* Inputs, active high in this interface */
#define PHX_IN_COIN 0x01
#define PHX_IN_START1 0x02
#define PHX_IN_START2 0x04
#define PHX_IN_FIRE 0x10
#define PHX_IN_RIGHT 0x20
#define PHX_IN_LEFT 0x40
#define PHX_IN_SHIELD 0x80

/* The ROM set, laid out as MAME's regions */
typedef struct
{
    uint8_t maincpu[0x4000];
    uint8_t bgtiles[0x1000];
    uint8_t fgtiles[0x1000];
    uint8_t proms[0x200];
} phx_roms_t;

/* What the renderer needs, copied at the start of VBLANK */
typedef struct
{
    uint8_t fg[0x340] __attribute__((aligned(4))); /* copied word by word */
    uint8_t bg[0x340] __attribute__((aligned(4)));
    uint8_t scroll;
    uint8_t palette_bank;
} phx_video_t;

typedef struct phx
{
    i8085_t cpu;
    const phx_roms_t *roms;
    uint8_t vram[2][0x1000] __attribute__((aligned(4))); /* tilemaps + work RAM, one page per player */
    uint8_t vram_page;
    uint8_t palette_bank;
    uint8_t scroll;
    uint8_t inputs; /* PHX_IN_* */
    uint8_t dsw;    /* DSW0 bits 0-6 */
    uint8_t vblank;

    phx_video_t video;
    phx_sound_t sound;

    int16_t *audio;
    int audio_samples; /* per frame */
    int audio_pos;
    int slice_end; /* frame cycle at which the current CPU run ends */
} phx_t;

void phx_init(phx_t *m, const phx_roms_t *roms, int samplerate, int samples_per_frame);
void phx_reset(phx_t *m);

/* Runs one frame (45056 CPU cycles) and renders samples_per_frame samples into
 * audio. m->video holds the picture of this frame afterwards. */
void phx_run_frame(phx_t *m, int16_t *audio);

/* ---------------------------------------------------------------------------
 * Rendering
 * ------------------------------------------------------------------------ */

typedef enum
{
    PHX_ORIENT_ROTATED = 0,  /* Tate mode Off: turned upright for a normal monitor */
    PHX_ORIENT_TATE_CW = 1,  /* Bottom left: raw raster; monitor turned clockwise */
    PHX_ORIENT_TATE_CCW = 2, /* Bottom right: raw raster rotated 180; monitor turned counter-clockwise */
    /* Not a setting: Tate mode Off on a display that scales the rows itself
     * (HSTX). All 256 rows of the upright picture, on a canvas `stride` pixels
     * wide and PHX_RAW_WIDTH (256) rows tall, centred at x = (stride - 208) / 2. */
    PHX_ORIENT_ROTATED_FULL = 3,
} phx_orient_t;

typedef enum
{
    PHX_FMT_RGB444 = 0, /* 0000 RRRR GGGG BBBB (PicoDVI) */
    PHX_FMT_RGB555 = 1, /* 0RRR RRGG GGGB BBBB (HSTX) */
    PHX_FMT_RGB565 = 2, /* RRRR RGGG GGGB BBBB (host harness) */
} phx_pixfmt_t;

typedef struct
{
    uint16_t pal[256];
    uint16_t black;
    /* 2bpp tiles packed 8 pixels per word, pixel i at bits 2i..2i+1.
     * rows[code][y] runs along x; cols[code][x] runs along y. */
    uint16_t fg_rows[256][8];
    uint16_t bg_rows[256][8];
    uint16_t fg_cols[256][8];
    uint16_t bg_cols[256][8];
    uint8_t palette_rgb[256][3];
} phx_gfx_t;

void phx_gfx_init(phx_gfx_t *g, const phx_roms_t *roms, phx_pixfmt_t fmt);

/* Draws the game area of the frame into a 320x240 canvas (stride in pixels).
 * Rotated mode shows the 208x256 picture as 208x240, centred, by dropping
 * every 16th row; tate shows the 256x208 raster centred. Rotated full uses
 * its own canvas, see PHX_ORIENT_ROTATED_FULL.
 *
 * The borders around the game area are not touched, so anything drawn there
 * (the FPS counter) survives from frame to frame instead of being erased and
 * redrawn while the display is scanning it out. Paint them with
 * phx_render_borders() whenever the orientation changes or something else
 * has drawn over the canvas. */
void phx_render(const phx_gfx_t *g, const phx_video_t *v, phx_orient_t orient, uint16_t *fb, int stride);
void phx_render_borders(const phx_gfx_t *g, phx_orient_t orient, uint16_t *fb, int stride);

#ifdef __cplusplus
}
#endif

#endif
