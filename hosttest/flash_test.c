/*
 * Tests the flash cache (galagino/gal_flash.c) on its own: fake large games of
 * chosen sizes in a simulated NOR flash. Placement from the top, dropping the
 * oldest copy, finding the slots again after a restart, damaged and half
 * written slots, a changed layout, a flash that does not start on a block.
 *
 *   ./hosttest/flash_test        exit status 0 when every check passes
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gal_internal.h"
#include "romload.h"

/* --- fake games -------------------------------------------------------------- */

/* A game of kb KB is made of 60 KB regions, one file each: copying loads one
 * region at a time into the SRAM arena (100 KB on the board). */
#define REGION_KB 60
static uint32_t game_kb[GAL_GAME_COUNT]; /* 0 = not large */
static uint8_t conv_version[GAL_GAME_COUNT];
static gal_game_ops_t ops[GAL_GAME_COUNT];
static gal_romfile_t files[GAL_GAME_COUNT][GAL_MAX_REGIONS];
/* gal_flash.c is built with gal_games renamed to this */
gal_game_info_t fake_games[GAL_GAME_COUNT];

static void derive(int i, const uint8_t *src, uint32_t ofs, uint8_t *out, uint32_t len)
{
    for (uint32_t k = 0; k < len; k++)
        out[k] = (uint8_t)(src[(ofs + k) % 4096] ^ (i + 1));
}

static void setup_game(int g, uint32_t kb)
{
    game_kb[g] = kb;
    memset(&ops[g], 0, sizeof(ops[g]));
    ops[g].large = kb ? 1 : 0;
    ops[g].nderived = 1;
    ops[g].derived_src[0] = 0;
    ops[g].derived_size[0] = 8192;
    ops[g].derive = derive;
    ops[g].conv_version = conv_version[g];
    const int n = (int)((kb + REGION_KB - 1) / REGION_KB);
    fake_games[g] = (gal_game_info_t){"game", "GAME", "game", {0}, files[g], (uint8_t)n, (uint8_t)n, 0, {0}};
    for (int r = 0; r < n; r++)
    {
        files[g][r] = (gal_romfile_t){"fake", 0, REGION_KB * 1024, (uint8_t)r, 0};
        fake_games[g].region_size[r] = REGION_KB * 1024;
    }
    static char titles[GAL_GAME_COUNT][8];
    snprintf(titles[g], sizeof(titles[g]), "G%d", g);
    fake_games[g].title = titles[g];
}

/* --- what gal_flash.c needs from the rest of the core ------------------------ */

const gal_game_ops_t *gal_ops(int game)
{
    return (game >= 0 && game < GAL_GAME_COUNT && game_kb[game]) ? &ops[game] : NULL;
}

static int fail_romload; /* the set cannot be read */

uint32_t gal_probe(gal_game_id_t game, const gal_io_t *io, const char *dir)
{
    (void)io, (void)dir;
    return fail_romload ? 0 : (1u << fake_games[game].nfiles) - 1;
}

int gal_set_complete(gal_game_id_t game, uint32_t found)
{
    return found == (1u << fake_games[game].nfiles) - 1;
}

uint32_t gal_romload(const gal_game_info_t *g, uint8_t *const *regions, const gal_io_t *io, const char *dir,
                     uint32_t found)
{
    (void)io, (void)dir;
    const int game = (int)(g - fake_games);
    for (int r = 0; r < g->nregions; r++)
    {
        if (!regions || !regions[r])
            continue;
        for (uint32_t k = 0; k < g->region_size[r]; k++)
            regions[r][k] = (uint8_t)(k * 7 + game * 31 + r);
        found |= 1u << r;
    }
    return found;
}

static uint8_t arena[100 * 1024];
static uint32_t lo, hi = sizeof(arena);
void gal_arena_reset(void)
{
    lo = 0;
    hi = sizeof(arena);
}
void *gal_alloc(uint32_t size)
{
    if (size > hi - lo)
        return NULL;
    lo += size;
    return arena + lo - size;
}
void *gal_temp_alloc(uint32_t size)
{
    if (size > hi - lo)
        return NULL;
    hi -= size;
    return arena + hi;
}
void gal_temp_release(void)
{
    hi = sizeof(arena);
}

/* --- simulated flash ----------------------------------------------------------- */

#define FLASH_TOTAL (2 * 1024 * 1024)
static uint8_t flash[FLASH_TOTAL];
static int errors;
static int program_budget = -1; /* pages left before programming "loses power" */

static void sim_erase(uint32_t ofs, uint32_t len)
{
    if ((ofs | len) % 4096 || ofs + len > FLASH_TOTAL)
    {
        errors++;
        return;
    }
    memset(flash + ofs, 0xff, len);
}

static void sim_program(uint32_t ofs, const uint8_t *data, uint32_t len)
{
    if ((ofs | len) % 256 || ofs + len > FLASH_TOTAL)
    {
        errors++;
        return;
    }
    for (uint32_t p = 0; p < len; p += 256)
    {
        if (program_budget == 0)
            return;
        if (program_budget > 0)
            program_budget--;
        for (uint32_t i = 0; i < 256; i++)
            flash[ofs + p + i] &= data[p + i];
    }
}

/* the cache: flash from 4 KB in (not on a block), size bytes */
static void open_cache(uint32_t size)
{
    const gal_flash_t fl = {flash + 4096, 4096, size, sim_erase, sim_program, NULL};
    gal_flash_open(&fl);
}

/* --- checks -------------------------------------------------------------------- */

static int failures;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        failures++;
}

static gal_games_t held(void)
{
    gal_games_t m = 0;
    for (int g = 0; g < GAL_GAME_COUNT; g++)
        if (gal_flash_holds((gal_game_id_t)g))
            m |= GAL_GAME_BIT(g);
    return m;
}

static int copy(int g)
{
    gal_flash_copy((gal_game_id_t)g, NULL, "");
    return gal_flash_holds((gal_game_id_t)g);
}

#define B(g) GAL_GAME_BIT(g)

int main(void)
{
    _Static_assert(GAL_GAME_COUNT >= 5, "the test needs five games");
    setvbuf(stdout, NULL, _IONBF, 0);
    memset(flash, 0xff, sizeof(flash));
    /* slots of 64 KB blocks: 5 x 60 KB + 8 KB derived + header -> 320 KB */
    setup_game(0, 300);
    setup_game(1, 300);
    setup_game(2, 300);
    setup_game(3, 300);
    setup_game(4, 0); /* not large: never in the cache */
    const uint32_t cache = 1024 * 1024; /* from 4 KB in: 64 KB skipped, 960 KB left */

    open_cache(cache);
    check(gal_flash_slot_size(0) == 320 * 1024, "slot size in whole blocks");
    check(gal_flash_room(0) && !gal_flash_room(4), "room for large games only");
    check(held() == 0, "empty cache holds nothing");

    check(copy(0) && copy(1) && copy(2), "three games fill the cache");
    check(held() == (B(0) | B(1) | B(2)), "all three held");
    check(errors == 0, "no alignment errors");

    open_cache(cache);
    check(held() == (B(0) | B(1) | B(2)), "found again after a restart");

    check(copy(3), "a fourth game is copied");
    check(held() == (B(1) | B(2) | B(3)), "the oldest copy (game 0) was dropped");
    open_cache(cache);
    check(held() == (B(1) | B(2) | B(3)), "and stays dropped after a restart");

    /* damage a byte of game 2's data */
    open_cache(cache);
    {
        const uint8_t *parts[GAL_MAX_PARTS];
        check(gal_flash_parts(2, parts), "game 2 has parts");
        ((uint8_t *)parts[0])[1000] ^= 0x55;
    }
    open_cache(cache);
    check(!gal_flash_holds(2), "a damaged slot is not used");
    check(gal_flash_holds(1) && gal_flash_holds(3), "the others still are");
    check(copy(2), "the damaged game is copied again");
    open_cache(cache);
    check(held() == (B(1) | B(2) | B(3)), "and found after a restart");

    /* power lost halfway through a copy: the header is never written */
    program_budget = 200;
    gal_flash_copy((gal_game_id_t)0, NULL, "");
    program_budget = -1;
    open_cache(cache);
    check(!gal_flash_holds(0), "a copy cut short is not used");
    check(copy(0), "and is copied again in full");
    open_cache(cache);
    check(gal_flash_holds(0), "found after a restart");

    /* a set that cannot be read: nothing is written or dropped */
    const gal_games_t before = held();
    fail_romload = 1;
    gal_flash_copy((gal_game_id_t)3, NULL, "");
    fail_romload = 0;
    open_cache(cache);
    check(held() == before, "an unreadable set changes nothing");

    /* a build with another conversion: the game's slot is not used */
    open_cache(cache);
    const int had3 = gal_flash_holds(3);
    conv_version[3] = 1;
    setup_game(3, 300);
    open_cache(cache);
    check(had3 && !gal_flash_holds(3), "a changed conv_version is not used");
    check(copy(3), "and is copied again");
    open_cache(cache);
    check(gal_flash_holds(3), "found after a restart");

    /* the program grows into the bottom of the cache */
    memset(flash + 65536, 0x00, 65536);
    open_cache(cache);
    int bottom_ok = 1;
    for (int g = 0; g < 4; g++)
    {
        const uint8_t *parts[GAL_MAX_PARTS];
        if (gal_flash_parts(g, parts) && parts[0] < flash + 2 * 65536)
            bottom_ok = 0; /* a game still claimed in the overwritten block */
    }
    check(bottom_ok, "a slot overwritten from below is not used");

    /* a cache too small for any game */
    open_cache(256 * 1024);
    check(!gal_flash_room(0), "no room in a cache smaller than a slot");

    /* a bigger cache keeps more games */
    memset(flash, 0xff, sizeof(flash));
    open_cache(FLASH_TOTAL - 4096);
    check(copy(0) && copy(1) && copy(2) && copy(3), "four games in 2 MB");
    check(held() == (B(0) | B(1) | B(2) | B(3)), "all four held");
    check(errors == 0, "no alignment errors at all");

    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
