/*
 * expm1f and log1pf for the sound path, inlined into their callers so they run
 * from SRAM: the libm versions live in flash, and the 555 timers call them on
 * every oscillator edge, tens of thousands of times a second.
 *
 * Only the ranges the 555 model needs are covered, both with single-precision
 * accuracy (a few ULP, checked against libm in the host harness):
 *   phx_expm1f(x)       x <= 0
 *   phx_log1pf_neg(y)   log(1 - y), 0 <= y < 1
 */
#ifndef PHX_MATH_H
#define PHX_MATH_H

#include <stdint.h>
#include <string.h>

#define PHX_ALWAYS_INLINE static inline __attribute__((always_inline))

/* exp(r) for |r| <= ln2/2, Taylor to r^7 (error < 2e-9 relative) */
PHX_ALWAYS_INLINE float phx_exp_reduced(float r)
{
    return 1.0f + r * (1.0f + r * (0.5f + r * (1.0f / 6 + r * (1.0f / 24 + r * (1.0f / 120 + r * (1.0f / 720 + r * (1.0f / 5040)))))));
}

/* 2^k as a float, k in -126..127 */
PHX_ALWAYS_INLINE float phx_pow2i(int k)
{
    uint32_t bits = (uint32_t)(k + 127) << 23;
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

/* exp(x) - 1 for x <= 0 */
PHX_ALWAYS_INLINE float phx_expm1f(float x)
{
    if (x > -0.5f)
    {
        /* Taylor around 0 avoids the cancellation of exp(x) - 1; to x^8 the
         * error at x = -0.5 is below 3e-9 relative */
        return x * (1.0f + x * (0.5f + x * (1.0f / 6 + x * (1.0f / 24 + x * (1.0f / 120 + x * (1.0f / 720 + x * (1.0f / 5040 + x * (1.0f / 40320))))))));
    }
    if (x < -87.0f)
        return -1.0f;
    /* x = k ln2 + r, |r| <= ln2/2 */
    const float ln2_hi = 0.693145751953125f, ln2_lo = 1.428606765330187e-06f;
    int k = (int)(x * 1.4426950408889634f - 0.5f);
    float r = (x - k * ln2_hi) - k * ln2_lo;
    return phx_exp_reduced(r) * phx_pow2i(k) - 1.0f;
}

/* log(m) for m in [sqrt(0.5), sqrt(2)): 2 atanh(s), s = (m - 1) / (m + 1) */
PHX_ALWAYS_INLINE float phx_log_atanh(float s)
{
    float s2 = s * s;
    return 2.0f * s * (1.0f + s2 * (1.0f / 3 + s2 * (1.0f / 5 + s2 * (1.0f / 7 + s2 * (1.0f / 9 + s2 * (1.0f / 11))))));
}

/* log(1 - y) for 0 <= y < 1 */
PHX_ALWAYS_INLINE float phx_log1pf_neg(float y)
{
    if (y < 0.29f)
    {
        /* log1p(u) = 2 atanh(u / (2 + u)) with u = -y, exact in u */
        return phx_log_atanh(-y / (2.0f - y));
    }
    if (y > 0.99999994f)
        y = 0.99999994f;
    /* 1 - y is exact enough here; split it into m * 2^e, m in [sqrt(0.5), sqrt(2)) */
    float z = 1.0f - y;
    uint32_t bits;
    memcpy(&bits, &z, sizeof(bits));
    int e = (int)((bits >> 23) & 0xff) - 127;
    bits = (bits & 0x007fffffu) | 0x3f800000u; /* m in [1, 2) */
    float m;
    memcpy(&m, &bits, sizeof(m));
    if (m > 1.41421356f)
    {
        m *= 0.5f;
        e++;
    }
    return phx_log_atanh((m - 1.0f) / (m + 1.0f)) + e * 0.6931471805599453f;
}

#endif
