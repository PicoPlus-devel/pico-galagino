/*
 * Namco's custom I/O chips 56XX and 58XX (Mappy, Tower of Druaga, Gaplus),
 * after MAME's namco/namcoio.cpp: sixteen nibbles of RAM shared with the main
 * CPU, a command in ram[8] carried out after each vertical blank.
 */
#include "gal_internal.h"

void gal_namcoio_reset(gal_namcoio_t *c)
{
    memset(c->ram, 0, sizeof(c->ram));
    c->lastcoins = c->lastbuttons = 0;
    c->credits = 0;
    c->coins[0] = c->coins[1] = 0;
    c->coins_per_cred[0] = c->coins_per_cred[1] = 1;
    c->creds_per_coin[0] = c->creds_per_coin[1] = 1;
}

static uint8_t in(const gal_namcoio_t *c, int port)
{
    return ~c->in(port) & 0x0f;
}

/* the credit mode: coins and starts counted by the chip */
static void coins(gal_namcoio_t *c, int swap)
{
    int credit_add = 0, credit_sub = 0;

    int val = in(c, 0);
    int toggled = val ^ c->lastcoins;
    c->lastcoins = (uint8_t)val;
    for (int k = 0; k < 2; k++)
        if (val & toggled & (1 << k))
        {
            c->coins[k]++;
            if (c->coins[k] >= (c->coins_per_cred[k] & 7))
            {
                credit_add = c->creds_per_coin[k] - (c->coins_per_cred[k] >> 3);
                c->coins[k] -= c->coins_per_cred[k] & 7;
            }
            else if (c->coins_per_cred[k] & 8)
                credit_add = 1;
        }
    if (val & toggled & 0x08)
        credit_add = 1;

    val = in(c, 3);
    toggled = val ^ c->lastbuttons;
    c->lastbuttons = (uint8_t)val;
    if ((c->ram[9] & 0x0f) == 0)
    {
        if (val & toggled & 0x04)
        {
            if (c->credits >= 1)
                credit_sub = 1;
        }
        else if (val & toggled & 0x08)
        {
            if (c->credits >= 2)
                credit_sub = 2;
        }
    }
    c->credits += credit_add - credit_sub;

    c->ram[0 ^ swap] = (uint8_t)((c->credits / 10) & 0x0f);
    c->ram[1 ^ swap] = (uint8_t)((c->credits % 10) & 0x0f);
    if (credit_add)
        c->ram[2 ^ swap] = (uint8_t)(credit_add & 0x0f);
    if (credit_sub)
        c->ram[3 ^ swap] = (uint8_t)(credit_sub & 0x0f);
    c->ram[4] = in(c, 1);
    c->ram[5] = (uint8_t)((((val & 0x05) << 1) | (val & toggled & 0x05)) & 0x0f);
    c->ram[6] = in(c, 2);
    c->ram[7] = (uint8_t)(((val & 0x0a) | ((val & toggled & 0x0a) >> 1)) & 0x0f);
}

static void coinage(gal_namcoio_t *c)
{
    c->coins_per_cred[0] = c->ram[9] & 0x0f;
    c->creds_per_coin[0] = c->ram[10] & 0x0f;
    c->coins_per_cred[1] = c->ram[11] & 0x0f;
    c->creds_per_coin[1] = c->ram[12] & 0x0f;
}

/* the inputs read twice, the multiplexer's select pin low and high */
static void dips(gal_namcoio_t *c)
{
    for (int m = 0; m < 2; m++)
    {
        if (c->out)
            c->out(m);
        for (int p = 0; p < 4; p++)
            c->ram[2 * p + m] = in(c, p);
    }
}

#define NEXT(n) ((((n) & 1) ? (n) ^ 0x90 : (n)) >> 1)

void gal_namcoio_run_58xx(gal_namcoio_t *c)
{
    switch (c->ram[8] & 0x0f)
    {
    case 1: /* read the switches */
        for (int p = 0; p < 4; p++)
            c->ram[4 + p] = in(c, p);
        break;
    case 2:
        coinage(c);
        break;
    case 3:
        coins(c, 2);
        break;
    case 4:
        dips(c);
        break;
    case 5: /* the boot-up check: XORs driven by a 7-bit LFSR */
    {
        static const uint8_t src[7] = {11, 10, 9, 15, 14, 13, 12};
        int n = ((c->ram[9] & 0x0f) * 16 + (c->ram[10] & 0x0f)) & 0x7f, seed = 0x22;
        for (int i = 0; i < n; i++)
            seed = NEXT(seed);
        for (int i = 1; i < 8; i++)
        {
            int rng = seed;
            n = 0;
            for (int k = 0; k < 7; k++)
            {
                if (rng & 1)
                    n ^= ~(c->ram[src[k]] & 0x0f);
                rng = NEXT(rng);
                if (k == 0)
                    seed = rng;
            }
            c->ram[i] = (uint8_t)(~n & 0x0f);
        }
        c->ram[0] = (c->ram[9] & 0x0f) == 0x0f ? 0x0f : 0x00;
        break;
    }
    default:
        break;
    }
}

void gal_namcoio_run_56xx(gal_namcoio_t *c)
{
    switch (c->ram[8] & 0x0f)
    {
    case 1: /* read the switches; ram[9] goes to the output pins */
        for (int p = 0; p < 4; p++)
            c->ram[p] = in(c, p);
        if (c->out)
            c->out(c->ram[9] & 0x0f);
        break;
    case 2:
        coinage(c);
        break;
    case 4:
        coins(c, 0);
        break;
    case 8: /* the boot-up check: a sum */
    {
        int sum = 0;
        for (int i = 9; i < 16; i++)
            sum += c->ram[i] & 0x0f;
        c->ram[0] = (uint8_t)((sum >> 4) & 0x0f);
        c->ram[1] = (uint8_t)(sum & 0x0f);
        break;
    }
    case 9:
        dips(c);
        break;
    default:
        break;
    }
}
