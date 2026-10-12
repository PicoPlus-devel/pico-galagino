/*
 * High scores, after GalaginoPlus's hiscore.cpp.
 *
 * A game's table is the bytes of the regions MAME's hiscore.dat lists for it,
 * in the main CPU's address space. Saved scores cannot be put in at once: the
 * game sets up its own table after a power-on reset (and some test their RAM
 * first). So the first and last byte of every region are watched until they
 * hold the values the game leaves there once its table is set up, for 30
 * frames in a row; then the saved bytes go in. From then on the table is read
 * every 5 seconds, and reported to be saved when it changed.
 */
#include "gal_internal.h"

#ifdef GAL_HISCORE_DEBUG
#include <stdio.h>
#define HS_LOG(...) printf(__VA_ARGS__)
#else
#define HS_LOG(...) ((void)0)
#endif

#define STABLE_FRAMES 30
#define POLL_FRAMES 300

static struct
{
    enum
    {
        HS_OFF,
        HS_WAIT, /* for the game to set up its table */
        HS_ACTIVE
    } state;
    int size;
    int frames, stable;
    int have_saved; /* `saved` holds scores to put in */
    int changed;    /* `saved` changed since gal_hiscore_poll reported it */
    uint8_t saved[GAL_HISCORE_MAX];
} hs;

static uint8_t rd(uint16_t addr)
{
    if (gal.ops->hs_read)
        return gal.ops->hs_read(addr);
    const uint8_t *page = gal.cpu[0].rd_page[addr >> 8];
    return page ? page[addr & 0xff] : 0;
}

static void wr(uint16_t addr, uint8_t value)
{
    if (gal.ops->hs_write)
    {
        gal.ops->hs_write(addr, value);
        return;
    }
    uint8_t *page = gal.cpu[0].wr_page[addr >> 8];
    if (page)
        page[addr & 0xff] = value;
}

static void read_all(uint8_t *buf)
{
    for (int i = 0; i < gal.ops->nhiscore; i++)
    {
        const gal_hiscore_region_t *r = &gal.ops->hiscore[i];
        for (int j = 0; j < r->len; j++)
            *buf++ = rd((uint16_t)(r->addr + j));
    }
}

static int set_up(void)
{
    for (int i = 0; i < gal.ops->nhiscore; i++)
    {
        const gal_hiscore_region_t *r = &gal.ops->hiscore[i];
        if (rd(r->addr) != r->start_byte || rd((uint16_t)(r->addr + r->len - 1)) != r->end_byte)
            return 0;
    }
    return 1;
}

void gal_hiscore_start(void)
{
    hs.state = HS_OFF;
    hs.size = 0;
    hs.have_saved = hs.changed = 0;
    if (!gal.ops || !gal.ops->nhiscore)
        return;
    int size = 0;
    for (int i = 0; i < gal.ops->nhiscore; i++)
        size += gal.ops->hiscore[i].len;
    if (size <= 0 || size > GAL_HISCORE_MAX)
        return;
    hs.size = size;
    hs.state = HS_WAIT;
    hs.frames = hs.stable = 0;
}

void gal_hiscore_stop(void)
{
    hs.state = HS_OFF;
    hs.size = 0;
}

int gal_hiscore_size(void)
{
    return hs.state == HS_OFF ? 0 : hs.size;
}

void gal_hiscore_load(const uint8_t *saved)
{
    if (hs.state != HS_WAIT || !saved)
        return;
    memcpy(hs.saved, saved, hs.size);
    hs.have_saved = 1;
}

void gal_hiscore_frame(void)
{
    if (hs.state == HS_WAIT)
    {
        if (!set_up())
        {
            hs.stable = 0;
            return;
        }
        if (++hs.stable < STABLE_FRAMES)
            return;
        if (hs.have_saved)
        {
            int k = 0;
            for (int i = 0; i < gal.ops->nhiscore; i++)
            {
                const gal_hiscore_region_t *r = &gal.ops->hiscore[i];
                for (int j = 0; j < r->len; j++)
                    wr((uint16_t)(r->addr + j), hs.saved[k++]);
            }
            if (gal.ops->hs_restored)
                gal.ops->hs_restored();
        }
        else
            read_all(hs.saved); /* the game's own table: only changes count */
        HS_LOG("hiscore: table set up, %s\n", hs.have_saved ? "saved scores put in" : "the game's own kept");
        hs.state = HS_ACTIVE;
        hs.frames = 0;
        return;
    }
    if (hs.state != HS_ACTIVE || ++hs.frames < POLL_FRAMES)
        return;
    hs.frames = 0;
    uint8_t now[GAL_HISCORE_MAX];
    read_all(now);
    if (memcmp(now, hs.saved, hs.size) != 0)
    {
        memcpy(hs.saved, now, hs.size);
        hs.changed = 1;
        HS_LOG("hiscore: changed\n");
    }
}

int gal_hiscore_poll(uint8_t *out)
{
    if (!hs.changed)
        return 0;
    hs.changed = 0;
    memcpy(out, hs.saved, hs.size);
    return 1;
}

int gal_hiscore_flush(uint8_t *out)
{
    if (hs.state == HS_ACTIVE)
    {
        hs.frames = POLL_FRAMES - 1;
        gal_hiscore_frame();
    }
    return gal_hiscore_poll(out);
}
