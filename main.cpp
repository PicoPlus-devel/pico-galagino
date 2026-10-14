/*
 * picoGalagino - Galagino, from GalaginoPlus's set of games, for RP2350 boards.
 *
 * Boots the pico_shared framework, finds the games whose ROM sets are on the SD
 * card, shows Galagino's game menu and runs the chosen game (galagino/) one
 * frame per display frame. The core is plain C shared with the host harness in
 * hosttest/; this file wires it to the display, audio and controller drivers
 * and to the settings menu.
 *
 * Like the other arcade games of this family there is no ROM browser:
 *   - menu() is never called; only the settings menu (SELECT + START) is used.
 *     In a game, its "Quit game" returns to the game menu.
 *   - each game reads its ROM set from its own folder below /roms/arcade: the
 *     MAME zip as is, or its files unzipped. The game menu lists the games whose
 *     sets are complete; without any, a screen names the folders.
 *   - a large game, whose data does not fit in SRAM (1942), runs from PSRAM.
 *     On a board without, it runs from a cache in flash, copied there the
 *     first time it is started: the board restarts and copies the set before
 *     the display starts. See "The flash cache".
 *   - high scores are kept in /SAVES/GALAGINO, a file per game.
 *
 * The games are vertical. The Tate mode setting chooses between the picture
 * upright for a normal monitor (default) and the unrotated raster for a monitor
 * turned on its side; see galagino/galagino.h. Upright, HSTX shows every row
 * of the picture, scaled to the screen height by the video driver; PicoDVI
 * leaves rows out to fit its 240 (see "Framebuffer access").
 */

#include <cstdio>
#include <cstring>

#include "pico/stdlib.h"
#include "hardware/vreg.h"
#include "hardware/watchdog.h"

#include "ff.h"
#include "tusb.h"

#include "FrensHelpers.h"
#include "FrensFonts.h"
#include "gamepad.h"
#include "menu.h"
#include "menu_settings.h"
#include "nespad.h"
#include "settings.h"
#include "vumeter.h"
#include "wiipad.h"

#include "galagino.h"

// The standard 252 MHz clock (10x the 640x480 pixel clock).
#define GALAGINO_CLOCKFREQ_KHZ 252000

// Must be a power of two (util::RingBuffer asserts it). It holds the audio
// cushion (see pushAudio()) plus a whole frame of 735 samples. The I2S ring of
// pico_shared is given the same size in CMakeLists.txt.
#define AUDIOBUFFERSIZE 2048

#define SAMPLERATE 44100
#define SAMPLES_PER_FRAME (SAMPLERATE / 60) // 735

#define ROMROOT "/roms/arcade"

// 1: print the time spent emulating and drawing a frame, on average and at
// worst, over the serial console every 5 seconds. For measuring a board.
#ifndef GALAGINO_FRAME_STATS
#define GALAGINO_FRAME_STATS 0
#endif

// 1: use the flash cache on a board with PSRAM as well, as a board without it
// does, ignoring the PSRAM. For testing the boards without PSRAM on one that
// has it (the Fruit Jam).
#ifndef GALAGINO_FLASH_CACHE_TEST
#define GALAGINO_FLASH_CACHE_TEST 0
#endif

// Output gains in Q8 (256 = unity). Galagino's levels peak at about 0.4 (Pac-
// Man) to 0.6 (Dig Dug) of full scale, which suits HDMI. The I2S DAC drives
// the amplifier or headphones directly, with no volume stage after it, and was
// far too loud at that level: it gets a quarter (-12 dB), peaks of about 3000
// to 5000, near the NES emulator on the same DAC.
#ifndef DVI_AUDIO_GAIN_Q8
#define DVI_AUDIO_GAIN_Q8 256
#endif
#ifndef EXT_AUDIO_GAIN_Q8
#define EXT_AUDIO_GAIN_Q8 64
#endif

static uint32_t CPUFreqKHz = GALAGINO_CLOCKFREQ_KHZ;

static int16_t audioBuf[SAMPLES_PER_FRAME];
static bool audioPending = false; // audioBuf holds a frame's sound, not pushed yet

// What the board is showing.
enum class State
{
    NoGames, // no SD card, or no complete ROM set: the folder list
    Menu,    // Galagino's game menu
    Game,    // a game is running
    Failed,  // a game could not be started: the reason, until a button
};
static State state = State::NoGames;

static bool sdOk = false;
static gal_games_t installed = 0; // the games that can be played
static bool showSettings = false;
static uint32_t fps = 0;
static uint64_t lastFrameUs = 0;

// ---------------------------------------------------------------------------
// Settings menu wiring.
//
// Positional, indexed by MenuSettingsIndex - append only, never reorder.
// 1 = shown, 0 = hidden, -1 = never shown. 0 and -1 differ only for Exit, Save/
// Restore state and Reset, which the in-game menu shows unless they are -1.
// C++ designated initializers may not skip members, so every entry up to the
// last one used is listed.
//
// Two tables: in a game, "Quit game" returns to the game menu and "Reset game"
// restarts the game; in the game menu neither means anything.
// ---------------------------------------------------------------------------
#define GALAGINO_VISIBILITY(inGame)                                                     \
    {                                                                                   \
        [MOPT_EXIT_GAME] = (inGame) ? 1 : -1, /* back to the game menu */               \
        [MOPT_RESET_GAME] = (inGame) ? 1 : -1,                                          \
        [MOPT_REBOOT_TO_LOADER] = BOOTLOADER_BUILD, /* the bootloader's picker */       \
        [MOPT_SAVE_RESTORE_STATE] = -1,             /* no save states */                \
        [MOPT_SCREENMODE] = 1,                                                          \
        [MOPT_SCANLINES] = 0, /* covered by the screen modes */                         \
        [MOPT_SCANLINE_TYPE] = HSTX,                                                    \
        [MOPT_FPS_OVERLAY] = 1,                                                         \
        [MOPT_AUDIO_ENABLE] = 1,                                                        \
        [MOPT_FRAMESKIP] = 0,                                                           \
        [MOPT_DISPLAY_MODE] = HSTX && ENABLEDVI,                                        \
        [MOPT_EXTERNAL_AUDIO] = EXT_AUDIO_IS_ENABLED,                                   \
        [MOPT_FONT_COLOR] = 0,                                                          \
        [MOPT_FONT_BACK_COLOR] = 0,                                                     \
        [MOPT_FRUITJAM_VUMETER] = ENABLE_VU_METER,                                      \
        [MOPT_FRUITJAM_VOLUME_CONTROL] = (HW_CONFIG == 8),                              \
        [MOPT_DMG_PALETTE] = 0,            /* Game Boy */                               \
        [MOPT_BORDER_MODE] = 0,            /* Game Boy */                               \
        [MOPT_RAPID_FIRE_ON_A] = 1,        /* A fires */                                \
        [MOPT_RAPID_FIRE_ON_B] = 1,        /* B fires as well (1942: loop) */           \
        [MOPT_AUTO_INSERT_FDS_DISK_A] = 0, /* Famicom Disk System */                    \
        [MOPT_AUTO_SWAP_FDS_DISK] = 0,     /* Famicom Disk System */                    \
        [MOPT_FDS_DISK_SWAP] = 0,          /* Famicom Disk System */                    \
        [MOPT_OVERCLOCK] = 0,              /* ROM browser only */                       \
        [MOPT_FM_AUDIO] = 0,               /* Master System YM2413 */                   \
        [MOPT_ENTER_BOOTSEL_MODE] = 1,                                                  \
        [MOPT_CONTROLLER_TEST] = 1,                                                     \
        [MOPT_RECENT_GAMES] = 0,  /* ROM browser only */                                \
        [MOPT_USB_DRIVE_MODE] = 0, /* shown regardless: FRENS_FORCE_USB_MSC_IN_SETTINGS */ \
        [MOPT_CASSETTE] = 0,      /* TI-99/4A */                                        \
        [MOPT_DISK] = 0,          /* TI-99/4A */                                        \
        [MOPT_SERIAL_KEYBOARD] = 0, /* TI-99/4A */                                      \
        [MOPT_SPRITE_LIMIT] = 0,  /* NES */                                             \
        [MOPT_MENU_OVERSCAN] = 0, /* shown regardless: listed below the menu colors */  \
        [MOPT_GENESIS_PAD] = 0,   /* Genesis */                                         \
        [MOPT_NES_PALETTE] = 0,   /* NES */                                             \
        [MOPT_HSTX_CLOCK_FIX] = 0, /* only matters at 378 MHz and up */                 \
        [MOPT_BUTTON_LAYOUT] = 0, /* NES */                                             \
        [MOPT_TATE_MODE] = 1,     /* the games are vertical */                          \
    }

int8_t g_settings_visibility_galagino_game[MOPT_COUNT] = GALAGINO_VISIBILITY(1);
int8_t g_settings_visibility_galagino_menu[MOPT_COUNT] = GALAGINO_VISIBILITY(0);

// The 8:7 modes stretch NES pixels; these games have square-ish pixels, so only
// the 1:1 modes are offered.
const uint8_t g_available_screen_modes_galagino[] = {
    0, // SCANLINE_8_7
    0, // NOSCANLINE_8_7
    1, // SCANLINE_1_1
    1, // NOSCANLINE_1_1
};

// ---------------------------------------------------------------------------
// Framebuffer access: the one place the two video back-ends differ.
// Both are a contiguous 16-bit buffer, RGB555 on HSTX and RGB444 on PicoDVI,
// of 320x240 pixels. Except on HSTX with Tate off: there the canvas is as tall
// as the picture, TALL_W x gal_view_height() (288 or 256), and the driver
// scales its rows to the 480 lines of the display (hstx_setFrameSize), so no
// row is left out. The picture sits in the middle, between columns of 16
// pixels that hold the FPS counter. It fits in the same memory.
// ---------------------------------------------------------------------------
#if HSTX
#define TALL_W 256
static_assert(TALL_W * 288 <= SCREENWIDTH * SCREENHEIGHT, "the tall canvas must fit the framebuffer");
#endif
static int fbWidth = SCREENWIDTH; // the canvas, and its stride
static int fbHeight = SCREENHEIGHT;

// Switches between the 320x240 canvas and the tall one. Returns true when it
// changed, which leaves the screen black.
static bool setTallCanvas(bool tall)
{
#if HSTX
    const int w = tall ? TALL_W : SCREENWIDTH;
    const int h = tall ? gal_view_height() : SCREENHEIGHT;
    if (w == fbWidth && h == fbHeight)
        return false;
    // read with the other stride, the old picture would show as noise
    memset(hstx_getframebuffer(), 0, SCREENWIDTH * SCREENHEIGHT * sizeof(uint16_t));
    if (!hstx_setFrameSize(w, h))
        panic("hstx_setFrameSize(%d, %d) failed\n", w, h);
    fbWidth = w;
    fbHeight = h;
    return true;
#else
    (void)tall;
    return false;
#endif
}

static inline uint16_t *fbLine(int line)
{
#if HSTX
    return hstx_getlineFromFramebuffer(line);
#else
    return &Frens::framebuffer[line * SCREENWIDTH];
#endif
}

static inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
#if HSTX
    return (uint16_t)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
#else
    return (uint16_t)(((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
#endif
}

// ---------------------------------------------------------------------------
// Text on the framebuffer, for the error screens and the FPS overlay.
// pico_shared's putText() only works inside its own menus, but the 8x8 font is
// exported.
// ---------------------------------------------------------------------------
static void screenClear(uint16_t bg)
{
    setTallCanvas(false);
    for (int y = 0; y < SCREENHEIGHT; y++)
    {
        uint16_t *dst = fbLine(y);
        for (int x = 0; x < SCREENWIDTH; x++)
            dst[x] = bg;
    }
}

// col/row in 8x8 cells of the canvas (40x30 on the 320x240 one); col < 0
// centres the text.
static void screenText(int col, int row, const char *s, uint16_t fg, uint16_t bg)
{
    const int cols = fbWidth / FONT_CHAR_WIDTH;
    const int len = (int)strlen(s);
    if (col < 0)
        col = (len < cols) ? (cols - len) / 2 : 0;
    for (int i = 0; i < len && col + i < cols; i++)
    {
        char c = s[i];
        if (c < FONT_FIRST_ASCII || c >= FONT_FIRST_ASCII + FONT_N_CHARS)
            c = ' ';
        for (int line = 0; line < FONT_CHAR_HEIGHT; line++)
        {
            uint16_t *dst = fbLine(row * FONT_CHAR_HEIGHT + line) + (col + i) * FONT_CHAR_WIDTH;
            // leftmost pixel in the low bit
            char slice = getcharslicefrom8x8font(c, line);
            for (int bit = 0; bit < FONT_CHAR_WIDTH; bit++, slice >>= 1)
                *dst++ = (slice & 1) ? fg : bg;
        }
    }
}

#define SCR_BG rgb(0, 0, 0)
#define SCR_FG rgb(200, 200, 200)
#define SCR_HI rgb(255, 64, 64)
#define SCR_FILE rgb(255, 255, 255)

static void gameDir(int game, char *buf, size_t n)
{
    snprintf(buf, n, ROMROOT "/%s", gal_games[game].folder);
}

// No SD card, or no complete ROM set.
static void drawNoGamesScreen()
{
    int row = 2;

    screenClear(SCR_BG);
    screenText(-1, row++, "picoGalagino", SCR_HI, SCR_BG);
    row++;
    if (!sdOk)
    {
        screenText(1, row++, "No SD card found.", SCR_HI, SCR_BG);
    }
    else
    {
        screenText(1, row++, "No game found.", SCR_HI, SCR_BG);
        row++;
        screenText(1, row++, "Each game reads its MAME ROM set", SCR_FG, SCR_BG);
        screenText(1, row++, "from its own folder, for example", SCR_FG, SCR_BG);
        screenText(3, row++, ROMROOT "/GALAGA/galaga.zip", SCR_FILE, SCR_BG);
        screenText(1, row++, "as a zip or unzipped. The README", SCR_FG, SCR_BG);
        screenText(1, row++, "lists the folders and zips.", SCR_FG, SCR_BG);
        row++;
        screenText(1, row++, "pico-bootLoader's updateAll fills", SCR_FG, SCR_BG);
        screenText(1, row++, "a card with every set.", SCR_FG, SCR_BG);
    }
    screenText(1, 26, "SELECT+START opens settings, which", SCR_HI, SCR_BG);
    screenText(1, 27, "can enter USB drive mode.", SCR_HI, SCR_BG);
}

// A game whose set looked complete could not be started.
static void drawFailedScreen(int game, uint32_t found)
{
    char buf[48];
    int row = 2;
    const gal_game_info_t *g = &gal_games[game];

    screenClear(SCR_BG);
    snprintf(buf, sizeof(buf), "%s could not be started.", g->title);
    screenText(1, row++, buf, SCR_HI, SCR_BG);
    row++;
    if (!gal_set_complete((gal_game_id_t)game, found))
    {
        screenText(1, row++, "These files could not be read:", SCR_FG, SCR_BG);
        int shown = 0, missing = 0;
        for (int i = 0; i < g->nfiles; i++)
        {
            if (found & (1u << i))
                continue;
            missing++;
            if (shown < 12)
            {
                snprintf(buf, sizeof(buf), "  %s", g->files[i].name);
                screenText(1, row++, buf, SCR_FILE, SCR_BG);
                shown++;
            }
        }
        if (missing > shown)
        {
            snprintf(buf, sizeof(buf), "  ...and %d more", missing - shown);
            screenText(1, row++, buf, SCR_FG, SCR_BG);
        }
    }
    else if (gal_needs_copy((gal_game_id_t)game))
    {
        screenText(1, row++, "It could not be copied into flash.", SCR_FG, SCR_BG);
    }
    else
    {
        screenText(1, row++, "Not enough memory.", SCR_FG, SCR_BG);
    }
    screenText(1, 27, "Press START to return to the menu.", SCR_HI, SCR_BG);
}

// A large game is about to be copied into flash: the board restarts for it.
static void drawCopyScreen(int game)
{
    char buf[48];
    screenClear(SCR_BG);
    snprintf(buf, sizeof(buf), "%s runs from flash.", gal_games[game].title);
    screenText(1, 10, buf, SCR_HI, SCR_BG);
    screenText(1, 12, "Its ROM set is copied there once:", SCR_FG, SCR_BG);
    screenText(1, 13, "the board restarts and copies it,", SCR_FG, SCR_BG);
    screenText(1, 14, "which takes a few seconds.", SCR_FG, SCR_BG);
}

// ---------------------------------------------------------------------------
// ROM loading through FatFs
// ---------------------------------------------------------------------------
static int ffListDir(void *ctx, const char *dir, gal_dir_cb cb, void *arg)
{
    (void)ctx;
    DIR *d = (DIR *)Frens::f_malloc(sizeof(DIR));
    FILINFO *fno = (FILINFO *)Frens::f_malloc(sizeof(FILINFO));
    int ok = 0;
    if (d && fno && f_opendir(d, dir) == FR_OK)
    {
        ok = 1;
        while (f_readdir(d, fno) == FR_OK && fno->fname[0])
            cb(arg, fno->fname, (uint32_t)fno->fsize, (fno->fattrib & AM_DIR) ? 1 : 0);
        f_closedir(d);
    }
    Frens::f_free(fno);
    Frens::f_free(d);
    return ok;
}

static void *ffOpen(void *ctx, const char *path)
{
    (void)ctx;
    FIL *fil = (FIL *)Frens::f_malloc(sizeof(FIL));
    if (fil && f_open(fil, path, FA_READ) == FR_OK)
        return fil;
    Frens::f_free(fil);
    return nullptr;
}

static uint32_t ffSize(void *ctx, void *file)
{
    (void)ctx;
    return (uint32_t)f_size((FIL *)file);
}

static int ffReadAt(void *ctx, void *file, uint32_t ofs, void *buf, uint32_t len)
{
    (void)ctx;
    UINT br = 0;
    if (f_lseek((FIL *)file, ofs) != FR_OK || f_read((FIL *)file, buf, len, &br) != FR_OK)
        return -1;
    return (int)br;
}

static void ffClose(void *ctx, void *file)
{
    (void)ctx;
    f_close((FIL *)file);
    Frens::f_free(file);
}

// The loader's scratch buffers go where f_malloc puts them: PSRAM on the boards
// that have it. There, SRAM is not claimed at boot (see claimHeap() in
// pico_shared) and the display and audio buffers leave little of the heap, so a
// 16 KB loose 1942 file, or a zip entry plus the inflate state, made malloc
// panic. Without PSRAM f_malloc is malloc over the whole claimed heap.
static void *ffAlloc(void *ctx, uint32_t size)
{
    (void)ctx;
    return Frens::f_malloc(size);
}

static void ffFree(void *ctx, void *p)
{
    (void)ctx;
    Frens::f_free(p);
}

static const gal_io_t romIo = {nullptr, ffListDir, ffOpen, ffSize, ffReadAt, ffClose, ffAlloc, ffFree};

// ---------------------------------------------------------------------------
// The flash cache (boards without PSRAM).
//
// A large game runs from flash there; see galagino/gal_flash.c. Its set is
// copied the first time it is started: startGame() notes the game in a watchdog
// scratch register and restarts the board, and initAll() runs prepareFlash()
// once the SD card is mounted, before the display starts, so flash is erased
// and programmed with nothing else running. main() then starts the game at
// once. Copying 1942 takes a few seconds; later starts are immediate.
//
// The cache is the flash pico_shared sets aside for roms on such boards, after
// the program (ROM_FILE_ADDR, maxRomSize). maxRomSize comes from the size the
// flash chip itself reports (storage_get_flash_capacity(), from its JEDEC ID),
// not from PICO_FLASH_SIZE_BYTES, which the board header may set lower than the
// chip; CMakeLists.txt sets PICO_FLASH_SIZE_BYTES to 16 MB so that the SDK's
// flash functions accept every offset of a larger chip.
//
// scratch[0..1]: picoGalagino's own (the SDK uses 4..7 to reboot, pico_shared
// 6 and 7 for pico-bootLoader). They survive the watchdog restart, also when
// pico-bootLoader resumes the application, and are cleared by a power cycle.
// ---------------------------------------------------------------------------
static constexpr uint32_t PENDING_COPY_MAGIC = 0x47414c50u; // "GALP"
static int copiedGame = -1;   // the game copied at this boot, to start at once
static uint32_t copyFound = 0; // its files found
static bool cacheOpen = false;

static int takePendingCopy()
{
    if (watchdog_hw->scratch[0] != PENDING_COPY_MAGIC)
        return -1;
    const uint32_t game = watchdog_hw->scratch[1];
    watchdog_hw->scratch[0] = 0;
    return game < GAL_GAME_COUNT ? (int)game : -1;
}

static void flashErase(uint32_t offset, uint32_t len)
{
    Frens::flashEraseSafe(offset, len);
}

static void flashProgram(uint32_t offset, const uint8_t *data, uint32_t len)
{
    Frens::flashProgramSafe(offset, data, len);
}

static void flashCopying(int game)
{
    static bool led = false;
    led = !led;
    Frens::blinkLed(led);
    printf("Copying %s to flash\n", gal_games[game].title);
}

static void openFlashCache()
{
#if GALAGINO_FLASH_CACHE_TEST
    // pico_shared leaves these at 0 with PSRAM; place them as it does without
    if (Frens::isPsramEnabled())
    {
        const uint32_t exp = Frens::storage_get_flash_jedec_id() & 0xff;
        ROM_FILE_ADDR = FLASHPARAM_ADDRESS + FLASH_SECTOR_SIZE;
        maxRomSize = (int)(XIP_BASE + (exp < 32 ? (1u << exp) : 0) - FLASH_RESERVED_TOP - ROM_FILE_ADDR);
    }
#else
    if (Frens::isPsramEnabled())
        return;
#endif
    if (cacheOpen || !ROM_FILE_ADDR || maxRomSize <= 0)
        return;
    uintptr_t end = ROM_FILE_ADDR + (uint32_t)maxRomSize;
    if (end > XIP_BASE + 16 * 1024 * 1024) // the most XIP reaches
        end = XIP_BASE + 16 * 1024 * 1024;
    gal_flash_t fl;
    fl.base = (const uint8_t *)ROM_FILE_ADDR;
    fl.offset = (uint32_t)(ROM_FILE_ADDR - XIP_BASE);
    fl.size = (uint32_t)(end - ROM_FILE_ADDR);
    fl.erase = flashErase;
    fl.program = flashProgram;
    fl.copying = flashCopying;
    gal_flash_open(&fl);
    cacheOpen = true;
    printf("Flash cache: %lu KB at 0x%08lx, holds %llx\n", (unsigned long)(fl.size / 1024),
           (unsigned long)ROM_FILE_ADDR, (unsigned long long)gal_flash_games());
}

// initAll()'s pre-video-init task: a game waiting to be copied is copied now.
static void prepareFlash()
{
    openFlashCache();
    const int game = takePendingCopy();
    if (game < 0 || !cacheOpen)
        return;
    char dir[48];
    gameDir(game, dir, sizeof(dir));
    const uint64_t t0 = Frens::time_us();
    copyFound = gal_flash_copy((gal_game_id_t)game, &romIo, dir);
    copiedGame = game;
    printf("Copied %s to flash in %lu ms: %s\n", gal_games[game].title,
           (unsigned long)((Frens::time_us() - t0) / 1000), gal_flash_holds((gal_game_id_t)game) ? "ok" : "FAILED");
}

// Restarts the board to copy the game into flash; see above.
static void __attribute__((noreturn)) copyAndRestart(int game)
{
    drawCopyScreen(game);
    sleep_ms(1500); // long enough to read
    watchdog_hw->scratch[1] = (uint32_t)game;
    watchdog_hw->scratch[0] = PENDING_COPY_MAGIC;
    // watchdog_reboot(0,0,0), not watchdog_enable(): pico_shared would take
    // the latter for a ROM picked in its browser, see menu.cpp.
    watchdog_reboot(0, 0, 0);
    while (true)
        tight_loop_contents();
}

// The games that can be played: the complete sets on the card, from the zip
// directories (nothing is loaded), and the games in the flash cache, which need
// no card.
static gal_games_t probeGames()
{
    gal_games_t mask = gal_flash_games();
    char dir[48];
    for (int g = 0; g < GAL_GAME_COUNT; g++)
    {
        if (!gal_supported((gal_game_id_t)g))
            continue;
        if (mask & GAL_GAME_BIT(g))
        {
            printf("%-16s in flash\n", gal_games[g].title);
            continue;
        }
        if (!sdOk)
            continue;
        if (!gal_available((gal_game_id_t)g))
        {
            printf("%-16s needs PSRAM or a larger flash\n", gal_games[g].title);
            continue;
        }
        gameDir(g, dir, sizeof(dir));
        const uint32_t found = gal_probe((gal_game_id_t)g, &romIo, dir);
        const bool complete = gal_set_complete((gal_game_id_t)g, found);
        printf("%-16s %s\n", gal_games[g].title, complete ? "found" : "not found");
        if (complete)
            mask |= GAL_GAME_BIT(g);
    }
    return mask;
}

// ---------------------------------------------------------------------------
// High scores: a file per game, GAMESAVEDIR/GALAGINO/<folder>.HI, the bytes
// the core reports (galagino.h). Read after a start or reset; written when the
// core reports a change (it looks every 5 seconds), and before the game is
// left, reset or the settings menu opens (which can restart the board).
// ---------------------------------------------------------------------------
#define HISCOREDIR GAMESAVEDIR "/GALAGINO"
static uint8_t hiscoreBuf[GAL_HISCORE_MAX];

static void hiscorePath(char *buf, size_t n, int game)
{
    snprintf(buf, n, HISCOREDIR "/%s.HI", gal_games[game].folder);
}

static void hiscoreLoad()
{
    const int game = gal_running();
    const int size = gal_hiscore_size();
    if (game < 0 || !size || !sdOk)
        return;
    char path[48];
    hiscorePath(path, sizeof(path), game);
    FIL *fil = (FIL *)Frens::f_malloc(sizeof(FIL));
    UINT br = 0;
    if (fil && f_open(fil, path, FA_READ) == FR_OK)
    {
        if (f_size(fil) == (FSIZE_t)size && f_read(fil, hiscoreBuf, size, &br) == FR_OK && (int)br == size)
        {
            gal_hiscore_load(hiscoreBuf);
            printf("High scores read from %s\n", path);
        }
        f_close(fil);
    }
    Frens::f_free(fil);
}

static void hiscoreSave()
{
    const int game = gal_running();
    const int size = gal_hiscore_size();
    if (game < 0 || !size || !sdOk)
        return;
    f_mkdir(GAMESAVEDIR);
    f_mkdir(HISCOREDIR);
    char path[48];
    hiscorePath(path, sizeof(path), game);
    FIL *fil = (FIL *)Frens::f_malloc(sizeof(FIL));
    UINT bw = 0;
    if (fil && f_open(fil, path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)
    {
        f_write(fil, hiscoreBuf, size, &bw);
        f_close(fil);
    }
    Frens::f_free(fil);
    printf("High scores %s %s\n", (int)bw == size ? "saved to" : "NOT saved to", path);
}

// Before the game is left or reset: the table as it is now.
static void hiscoreFlush()
{
    if (gal_running() >= 0 && gal_hiscore_flush(hiscoreBuf))
        hiscoreSave();
}

static void enterMenu()
{
    gal_stop();
    if (!installed)
    {
        // after a game that could not be copied into flash, say
        state = State::NoGames;
        drawNoGamesScreen();
        return;
    }
    gal_menu_init(installed);
    state = State::Menu;
}

static void startGame(int game)
{
    if (gal_needs_copy((gal_game_id_t)game) && gal_flash_room((gal_game_id_t)game) && sdOk)
        copyAndRestart(game);
    char dir[48];
    gameDir(game, dir, sizeof(dir));
    printf("Starting %s from %s\n", gal_games[game].title, dir);
    const uint64_t t0 = Frens::time_us();
    const uint32_t found = gal_start((gal_game_id_t)game, &romIo, dir);
    printf("  %s in %lu ms, arena %lu of %lu bytes, PSRAM %lu of %lu bytes\n",
           gal_running() == game ? "started" : "FAILED", (unsigned long)((Frens::time_us() - t0) / 1000),
           (unsigned long)gal_arena_peak(), (unsigned long)gal_arena_size(), (unsigned long)gal_psram_peak(),
           (unsigned long)gal_psram_size());
    if (gal_running() == game)
    {
        hiscoreLoad();
        state = State::Game;
        return;
    }
    drawFailedScreen(game, found);
    state = State::Failed;
}

// ---------------------------------------------------------------------------
// Audio: one frame of mono samples to whichever sink is active.
//
// The machine renders a frame's 735 samples in one go, after the frame has
// run. Pushed as a burst into a sink with nothing queued in front of it, the
// sink runs dry just before the next burst arrives and plays a short gap on
// every frame (PicoDVI fills it with silence, the I2S DMA stops): a buzz at
// the frame rate. So the PicoDVI and I2S rings keep a cushion of
// AUDIO_CUSHION samples queued in front of each frame's burst:
//   - when the sink has drained (at start-up, after the settings menu, after a
//     slow frame), the cushion is refilled with silence, once;
//   - when the level has drifted more than AUDIO_TRIM_BAND away from the
//     cushion, the frame pushes one sample less or one more. That matches the
//     sink's clock to the emulated one: one sample per frame is 1360 ppm, far
//     more than the two clocks differ.
// The HSTX data-island queue plays its packets on the clock that also paces
// the frames (see paceFrame()), so it needs no trim. It drops what goes over
// its high watermark of 800 samples, little more than a frame, so it only gets
// a small cushion: HSTX_CUSHION samples when it has run dry.
//
// A frame's sound is pushed at the start of the next frame, right after the
// pace, not when the machine has made it: the time the machine takes varies
// from frame to frame, the start of a frame does not. That keeps the bursts a
// frame apart, which the small HSTX cushion relies on.
//
// Runs from SRAM (noinline, or it would be folded into the flash-resident
// caller). From pico-mooncresta.
// ---------------------------------------------------------------------------
#define AUDIO_CUSHION 512   // ~12 ms
#define AUDIO_TRIM_BAND 192 // drift tolerated before a frame is trimmed
#define HSTX_CUSHION 32     // ~0.7 ms, 8 packets

static inline int16_t applyGain(int x, int gainQ8)
{
    int32_t v = (x * gainQ8) >> 8;
    if (v > 32767)
        v = 32767;
    else if (v < -32768)
        v = -32768;
    return (int16_t)v;
}

// The two ring sinks: the I2S DAC (ext) or, on PicoDVI, the HDMI audio ring.
static inline int ringQueued(bool ext)
{
#if EXT_AUDIO_IS_ENABLED
    if (ext)
        return I2S_AUDIO_RING_SIZE - 1 - EXT_AUDIO_GET_FREE();
#endif
#if !HSTX
    return (int)dvi_->getAudioRingBuffer().getFullReadableSize();
#else
    return 0;
#endif
}

static inline void ringPut(bool ext, int16_t s)
{
#if EXT_AUDIO_IS_ENABLED
    if (ext)
    {
        EXT_AUDIO_ENQUEUE_SAMPLE(s, s);
        return;
    }
#endif
#if !HSTX
    auto &ring = dvi_->getAudioRingBuffer();
    if (ring.getWritableSize() == 0)
        return; // full: dropped
    *ring.getWritePointer() = {s, s};
    ring.advanceWritePointer(1);
#endif
}

static void __noinline __not_in_flash_func(pushAudio)(const int16_t *buf, int n)
{
    const bool mute = !settings.flags.audioEnabled;

#if EXT_AUDIO_IS_ENABLED
#if HSTX
    const bool ext = settings.flags.useExtAudio || Frens::isHeadPhoneJackConnected();
#else
    const bool ext = settings.flags.useExtAudio;
#endif
#else
    const bool ext = false;
#endif

#if HSTX
    if (!ext)
    {
        if (hstx_di_queue_get_level() == 0)
        {
            for (int i = 0; i < HSTX_CUSHION; i++)
                hstx_push_audio_sample(0, 0);
        }
        for (int i = 0; i < n; i++)
        {
            int16_t s = mute ? 0 : buf[i];
#if ENABLE_VU_METER
            if (settings.flags.enableVUMeter)
                addSampleToVUMeter(s);
#endif
            int16_t g = applyGain(s, DVI_AUDIO_GAIN_Q8);
            hstx_push_audio_sample(g, g);
        }
        return;
    }
#endif

    // the cushion and the drift trim, see above
    const int queued = ringQueued(ext);
    int repeat = 0;
    if (queued < AUDIO_CUSHION / 4)
    {
        for (int i = queued; i < AUDIO_CUSHION; i++)
            ringPut(ext, 0);
    }
    else if (queued > AUDIO_CUSHION + AUDIO_TRIM_BAND)
    {
        n--;
    }
    else if (queued < AUDIO_CUSHION - AUDIO_TRIM_BAND)
    {
        repeat = 1;
    }

    int16_t s = 0;
    for (int i = 0; i < n; i++)
    {
        s = mute ? 0 : buf[i];
#if ENABLE_VU_METER
        if (settings.flags.enableVUMeter)
            addSampleToVUMeter(s);
#endif
        ringPut(ext, applyGain(s, ext ? EXT_AUDIO_GAIN_Q8 : DVI_AUDIO_GAIN_Q8));
    }
    if (repeat)
        ringPut(ext, applyGain(s, ext ? EXT_AUDIO_GAIN_Q8 : DVI_AUDIO_GAIN_Q8));
}

// ---------------------------------------------------------------------------
// Video: draw the frame the machine finished last (or the menu), plus the FPS
// digits. Called right after the frame pace returns, i.e. at the start of
// output VBLANK, so the single-buffered framebuffer is rewritten ahead of
// scan-out.
//
// The borders are painted only when they need it: at the first frame, when the
// orientation changes, after the settings menu or an error screen has drawn
// over the screen, and when the FPS counter is switched off. The counter lives
// in the border, so it is overwritten in place every frame instead of being
// cleared and redrawn.
// ---------------------------------------------------------------------------
static bool bordersValid = false;
static int bordersOrient = -1;
static bool fpsShown = false;

#if GALAGINO_FRAME_STATS
#include "hardware/structs/m33.h"
#include "hardware/clocks.h"

// gal_internal.h's profiling: cycles per part of a frame, from the DWT cycle
// counter
extern "C" uint32_t gal_prof[8];
extern "C" uint32_t gal_cycles(void)
{
    return m33_hw->dwt_cyccnt;
}

static void statsStartCounter()
{
    m33_hw->demcr |= M33_DEMCR_TRCENA_BITS;
    m33_hw->dwt_ctrl |= M33_DWT_CTRL_CYCCNTENA_BITS;
}

static struct
{
    uint32_t frames, emu_sum, emu_max, draw_sum, draw_max;
    uint32_t idle_sum, idle_min, late; // paceFrame(): the wait for the display
} stats = {0, 0, 0, 0, 0, 0, UINT32_MAX, 0};

static void statsAdd(uint32_t emu, uint32_t draw)
{
    stats.emu_sum += emu;
    stats.draw_sum += draw;
    if (emu > stats.emu_max)
        stats.emu_max = emu;
    if (draw > stats.draw_max)
        stats.draw_max = draw;
    if (++stats.frames == 300)
    {
        printf("frame stats: emulate %lu us (max %lu), draw %lu us (max %lu), %lu fps\n",
               (unsigned long)(stats.emu_sum / stats.frames), (unsigned long)stats.emu_max,
               (unsigned long)(stats.draw_sum / stats.frames), (unsigned long)stats.draw_max, (unsigned long)fps);
        // the parts, in us a frame: run_slice, sound, then the game's own
        const uint32_t mhz = clock_get_hz(clk_sys) / 1000000;
        printf("  idle us: avg %lu min %lu, late frames %lu\n", (unsigned long)(stats.idle_sum / stats.frames),
               (unsigned long)stats.idle_min, (unsigned long)stats.late);
        printf("  parts us:");
        for (int i = 0; i < 8; i++)
        {
            printf(" %lu", (unsigned long)(gal_prof[i] / mhz / stats.frames));
            gal_prof[i] = 0;
        }
        printf("\n");
        memset(&stats, 0, sizeof(stats));
        stats.idle_min = UINT32_MAX;
    }
}
#endif

static uint32_t lastDrawUs = 0;

static void __noinline __not_in_flash_func(presentFrame)()
{
    if (state != State::Menu && state != State::Game)
        return;
    const uint64_t t0 = Frens::time_us();
    gal_orient_t orient = (gal_orient_t)settings.flags.tateMode;
    const bool showFps = settings.flags.displayFrameRate;

    // Tate off on HSTX: the tall canvas. Also follows the height of the picture
    // from one game to the next.
    if (setTallCanvas(HSTX && orient == GAL_ORIENT_UPRIGHT))
        bordersValid = false;
    if (fbWidth != SCREENWIDTH)
        orient = GAL_ORIENT_UPRIGHT_FULL;

    bool repaint = false;
    if (!bordersValid || orient != bordersOrient || (fpsShown && !showFps))
    {
        gal_render_borders(orient, fbLine(0), fbWidth);
        bordersValid = true;
        bordersOrient = orient;
        repaint = true;
    }
    fpsShown = showFps;

    if (state == State::Menu)
        gal_menu_render(orient, fbLine(0), fbWidth, repaint);
    else
        gal_render(orient, fbLine(0), fbWidth);

    if (showFps)
    {
        char s[3] = {(char)('0' + (fps / 10) % 10), (char)('0' + fps % 10), 0};
        // In the left border: one cell in from the corner a TV that overscans
        // cuts off when upright (48 px), at its edge in tate (16 px) and in the
        // 16-px column of the tall canvas (64 px in from the screen edge).
        screenText(orient == GAL_ORIENT_UPRIGHT ? 1 : 0, 1, s, rgb(255, 255, 255), rgb(0, 0, 0));
    }
    lastDrawUs = (uint32_t)(Frens::time_us() - t0);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

// One GPIO port's pad in io::GamePadState button bits; see pico-outrun's
// nespadGameBits() for why the pad type is read as "SNES or not".
static uint32_t nespadGameBits(uint16_t n, uint8_t type)
{
    typedef io::GamePadState::Button B;
    uint32_t b = 0;
    if (n & (1u << 2)) b |= B::SELECT;
    if (n & (1u << 3)) b |= B::START;
    if (n & (1u << 4)) b |= B::UP;
    if (n & (1u << 5)) b |= B::DOWN;
    if (n & (1u << 6)) b |= B::LEFT;
    if (n & (1u << 7)) b |= B::RIGHT;
    if (type != NESPAD_TYPE_SNES)
    {
        if (n & (1u << 0)) b |= B::A;
        if (n & (1u << 1)) b |= B::B;
    }
    else
    {
        if (n & (1u << 0)) b |= B::B;
        if (n & (1u << 1)) b |= B::Y;
        if (n & (1u << 8)) b |= B::A;
        if (n & (1u << 9)) b |= B::X;
    }
    return b;
}

// Every pad source merged into one: the games are played by one player at a
// time (two players take turns on the same controls).
static uint32_t readPads(uint16_t wii, uint32_t *pad2)
{
    typedef io::GamePadState::Button B;
    uint32_t b = 0;
    auto &gp = io::getCurrentGamePadState(0);
    if (gp.connected)
        b |= gp.buttons;
    auto &gp2 = io::getCurrentGamePadState(1);
    *pad2 = gp2.connected ? gp2.buttons : 0;
#if NES_PIN_CLK != -1
    b |= nespadGameBits(nespad_states_ext[0], nespad_padtype[0]);
#endif
#if NES_PIN_CLK_1 != -1
    b |= nespadGameBits(nespad_states_ext[1], nespad_padtype[1]);
#endif
    if (wii & (1u << 0)) b |= B::A;
    if (wii & (1u << 1)) b |= B::B;
    if (wii & (1u << 2)) b |= B::SELECT;
    if (wii & (1u << 3)) b |= B::START;
    if (wii & (1u << 4)) b |= B::UP;
    if (wii & (1u << 5)) b |= B::DOWN;
    if (wii & (1u << 6)) b |= B::LEFT;
    if (wii & (1u << 7)) b |= B::RIGHT;
    if (wii & (1u << 8)) b |= B::X;
    if (wii & (1u << 9)) b |= B::Y;
    return b;
}

// Maps the pad onto the cabinet:
//   d-pad       joystick        A (or X)  fire       B (or Y)  fire, or the game's
//                                                              second button (1942: loop)
//   SELECT      coin            START     1 player   START on a second USB pad: 2 players
//
// A and B both press fire, each with its own rapid fire setting, so one of them
// can fire single shots and the other automatically. In a game with a second
// button of its own (gal_has_extra_button: 1942's loop), B is that instead.
//
// SELECT doubles as the modifier of SELECT+START (settings menu), so the coin
// is inserted when SELECT is released, and only if no other button was pressed
// while it was held. A coin switch is a pulse; it is held for a few frames.
//
// START + A toggles the frame rate display, as in the sibling emulators. While
// START is held, A therefore does not fire.
static bool selectUsedAsModifier = false;
static uint32_t prevButtons = 0;
static int coinFrames = 0;

// Forget the buttons seen before the settings menu opened: its SELECT+START
// never reaches mapInputs(), so the SELECT release afterwards would otherwise
// insert a coin.
static void resetInputs()
{
    selectUsedAsModifier = false;
    prevButtons = 0;
    coinFrames = 0;
}

static uint16_t mapInputs(uint32_t buttons, uint32_t pad2)
{
    typedef io::GamePadState::Button B;
    static uint32_t frame = 0;
    frame++;
    const uint32_t pushed = buttons & ~prevButtons;

    if ((buttons & B::START) && (pushed & B::A))
        settings.flags.displayFrameRate = !settings.flags.displayFrameRate;

    if (buttons & B::SELECT)
    {
        if (buttons & ~(uint32_t)B::SELECT)
            selectUsedAsModifier = true;
    }
    else if (prevButtons & B::SELECT)
    {
        if (!selectUsedAsModifier)
            coinFrames = 6;
        selectUsedAsModifier = false;
    }
    prevButtons = buttons;

    uint16_t in = 0;
    if (coinFrames > 0)
    {
        coinFrames--;
        in |= GAL_BTN_COIN;
    }
    if (buttons & B::SELECT)
        return in; // a modifier combination, not game input

    if (buttons & B::START)
        in |= GAL_BTN_START;
    if (pad2 & B::START)
        in |= GAL_BTN_START2;
    if (buttons & B::LEFT)
        in |= GAL_BTN_LEFT;
    if (buttons & B::RIGHT)
        in |= GAL_BTN_RIGHT;
    if (buttons & B::UP)
        in |= GAL_BTN_UP;
    if (buttons & B::DOWN)
        in |= GAL_BTN_DOWN;

    // Rapid fire: 4 frames pressed, 4 released.
    const bool rapidPhase = (frame & 4) != 0;
    if ((buttons & B::X) || ((buttons & B::A) && !(buttons & B::START)))
    {
        if (!settings.flags.rapidFireOnA || rapidPhase)
            in |= GAL_BTN_FIRE;
    }
    if (buttons & (B::B | B::Y))
    {
        if (gal_running() >= 0 && gal_has_extra_button((gal_game_id_t)gal_running()))
            in |= GAL_BTN_EXTRA;
        else if (!settings.flags.rapidFireOnB || rapidPhase)
            in |= GAL_BTN_FIRE;
    }
    return in;
}

// ---------------------------------------------------------------------------
// Wii Classic controller (I2C).
//
// initAll() tries it once at boot, except on WIIPAD_DELAYED_START boards
// without a TLV320 DAC (Murmulator M2), where pico_shared leaves it to its
// menus, and this game never shows the ROM browser. A pad plugged in after
// boot is not picked up on any board either. So it is started here before the
// game, and retried about once a second while no pad answers. A retry without
// a pad costs about 0.3 ms; finding one stalls once for 200 ms (the pad's own
// init delays). From pico-mooncresta.
// ---------------------------------------------------------------------------
#if WII_PIN_SDA >= 0 and WII_PIN_SCL >= 0
static void wiipadPoll()
{
    static int frames = 0;
    if (wiipad_is_connected() || ++frames < 60)
        return;
    frames = 0;
    wiipad_begin();
}
#endif

// ---------------------------------------------------------------------------
// Frame pacing: one game frame per displayed frame, from the display's frame
// counter.
//
// The sound is made at 735 samples a frame, so this is what keeps it at 44.1
// kHz: the HDMI audio packets are clocked from the same 60 Hz display.
// pico_shared's PaceFrames60fps() does not fit either back-end: on HSTX it
// paces at the PC Engine's 59.83 Hz, 0.3% fewer samples than HDMI plays and
// more than the I2S trim in pushAudio() can make up, so both ran dry every few
// seconds; on PicoDVI it busy-waits for core1's vsync flag, which core0 misses
// about half the time.
//
// A frame that overruns a displayed frame is caught up by starting the next
// one at once, so no sound is lost; one more than a frame behind, the missed
// frames are dropped.
// ---------------------------------------------------------------------------
static inline uint32_t displayFrameCounter()
{
#if HSTX
    return hstx_getframecounter();
#else
    return dvi_->getFrameCounter();
#endif
}

static void paceFrame(bool init)
{
    static uint32_t target = 0;
    if (init)
    {
        target = displayFrameCounter();
        return;
    }
    target++;
    const int32_t lag = (int32_t)(displayFrameCounter() - target);
#if GALAGINO_FRAME_STATS
    const uint64_t w0 = Frens::time_us();
#endif
    if (lag < 0)
    {
        while ((int32_t)(displayFrameCounter() - target) < 0)
            __compiler_memory_barrier(); // the counter is advanced by core1
    }
    else if (lag > 0)
    {
        target = displayFrameCounter();
#if GALAGINO_FRAME_STATS
        stats.late++;
#endif
    }
#if GALAGINO_FRAME_STATS
    const uint32_t idle = (uint32_t)(Frens::time_us() - w0);
    stats.idle_sum += idle;
    if (idle < stats.idle_min)
        stats.idle_min = idle;
#endif
}

// ---------------------------------------------------------------------------
// Once per frame
// ---------------------------------------------------------------------------
static void processPerFrame()
{
    paceFrame(false);

    // the sound of the frame made last time round; see pushAudio()
    if (audioPending)
    {
        pushAudio(audioBuf, SAMPLES_PER_FRAME);
        audioPending = false;
    }

    presentFrame();

    Frens::pollHeadPhoneJack();

#if ENABLE_VU_METER
    // Fruit Jam Button 2 toggles the VU meter, as in the sibling emulators.
    if (isVUMeterToggleButtonPressed())
    {
        settings.flags.enableVUMeter = !settings.flags.enableVUMeter;
        FrensSettings::savesettings();
        turnOffAllLeds();
    }
#endif

    nespad_read_start();
#if HSTX
    uint32_t count = hstx_getframecounter();
#else
    uint32_t count = dvi_->getFrameCounter();
#endif
    Frens::blinkLed((count >> 5) & 1);
    nespad_read_finish();

    tuh_task();
    uint16_t wii = 0;
#if WII_PIN_SDA >= 0 and WII_PIN_SCL >= 0
    wiipadPoll();
    wii = wiipad_read(); // boards without the Wii port do not link wiipad at all
#endif

    uint32_t pad2 = 0;
    uint32_t buttons = readPads(wii, &pad2);

    if ((buttons & io::GamePadState::Button::SELECT) && (buttons & io::GamePadState::Button::START))
        showSettings = true;

    if (showSettings)
    {
        showSettings = false;
        resetInputs();
        hiscoreFlush(); // the settings menu can restart the board
        g_settings_visibility =
            (state == State::Game) ? g_settings_visibility_galagino_game : g_settings_visibility_galagino_menu;
        setTallCanvas(false); // the settings menu draws on the 320x240 canvas
        int rval = showSettingsMenu(true);

        // menu.cpp saves only down the SAVE path; `settings` only ever holds
        // committed values, so writing again can never persist a cancelled edit.
        FrensSettings::savesettings();
        scaleMode8_7_ = Frens::applyScreenMode(settings.screenMode);
        EXT_AUDIO_SETVOLUME(settings.fruitjamVolumeLevel);

        // Without a game there is nothing to return to. Reboot, so a set just
        // copied onto the card in USB drive mode is picked up.
        if (state == State::NoGames)
        {
            printf("Rebooting to look for the ROM sets again...\n");
            watchdog_reboot(0, 0, 0);
            while (true)
                tight_loop_contents();
        }
        if (state == State::Game)
        {
            if (rval == 3) // Quit game: back to the game menu
                enterMenu();
            else if (rval == 5) // Reset Game
            {
                gal_reset();
                hiscoreLoad();
            }
        }
        else if (state == State::Failed)
        {
            enterMenu(); // the settings menu covered the reason; go back
        }
        bordersValid = false; // the menu drew over the whole screen
        paceFrame(true);
        lastFrameUs = Frens::time_us();
        return;
    }

    uint16_t in = mapInputs(buttons, pad2);
#ifdef GALAGINO_AUTOSTART
    {
        // profiling builds: a coin after 15 s (the longest boot, Xevious's, is
        // over by then), a start, then fire and steer
        static uint32_t frame;
        frame++;
        if (frame >= 900 && frame < 905)
            in |= GAL_BTN_COIN;
        if (frame >= 1000 && frame < 1005)
            in |= GAL_BTN_START;
        if (frame >= 1100)
        {
            if (frame & 8)
                in |= GAL_BTN_FIRE;
            in |= ((frame / 64) & 1) ? GAL_BTN_LEFT : GAL_BTN_RIGHT;
            if ((frame / 128) & 1)
                in |= GAL_BTN_UP;
        }
    }
#endif
    switch (state)
    {
    case State::Game:
    {
        const uint64_t t0 = Frens::time_us();
        gal_run_frame(in, audioBuf, SAMPLES_PER_FRAME);
#if GALAGINO_FRAME_STATS
        statsAdd((uint32_t)(Frens::time_us() - t0), lastDrawUs);
#else
        (void)t0;
#endif
        audioPending = true;
        if (gal_hiscore_poll(hiscoreBuf))
            hiscoreSave();
        break;
    }
    case State::Menu:
    {
        memset(audioBuf, 0, sizeof(audioBuf));
        audioPending = true;
        const int game = gal_menu_frame(in);
        if (game >= 0)
        {
            // a start without any button held is the menu's attract timeout
            if (!in)
                printf("Attract mode: %s\n", gal_games[game].title);
            startGame(game);
            bordersValid = false;
            paceFrame(true);
        }
        break;
    }
    case State::Failed:
        if (in & GAL_BTN_START)
        {
            enterMenu();
            bordersValid = false;
        }
        break;
    case State::NoGames:
        break;
    }

    if (settings.flags.displayFrameRate)
    {
        uint64_t now = Frens::time_us();
        uint64_t dt = now - lastFrameUs;
        lastFrameUs = now;
        if (dt > 0)
            fps = (uint32_t)((1000000 + dt / 2) / dt);
    }
}

int main()
{
    Frens::setClocksAndStartStdio(CPUFreqKHz, VREG_VOLTAGE_1_20);

    printf("==========================================================================================\n");
    printf("picoGalagino %s\n", SWVERSION);
    printf("Build date: %s %s\n", __DATE__, __TIME__);
    printf("HW_CONFIG=%d  HSTX=%d  CPU freq: %lu kHz\n", HW_CONFIG, HSTX, (unsigned long)(clock_get_hz(clk_sys) / 1000));
    printf("==========================================================================================\n");

#if GALAGINO_FRAME_STATS
    statsStartCounter();
#endif
    FrensSettings::initSettings(FrensSettings::ARCADE);
    gal_init(HSTX ? GAL_FMT_RGB555 : GAL_FMT_RGB444, SAMPLERATE);

    // No ROM is ever selected through the browser; the sets are read below.
    // A large game waiting to be copied into flash is copied inside initAll(),
    // before the display starts (see "The flash cache").
    char dummyRom[FF_MAX_LFN];
    dummyRom[0] = 0;
    Frens::setPreVideoInitTask(prepareFlash);
    sdOk = Frens::initAll(dummyRom, CPUFreqKHz, 0, 0, AUDIOBUFFERSIZE, false, true);
    Frens::setPreVideoInitTask(nullptr);
    takePendingCopy(); // without a card it was not taken: forget it

    if (sdOk)
    {
        // loadsettings() inside initAll resets every setting when
        // settings.currentDir does not exist. There is no ROM browser to
        // create it, so make it (FR_EXIST later on is fine) and load again.
        // The game folders are not made: with this many games they would only
        // litter the card. The README and updateAll name them.
        f_mkdir("/roms");
        f_mkdir(ROMROOT);
        FrensSettings::loadsettings();
    }
    // All arcade games share /settings_ARC.dat and with it currentDir. Keep it
    // at /roms/arcade, which every arcade game creates: a game's own folder
    // would make the next game reset its settings when that folder is missing.
    strcpy(settings.currentDir, ROMROOT);
    g_settings_visibility = g_settings_visibility_galagino_menu;
    g_available_screen_modes = g_available_screen_modes_galagino;
    if (!g_available_screen_modes[static_cast<int>(settings.screenMode)])
        settings.screenMode = ScreenMode::NOSCANLINE_1_1;
    scaleMode8_7_ = Frens::applyScreenMode(settings.screenMode);
    // Apply the saved DAC volume now; otherwise it only takes effect after the
    // settings menu has been opened and closed. No-op without a TLV320.
    EXT_AUDIO_SETVOLUME(settings.fruitjamVolumeLevel);

#if WII_PIN_SDA >= 0 and WII_PIN_SCL >= 0
    // after initAll(), so after the DAC: see wiipadPoll()
    if (!wiipad_is_connected())
        wiipad_begin();
#endif

    // The large games (1942) do not fit in SRAM; they run from a block of
    // PSRAM on the boards that have it, and from the flash cache on the others.
    if (Frens::isPsramEnabled() && !GALAGINO_FLASH_CACHE_TEST)
        gal_set_psram(Frens::f_malloc(GAL_PSRAM_SIZE), GAL_PSRAM_SIZE);
    openFlashCache(); // without a card initAll() did not; the games in it still play
    installed = probeGames();
    if (copiedGame >= 0)
    {
        // just copied into flash: started straight away, as it was chosen
        if (gal_flash_holds((gal_game_id_t)copiedGame))
            startGame(copiedGame);
        else
        {
            drawFailedScreen(copiedGame, copyFound);
            state = State::Failed;
        }
    }
#ifdef GALAGINO_AUTOSTART // profiling builds: the game at once (see CMakeLists.txt)
    else if (installed)
        startGame(GALAGINO_AUTOSTART);
#endif
    else if (installed)
        enterMenu();
    else
    {
        state = State::NoGames;
        drawNoGamesScreen();
    }

    paceFrame(true);
    lastFrameUs = Frens::time_us();
    while (true)
        processPerFrame();
}
