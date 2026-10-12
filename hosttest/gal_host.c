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
 *
 * Every dumped frame is outdir/frame_NNNNN.ppm (320x240, the canvas the board
 * shows; with --tall the 640x480 the HSTX driver sends). A summary line per
 * second reports the time spent per frame.
 */
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "galagino.h"
#include "hstx_scale.h"

#define SAMPLERATE 44100
#define SAMPLES_PER_FRAME 735
#define MAX_KEYS 64
#define TALL_W 256 /* the tall canvas of main.cpp */

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

/* --- output helpers ------------------------------------------------------ */

static void write_ppm(const char *path, const uint16_t *fb)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n320 240\n255\n");
    for (int i = 0; i < 320 * 240; i++)
    {
        uint16_t p = fb[i];
        uint8_t rgb[3] = {(uint8_t)(((p >> 11) & 0x1f) * 255 / 31), (uint8_t)(((p >> 5) & 0x3f) * 255 / 63),
                          (uint8_t)((p & 0x1f) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

/* --tall: a TALL_W x rows RGB555 canvas as the HSTX driver sends it, 640x480:
 * the rows scaled to the lines, the pixels doubled between black borders. */
static void write_ppm_tall(const char *path, const uint16_t *fb, int rows)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n640 480\n255\n");
    const int pairs = TALL_W / 2, border = (640 - 2 * TALL_W) / 2;
    for (uint32_t line = 0; line < 480; line++)
    {
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
        {
            const uint16_t p = (x < border || x >= 640 - border) ? 0 : px[(x - border) / 2];
            uint8_t rgb[3] = {(uint8_t)(((p >> 10) & 0x1f) * 255 / 31), (uint8_t)(((p >> 5) & 0x1f) * 255 / 31),
                              (uint8_t)((p & 0x1f) * 255 / 31)};
            fwrite(rgb, 1, 3, f);
        }
    }
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
    if (argc < 5)
    {
        fprintf(stderr, "usage: %s <romroot> <game|menu> <frames> <dump-every-N> [outdir] [options]\n", argv[0]);
        return 1;
    }
    const char *romroot = argv[1];
    const int menu_mode = !strcmp(argv[2], "menu");
    const int game = menu_mode ? -1 : game_by_name(argv[2]);
    const int frames = atoi(argv[3]);
    const int every = atoi(argv[4]);
    const char *outdir = "hosttest/out";
    int tate = 0, tall = 0, coin_at = -1, start_at = -1, play_at = -1, no_psram = 0;
    const char *flash_path = NULL, *hiscore_dir = NULL;
    int key_at[MAX_KEYS], key_mask[MAX_KEYS], nkeys = 0;
    const char *wav_path = NULL;
    int a = 5;
    if (argc > 5 && argv[5][0] != '-')
        outdir = argv[a++];
    for (; a < argc; a++)
    {
        if (!strcmp(argv[a], "--tate") && a + 1 < argc)
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
    if (!menu_mode && (game < 0 || game >= GAL_GAME_COUNT))
    {
        fprintf(stderr, "unknown game %s\n", argv[2]);
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
        gal_menu_init(installed);
    else
    {
        snprintf(dir, sizeof(dir), "%s/%s", romroot, gal_games[game].folder);
        if (gal_needs_copy((gal_game_id_t)game) && gal_flash_room((gal_game_id_t)game))
        {
            /* the board restarts and copies the set before its display starts */
            double t0 = now_s();
            flash_erased = flash_programmed = 0;
            gal_flash_copy((gal_game_id_t)game, &io, dir);
            printf("flash cache: %s, %u KB erased, %u KB programmed, %d errors, %.0f ms, holds %llx\n",
                   gal_flash_holds((gal_game_id_t)game) ? "copied" : "COPY FAILED", flash_erased / 1024,
                   flash_programmed / 1024, flash_errors, (now_s() - t0) * 1000,
                   (unsigned long long)gal_flash_games());
            save_flash(flash_path);
        }
        double t0 = now_s();
        uint32_t found = gal_start((gal_game_id_t)game, &io, dir);
        printf("%s: start %s in %.1f ms, arena %u of %u bytes, PSRAM %u of %u bytes\n", gal_games[game].title,
               gal_running() == game ? "ok" : "FAILED", (now_s() - t0) * 1000, gal_arena_peak(), gal_arena_size(),
               gal_psram_peak(), gal_psram_size());
        if (gal_running() != game)
        {
            for (int i = 0; i < gal_games[game].nfiles; i++)
                if (!(found & (1u << i)))
                    printf("  missing: %s (crc %08x)\n", gal_games[game].files[i].name, gal_games[game].files[i].crc);
            return 2;
        }
        if (hiscore_dir && gal_hiscore_size())
        {
            char path[1024];
            uint8_t saved[GAL_HISCORE_MAX];
            hiscore_path(path, sizeof(path), hiscore_dir, game);
            FILE *f = fopen(path, "rb");
            if (f)
            {
                if ((int)fread(saved, 1, sizeof(saved), f) == gal_hiscore_size())
                {
                    gal_hiscore_load(saved);
                    printf("high scores: read %s\n", path);
                }
                fclose(f);
            }
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

    static uint16_t fb[320 * 240];
    int16_t audio[SAMPLES_PER_FRAME];
    uint32_t total_samples = 0;
    double t_emu = 0, t_render = 0, t_emu_max = 0, t_render_max = 0;
    int peak = 0;

    const gal_orient_t orient = tall ? GAL_ORIENT_UPRIGHT_FULL : (gal_orient_t)tate;
    const int stride = tall ? TALL_W : 320;
    gal_render_borders(orient, fb, stride);
    for (int f = 0; f < frames; f++)
    {
        uint16_t in = 0;
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
        if (menu_mode)
        {
            int g = gal_menu_frame(in);
            if (g >= 0)
                printf("frame %d: menu starts %s\n", f, gal_games[g].title);
            memset(audio, 0, sizeof(audio));
        }
        else
        {
            gal_run_frame(in, audio, SAMPLES_PER_FRAME);
            uint8_t table[GAL_HISCORE_MAX];
            if (hiscore_dir && gal_hiscore_poll(table))
                hiscore_save(hiscore_dir, game, table);
        }
        double t1 = now_s();
        if (menu_mode)
            gal_menu_render(orient, fb, stride, f == 0);
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

        if (every > 0 && (f % every) == 0)
        {
            char path[1100];
            snprintf(path, sizeof(path), "%s/frame_%05d.ppm", outdir, f);
            if (tall)
                write_ppm_tall(path, fb, gal_view_height());
            else
                write_ppm(path, fb);
        }
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
    uint8_t table[GAL_HISCORE_MAX];
    if (!menu_mode && hiscore_dir && gal_hiscore_flush(table))
        hiscore_save(hiscore_dir, game, table);
    return 0;
}
