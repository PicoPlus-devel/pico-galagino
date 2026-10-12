// license:BSD-3-Clause
// copyright-holders:Juergen Buchmueller
/*
 * MM6221AA melody generator, ported from MAME's tms36xx.cpp. See tms36xx.h.
 */
#include "tms36xx.h"

#include <string.h>

#include "phx_port.h"

#define TMS36XX_VMIN 0x0000
#define TMS36XX_VMAX 0x7fff

/* the frequencies are later adjusted by "* clock / FSCALE" */
#define FSCALE 1024

#define C(n) (int)((FSCALE << (n - 1)) * 1.18921)  /* 2^(3/12) */
#define Cx(n) (int)((FSCALE << (n - 1)) * 1.25992) /* 2^(4/12) */
#define D(n) (int)((FSCALE << (n - 1)) * 1.33484)  /* 2^(5/12) */
#define Dx(n) (int)((FSCALE << (n - 1)) * 1.41421) /* 2^(6/12) */
#define E(n) (int)((FSCALE << (n - 1)) * 1.49831)  /* 2^(7/12) */
#define F(n) (int)((FSCALE << (n - 1)) * 1.58740)  /* 2^(8/12) */
#define Fx(n) (int)((FSCALE << (n - 1)) * 1.68179) /* 2^(9/12) */
#define G(n) (int)((FSCALE << (n - 1)) * 1.78180)  /* 2^(10/12) */
#define Gx(n) (int)((FSCALE << (n - 1)) * 1.88775) /* 2^(11/12) */
#define A(n) (int)((FSCALE << n))                  /* A */
#define Ax(n) (int)((FSCALE << n) * 1.05946)       /* 2^(1/12) */
#define B(n) (int)((FSCALE << n) * 1.12246)        /* 2^(2/12) */

/*
 * Alarm sound?
 * It is unknown what this sound is like. Until somebody manages
 * trigger sound #1 of the Phoenix PCB sound chip I put just something
 * 'alarming' in here.
 */
static const int tune1[96*6] = {
	C(3),   0,      0,      C(2),   0,      0,
	G(3),   0,      0,      0,      0,      0,
	C(3),   0,      0,      0,      0,      0,
	G(3),   0,      0,      0,      0,      0,
	C(3),   0,      0,      0,      0,      0,
	G(3),   0,      0,      0,      0,      0,
	C(3),   0,      0,      0,      0,      0,
	G(3),   0,      0,      0,      0,      0,
	C(3),   0,      0,      C(4),   0,      0,
	G(3),   0,      0,      0,      0,      0,
	C(3),   0,      0,      0,      0,      0,
	G(3),   0,      0,      0,      0,      0,
	C(3),   0,      0,      0,      0,      0,
	G(3),   0,      0,      0,      0,      0,
	C(3),   0,      0,      0,      0,      0,
	G(3),   0,      0,      0,      0,      0,
	C(3),   0,      0,      C(2),   0,      0,
	G(3),   0,      0,      0,      0,      0,
	C(3),   0,      0,      0,      0,      0,
	G(3),   0,      0,      0,      0,      0,
	C(3),   0,      0,      0,      0,      0,
	G(3),   0,      0,      0,      0,      0,
	C(3),   0,      0,      0,      0,      0,
	G(3),   0,      0,      0,      0,      0,
	C(3),   0,      0,      C(4),   0,      0,
	G(3),   0,      0,      0,      0,      0,
	C(3),   0,      0,      0,      0,      0,
	G(3),   0,      0,      0,      0,      0,
	C(3),   0,      0,      0,      0,      0,
	G(3),   0,      0,      0,      0,      0,
	C(3),   0,      0,      0,      0,      0,
	G(3),   0,      0,      0,      0,      0,
};

/*
 * Fuer Elise, Beethoven
 * (Excuse my non-existent musical skill, Mr. B ;-)
 */
static const int tune2[96*6] = {
	D(3),   D(4),   D(5),   0,      0,      0,
	Cx(3),  Cx(4),  Cx(5),  0,      0,      0,
	D(3),   D(4),   D(5),   0,      0,      0,
	Cx(3),  Cx(4),  Cx(5),  0,      0,      0,
	D(3),   D(4),   D(5),   0,      0,      0,
	A(2),   A(3),   A(4),   0,      0,      0,
	C(3),   C(4),   C(5),   0,      0,      0,
	Ax(2),  Ax(3),  Ax(4),  0,      0,      0,
	G(2),   G(3),   G(4),   0,      0,      0,
	D(1),   D(2),   D(3),   0,      0,      0,
	G(1),   G(2),   G(3),   0,      0,      0,
	Ax(1),  Ax(2),  Ax(3),  0,      0,      0,

	D(2),   D(3),   D(4),   0,      0,      0,
	G(2),   G(3),   G(4),   0,      0,      0,
	A(2),   A(3),   A(4),   0,      0,      0,
	D(1),   D(2),   D(3),   0,      0,      0,
	A(1),   A(2),   A(3),   0,      0,      0,
	D(2),   D(3),   D(4),   0,      0,      0,
	Fx(2),  Fx(3),  Fx(4),  0,      0,      0,
	A(2),   A(3),   A(4),   0,      0,      0,
	Ax(2),  Ax(3),  Ax(4),  0,      0,      0,
	D(1),   D(2),   D(3),   0,      0,      0,
	G(1),   G(2),   G(3),   0,      0,      0,
	Ax(1),  Ax(2),  Ax(3),  0,      0,      0,

	D(3),   D(4),   D(5),   0,      0,      0,
	Cx(3),  Cx(4),  Cx(5),  0,      0,      0,
	D(3),   D(4),   D(5),   0,      0,      0,
	Cx(3),  Cx(4),  Cx(5),  0,      0,      0,
	D(3),   D(4),   D(5),   0,      0,      0,
	A(2),   A(3),   A(4),   0,      0,      0,
	C(3),   C(4),   C(5),   0,      0,      0,
	Ax(2),  Ax(3),  Ax(4),  0,      0,      0,
	G(2),   G(3),   G(4),   0,      0,      0,
	D(1),   D(2),   D(3),   0,      0,      0,
	G(1),   G(2),   G(3),   0,      0,      0,
	Ax(1),  Ax(2),  Ax(3),  0,      0,      0,

	D(2),   D(3),   D(4),   0,      0,      0,
	G(2),   G(3),   G(4),   0,      0,      0,
	A(2),   A(3),   A(4),   0,      0,      0,
	D(1),   D(2),   D(3),   0,      0,      0,
	A(1),   A(2),   A(3),   0,      0,      0,
	D(2),   D(3),   D(4),   0,      0,      0,
	Ax(2),  Ax(3),  Ax(4),  0,      0,      0,
	A(2),   A(3),   A(4),   0,      0,      0,
	0,      0,      0,      G(2),   G(3),   G(4),
	D(1),   D(2),   D(3),   0,      0,      0,
	G(1),   G(2),   G(3),   0,      0,      0,
	0,      0,      0,      0,      0,      0
};

/*
 * The theme from Phoenix, a sad little tune.
 * Gerald Coy:
 *   The starting song from Phoenix comes from an old French movie and
 *   it's called : "Jeux interdits" which means "unallowed games"  ;-)
 * Mirko Buffoni:
 *   It's called "Sogni proibiti" in Italian, by Anonymous.
 * Magic*:
 *   This song is a classical piece called "ESTUDIO" from M.A.Robira.
 */
static const int tune3[96*6] = {
	A(2),   A(3),   A(4),   D(1),    D(2),    D(3),
	0,      0,      0,      0,       0,       0,
	A(2),   A(3),   A(4),   0,       0,       0,
	0,      0,      0,      0,       0,       0,
	A(2),   A(3),   A(4),   0,       0,       0,
	0,      0,      0,      0,       0,       0,

	A(2),   A(3),   A(4),   A(1),    A(2),    A(3),
	0,      0,      0,      0,       0,       0,
	G(2),   G(3),   G(4),   0,       0,       0,
	0,      0,      0,      0,       0,       0,
	F(2),   F(3),   F(4),   0,       0,       0,
	0,      0,      0,      0,       0,       0,

	F(2),   F(3),   F(4),   F(1),    F(2),    F(3),
	0,      0,      0,      0,       0,       0,
	E(2),   E(3),   E(4),   F(1),    F(2),    F(3),
	0,      0,      0,      0,       0,       0,
	D(2),   D(3),   D(4),   F(1),    F(2),    F(3),
	0,      0,      0,      0,       0,       0,

	D(2),   D(3),   D(4),   A(1),    A(2),    A(3),
	0,      0,      0,      0,       0,       0,
	F(2),   F(3),   F(4),   0,       0,       0,
	0,      0,      0,      0,       0,       0,
	A(2),   A(3),   A(4),   0,       0,       0,
	0,      0,      0,      0,       0,       0,

	D(3),   D(4),   D(5),   D(1),    D(2),    D(3),
	0,      0,      0,      0,       0,       0,
	0,      0,      0,      D(1),    D(2),    D(3),
	0,      0,      0,      F(1),    F(2),    F(3),
	0,      0,      0,      A(1),    A(2),    A(3),
	0,      0,      0,      D(2),    D(2),    D(2),

	D(3),   D(4),   D(5),   D(1),    D(2),    D(3),
	0,      0,      0,      0,       0,       0,
	C(3),   C(4),   C(5),   0,       0,       0,
	0,      0,      0,      0,       0,       0,
	Ax(2),  Ax(3),  Ax(4),  0,       0,       0,
	0,      0,      0,      0,       0,       0,

	Ax(2),  Ax(3),  Ax(4),  Ax(1),   Ax(2),   Ax(3),
	0,      0,      0,      0,       0,       0,
	A(2),   A(3),   A(4),   0,       0,       0,
	0,      0,      0,      0,       0,       0,
	G(2),   G(3),   G(4),   0,       0,       0,
	0,      0,      0,      0,       0,       0,

	G(2),   G(3),   G(4),   G(1),    G(2),    G(3),
	0,      0,      0,      0,       0,       0,
	A(2),   A(3),   A(4),   0,       0,       0,
	0,      0,      0,      0,       0,       0,
	Ax(2),  Ax(3),  Ax(4),  0,       0,       0,
	0,      0,      0,      0,       0,       0,

	A(2),   A(3),   A(4),   A(1),    A(2),    A(3),
	0,      0,      0,      0,       0,       0,
	Ax(2),  Ax(3),  Ax(4),  0,       0,       0,
	0,      0,      0,      0,       0,       0,
	A(2),   A(3),   A(4),   0,       0,       0,
	0,      0,      0,      0,       0,       0,

	Cx(3),  Cx(4),  Cx(5),  A(1),    A(2),    A(3),
	0,      0,      0,      0,       0,       0,
	Ax(2),  Ax(3),  Ax(4),  0,       0,       0,
	0,      0,      0,      0,       0,       0,
	A(2),   A(3),   A(4),   0,       0,       0,
	0,      0,      0,      0,       0,       0,

	A(2),   A(3),   A(4),   F(1),    F(2),    F(3),
	0,      0,      0,      0,       0,       0,
	G(2),   G(3),   G(4),   0,       0,       0,
	0,      0,      0,      0,       0,       0,
	F(2),   F(3),   F(4),   0,       0,       0,
	0,      0,      0,      0,       0,       0,

	F(2),   F(3),   F(4),   D(1),    D(2),    D(3),
	0,      0,      0,      0,       0,       0,
	E(2),   E(3),   E(4),   0,       0,       0,
	0,      0,      0,      0,       0,       0,
	D(2),   D(3),   D(4),   0,       0,       0,
	0,      0,      0,      0,       0,       0,

	E(2),   E(3),   E(4),   E(1),    E(2),    E(3),
	0,      0,      0,      0,       0,       0,
	E(2),   E(3),   E(4),   0,       0,       0,
	0,      0,      0,      0,       0,       0,
	E(2),   E(3),   E(4),   0,       0,       0,
	0,      0,      0,      0,       0,       0,

	E(2),   E(3),   E(4),   Ax(1),   Ax(2),   Ax(3),
	0,      0,      0,      0,       0,       0,
	F(2),   F(3),   F(4),   0,       0,       0,
	0,      0,      0,      0,       0,       0,
	E(2),   E(3),   E(4),   F(1),    F(2),    F(3),
	0,      0,      0,      0,       0,       0,

	D(2),   D(3),   D(4),   D(1),    D(2),    D(3),
	0,      0,      0,      0,       0,       0,
	F(2),   F(3),   F(4),   A(1),    A(2),    A(3),
	0,      0,      0,      0,       0,       0,
	A(2),   A(3),   A(4),   F(1),    F(2),    F(3),
	0,      0,      0,      0,       0,       0,

	D(3),   D(4),   D(5),   D(1),    D(2),    D(3),
	0,      0,      0,      0,       0,       0,
	0,      0,      0,      0,       0,       0,
	0,      0,      0,      0,       0,       0,
	0,      0,      0,      0,       0,       0,
	0,      0,      0,      0,       0,       0
};

static const int *const tunes[] = {NULL, tune1, tune2, tune3};

void tms36xx_init(tms36xx_t *t, int clock, int samplerate, const float decay[6], float speed)
{
    memset(t, 0, sizeof(*t));
    t->samplerate = samplerate;
    t->basefreq = clock;
    t->speed = (speed > 0) ? (int)(TMS36XX_VMAX / speed) : TMS36XX_VMAX;

    int enable = 0;
    for (int j = 0; j < 6; j++)
    {
        if (decay[j] > 0)
        {
            t->decay[j + 0] = t->decay[j + 6] = (int)(TMS36XX_VMAX / decay[j]);
            enable |= 0x41 << j;
        }
    }

    /* duplicate the 6 voice enable bits; each voice has two instances */
    enable = (enable & 0x3f) | ((enable & 0x3f) << 6);
    int bits = 0;
    for (int i = 0; i < 6; i++)
        if (enable & (1 << i))
            bits += 2;
    t->enable = enable;
    t->voices = bits;
}

void mm6221aa_tune_w(tms36xx_t *t, int tune)
{
    tune &= 3;
    if (tune == t->tune_num)
        return;
    t->tune_num = tune;
    t->tune = tunes[tune];
    t->tune_ofs = 0;
    t->tune_max = 96; /* fixed for now */
}

int PHX_HOT(tms36xx_sample)(tms36xx_t *t)
{
    const int samplerate = t->samplerate;
    int sum = 0;

    /* no tune played? */
    if (!t->tune || t->voices == 0)
        return 0;

    /* decay the twelve voices */
    for (int v = 0; v < 12; v++)
    {
        if (t->vol[v] > TMS36XX_VMIN)
        {
            t->vol_counter[v] -= t->decay[v];
            while (t->vol_counter[v] <= 0)
            {
                t->vol_counter[v] += samplerate;
                if (t->vol[v]-- <= TMS36XX_VMIN)
                {
                    t->frequency[v] = 0;
                    t->vol[v] = TMS36XX_VMIN;
                    break;
                }
            }
        }
    }

    /* musical note timing */
    t->tune_counter -= t->speed;
    if (t->tune_counter <= 0)
    {
        int n = (-t->tune_counter / samplerate) + 1;
        t->tune_counter += n * samplerate;

        if ((t->note_counter -= n) <= 0)
        {
            t->note_counter += TMS36XX_VMAX;
            if (t->tune_ofs < t->tune_max)
            {
                /* shift to the other 'bank' of voices */
                t->shift ^= 6;
                /* restart one 'bank' of voices */
                const int *tune = t->tune;
                for (int v = 0; v < 6; v++)
                {
                    if (tune[t->tune_ofs * 6 + v])
                    {
                        t->frequency[t->shift + v] = tune[t->tune_ofs * 6 + v] * t->basefreq / FSCALE;
                        t->vol[t->shift + v] = TMS36XX_VMAX;
                    }
                }
                t->tune_ofs++;
            }
        }
    }

    /* update the twelve voices */
    for (int v = 0; v < 12; v++)
    {
        if ((t->enable & (1 << v)) && t->frequency[v])
        {
            t->counter[v] -= t->frequency[v];
            while (t->counter[v] <= 0)
            {
                t->counter[v] += samplerate;
                t->output ^= 1 << v;
            }
            if (t->output & t->enable & (1 << v))
                sum += t->vol[v];
        }
    }
    return sum;
}
