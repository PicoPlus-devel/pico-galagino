/*
 * The DIP switch file (dipfile.c) on a simulated FatFs (fatfs_sim/ff.h), and the
 * tables of all the games. Needs no ROM sets.
 *
 *   ./hosttest/dip_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dipfile.h"
#include "gal_internal.h"

/* --- The simulated FatFs ---------------------------------------------------- */

#define SIM_MAX 4096

static struct
{
    int exists;
    char path[64];
    uint8_t data[SIM_MAX];
    UINT size;
    /* errors to order */
    FRESULT open_read_err, open_write_err, read_err, write_err, close_err;
    int write_room; /* bytes that fit on the card; -1: all */
    int open_files;
    int reads, writes;
} sim;

static void sim_reset(void)
{
    memset(&sim, 0, sizeof(sim));
    sim.write_room = -1;
}

static void sim_file(const char *text)
{
    sim.exists = 1;
    sim.size = (UINT)strlen(text);
    memcpy(sim.data, text, sim.size);
}

FRESULT f_open(FIL *fp, const char *path, BYTE mode)
{
    memset(fp, 0, sizeof(*fp));
    if (mode & FA_WRITE)
    {
        if (sim.open_write_err)
            return sim.open_write_err;
        if (mode & FA_CREATE_ALWAYS)
        {
            sim.exists = 1;
            sim.size = 0;
            snprintf(sim.path, sizeof(sim.path), "%s", path);
        }
    }
    else
    {
        if (sim.open_read_err)
            return sim.open_read_err;
        if (!sim.exists || strcmp(path, sim.path))
            return FR_NO_FILE;
    }
    fp->open = 1;
    fp->mode = mode;
    fp->obj.objsize = sim.size;
    sim.open_files++;
    return FR_OK;
}

FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br)
{
    *br = 0;
    if (!fp->open || !(fp->mode & FA_READ))
        return FR_DENIED;
    sim.reads++;
    if (sim.read_err)
        return sim.read_err;
    UINT n = sim.size - fp->pos;
    if (n > btr)
        n = btr;
    memcpy(buff, sim.data + fp->pos, n);
    fp->pos += n;
    *br = n;
    return FR_OK;
}

FRESULT f_write(FIL *fp, const void *buff, UINT btw, UINT *bw)
{
    *bw = 0;
    if (!fp->open || !(fp->mode & FA_WRITE))
        return FR_DENIED;
    sim.writes++;
    if (sim.write_err)
        return sim.write_err;
    UINT n = btw;
    if (sim.write_room >= 0 && n > (UINT)sim.write_room)
        n = (UINT)sim.write_room; /* a full card: FatFs reports FR_OK and writes less */
    memcpy(sim.data + fp->pos, buff, n);
    fp->pos += n;
    sim.size = fp->pos;
    *bw = n;
    return FR_OK;
}

FRESULT f_close(FIL *fp)
{
    if (!fp->open)
        return FR_DENIED;
    fp->open = 0;
    sim.open_files--;
    return sim.close_err;
}

/* --- Checks ----------------------------------------------------------------- */

static int failures, checks;

#define CHECK(cond, ...)                                                                     \
    do                                                                                       \
    {                                                                                        \
        checks++;                                                                            \
        if (!(cond))                                                                         \
        {                                                                                    \
            failures++;                                                                      \
            printf("FAILED %s:%d: ", __func__, __LINE__);                                    \
            printf(__VA_ARGS__);                                                             \
            printf("\n");                                                                    \
        }                                                                                    \
    } while (0)

/* Made-up rows, on two ports: bits 7, 6, 3, 2 of port 0, the rest of port 1 and
 * all of port 2 are not offered. */
static const char *const names4[] = {"a", "b", "c", "d"};
static const uint8_t lives_values[] = {0x00, 0x01, 0x02, 0x03};
static const uint8_t coin_values[] = {0x10, 0x20, 0x30}; /* 0x00 is no choice */
static const uint8_t diff_values[] = {0x80, 0x00};
static const gal_dip_t rows[] = {
    {"Lives", "h", 0, 0x03, 4, names4, lives_values},
    {"Coin", "h", 0, 0x30, 3, names4, coin_values},
    {"Diff", "h", 1, 0x80, 2, names4, diff_values},
};
#define NROWS 3
#define PATH "/SAVES/GALAGINO/TEST.DIP"
/* Lives 2 (c), coin 0x10 (a), Diff 0x80 (a); the other bits are all "off": set */
static const uint8_t defaults[GAL_DIP_PORTS] = {0xd2, 0xff, 0x5a};

static FIL fil;

/* Loads text from the card; the ports it leaves. */
static int load(const char *text, uint8_t *ports)
{
    sim_reset();
    if (text)
    {
        sim_file(text);
        snprintf(sim.path, sizeof(sim.path), "%s", PATH);
    }
    memcpy(ports, defaults, GAL_DIP_PORTS);
    return dipFileLoad(&fil, PATH, rows, NROWS, ports);
}

static void expect_defaults(const char *what, const uint8_t *ports)
{
    CHECK(!memcmp(ports, defaults, GAL_DIP_PORTS), "%s: ports %02x %02x %02x", what, ports[0], ports[1], ports[2]);
}

static void test_missing_and_errors(void)
{
    uint8_t p[GAL_DIP_PORTS];
    CHECK(!load(NULL, p), "missing file reported as read");
    expect_defaults("missing file", p);
    CHECK(sim.open_files == 0, "missing file: left open");

    CHECK(load("", p), "an empty file is a file");
    expect_defaults("empty file", p);

    sim_reset();
    sim_file("dsw0=d3\n");
    snprintf(sim.path, sizeof(sim.path), "%s", PATH);
    sim.open_read_err = FR_DISK_ERR;
    memcpy(p, defaults, sizeof(p));
    CHECK(!dipFileLoad(&fil, PATH, rows, NROWS, p), "open error reported as read");
    expect_defaults("open error", p);

    sim_reset();
    sim_file("dsw0=d3\n");
    snprintf(sim.path, sizeof(sim.path), "%s", PATH);
    sim.read_err = FR_DISK_ERR;
    memcpy(p, defaults, sizeof(p));
    CHECK(!dipFileLoad(&fil, PATH, rows, NROWS, p), "read error reported as read");
    expect_defaults("read error", p);
    CHECK(sim.open_files == 0, "read error: left open");
}

static void test_good_file(void)
{
    uint8_t p[GAL_DIP_PORTS];
    CHECK(load("dsw0=e3\ndsw1=7f\n", p), "good file");
    /* Lives 3, coin 0x20, Diff 0x00; bits not offered stay */
    CHECK(p[0] == 0xe3 && p[1] == 0x7f && p[2] == 0x5a, "good: %02x %02x", p[0], p[1]);
    CHECK(sim.open_files == 0, "good file: left open");
    CHECK(load("dsw0=e3\r\ndsw1=7f\r\n", p) && p[0] == 0xe3 && p[1] == 0x7f, "CRLF: %02x %02x", p[0], p[1]);
    CHECK(load("dsw0=E3", p) && p[0] == 0xe3, "upper case, no last newline: %02x", p[0]);
    CHECK(load("dsw1=7f\n", p) && p[0] == 0xd2 && p[1] == 0x7f, "a port left out: %02x %02x", p[0], p[1]);
    CHECK(load("dsw0=e3\ndsw0=d1\n", p) && p[0] == 0xd1, "the last line counts: %02x", p[0]);
}

static void test_malformed(void)
{
    static const char *const bad[] = {
        "garbage\n", "=\n", "dsw0\n", "dsw0=\n", "dsw0=e\n", "dsw0=e33\n", "dsw0=zz\n", "dsw0= e3\n",
        " dsw0=e3\n", "dsw0=e3 \n", "DSW0=e3\n", "dswx=e3\n", "dsw9=e3\n", "dsw3=e3\n", "dsw/=e3\n",
        "dsw0=-1\n", "dsw0=0x\n", "\n\n\n", "\r\r\n", "dsw0:e3\n",
    };
    uint8_t p[GAL_DIP_PORTS];
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
        load(bad[i], p);
        expect_defaults(bad[i], p);
    }
    /* good lines among the bad ones still count */
    CHECK(load("junk\ndsw0=e3\nmore junk\n", p) && p[0] == 0xe3, "junk around a line: %02x", p[0]);
    /* a NUL in the file */
    sim_reset();
    sim_file("dsw0=e3\n");
    sim.data[4] = 0;
    snprintf(sim.path, sizeof(sim.path), "%s", PATH);
    memcpy(p, defaults, sizeof(p));
    dipFileLoad(&fil, PATH, rows, NROWS, p);
    expect_defaults("NUL", p);
}

static void test_oversized(void)
{
    uint8_t p[GAL_DIP_PORTS];
    char big[SIM_MAX];
    /* exactly the limit: read; one more: ignored as a whole */
    memset(big, '\n', sizeof(big));
    memcpy(big, "dsw0=e3\n", 8);
    big[DIPFILE_MAX] = 0;
    CHECK(load(big, p) && p[0] == 0xe3, "at the limit: %02x", p[0]);
    big[DIPFILE_MAX] = '\n';
    big[DIPFILE_MAX + 1] = 0;
    CHECK(!load(big, p), "over the limit reported as read");
    expect_defaults("just over the limit", p);
    CHECK(sim.open_files == 0, "over the limit: left open");
    big[DIPFILE_MAX + 1] = '\n';
    big[SIM_MAX - 1] = 0;
    CHECK(!load(big, p), "far over the limit reported as read");
    expect_defaults("far over the limit", p);
    CHECK(sim.reads == 0, "an oversized file is not read");
}

static void test_only_offered_bits(void)
{
    uint8_t p[GAL_DIP_PORTS];
    /* every bit flipped */
    load("dsw0=2d\ndsw1=00\ndsw2=00\n", p);
    /* 0x2d: lives bits 01 -> 1; coin bits 0x20 -> choice; bits 7, 6, 3, 2 stay */
    CHECK(p[0] == ((0xd2 & ~0x33) | 0x01 | 0x20), "port 0: %02x", p[0]);
    CHECK(p[1] == 0x7f, "port 1: only the row's bit: %02x", p[1]);
    CHECK(p[2] == 0x5a, "a port without rows: %02x", p[2]);
    /* the coin bits as 0x00: not a choice, so that row keeps its default */
    load("dsw0=03\n", p);
    CHECK((p[0] & 0x30) == 0x10, "not a choice: coin bits %02x", p[0] & 0x30);
    CHECK((p[0] & 0x03) == 0x03, "the other row still counts: %02x", p[0] & 0x03);
}

static void test_save(void)
{
    uint8_t p[GAL_DIP_PORTS], q[GAL_DIP_PORTS];
    memcpy(p, defaults, sizeof(p));
    p[0] = 0xe3;
    sim_reset();
    CHECK(dipFileSave(&fil, PATH, rows, NROWS, p), "save");
    sim.data[sim.size] = 0;
    CHECK(!strcmp((char *)sim.data, "dsw0=e3\ndsw1=ff\n"), "only the ports with rows: '%s'", sim.data);
    CHECK(sim.open_files == 0, "save: left open");
    CHECK(sim.size <= DIPFILE_MAX, "what is saved can be read back");

    memcpy(q, defaults, sizeof(q));
    CHECK(dipFileLoad(&fil, PATH, rows, NROWS, q), "read back");
    CHECK(q[0] == 0xe3, "read back: %02x", q[0]);

    sim_reset();
    sim.open_write_err = FR_DENIED;
    CHECK(!dipFileSave(&fil, PATH, rows, NROWS, p), "open error reported as saved");
    sim_reset();
    sim.write_err = FR_DISK_ERR;
    CHECK(!dipFileSave(&fil, PATH, rows, NROWS, p), "write error reported as saved");
    CHECK(sim.open_files == 0, "write error: left open");
    sim_reset();
    sim.close_err = FR_DISK_ERR;
    CHECK(!dipFileSave(&fil, PATH, rows, NROWS, p), "close error reported as saved");

    /* a full card: a line cut short does not count when it is read back */
    sim_reset();
    sim.write_room = 11;
    CHECK(!dipFileSave(&fil, PATH, rows, NROWS, p), "short write reported as saved");
    memcpy(q, defaults, sizeof(q));
    CHECK(dipFileLoad(&fil, PATH, rows, NROWS, q), "short file read back");
    CHECK(q[0] == 0xe3 && q[1] == 0xff, "short file: %02x %02x", q[0], q[1]);
}

/* --- The tables of all the games ----------------------------------------------- */

#define MAX_ROW_TEXT 36 /* the width of the settings menu with the overscan setting on */

static void test_tables(void)
{
    int with_table = 0;
    for (int g = 0; g < GAL_GAME_COUNT; g++)
    {
        const gal_game_ops_t *ops = gal_ops(g);
        if (!ops)
            continue;
        const char *title = gal_games[g].title;
        CHECK(!ops->ndips == !ops->dips, "%s: dips and ndips disagree", title);
        CHECK(ops->ndips <= 8, "%s: %d rows do not fit MENU_MAX_EMULATOR_OPTIONS", title, ops->ndips);
        if (!ops->ndips)
            continue;
        with_table++;
        uint8_t used[GAL_DIP_PORTS] = {0};
        for (int k = 0; k < ops->ndips; k++)
        {
            const gal_dip_t *d = &ops->dips[k];
            CHECK(d->label && d->help && d->names && d->values, "%s row %d: a NULL", title, k);
            CHECK(strlen(d->help) <= 40, "%s %s: help of %zu characters", title, d->label, strlen(d->help));
            CHECK(d->port < GAL_DIP_PORTS, "%s %s: port %d", title, d->label, d->port);
            CHECK(d->count >= 2, "%s %s: %d choices", title, d->label, d->count);
            CHECK(d->mask, "%s %s: no bits", title, d->label);
            CHECK(!(used[d->port] & d->mask), "%s %s: bits shared with another row", title, d->label);
            used[d->port] |= d->mask;
            for (int c = 0; c < d->count; c++)
            {
                const size_t len = strlen(d->label) + 2 + strlen(d->names[c]);
                CHECK(len <= MAX_ROW_TEXT, "%s %s %s: %zu columns", title, d->label, d->names[c], len);
                CHECK(!(d->values[c] & ~d->mask), "%s %s %s: bits outside the mask", title, d->label, d->names[c]);
                for (int e = 0; e < c; e++)
                    CHECK(d->values[c] != d->values[e], "%s %s: %s and %s are the same", title, d->label, d->names[c], d->names[e]);
            }
        }

        /* every choice of every row survives a save and a load, and leaves the others */
        for (int k = 0; k < ops->ndips; k++)
        {
            const gal_dip_t *d = &ops->dips[k];
            for (int c = 0; c < d->count; c++)
            {
                uint8_t start[GAL_DIP_PORTS], saved[GAL_DIP_PORTS], got[GAL_DIP_PORTS];
                memset(start, 0xff, sizeof(start));
                for (int j = 0; j < ops->ndips; j++)
                    gal_dip_select(&ops->dips[j], start, 0);
                memcpy(saved, start, sizeof(saved));
                gal_dip_select(d, saved, c);
                CHECK(gal_dip_choice(d, saved) == c, "%s %s: select %d", title, d->label, c);
                sim_reset();
                CHECK(dipFileSave(&fil, PATH, ops->dips, ops->ndips, saved), "%s: save", title);
                CHECK(sim.size <= DIPFILE_MAX, "%s: %u bytes saved", title, sim.size);
                memcpy(got, start, sizeof(got));
                CHECK(dipFileLoad(&fil, PATH, ops->dips, ops->ndips, got), "%s: load", title);
                CHECK(!memcmp(got, saved, sizeof(got)), "%s %s %s: round trip", title, d->label, d->names[c]);
            }
        }
    }
    printf("%d games have DIP switch tables\n", with_table);
    /* the games of this stage, and only those for now: a later stage changes this */
    static const gal_game_id_t expected[] = {GAL_GAME_PACMAN, GAL_GAME_GALAGA, GAL_GAME_DKONG, GAL_GAME_DIGDUG};
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
        CHECK(gal_ops(expected[i])->ndips > 0, "%s has no table", gal_games[expected[i]].title);
}

/* The coinage of the 51XX, from the bytes the games send */
static void test_51xx(void)
{
    gal_51xx_coinage_t c;
    gal_51xx_reset(&c);
    CHECK(gal_51xx_coin(&c, 0) == 1, "1C1C until told otherwise");
    CHECK(!gal_51xx_write(&c, 5), "another command is a command");
    CHECK(gal_51xx_coin(&c, 1) == 2, "and changes nothing");

    /* Galaga 2 coins 3 credits, the second slot's bytes not used */
    static const uint8_t gg[] = {0x02, 0x03, 0x02, 0x03};
    CHECK(!gal_51xx_write(&c, 1), "command 1 is a command");
    for (int i = 0; i < 4; i++)
        CHECK(gal_51xx_write(&c, gg[i]), "Galaga byte %d", i);
    CHECK(!gal_51xx_write(&c, 2), "the next byte is a command");
    CHECK(gal_51xx_coin(&c, 0) == 0, "2C3C, first coin");
    CHECK(gal_51xx_coin(&c, 0) == 3, "2C3C, second coin");
    CHECK(gal_51xx_coin(&c, 3) == 3, "2C3C, third coin");
    CHECK(gal_51xx_coin(&c, 3) == 6, "2C3C, fourth coin");
    CHECK(gal_51xx_coin(&c, 98) == 98, "odd coin at 98");
    CHECK(gal_51xx_coin(&c, 98) == 99, "at most 99");

    /* Dig Dug adds 0x10 */
    static const uint8_t dd[] = {0x00, 0x01, 0x12, 0x11, 0x11, 0x17, 0x04, 0x02, 0x02};
    gal_51xx_reset(&c);
    for (size_t i = 0; i < sizeof(dd); i++)
        gal_51xx_write(&c, dd[i]);
    CHECK(c.left == 0, "Dig Dug's whole sequence");
    CHECK(gal_51xx_coin(&c, 0) == 0 && gal_51xx_coin(&c, 0) == 1, "Dig Dug 2C1C");

    /* free play: coins do nothing */
    gal_51xx_reset(&c);
    gal_51xx_write(&c, 1);
    for (int i = 0; i < 4; i++)
        gal_51xx_write(&c, 0);
    CHECK(c.coins_per_credit == 0 && gal_51xx_coin(&c, 5) == 5, "free play");
}

int main(void)
{
    test_missing_and_errors();
    test_good_file();
    test_malformed();
    test_oversized();
    test_only_offered_bits();
    test_save();
    test_tables();
    test_51xx();
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
