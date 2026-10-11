/*
 * picoGalagino - Galagino (Pac-Man, Galaga, Donkey Kong, Frogger, Dig Dug,
 * 1942) for RP2350 boards.
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
 *   - on a board without PSRAM the sets are copied into flash at boot, before
 *     the display starts, once: the games then start from flash. That is also
 *     what lets 1942, too big for SRAM, run without PSRAM. See copyRomsToFlash().
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
static uint32_t installed = 0;  // bit = gal_game_id_t, the complete sets
static uint32_t flashGames = 0; // the games in the flash store
static bool flashSynced = false;
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
    char buf[48];
    int row = 2;

    screenClear(SCR_BG);
    screenText(-1, row++, "picoGalagino", SCR_HI, SCR_BG);
    row++;
    if (!sdOk)
    {
        screenText(1, row++, "No SD card found.", SCR_HI, SCR_BG);
        row = 20;
    }
    else
    {
        screenText(1, row++, "No game found. Copy a MAME zip, as it", SCR_HI, SCR_BG);
        screenText(1, row++, "is or unzipped, to its folder:", SCR_HI, SCR_BG);
        row++;
        for (int g = 0; g < GAL_GAME_COUNT; g++)
        {
            if (!gal_supported((gal_game_id_t)g))
                continue;
            snprintf(buf, sizeof(buf), "%-12s %s.zip", gal_games[g].title, gal_games[g].mameset);
            screenText(2, row++, buf, SCR_FG, SCR_BG);
            gameDir(g, buf, sizeof(buf));
            screenText(4, row++, buf, SCR_FILE, SCR_BG);
        }
        row++;
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
    if (gal_set_complete((gal_game_id_t)game, found))
    {
        screenText(1, row++, "Not enough memory.", SCR_FG, SCR_BG);
    }
    else
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
    screenText(1, 27, "Press START to return to the menu.", SCR_HI, SCR_BG);
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
// The flash store (boards without PSRAM).
//
// initAll() runs copyRomsToFlash() once the SD card is mounted, before the
// display starts, so flash is erased and programmed with nothing else running.
// It copies the complete sets that are not in flash yet, and leaves the ones
// that are; see galagino/gal_flash.c. The store is the flash pico_shared sets
// aside for roms on such boards, after the program (ROM_FILE_ADDR).
//
// Copying all six games writes about 530 KB; the first boot with new sets takes
// a few seconds longer, with the LED blinking. Later boots only check the CRCs.
// ---------------------------------------------------------------------------
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

static bool flashStore(gal_flash_t *fl)
{
    if (Frens::isPsramEnabled() || !ROM_FILE_ADDR || maxRomSize <= 0)
        return false;
    fl->base = (const uint8_t *)ROM_FILE_ADDR;
    fl->offset = (uint32_t)(ROM_FILE_ADDR - XIP_BASE);
    fl->size = (uint32_t)maxRomSize;
    fl->erase = flashErase;
    fl->program = flashProgram;
    fl->copying = flashCopying;
    return true;
}

static void copyRomsToFlash()
{
    gal_flash_t fl;
    if (!flashStore(&fl))
        return;
    const uint64_t t0 = Frens::time_us();
    flashGames = gal_flash_sync(&fl, &romIo, ROMROOT);
    flashSynced = true;
    printf("Flash store: games %02lx in %lu ms (%lu of %lu KB)\n", (unsigned long)flashGames,
           (unsigned long)((Frens::time_us() - t0) / 1000), (unsigned long)(gal_flash_size_needed() / 1024),
           (unsigned long)(fl.size / 1024));
}

// The complete sets on the card, from the zip directories: nothing is loaded.
// The games in the flash store need no card.
static uint32_t probeGames()
{
    uint32_t mask = flashGames;
    char dir[48];
    for (int g = 0; g < GAL_GAME_COUNT; g++)
    {
        if (flashGames & (1u << g))
        {
            printf("%-12s in flash\n", gal_games[g].title);
            continue;
        }
        if (!sdOk)
            continue;
        if (!gal_available((gal_game_id_t)g))
        {
            // 1942 without PSRAM runs from the flash store only; its set was
            // not found when the store was filled at boot
            printf("%-12s not found\n", gal_games[g].title);
            continue;
        }
        gameDir(g, dir, sizeof(dir));
        const uint32_t found = gal_probe((gal_game_id_t)g, &romIo, dir);
        const bool complete = gal_set_complete((gal_game_id_t)g, found);
        printf("%-12s %s\n", gal_games[g].title, complete ? "found" : "not found");
        if (complete)
            mask |= 1u << g;
    }
    return mask;
}

static void enterMenu()
{
    gal_stop();
    gal_menu_init(installed);
    state = State::Menu;
}

static void startGame(int game)
{
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
static struct
{
    uint32_t frames, emu_sum, emu_max, draw_sum, draw_max;
} stats;

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
        memset(&stats, 0, sizeof(stats));
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
//   d-pad       joystick        A (or X)  fire       B (or Y)  fire (1942: loop)
//   SELECT      coin            START     1 player   START on a second USB pad: 2 players
//
// A and B both press fire, each with its own rapid fire setting, so one of them
// can fire single shots and the other automatically. In 1942, B is the loop
// button instead.
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
        if (gal_running() == GAL_GAME_1942)
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
    if (lag < 0)
    {
        while ((int32_t)(displayFrameCounter() - target) < 0)
            __compiler_memory_barrier(); // the counter is advanced by core1
    }
    else if (lag > 0)
    {
        target = displayFrameCounter();
    }
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
                gal_reset();
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

    const uint16_t in = mapInputs(buttons, pad2);
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
        break;
    }
    case State::Menu:
    {
        memset(audioBuf, 0, sizeof(audioBuf));
        audioPending = true;
        const int game = gal_menu_frame(in);
        if (game >= 0)
        {
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

    FrensSettings::initSettings(FrensSettings::ARCADE);
    gal_init(HSTX ? GAL_FMT_RGB555 : GAL_FMT_RGB444, SAMPLERATE);

    // No ROM is ever selected through the browser; the sets are read below.
    // Without PSRAM they are copied to flash first, inside initAll().
    char dummyRom[FF_MAX_LFN];
    dummyRom[0] = 0;
    Frens::setPreVideoInitTask(copyRomsToFlash);
    sdOk = Frens::initAll(dummyRom, CPUFreqKHz, 0, 0, AUDIOBUFFERSIZE, false, true);
    Frens::setPreVideoInitTask(nullptr);

    if (sdOk)
    {
        // loadsettings() inside initAll resets every setting when
        // settings.currentDir does not exist. There is no ROM browser to
        // create it, so make it (FR_EXIST later on is fine) and load again.
        // The game folders are made too, so the user sees where the sets go.
        f_mkdir("/roms");
        f_mkdir(ROMROOT);
        char dir[48];
        for (int g = 0; g < GAL_GAME_COUNT; g++)
        {
            gameDir(g, dir, sizeof(dir));
            f_mkdir(dir);
        }
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

    // 1942 does not fit in SRAM; it runs from a block of PSRAM, on the boards
    // that have it, and from the flash store on the others.
    if (Frens::isPsramEnabled())
        gal_set_psram(Frens::f_malloc(GAL_PSRAM_SIZE), GAL_PSRAM_SIZE);
    if (!flashSynced)
    {
        // no SD card: the games already in flash can still be played
        gal_flash_t fl;
        if (flashStore(&fl))
            flashGames = gal_flash_sync(&fl, nullptr, ROMROOT);
    }
    installed = probeGames();
    if (installed)
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
