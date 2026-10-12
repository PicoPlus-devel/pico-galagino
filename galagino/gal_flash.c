/*
 * The flash cache: the large games, whose data does not fit in SRAM, copied
 * into flash on boards without PSRAM (see galagino.h).
 *
 * A slot holds one game:
 *
 *   +0      header page (256 bytes), written last
 *   +256    region 0, region 1, ... each starting on a 256-byte page
 *           derived table 0, 1, ... (1942: its converted tiles and sprites)
 *
 * Slots start on a 64 KB boundary of flash and take whole 64 KB blocks, which
 * flash erases several times faster than 4 KB sectors. They are placed in the
 * highest free gap: the bottom of the cache lies just above the program, which
 * may grow with an update, and is where other applications of pico-bootLoader
 * keep their ROMs.
 *
 * Nothing besides the slots is kept. On opening, the start of every block is
 * looked at for a header: one whose own CRC, layout and place check out is a
 * candidate. Candidates are taken newest first, by the copy sequence number in
 * the header; one that overlaps a newer slot, or holds a game a newer slot
 * holds too, is left out. A slot's data is checked against the CRC in its
 * header once, when the game first starts from it. Anything else in flash
 * (an erased block, another application's ROM, a copy cut short) is free room.
 *
 * Copying loads one region at a time from the zips into the SRAM arena, which
 * is free while no game runs, and verifies every page programmed against the
 * RAM it came from. So no region of a large game may be larger than the arena
 * less a 4 KB chunk (1942's sprite ROMs, 64 KB, are the largest so far); a game
 * with one is not taken into the cache.
 */
#include "gal_internal.h"

#include <stddef.h>
#include <stdio.h>

#include "miniz.h"
#include "romload.h"

#define MAGIC 0x434c4147u /* "GALC" */
#define VERSION 2
#define HDR_SIZE 256
#define PAGE 256
#define SECTOR 4096
#define BLOCK 65536
#define CHUNK 4096
#define MAX_CANDIDATES 64

typedef struct
{
    uint32_t magic;
    uint8_t version, game, nparts, reserved;
    uint32_t signature; /* the layout: the part sizes and conv_version */
    uint32_t slot_size; /* header included, whole blocks */
    uint32_t data_size; /* from the end of the header page */
    uint32_t data_crc;
    uint32_t seq; /* copy order: the lowest goes first when room is needed */
    uint32_t part_ofs[GAL_MAX_PARTS]; /* from the start of the slot */
    uint32_t part_size[GAL_MAX_PARTS];
    uint32_t hdr_crc; /* of everything above */
} hdr_t;

_Static_assert(sizeof(hdr_t) <= HDR_SIZE, "the header must fit its page");

typedef struct
{
    uint32_t ofs; /* from the start of the cache */
    uint32_t seq;
    uint8_t present; /* a valid header (the data is checked on first use) */
    uint8_t checked; /* the data matched its CRC */
} slot_t;

/* A valid header found in the cache, used or not. */
typedef struct
{
    uint32_t ofs, seq, size;
    uint8_t game;
} cand_t;

static gal_flash_t fl;
static int opened;
static uint32_t max_seq;
static slot_t slots[GAL_GAME_COUNT];
static cand_t cand[MAX_CANDIDATES];
static int ncand;

static uint32_t round_up(uint32_t v, uint32_t to)
{
    return (v + to - 1) / to * to;
}

static uint32_t crc32(const void *p, size_t n)
{
    return (uint32_t)mz_crc32(MZ_CRC32_INIT, (const uint8_t *)p, n);
}

/* The header a slot of game g carries, but for seq and the CRCs (0).
 * Returns 0 for a game that does not use the cache. */
static int layout(int g, hdr_t *h)
{
    memset(h, 0, sizeof(*h));
    const gal_game_ops_t *ops = gal_ops(g);
    if (!ops || !ops->large)
        return 0;
    const gal_game_info_t *info = &gal_games[g];
    /* copying loads a region at a time into the SRAM arena, next to a chunk */
    for (int r = 0; r < info->nregions; r++)
        if (round_up(info->region_size[r], PAGE) > GAL_ARENA_SIZE - CHUNK)
            return 0;
    h->magic = MAGIC;
    h->version = VERSION;
    h->game = (uint8_t)g;
    h->nparts = (uint8_t)(info->nregions + ops->nderived);
    uint32_t d = HDR_SIZE;
    for (int r = 0; r < info->nregions; r++)
    {
        h->part_ofs[r] = d;
        h->part_size[r] = info->region_size[r];
        d += round_up(info->region_size[r], PAGE);
    }
    for (int i = 0; i < ops->nderived; i++)
    {
        h->part_ofs[GAL_MAX_REGIONS + i] = d;
        h->part_size[GAL_MAX_REGIONS + i] = ops->derived_size[i];
        d += round_up(ops->derived_size[i], PAGE);
    }
    h->data_size = d - HDR_SIZE;
    h->slot_size = round_up(d, BLOCK);
    h->signature = crc32(h, sizeof(*h)) ^ ops->conv_version;
    return 1;
}

/* The header at ofs when it is a valid one. */
static int header_at(uint32_t ofs, hdr_t *h)
{
    if (ofs + HDR_SIZE > fl.size)
        return 0;
    memcpy(h, fl.base + ofs, sizeof(*h));
    if (h->magic != MAGIC || h->version != VERSION || h->game >= GAL_GAME_COUNT ||
        crc32(h, offsetof(hdr_t, hdr_crc)) != h->hdr_crc)
        return 0;
    hdr_t want;
    if (!layout(h->game, &want))
        return 0;
    want.seq = h->seq;
    want.data_crc = h->data_crc;
    want.hdr_crc = h->hdr_crc;
    return memcmp(h, &want, sizeof(want)) == 0 && ofs + h->slot_size <= fl.size;
}

static int overlaps(uint32_t a, uint32_t a_size, uint32_t b, uint32_t b_size)
{
    return a < b + b_size && b < a + a_size;
}

void gal_flash_open(const gal_flash_t *flash)
{
    fl = *flash;
    /* whole blocks only */
    const uint32_t skip = (BLOCK - fl.offset % BLOCK) % BLOCK;
    if (!fl.base || fl.size <= skip)
        fl.size = 0;
    else
    {
        fl.base += skip;
        fl.offset += skip;
        fl.size = (fl.size - skip) / BLOCK * BLOCK;
    }
    opened = fl.size > 0;
    max_seq = 0;
    memset(slots, 0, sizeof(slots));
    if (!opened)
        return;

    ncand = 0;
    for (uint32_t ofs = 0; ofs < fl.size && ncand < MAX_CANDIDATES; ofs += BLOCK)
    {
        hdr_t h;
        if (!header_at(ofs, &h))
            continue;
        cand[ncand++] = (cand_t){ofs, h.seq, h.slot_size, h.game};
        if (h.seq > max_seq)
            max_seq = h.seq;
    }

    /* newest first */
    for (int i = 1; i < ncand; i++)
        for (int j = i; j > 0 && cand[j].seq > cand[j - 1].seq; j--)
        {
            const cand_t t = cand[j];
            cand[j] = cand[j - 1];
            cand[j - 1] = t;
        }
    for (int i = 0; i < ncand; i++)
    {
        if (slots[cand[i].game].present)
            continue;
        int clash = 0;
        for (int g = 0; g < GAL_GAME_COUNT && !clash; g++)
            if (slots[g].present)
                clash = overlaps(cand[i].ofs, cand[i].size, slots[g].ofs, gal_flash_slot_size((gal_game_id_t)g));
        if (clash)
            continue;
        slots[cand[i].game].ofs = cand[i].ofs;
        slots[cand[i].game].seq = cand[i].seq;
        slots[cand[i].game].present = 1;
    }
}

uint32_t gal_flash_slot_size(gal_game_id_t game)
{
    hdr_t h;
    return (game >= 0 && game < GAL_GAME_COUNT && layout(game, &h)) ? h.slot_size : 0;
}

int gal_flash_room(gal_game_id_t game)
{
    const uint32_t size = gal_flash_slot_size(game);
    return opened && size && size <= fl.size;
}

int gal_flash_holds(gal_game_id_t game)
{
    if (!opened || game < 0 || game >= GAL_GAME_COUNT || !slots[game].present)
        return 0;
    slot_t *s = &slots[game];
    if (!s->checked)
    {
        hdr_t h;
        memcpy(&h, fl.base + s->ofs, sizeof(h));
        if (crc32(fl.base + s->ofs + HDR_SIZE, h.data_size) != h.data_crc)
        {
            printf("flash cache: %s is damaged\n", gal_games[game].title);
            s->present = 0;
            return 0;
        }
        s->checked = 1;
    }
    return 1;
}

gal_games_t gal_flash_games(void)
{
    gal_games_t m = 0;
    for (int g = 0; g < GAL_GAME_COUNT; g++)
        if (opened && slots[g].present)
            m |= GAL_GAME_BIT(g);
    return m;
}

int gal_flash_parts(int game, const uint8_t **parts)
{
    if (!gal_flash_holds((gal_game_id_t)game))
        return 0;
    hdr_t h;
    layout(game, &h);
    const uint8_t *slot = fl.base + slots[game].ofs;
    for (int p = 0; p < GAL_MAX_PARTS; p++)
        parts[p] = h.part_size[p] ? slot + h.part_ofs[p] : NULL;
    return 1;
}

/* --- Copying ------------------------------------------------------------------ */

/* The start of the highest free gap of at least size bytes, leaving out game
 * `except` (whose slot is about to be replaced), or -1. */
static int64_t find_gap(uint32_t size, int except)
{
    uint32_t top = fl.size;
    for (;;)
    {
        /* the highest slot below top */
        int below = -1;
        for (int g = 0; g < GAL_GAME_COUNT; g++)
            if (g != except && slots[g].present && slots[g].ofs < top &&
                (below < 0 || slots[g].ofs > slots[below].ofs))
                below = g;
        const uint32_t floor = below < 0 ? 0 : slots[below].ofs + gal_flash_slot_size((gal_game_id_t)below);
        if (floor <= top && top - floor >= size)
            return (int64_t)(top - size);
        if (below < 0)
            return -1;
        top = slots[below].ofs;
    }
}

/* Programs len bytes (a multiple of PAGE) at ofs in the cache, and checks them. */
static int put(uint32_t ofs, const uint8_t *data, uint32_t len)
{
    fl.program(fl.offset + ofs, data, len);
    return memcmp(fl.base + ofs, data, len) == 0;
}

uint32_t gal_flash_copy(gal_game_id_t game, const gal_io_t *io, const char *dir)
{
    hdr_t h;
    if (!gal_flash_room(game) || !layout(game, &h))
        return 0;
    const gal_game_info_t *info = &gal_games[game];
    const gal_game_ops_t *ops = gal_ops(game);
    const uint32_t found = gal_probe(game, io, dir);
    if (!gal_set_complete(game, found))
        return found;

    /* room: drop the games copied longest ago until a gap is large enough */
    slots[game].present = slots[game].checked = 0;
    int64_t pos;
    while ((pos = find_gap(h.slot_size, game)) < 0)
    {
        int oldest = -1;
        for (int g = 0; g < GAL_GAME_COUNT; g++)
            if (slots[g].present && (oldest < 0 || slots[g].seq < slots[oldest].seq))
                oldest = g;
        if (oldest < 0)
            return found; /* cannot happen: the slot fits the cache */
        printf("flash cache: dropping %s\n", gal_games[oldest].title);
        slots[oldest].present = 0;
    }
    const uint32_t at = (uint32_t)pos;

    if (fl.copying)
        fl.copying(game);
    /* Headers that would otherwise still count: those of slots not in use any
     * more (dropped, damaged, replaced, left out). A header found is never
     * inside a slot in use: writing that slot would have erased it. */
    int kept = 0;
    for (int i = 0; i < ncand; i++)
    {
        const slot_t *s = &slots[cand[i].game];
        if (s->present && s->ofs == cand[i].ofs)
            cand[kept++] = cand[i];
        else if (!overlaps(cand[i].ofs, SECTOR, at, h.slot_size))
            fl.erase(fl.offset + cand[i].ofs, SECTOR);
    }
    ncand = kept;
    fl.erase(fl.offset + at, h.slot_size);

    gal_arena_reset();
    uint8_t *chunk = (uint8_t *)gal_alloc(CHUNK);
    int ok = chunk != NULL;
    uint32_t loaded = 0;
    for (int r = 0; ok && r < info->nregions; r++)
    {
        const uint32_t size = round_up(info->region_size[r], PAGE);
        uint8_t *buf = (uint8_t *)gal_temp_alloc(size);
        if (!buf)
        {
            ok = 0;
            break;
        }
        memset(buf, 0, size);

        /* this region's files only */
        uint8_t *regions[GAL_MAX_REGIONS] = {0};
        regions[r] = buf;
        loaded |= gal_romload(info, regions, io, dir, 0);
        for (int i = 0; i < info->nfiles; i++)
            if (info->files[i].region == r && !(loaded & (1u << i)))
                ok = 0;
        ok = ok && put(at + h.part_ofs[r], buf, size);

        /* the tables built from this region */
        for (int d = 0; ok && d < ops->nderived; d++)
        {
            if (ops->derived_src[d] != r)
                continue;
            for (uint32_t ofs = 0; ok && ofs < ops->derived_size[d]; ofs += CHUNK)
            {
                ops->derive(d, buf, ofs, chunk, CHUNK);
                ok = put(at + h.part_ofs[GAL_MAX_REGIONS + d] + ofs, chunk, CHUNK);
            }
        }
        gal_temp_release();
    }

    if (ok)
    {
        /* the header last: until it is there, the slot is not in use */
        h.seq = ++max_seq;
        h.data_crc = crc32(fl.base + at + HDR_SIZE, h.data_size);
        h.hdr_crc = crc32(&h, offsetof(hdr_t, hdr_crc));
        memset(chunk, 0xff, HDR_SIZE);
        memcpy(chunk, &h, sizeof(h));
        ok = put(at, chunk, HDR_SIZE);
    }
    gal_arena_reset();
    if (ok)
    {
        slots[game] = (slot_t){at, h.seq, 1, 0};
        if (ncand < MAX_CANDIDATES)
            cand[ncand++] = (cand_t){at, h.seq, h.slot_size, (uint8_t)game};
        ok = gal_flash_holds(game);
    }
    printf("flash cache: %s %s at +%lu KB (%lu KB)\n", info->title, ok ? "copied" : "NOT copied",
           (unsigned long)(at / 1024), (unsigned long)(h.slot_size / 1024));
    return ok ? found : (found & loaded);
}
