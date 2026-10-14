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

/* The games, in the order of the menu (by title). Upstream's six are the
 * first that run; the others have their ROM sets listed (gal_games), for the
 * menu to know them and for pico-bootLoader's updateAll, until they are
 * ported (gal_supported). */
typedef enum
{
    GAL_GAME_1942 = 0,
    GAL_GAME_ALIBABA,
    GAL_GAME_AMIDAR,
    GAL_GAME_ANTEATER,
    GAL_GAME_BAGMAN,
    GAL_GAME_BOMBJACK,
    GAL_GAME_BNJ,
    GAL_GAME_BTIME,
    GAL_GAME_CIRCUSC,
    GAL_GAME_CRUSH,
    GAL_GAME_DIGDUG,
    GAL_GAME_DKONG,
    GAL_GAME_DKONG3,
    GAL_GAME_DKONGJR,
    GAL_GAME_EYES,
    GAL_GAME_FANTASY,
    GAL_GAME_FROGGER,
    GAL_GAME_GALAGA,
    GAL_GAME_GALAXIAN,
    GAL_GAME_GAPLUS,
    GAL_GAME_GYRUSS,
    GAL_GAME_LADYBUG,
    GAL_GAME_LIZWIZ,
    GAL_GAME_MAPPY,
    GAL_GAME_MOONCRST,
    GAL_GAME_MRDO,
    GAL_GAME_MRTNT,
    GAL_GAME_MSPACMAN,
    GAL_GAME_NIBBLER,
    GAL_GAME_PACMAN,
    GAL_GAME_PENGO,
    GAL_GAME_PHOENIX,
    GAL_GAME_PBACTION,
    GAL_GAME_POOYAN,
    GAL_GAME_ROCNROPE,
    GAL_GAME_SCRAMBLE,
    GAL_GAME_INVADERS,
    GAL_GAME_STARFORC,
    GAL_GAME_SCOBRA,
    GAL_GAME_THEGLOB,
    GAL_GAME_TIMEPLT,
    GAL_GAME_TODRUAGA,
    GAL_GAME_TURTLES,
    GAL_GAME_TUTANKHM,
    GAL_GAME_VANVAN,
    GAL_GAME_VANGUARD,
    GAL_GAME_XEVIOUS,
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

#define GAL_MAX_REGIONS 16
#define GAL_MAX_ZIPS 3

typedef struct
{
    const char *title;   /* "Pac-Man" */
    const char *folder;  /* below /roms/arcade: "PACMAN" */
    const char *mameset; /* "puckman", the zip the set is usually found in */
    /* The zips updateAll takes the files from, in order: MAME's parent set
     * first for a clone ({"puckman.zip", "pacman.zip"}). Unused entries NULL. */
    const char *zips[GAL_MAX_ZIPS];
    const gal_romfile_t *files;
    uint8_t nfiles;
    uint8_t nregions;
    uint16_t temp_mask; /* regions only needed while converting (bit = region) */
    uint32_t region_size[GAL_MAX_REGIONS];
} gal_game_info_t;

extern const gal_game_info_t gal_games[GAL_GAME_COUNT];

/* A set of games, bit = gal_game_id_t. */
typedef uint64_t gal_games_t;
#define GAL_GAME_BIT(g) ((gal_games_t)1 << (g))

/* --- Life cycle ---------------------------------------------------------- */

/* Once, before anything else. */
void gal_init(gal_fmt_t fmt, int samplerate);

/* Memory outside SRAM (PSRAM) for the large games (1942). Without it those
 * games run from the flash cache. */
void gal_set_psram(void *mem, uint32_t size);
#define GAL_PSRAM_SIZE (384 * 1024)

/* --- The flash cache, for boards without PSRAM --------------------------------
 * A large game, whose data does not fit in SRAM, runs from flash there: its
 * ROM regions as the loader builds them from the zips, and the tables built
 * from them that the game reads in place (1942's playfield tiles and sprites).
 * The cache holds a slot per game copied into it. Slots are copied when a game
 * is first started, and stay until the room is needed for another game: then
 * the slot copied longest ago goes first. Every slot starts with a header that
 * describes it, written last, so a copy cut short is never used; the data is
 * checked against the CRC in the header once per boot, before the game first
 * starts from it.
 *
 * Copying erases and programs flash, so it must run with nothing else using
 * flash: on the board before the display starts. */
typedef struct
{
    const uint8_t *base; /* where the cache is read (XIP) */
    uint32_t offset;     /* its offset in flash, for erase and program */
    uint32_t size;
    /* flash offsets; erase in 4 KB sectors, program in 256-byte pages */
    void (*erase)(uint32_t offset, uint32_t len);
    void (*program)(uint32_t offset, const uint8_t *data, uint32_t len);
    /* called before a game is copied; may be NULL */
    void (*copying)(int game);
} gal_flash_t;

/* Takes the cache into use and finds the games in it. Reads only. */
void gal_flash_open(const gal_flash_t *flash);

/* The bytes the game takes in the cache; 0 for a game that does not use it. */
uint32_t gal_flash_slot_size(gal_game_id_t game);

/* True when the cache is open and large enough for the game. */
int gal_flash_room(gal_game_id_t game);

/* True when the game is in the cache and its data checks out. */
int gal_flash_holds(gal_game_id_t game);

/* Copies the game's set from dir into the cache, making room by dropping the
 * games copied longest ago. Returns the mask of the set's files found; the
 * copy succeeded when gal_flash_holds(game) is true afterwards. */
uint32_t gal_flash_copy(gal_game_id_t game, const gal_io_t *io, const char *dir);

/* The games in the cache. */
gal_games_t gal_flash_games(void);

/* The mask of the game's files present in dir (bit i = files[i]), from the zip
 * directories and the CRCs of loose files, without loading anything. */
uint32_t gal_probe(gal_game_id_t game, const gal_io_t *io, const char *dir);

/* True when the game is part of this build. */
int gal_supported(gal_game_id_t game);

/* True when the game can run on this board: supported, and for a large game
 * PSRAM or room in the flash cache. */
int gal_available(gal_game_id_t game);

/* True when the game has a second button of its own (GAL_BTN_EXTRA); for the
 * others B is a second fire button. */
int gal_has_extra_button(gal_game_id_t game);

/* True when the game's data does not fit in SRAM: it runs from PSRAM, or from
 * the flash cache on a board without. */
int gal_is_large(gal_game_id_t game);

/* True when the game has to be copied into the flash cache before it can
 * start: a large game, no PSRAM, and not in the cache yet. */
int gal_needs_copy(gal_game_id_t game);

/* True when `found` covers the whole set. */
int gal_set_complete(gal_game_id_t game, uint32_t found);

/* Loads, converts and starts a game: from dir, into SRAM, or into PSRAM for a
 * large game; from the flash cache for a large game on a board without PSRAM
 * (gal_needs_copy must be false). Returns the mask of files found; the game
 * runs only when the set is complete (gal_set_complete). */
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

/* --- High scores ------------------------------------------------------------
 * The running game's high score table: the bytes of the regions of MAME's
 * hiscore.dat, as GalaginoPlus saves them. The board keeps them in a file; the
 * core says when to read and write it. */
#define GAL_HISCORE_MAX 640

/* The table's size in bytes; 0 when the running game keeps none. */
int gal_hiscore_size(void);

/* After gal_start (and gal_reset): the table saved last time, or NULL. It is
 * put into the game once the game has set up its own table, after start-up. */
void gal_hiscore_load(const uint8_t *saved);

/* After each gal_run_frame: true when the table changed since it was loaded
 * or last reported; it is then in out (gal_hiscore_size() bytes), to be saved.
 * Looked at every 5 seconds. */
int gal_hiscore_poll(uint8_t *out);

/* The same, looked at now: before gal_stop or gal_reset. */
int gal_hiscore_flush(uint8_t *out);

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

/* Shows Galagino's logo menu with the games in `installed`, `select` selected
 * (Galaga if it is not listed, e.g. -1). Stops the game, if one is running: the
 * menu keeps tables in its arena. */
void gal_menu_init(gal_games_t installed, int select);

/* One frame of the menu. Returns the game to start, or -1. After 20 seconds
 * without input a random game is started, as in Galagino; buttons still held
 * when the menu is shown hold that off until they are released. */
int gal_menu_frame(uint16_t buttons);

/* Draws the menu. Only redraws when something changed, unless `force`. */
void gal_menu_render(gal_orient_t orient, uint16_t *fb, int stride, int force);

#ifdef __cplusplus
}
#endif

#endif
