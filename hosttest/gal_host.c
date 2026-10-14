/*
 * Host harness for the Galagino core: loads a game through the same loader as
 * the firmware, runs it, dumps frames as PPM and the audio as WAV.
 *
 *   ./hosttest/gal_host <romroot> <game|menu> <frames> <dump-every-N> [outdir] [options]
 *
 * <romroot> holds the game folders as on the SD card (PACMAN, GALAGA, ...);
 * <game> is a folder name or a number (0 Pac-Man .. 5 1942). "menu" shows the
 * game menu with the games found under <romroot>.
 *
 * Options:
 *   --tate 0|1|2   Tate mode: 0 Off (default), 1 Bottom left, 2 Bottom right
 *   --tall         Tate off as HSTX boards show it: every row of the picture,
 *                  scaled to 480 lines by the driver's code (hstx_scale.h)
 *   --wav FILE     write the audio (mono, 16-bit, 44.1 kHz)
 *   --coin F       insert a coin at frame F (held for 4 frames)
 *   --start F      press 1P start at frame F (held for 4 frames)
 *   --play F       from frame F: fire every 16 frames, steer around
 *   --keys F:MASK  hold buttons MASK (GAL_BTN_*, hex) from frame F for 4 frames
 *   --no-psram     run as a board without PSRAM (the large games, 1942, then
 *                  need --flash)
 *   --flash FILE   a flash cache, kept in FILE between runs: a large game not
 *                  in it is copied into it first, as the board does after its
 *                  restart, and starts from it. The simulated flash checks
 *                  alignment and only clears bits when programmed, as NOR
 *                  flash does.
 *   --flash-kb N   the size of that flash (default 2048)
 *   --hiscore DIR  high scores: read DIR/<FOLDER>.HI at the start, written
 *                  when the game reports a change and at the end
 *   --select GAME  menu: the game selected at the start, as after quitting it
 *   --attract-now  menu: attract mode's pick at once, as when an attract game
 *                  has run its time (otherwise after 20 seconds, frame 1200)
 *   --seed N       srand(N) for that pick (the board seeds from its RNG)
 *   --rotate N     menu: attract mode as on the board. The game the menu picks
 *                  is started and, while no button is pressed, runs N seconds;
 *                  then the menu returns and picks the next one
 *                  (GALAGINO_ATTRACT_SECONDS)
 *   --video FILE   every frame, as raw RGB24 (320x240, with --tall 640x480),
 *                  for ffmpeg -f rawvideo -pix_fmt rgb24 -s WxH -r 60 -i FILE
 *
 * Every dumped frame is outdir/frame_NNNNN.ppm (320x240, the canvas the board
 * shows; with --tall the 640x480 the HSTX driver sends). A summary line per
 * second reports the time spent per frame.
 *
 * Live mode (built with SDL2, see build.sh):
 *
 *   ./hosttest/gal_host <romroot> [game|menu] [frames] [dump-every-N] [outdir] --live [options]
 *
 * shows the frames in a window, plays the sound and reads the keyboard, in
 * real time. The game defaults to the menu, which starts the game picked, as
 * on the board; attract mode runs each game 180 seconds unless --rotate says
 * otherwise. Without a frame count it runs until the window is closed; frames
 * are dumped with F12, and every Nth when N is given. Scripted input is ORed
 * with the keyboard, and --wav and --video record what was played.
 *   --scale N      window size, N times the picture (default 3, with --tall 2)
 *   --mute         no sound output; paced on a timer
 * The keys are those of a USB keyboard on the board: arrows joystick, Z (or C)
 * fire, X (or V) fire or the game's second button, A coin (when released),
 * S start; also MAME's 5 coin, 1 start, 2 two player start. A+S leaves a game
 * for the menu, as SELECT+START and Quit game do on the board. Space pauses,
 * N steps a frame while paused, Tab (held) fast-forwards, F12 dumps the frame
 * on screen, Esc quits. With --tate 1 or 2 the window shows the picture
 * upright; the dumps keep the canvas as it is.
 */
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "galagino.h"
#include "hstx_scale.h"
#if HOST_SDL
#include "host_sdl.h"
#endif

#define SAMPLERATE 44100
#define SAMPLES_PER_FRAME 735
#define MAX_KEYS 64
#define TALL_W 256 /* the tall canvas of main.cpp */
#define ATTRACT_SECONDS 180 /* main.cpp's GALAGINO_ATTRACT_SECONDS */

/* --- stdio implementation of gal_io_t ----------------------------------- */

static int host_list_dir(void *ctx, const char *dir, gal_dir_cb cb, void *arg)
{
    (void)ctx;
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
    {
        if (e->d_name[0] == '.')
            continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(path, &st) != 0)
            continue;
        cb(arg, e->d_name, (uint32_t)st.st_size, S_ISDIR(st.st_mode));
    }
    closedir(d);
    return 1;
}

static void *host_open(void *ctx, const char *path)
{
    (void)ctx;
    return fopen(path, "rb");
}

static uint32_t host_size(void *ctx, void *f)
{
    (void)ctx;
    long cur = ftell((FILE *)f);
    fseek((FILE *)f, 0, SEEK_END);
    long n = ftell((FILE *)f);
    fseek((FILE *)f, cur, SEEK_SET);
    return (uint32_t)n;
}

static int host_read_at(void *ctx, void *f, uint32_t ofs, void *buf, uint32_t len)
{
    (void)ctx;
    if (fseek((FILE *)f, ofs, SEEK_SET) != 0)
        return -1;
    return (int)fread(buf, 1, len, (FILE *)f);
}

static void host_close(void *ctx, void *f)
{
    (void)ctx;
    fclose((FILE *)f);
}

static const gal_io_t io = {NULL, host_list_dir, host_open, host_size, host_read_at, host_close, NULL, NULL};

/* --- a simulated flash for the store ---------------------------------------- */

static uint32_t FLASH_SIZE = 2 * 1024 * 1024;
static uint8_t *flash_mem;
static uint32_t flash_erased, flash_programmed;
static int flash_errors;

static void sim_erase(uint32_t ofs, uint32_t len)
{
    if ((ofs | len) % 4096 || ofs + len > FLASH_SIZE)
    {
        flash_errors++;
        return;
    }
    memset(flash_mem + ofs, 0xff, len);
    flash_erased += len;
}

static void sim_program(uint32_t ofs, const uint8_t *data, uint32_t len)
{
    if ((ofs | len) % 256 || ofs + len > FLASH_SIZE)
    {
        flash_errors++;
        return;
    }
    for (uint32_t i = 0; i < len; i++)
        flash_mem[ofs + i] &= data[i]; /* NOR: programming only clears bits */
    flash_programmed += len;
}

static void sim_copying(int game)
{
    printf("flash cache: copying %s\n", gal_games[game].title);
}

static void save_flash(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (f)
    {
        fwrite(flash_mem, 1, FLASH_SIZE, f);
        fclose(f);
    }
}

/* --- high scores ------------------------------------------------------------ */

static void hiscore_path(char *out, size_t n, const char *dir, int game)
{
    snprintf(out, n, "%s/%s.HI", dir, gal_games[game].folder);
}

static void hiscore_save(const char *dir, int game, const uint8_t *data)
{
    char path[1024];
    hiscore_path(path, sizeof(path), dir, game);
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fwrite(data, 1, gal_hiscore_size(), f);
    fclose(f);
    printf("high scores: wrote %s (%d bytes)\n", path, gal_hiscore_size());
}

static void hiscore_load(const char *dir, int game)
{
    if (!gal_hiscore_size())
        return;
    char path[1024];
    uint8_t saved[GAL_HISCORE_MAX];
    hiscore_path(path, sizeof(path), dir, game);
    FILE *f = fopen(path, "rb");
    if (!f)
        return;
    if ((int)fread(saved, 1, sizeof(saved), f) == gal_hiscore_size())
    {
        gal_hiscore_load(saved);
        printf("high scores: read %s\n", path);
    }
    fclose(f);
}

/* --- output helpers ------------------------------------------------------ */

static uint32_t rgb565(uint16_t p)
{
    return (uint32_t)(((p >> 11) & 0x1f) * 255 / 31) << 16 | (uint32_t)(((p >> 5) & 0x3f) * 255 / 63) << 8 |
           (uint32_t)((p & 0x1f) * 255 / 31);
}

static uint32_t rgb555(uint16_t p)
{
    return (uint32_t)(((p >> 10) & 0x1f) * 255 / 31) << 16 | (uint32_t)(((p >> 5) & 0x1f) * 255 / 31) << 8 |
           (uint32_t)((p & 0x1f) * 255 / 31);
}

/* --tall: a line of the TALL_W x rows RGB555 canvas as the HSTX driver sends
 * it, 640 wide: the rows scaled to 480 lines, the pixels doubled between black
 * borders. */
static void tall_line(const uint16_t *fb, int rows, uint32_t line, uint32_t *out)
{
    const int pairs = TALL_W / 2, border = (640 - 2 * TALL_W) / 2;
    uint32_t q, a[TALL_W / 2], b[TALL_W / 2];
    uint16_t px[TALL_W];
    const uint32_t row = hstx_scale_row(line, (uint32_t)rows, 480, &q);
    memcpy(a, fb + row * TALL_W, sizeof(a));
    if (q)
    {
        memcpy(b, fb + (row + 1) * TALL_W, sizeof(b));
        hstx_scale_blend(a, a, b, pairs, q);
    }
    memcpy(px, a, sizeof(px));
    for (int x = 0; x < 640; x++)
        out[x] = (x < border || x >= 640 - border) ? 0 : rgb555(px[(x - border) / 2]);
}

/* The picture as 0xRRGGBB: the 320x240 RGB565 canvas, or with --tall the
 * 640x480 the HSTX driver makes of its canvas */
static void to_rgb32(const uint16_t *fb, int tall, uint32_t *out)
{
    if (tall)
        for (uint32_t line = 0; line < 480; line++)
            tall_line(fb, gal_view_height(), line, out + line * 640);
    else
        for (int i = 0; i < 320 * 240; i++)
            out[i] = rgb565(fb[i]);
}

/* The picture as RGB24 */
static void put_rgb(FILE *f, const uint16_t *fb, int tall)
{
    static uint32_t px[640 * 480];
    static uint8_t rgb[640 * 480 * 3];
    const int n = tall ? 640 * 480 : 320 * 240;
    to_rgb32(fb, tall, px);
    for (int i = 0; i < n; i++)
    {
        rgb[3 * i] = (uint8_t)(px[i] >> 16);
        rgb[3 * i + 1] = (uint8_t)(px[i] >> 8);
        rgb[3 * i + 2] = (uint8_t)px[i];
    }
    fwrite(rgb, 3, n, f);
}

static void dump_frame(const char *outdir, int frame, const uint16_t *fb, int tall)
{
    char path[1100];
    snprintf(path, sizeof(path), "%s/frame_%05d.ppm", outdir, frame);
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n%d %d\n255\n", tall ? 640 : 320, tall ? 480 : 240);
    put_rgb(f, fb, tall);
    fclose(f);
}

static void put16(FILE *f, uint16_t v)
{
    fputc(v & 0xff, f);
    fputc(v >> 8, f);
}

static void put32(FILE *f, uint32_t v)
{
    put16(f, v & 0xffff);
    put16(f, v >> 16);
}

static void wav_header(FILE *f, uint32_t samples)
{
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + samples * 2);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, 1);
    put16(f, 1);
    put32(f, SAMPLERATE);
    put32(f, SAMPLERATE * 2);
    put16(f, 2);
    put16(f, 16);
    fwrite("data", 1, 4, f);
    put32(f, samples * 2);
}

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* Starts a game as the board does: a large game that is not in the flash cache
 * is copied into it first (the board restarts for that), and its high scores
 * are read. Returns the files found; gal_running() tells whether it started. */
static uint32_t start_game(int game, const char *romroot, const char *flash_path, const char *hiscore_dir)
{
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s/%s", romroot, gal_games[game].folder);
    if (gal_needs_copy((gal_game_id_t)game) && gal_flash_room((gal_game_id_t)game))
    {
        /* the board restarts and copies the set before its display starts */
        double t0 = now_s();
        flash_erased = flash_programmed = 0;
        gal_flash_copy((gal_game_id_t)game, &io, dir);
        printf("flash cache: %s, %u KB erased, %u KB programmed, %d errors, %.0f ms, holds %llx\n",
               gal_flash_holds((gal_game_id_t)game) ? "copied" : "COPY FAILED", flash_erased / 1024,
               flash_programmed / 1024, flash_errors, (now_s() - t0) * 1000, (unsigned long long)gal_flash_games());
        save_flash(flash_path);
    }
    double t0 = now_s();
    uint32_t found = gal_start((gal_game_id_t)game, &io, dir);
    printf("%s: start %s in %.1f ms, arena %u of %u bytes, PSRAM %u of %u bytes\n", gal_games[game].title,
           gal_running() == game ? "ok" : "FAILED", (now_s() - t0) * 1000, gal_arena_peak(), gal_arena_size(),
           gal_psram_peak(), gal_psram_size());
    if (gal_running() == game && hiscore_dir)
        hiscore_load(hiscore_dir, game);
    return found;
}

/* Back to the game menu, the game just played selected, as main.cpp's
 * enterMenu(). gal_menu_init stops the game. */
static void leave_game(int game, gal_games_t installed, const char *hiscore_dir)
{
    uint8_t table[GAL_HISCORE_MAX];
    if (hiscore_dir && gal_hiscore_flush(table))
        hiscore_save(hiscore_dir, game, table);
    gal_menu_init(installed, game);
}

#if HOST_SDL
/* --- live mode: the keyboard as the pad of the cabinet --------------------- */

/* main.cpp's mapInputs() without rapid fire and the START + A frame rate
 * toggle: d-pad joystick, A (or X) fire, B (or Y) fire or the game's second
 * button (1942's loop), START 1 player start. SELECT inserts a coin when it is
 * released, unless another button was pressed while it was held; SELECT +
 * START leaves a game (*leave), as the settings menu's Quit game does on the
 * board. After that the keys count again once all are released, or a START
 * still held would start the game selected in the menu. */
static uint32_t live_prev;
static int live_select_used, live_coin, live_wait_release;

static uint16_t live_input(uint32_t pad, int *leave)
{
    const uint32_t combo = HSDL_SELECT | HSDL_START;
    if ((pad & combo) == combo && (live_prev & combo) != combo)
    {
        *leave = 1;
        live_wait_release = 1;
    }
    if (pad & HSDL_SELECT)
    {
        if (pad & ~(uint32_t)HSDL_SELECT)
            live_select_used = 1;
    }
    else if (live_prev & HSDL_SELECT)
    {
        if (!live_select_used)
            live_coin = 6; /* a coin switch is a pulse */
        live_select_used = 0;
    }
    if ((pad & HSDL_COIN) && !(live_prev & HSDL_COIN))
        live_coin = 6;
    live_prev = pad;
    if (live_wait_release)
    {
        live_wait_release = pad != 0;
        live_coin = 0;
        return 0;
    }

    uint16_t in = 0;
    if (live_coin > 0)
    {
        live_coin--;
        in |= GAL_BTN_COIN;
    }
    if (pad & HSDL_SELECT)
        return in; /* a modifier combination, not game input */
    if (pad & HSDL_START)
        in |= GAL_BTN_START;
    if (pad & HSDL_START2)
        in |= GAL_BTN_START2;
    if (pad & HSDL_LEFT)
        in |= GAL_BTN_LEFT;
    if (pad & HSDL_RIGHT)
        in |= GAL_BTN_RIGHT;
    if (pad & HSDL_UP)
        in |= GAL_BTN_UP;
    if (pad & HSDL_DOWN)
        in |= GAL_BTN_DOWN;
    if (pad & (HSDL_A | HSDL_X))
        in |= GAL_BTN_FIRE;
    if (pad & (HSDL_B | HSDL_Y))
    {
        if (gal_running() >= 0 && gal_has_extra_button((gal_game_id_t)gal_running()))
            in |= GAL_BTN_EXTRA;
        else
            in |= GAL_BTN_FIRE;
    }
    return in;
}
#endif

static int game_by_name(const char *s)
{
    for (int g = 0; g < GAL_GAME_COUNT; g++)
        if (!strcasecmp(s, gal_games[g].folder))
            return g;
    if (isdigit((unsigned char)s[0]))
        return atoi(s);
    return -1;
}

/* --- main ---------------------------------------------------------------- */

int main(int argc, char **argv)
{
    /* the positional arguments come before the options */
    const char *pos[5] = {NULL};
    int npos = 0, a = 1;
    for (; a < argc && argv[a][0] != '-'; a++)
    {
        if (npos == 5)
        {
            fprintf(stderr, "unexpected argument %s\n", argv[a]);
            return 1;
        }
        pos[npos++] = argv[a];
    }
    int tate = 0, tall = 0, coin_at = -1, start_at = -1, play_at = -1, no_psram = 0, select = -1;
    int attract_now = 0, rotate = -1, live = 0, scale = 0, mute = 0;
    const char *video_path = NULL;
    const char *flash_path = NULL, *hiscore_dir = NULL;
    int key_at[MAX_KEYS], key_mask[MAX_KEYS], nkeys = 0;
    const char *wav_path = NULL;
    for (; a < argc; a++)
    {
        if (!strcmp(argv[a], "--live"))
            live = 1;
        else if (!strcmp(argv[a], "--scale") && a + 1 < argc)
            scale = atoi(argv[++a]);
        else if (!strcmp(argv[a], "--mute"))
            mute = 1;
        else if (!strcmp(argv[a], "--tate") && a + 1 < argc)
            tate = atoi(argv[++a]);
        else if (!strcmp(argv[a], "--tall"))
            tall = 1;
        else if (!strcmp(argv[a], "--wav") && a + 1 < argc)
            wav_path = argv[++a];
        else if (!strcmp(argv[a], "--coin") && a + 1 < argc)
            coin_at = atoi(argv[++a]);
        else if (!strcmp(argv[a], "--start") && a + 1 < argc)
            start_at = atoi(argv[++a]);
        else if (!strcmp(argv[a], "--play") && a + 1 < argc)
            play_at = atoi(argv[++a]);
        else if (!strcmp(argv[a], "--no-psram"))
            no_psram = 1;
        else if (!strcmp(argv[a], "--flash") && a + 1 < argc)
            flash_path = argv[++a];
        else if (!strcmp(argv[a], "--flash-kb") && a + 1 < argc)
            FLASH_SIZE = (uint32_t)atoi(argv[++a]) * 1024;
        else if (!strcmp(argv[a], "--hiscore") && a + 1 < argc)
            hiscore_dir = argv[++a];
        else if (!strcmp(argv[a], "--select") && a + 1 < argc)
            select = game_by_name(argv[++a]);
        else if (!strcmp(argv[a], "--attract-now"))
            attract_now = 1;
        else if (!strcmp(argv[a], "--rotate") && a + 1 < argc)
            rotate = atoi(argv[++a]);
        else if (!strcmp(argv[a], "--video") && a + 1 < argc)
            video_path = argv[++a];
        else if (!strcmp(argv[a], "--seed") && a + 1 < argc)
            srand((unsigned)atoi(argv[++a]));
        else if (!strcmp(argv[a], "--keys") && a + 1 < argc && nkeys < MAX_KEYS)
        {
            if (sscanf(argv[++a], "%d:%x", &key_at[nkeys], &key_mask[nkeys]) == 2)
                nkeys++;
        }
        else
        {
            fprintf(stderr, "unknown option %s\n", argv[a]);
            return 1;
        }
    }
#if !HOST_SDL
    (void)scale;
    (void)mute;
    if (live)
    {
        fprintf(stderr,
                "%s: built without SDL2, so --live is not available.\n"
                "Install libsdl2-dev and rerun hosttest/build.sh.\n",
                argv[0]);
        return 1;
    }
#endif
    if (npos < (live ? 1 : 4))
    {
        fprintf(stderr,
                "usage: %s <romroot> <game|menu> <frames> <dump-every-N> [outdir] [options]\n"
                "       %s <romroot> [game|menu] [frames] [dump-every-N] [outdir] --live [options]\n",
                argv[0], argv[0]);
        return 1;
    }
    const char *romroot = pos[0];
    const int menu_mode = !pos[1] || !strcmp(pos[1], "menu");
    const int game = menu_mode ? -1 : game_by_name(pos[1]);
    /* live: until the window is closed, frames dumped with F12 */
    const int frames = pos[2] ? atoi(pos[2]) : INT_MAX;
    const int every = pos[3] ? atoi(pos[3]) : 0;
    const char *outdir = pos[4] ? pos[4] : "hosttest/out";
    /* live: attract mode runs as on the board */
    if (rotate < 0)
        rotate = live ? ATTRACT_SECONDS : 0;
    if (!menu_mode && (game < 0 || game >= GAL_GAME_COUNT))
    {
        fprintf(stderr, "unknown game %s\n", pos[1]);
        return 1;
    }

    /* --tall blends RGB555, as the HSTX driver does */
    gal_init(tall ? GAL_FMT_RGB555 : GAL_FMT_RGB565, SAMPLERATE);
    /* the board's PSRAM block */
    if (!no_psram)
        gal_set_psram(malloc(GAL_PSRAM_SIZE), GAL_PSRAM_SIZE);

    if (flash_path)
    {
        flash_mem = malloc(FLASH_SIZE);
        memset(flash_mem, 0xff, FLASH_SIZE);
        FILE *f = fopen(flash_path, "rb");
        if (f)
        {
            if (fread(flash_mem, 1, FLASH_SIZE, f) != FLASH_SIZE)
                printf("flash store: %s is short, the rest reads erased\n", flash_path);
            fclose(f);
        }
        const gal_flash_t fl = {flash_mem, 0, FLASH_SIZE, sim_erase, sim_program, sim_copying};
        gal_flash_open(&fl);
        printf("flash cache: %u KB, holds %llx\n", FLASH_SIZE / 1024, (unsigned long long)gal_flash_games());
    }

    char dir[1024];
    gal_games_t installed = 0;
    for (int g = 0; g < GAL_GAME_COUNT; g++)
    {
        snprintf(dir, sizeof(dir), "%s/%s", romroot, gal_games[g].folder);
        uint32_t found = gal_probe((gal_game_id_t)g, &io, dir);
        int n = 0;
        for (int i = 0; i < gal_games[g].nfiles; i++)
            n += (found >> i) & 1;
        printf("%-16s %2d of %2d files%s%s\n", gal_games[g].title, n, gal_games[g].nfiles,
               gal_set_complete((gal_game_id_t)g, found) ? ", complete" : "",
               !gal_supported((gal_game_id_t)g)   ? " (not supported yet)"
               : !gal_available((gal_game_id_t)g) ? " (needs PSRAM or flash)"
               : gal_needs_copy((gal_game_id_t)g) ? " (to be copied to flash)"
                                                  : "");
        if (gal_set_complete((gal_game_id_t)g, found))
            installed |= GAL_GAME_BIT(g);
    }

    if (menu_mode)
    {
        gal_menu_init(installed, select);
        if (attract_now)
            gal_menu_attract_now();
    }
    else
    {
        uint32_t found = start_game(game, romroot, flash_path, hiscore_dir);
        if (gal_running() != game)
        {
            for (int i = 0; i < gal_games[game].nfiles; i++)
                if (!(found & (1u << i)))
                    printf("  missing: %s (crc %08x)\n", gal_games[game].files[i].name, gal_games[game].files[i].crc);
            return 2;
        }
    }

    mkdir(outdir, 0755);

    FILE *wav = NULL;
    if (wav_path)
    {
        wav = fopen(wav_path, "wb");
        if (wav)
            wav_header(wav, 0);
    }
    FILE *video = video_path ? fopen(video_path, "wb") : NULL;

    static uint16_t fb[320 * 240];
    int16_t audio[SAMPLES_PER_FRAME];
    uint32_t total_samples = 0;
    double t_emu = 0, t_render = 0, t_emu_max = 0, t_render_max = 0;
    int peak = 0;

    const gal_orient_t orient = tall ? GAL_ORIENT_UPRIGHT_FULL : (gal_orient_t)tate;
    const int stride = tall ? TALL_W : 320;
    gal_render_borders(orient, fb, stride);

#if HOST_SDL
    static uint32_t px[640 * 480];
    if (live)
    {
        const int turn = tall ? 0 : tate == GAL_ORIENT_TATE_CW ? 90 : tate == GAL_ORIENT_TATE_CCW ? 270 : 0;
        if (!hsdl_init(tall ? 640 : 320, tall ? 480 : 240, turn, scale ? scale : tall ? 2 : 3, !mute, 16667))
            return 1;
    }
#endif

    /* the running game, or -1 for the menu, and whether attract mode started it */
    int in_game = menu_mode ? -1 : game, attract = 0, game_frames = 0;
    uint32_t pad = 0; /* live: the keyboard */
    int quit = 0;
    for (int f = 0; f < frames && !quit; f++)
    {
        int repaint = 0, leave = 0;
        uint16_t in = 0;
#if HOST_SDL
        if (live)
            in |= live_input(pad, &leave);
#endif
        if (coin_at >= 0 && f >= coin_at && f < coin_at + 4)
            in |= GAL_BTN_COIN;
        if (start_at >= 0 && f >= start_at && f < start_at + 4)
            in |= GAL_BTN_START;
        for (int k = 0; k < nkeys; k++)
            if (f >= key_at[k] && f < key_at[k] + 4)
                in |= (uint16_t)key_mask[k];
        if (play_at >= 0 && f >= play_at)
        {
            static const uint16_t dirs[4] = {GAL_BTN_LEFT, GAL_BTN_UP, GAL_BTN_RIGHT, GAL_BTN_DOWN};
            int p = f - play_at;
            if ((p & 15) < 4)
                in |= GAL_BTN_FIRE;
            in |= dirs[(p / 90) & 3];
        }

        double t0 = now_s();
        if (in_game >= 0 && (in || pad))
            attract = 0; /* a player took over */
        if (in_game >= 0 && leave)
        {
            printf("frame %d: back to the menu\n", f);
            leave_game(in_game, installed, hiscore_dir);
            in_game = -1;
            repaint = 1;
            memset(audio, 0, sizeof(audio));
        }
        else if (in_game < 0)
        {
            int g = gal_menu_frame(in);
            if (g >= 0)
            {
                printf("frame %d: menu starts %s\n", f, gal_games[g].title);
                /* without --rotate or --live the menu stays */
                if (rotate > 0 || live)
                {
                    start_game(g, romroot, flash_path, hiscore_dir);
                    attract = !in;
                    game_frames = 0;
                    if (gal_running() == g)
                        in_game = g;
                    else
                    {
                        gal_menu_init(installed, g);
                        if (attract)
                            gal_menu_attract_now();
                    }
                    repaint = 1;
                }
            }
            memset(audio, 0, sizeof(audio));
        }
        else if (attract && rotate > 0 && ++game_frames > rotate * 60)
        {
            printf("frame %d: attract mode: next game\n", f);
            leave_game(in_game, installed, hiscore_dir);
            gal_menu_attract_now();
            in_game = -1;
            repaint = 1;
            memset(audio, 0, sizeof(audio));
        }
        else
        {
            gal_run_frame(in, audio, SAMPLES_PER_FRAME);
            uint8_t table[GAL_HISCORE_MAX];
            if (hiscore_dir && gal_hiscore_poll(table))
                hiscore_save(hiscore_dir, in_game, table);
        }
        double t1 = now_s();
        if (repaint)
        {
            /* the canvas changes height with --tall, as on the board */
            memset(fb, 0, sizeof(fb));
            gal_render_borders(orient, fb, stride);
        }
        if (in_game < 0)
            gal_menu_render(orient, fb, stride, f == 0 || repaint);
        else
            gal_render(orient, fb, stride);
        double t2 = now_s();
        t_emu += t1 - t0;
        t_render += t2 - t1;
        if (t1 - t0 > t_emu_max)
            t_emu_max = t1 - t0;
        if (t2 - t1 > t_render_max)
            t_render_max = t2 - t1;

        for (int i = 0; i < SAMPLES_PER_FRAME; i++)
        {
            int v = audio[i] < 0 ? -audio[i] : audio[i];
            if (v > peak)
                peak = v;
        }
        if (wav)
        {
            fwrite(audio, sizeof(int16_t), SAMPLES_PER_FRAME, wav);
            total_samples += SAMPLES_PER_FRAME;
        }

        if (video)
            put_rgb(video, fb, tall);
        if (every > 0 && (f % every) == 0)
            dump_frame(outdir, f, fb, tall);

#if HOST_SDL
        if (live)
        {
            to_rgb32(fb, tall, px);
            hsdl_present(px);
            for (;;)
            {
                int ev = 0;
                pad = hsdl_poll(&ev);
                if (ev & HSDL_EV_DUMP)
                {
                    dump_frame(outdir, f, fb, tall);
                    printf("LIVE: frame %d dumped to %s/frame_%05d.ppm\n", f, outdir, f);
                }
                if (ev & HSDL_EV_QUIT)
                {
                    quit = 1;
                    break;
                }
                hsdl_status(in_game >= 0 ? gal_games[in_game].title : "game menu", f);
                if (!hsdl_paused() || (ev & HSDL_EV_STEP))
                    break;
                hsdl_idle();
            }
            if (!quit)
            {
                hsdl_audio(audio, SAMPLES_PER_FRAME);
                hsdl_pace();
            }
        }
#endif
        if ((f + 1) % 60 == 0)
        {
            printf("frame %5d  emu %.3f ms (max %.3f)  render %.3f ms (max %.3f)  audio peak %d\n", f + 1,
                   t_emu * 1000 / 60, t_emu_max * 1000, t_render * 1000 / 60, t_render_max * 1000, peak);
            t_emu = t_render = t_emu_max = t_render_max = 0;
            peak = 0;
        }
    }

    if (wav)
    {
        wav_header(wav, total_samples);
        fclose(wav);
    }
    if (video)
        fclose(video);
    uint8_t table[GAL_HISCORE_MAX];
    if (in_game >= 0 && hiscore_dir && gal_hiscore_flush(table))
        hiscore_save(hiscore_dir, in_game, table);
#if HOST_SDL
    if (live)
        hsdl_quit();
#endif
    return 0;
}
