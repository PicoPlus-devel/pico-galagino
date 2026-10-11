/*
 * Galagino core: Till Harbaum's arcade emulator (https://github.com/harbaum/galagino)
 * for the Frens family of RP2350 emulators.
 *
 * Plain C with no Pico dependencies: the same sources build for the board and
 * for the host harness in hosttest/. One game runs at a time. The ROM set is read
 * from MAME zips (or their files unzipped) through gal_io_t and converted into
 * Galagino's formats at start-up, where upstream converts them on a PC into C
 * headers.
 *
 * Video. Every game draws in upright coordinates, 224x288, the way Galagino
 * does, into a small band buffer that is then copied into the canvas of the
 * framework in one of the orientations of gal_orient_t: the 320x240 canvas, or
 * for GAL_ORIENT_UPRIGHT_FULL one as tall as the picture. The bands are emitted
 * so that canvas rows are written top to bottom: the canvas is single buffered
 * and is rewritten ahead of the scan-out.
 */
#ifndef GALAGINO_H
#define GALAGINO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Buttons, as in Galagino's emulation.h. */
#define GAL_BTN_LEFT 0x01
#define GAL_BTN_RIGHT 0x02
#define GAL_BTN_UP 0x04
#define GAL_BTN_DOWN 0x08
#define GAL_BTN_FIRE 0x10
#define GAL_BTN_START 0x20
#define GAL_BTN_COIN 0x40
#define GAL_BTN_EXTRA 0x80 /* second button: 1942's loop */
#define GAL_BTN_START2 0x100 /* not in Galagino: 2 player start */

typedef enum
{
    GAL_GAME_PACMAN = 0,
    GAL_GAME_GALAGA,
    GAL_GAME_DKONG,
    GAL_GAME_FROGGER,
    GAL_GAME_DIGDUG,
    GAL_GAME_1942,
    GAL_GAME_COUNT
} gal_game_id_t;

/* The value of settings.flags.tateMode, plus GAL_ORIENT_UPRIGHT_FULL. */
typedef enum
{
    GAL_ORIENT_UPRIGHT = 0, /* Tate off: upright picture, 224x240 (1 row in 6 dropped) */
    GAL_ORIENT_TATE_CW = 1, /* Bottom left: raw 288x224 for a monitor turned clockwise */
    GAL_ORIENT_TATE_CCW = 2, /* Bottom right: the same turned 180 degrees */
    /* Not a setting: Tate off on a display that scales the rows itself (HSTX).
     * All rows of the picture, on a canvas `stride` pixels wide and
     * gal_view_height() rows tall, the picture centred at x = (stride - 224) / 2. */
    GAL_ORIENT_UPRIGHT_FULL = 3
} gal_orient_t;

/* Pixel format of the canvas: RGB555 on HSTX, RGB444 on PicoDVI, RGB565 on the host. */
typedef enum
{
    GAL_FMT_RGB555 = 0,
    GAL_FMT_RGB444,
    GAL_FMT_RGB565
} gal_fmt_t;

#define GAL_CANVAS_W 320
#define GAL_CANVAS_H 240

/* --- File access, implemented by the board (FatFs) and the host (stdio) --- */

typedef void (*gal_dir_cb)(void *arg, const char *name, uint32_t size, int is_dir);

typedef struct
{
    void *ctx;
    /* Calls cb for every entry of dir; returns 0 when dir cannot be opened. */
    int (*list_dir)(void *ctx, const char *dir, gal_dir_cb cb, void *arg);
    void *(*open)(void *ctx, const char *path);
    uint32_t (*size)(void *ctx, void *file);
    /* Returns the number of bytes read, or -1. */
    int (*read_at)(void *ctx, void *file, uint32_t offset, void *buf, uint32_t len);
    void (*close)(void *ctx, void *file);
    /* The loader's scratch memory, all freed before it returns: a whole loose
     * file (up to 16 KB for 1942), a compressed zip entry and the inflate state
     * (about 11 KB). NULL for malloc and free. */
    void *(*alloc)(void *ctx, uint32_t size);
    void (*free)(void *ctx, void *p);
} gal_io_t;

/* --- The games ------------------------------------------------------------ */

typedef struct
{
    const char *name; /* MAME name, for messages */
    uint32_t crc;
    uint32_t size;
    uint8_t region; /* index into the game's regions, see gal_game_info_t */
    uint32_t offset;
} gal_romfile_t;

#define GAL_MAX_REGIONS 12

typedef struct
{
    const char *title;   /* "Pac-Man" */
    const char *folder;  /* below /roms/arcade: "PACMAN" */
    const char *mameset; /* "puckman", the zip the set is usually found in */
    const gal_romfile_t *files;
    uint8_t nfiles;
    uint8_t nregions;
    uint16_t temp_mask; /* regions only needed while converting (bit = region) */
    uint32_t region_size[GAL_MAX_REGIONS];
} gal_game_info_t;

extern const gal_game_info_t gal_games[GAL_GAME_COUNT];

/* --- Life cycle ---------------------------------------------------------- */

/* Once, before anything else. */
void gal_init(gal_fmt_t fmt, int samplerate);

/* Memory outside SRAM (PSRAM) for the games that need it (1942). Without it
 * those games are only available from the flash store. */
void gal_set_psram(void *mem, uint32_t size);
#define GAL_PSRAM_SIZE (384 * 1024)

/* --- The flash store, for boards without PSRAM ------------------------------
 * The ROM sets copied once into flash, a slot per game at a fixed place: the
 * game's ROM regions as the loader builds them from the zips, and for 1942 its
 * playfield tiles and sprites, converted, as they do not fit in SRAM. A slot
 * ends in a header with the layout and a CRC of the data, written last; a slot
 * whose header does not match is copied again. A game in the store starts from
 * it, without the SD card. */
typedef struct
{
    const uint8_t *base; /* where the store is read (XIP) */
    uint32_t offset;     /* its offset in flash, for erase and program */
    uint32_t size;
    /* flash offsets; erase in 4 KB sectors, program in 256-byte pages */
    void (*erase)(uint32_t offset, uint32_t len);
    void (*program)(uint32_t offset, const uint8_t *data, uint32_t len);
    /* called before a game is copied; may be NULL */
    void (*copying)(int game);
} gal_flash_t;

/* Takes the store into use: checks the games already in it and, with io set,
 * copies the complete sets found under root that are not. Returns the mask of
 * the games it holds (bit = gal_game_id_t). Call with no game running; copying
 * erases and programs flash, so do it while the display is not running. */
uint32_t gal_flash_sync(const gal_flash_t *flash, const gal_io_t *io, const char *root);

/* The bytes the store needs for all games. */
uint32_t gal_flash_size_needed(void);

/* The mask of the game's files present in dir (bit i = files[i]), from the zip
 * directories and the CRCs of loose files, without loading anything. */
uint32_t gal_probe(gal_game_id_t game, const gal_io_t *io, const char *dir);

/* True when the game is part of this build. */
int gal_supported(gal_game_id_t game);

/* True when the game can run on this board: supported, and given PSRAM or in
 * the flash store if it needs either. */
int gal_available(gal_game_id_t game);

/* True when the game needs PSRAM. */
int gal_needs_psram(gal_game_id_t game);

/* True when `found` covers the whole set. */
int gal_set_complete(gal_game_id_t game, uint32_t found);

/* Loads, converts and starts a game, from the flash store when it holds it,
 * from dir otherwise. Returns the mask of files found; the game runs only when
 * the set is complete (gal_set_complete). */
uint32_t gal_start(gal_game_id_t game, const gal_io_t *io, const char *dir);

/* Stops the running game and frees its memory. */
void gal_stop(void);

/* The running game, or -1. */
int gal_running(void);

/* Power-on reset of the running game. */
void gal_reset(void);

/* Runs one frame of the running game with these buttons (GAL_BTN_*) and
 * renders its audio, n mono samples at the rate given to gal_init. */
void gal_run_frame(uint16_t buttons, int16_t *audio, int n);

/* Draws the game area of the canvas (stride in pixels). */
void gal_render(gal_orient_t orient, uint16_t *fb, int stride);

/* Paints everything outside the game area black. */
void gal_render_borders(gal_orient_t orient, uint16_t *fb, int stride);

/* The rows of the upright picture that are shown: 288 for the menu and the
 * Namco boards, 256 for the Galaxian-class ones. The height of the canvas for
 * GAL_ORIENT_UPRIGHT_FULL. */
int gal_view_height(void);

/* Bytes in use at the peak of the last gal_start, and the size, of the SRAM
 * arena and of the PSRAM block. */
uint32_t gal_arena_peak(void);
uint32_t gal_arena_size(void);
uint32_t gal_psram_peak(void);
uint32_t gal_psram_size(void);

/* --- The game menu ------------------------------------------------------- */

/* Shows Galagino's logo menu with the games in `installed` (bit = gal_game_id_t). */
void gal_menu_init(uint32_t installed);

/* One frame of the menu. Returns the game to start, or -1. After 20 seconds
 * without input a random game is started, as in Galagino. */
int gal_menu_frame(uint16_t buttons);

/* Draws the menu. Only redraws when something changed, unless `force`. */
void gal_menu_render(gal_orient_t orient, uint16_t *fb, int stride, int force);

#ifdef __cplusplus
}
#endif

#endif
