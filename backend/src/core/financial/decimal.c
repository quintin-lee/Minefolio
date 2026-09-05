#include "core/financial/decimal.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Precomputed powers of 10 from 10^0 up to 10^38 (fits in 128-bit unsigned integer) */
static const unsigned __int128 k_pow10[39] = {
    (unsigned __int128)1ULL,                                                     /* 10^0 */
    (unsigned __int128)10ULL,                                                    /* 10^1 */
    (unsigned __int128)100ULL,                                                   /* 10^2 */
    (unsigned __int128)1000ULL,                                                  /* 10^3 */
    (unsigned __int128)10000ULL,                                                 /* 10^4 */
    (unsigned __int128)100000ULL,                                                /* 10^5 */
    (unsigned __int128)1000000ULL,                                               /* 10^6 */
    (unsigned __int128)10000000ULL,                                              /* 10^7 */
    (unsigned __int128)100000000ULL,                                             /* 10^8 */
    (unsigned __int128)1000000000ULL,                                            /* 10^9 */
    (unsigned __int128)10000000000ULL,                                           /* 10^10 */
    (unsigned __int128)100000000000ULL,                                          /* 10^11 */
    (unsigned __int128)1000000000000ULL,                                         /* 10^12 */
    (unsigned __int128)10000000000000ULL,                                        /* 10^13 */
    (unsigned __int128)100000000000000ULL,                                       /* 10^14 */
    (unsigned __int128)1000000000000000ULL,                                      /* 10^15 */
    (unsigned __int128)10000000000000000ULL,                                     /* 10^16 */
    (unsigned __int128)100000000000000000ULL,                                    /* 10^17 */
    (unsigned __int128)1000000000000000000ULL,                                   /* 10^18 */
    (unsigned __int128)10000000000000000000ULL,                                  /* 10^19 */
    (((unsigned __int128)5ULL << 64) | 7766279631452241920ULL),                  /* 10^20 */
    (((unsigned __int128)54ULL << 64) | 3875820019684212736ULL),                 /* 10^21 */
    (((unsigned __int128)542ULL << 64) | 1864712049423024128ULL),                /* 10^22 */
    (((unsigned __int128)5421ULL << 64) | 200376420520689664ULL),                /* 10^23 */
    (((unsigned __int128)54210ULL << 64) | 2003764205206896640ULL),              /* 10^24 */
    (((unsigned __int128)542101ULL << 64) | 1590897978359414784ULL),             /* 10^25 */
    (((unsigned __int128)5421010ULL << 64) | 15908979783594147840ULL),           /* 10^26 */
    (((unsigned __int128)54210108ULL << 64) | 11515845246265065472ULL),          /* 10^27 */
    (((unsigned __int128)542101086ULL << 64) | 4477988020393345024ULL),          /* 10^28 */
    (((unsigned __int128)5421010862ULL << 64) | 7886392056514347008ULL),         /* 10^29 */
    (((unsigned __int128)54210108624ULL << 64) | 5076944270305263616ULL),        /* 10^30 */
    (((unsigned __int128)542101086242ULL << 64) | 13875954555633532928ULL),      /* 10^31 */
    (((unsigned __int128)5421010862427ULL << 64) | 9632337040368467968ULL),      /* 10^32 */
    (((unsigned __int128)54210108624275ULL << 64) | 4089650035136921600ULL),     /* 10^33 */
    (((unsigned __int128)542101086242752ULL << 64) | 4003012203950112768ULL),    /* 10^34 */
    (((unsigned __int128)5421010862427522ULL << 64) | 3136633892082024448ULL),   /* 10^35 */
    (((unsigned __int128)54210108624275221ULL << 64) | 12919594847110692864ULL), /* 10^36 */
    (((unsigned __int128)542101086242752217ULL << 64) | 68739955140067328ULL),   /* 10^37 */
    (((unsigned __int128)5421010862427522170ULL << 64) | 687399551400673280ULL)  /* 10^38 */
};

static inline unsigned __int128
pow10_u128(int32_t scale)
{
    if (scale <= 0) {
        return 1;
    }
    if (scale <= 38) {
        return k_pow10[scale];
    }
    return 0;
}

static inline __int128_t
pow10_val(int32_t scale)
{
    if (scale <= 0) {
        return 1;
    }
    if (scale <= 38) {
        return (__int128_t)k_pow10[scale];
    }
    return 0;
}

/* 256-bit unsigned integer arithmetic for intermediate exact operations */
typedef struct {
    unsigned __int128 hi;
    unsigned __int128 lo;
} u256_t;

static inline u256_t
u256_zero(void)
{
    u256_t r = {0, 0};
    return r;
}

static inline bool
u256_is_zero(u256_t a)
{
    return (a.hi == 0 && a.lo == 0);
}

static inline u256_t
u256_from_u128(unsigned __int128 v)
{
    u256_t r = {0, v};
    return r;
}

static inline int
u256_cmp(u256_t a, u256_t b)
{
    if (a.hi < b.hi) {
        return -1;
    }
    if (a.hi > b.hi) {
        return 1;
    }
    if (a.lo < b.lo) {
        return -1;
    }
    if (a.lo > b.lo) {
        return 1;
    }
    return 0;
}

static inline u256_t
u256_add(u256_t a, u256_t b)
{
    u256_t r;
    r.lo = a.lo + b.lo;
    r.hi = a.hi + b.hi + (r.lo < a.lo ? 1 : 0);
    return r;
}

static inline u256_t
u256_sub(u256_t a, u256_t b)
{
    u256_t r;
    r.lo = a.lo - b.lo;
    r.hi = a.hi - b.hi - (a.lo < b.lo ? 1 : 0);
    return r;
}

static inline u256_t
u256_shr1(u256_t a)
{
    u256_t r;
    r.lo = (a.lo >> 1) | (a.hi << 127);
    r.hi = a.hi >> 1;
    return r;
}

static inline u256_t
u256_shl(u256_t a, int shift)
{
    if (shift <= 0) {
        return a;
    }
    if (shift >= 256) {
        return u256_zero();
    }
    if (shift >= 128) {
        u256_t r = {.hi = a.lo << (shift - 128), .lo = 0};
        return r;
    }
    u256_t r = {.hi = (a.hi << shift) | (a.lo >> (128 - shift)), .lo = a.lo << shift};
    return r;
}

static inline int
u256_clz(u256_t a)
{
    if (a.hi != 0) {
        uint64_t hi_hi = (uint64_t)(a.hi >> 64);
        if (hi_hi != 0) {
            return __builtin_clzll(hi_hi);
        }
        return 64 + __builtin_clzll((uint64_t)a.hi);
    }
    if (a.lo != 0) {
        uint64_t lo_hi = (uint64_t)(a.lo >> 64);
        if (lo_hi != 0) {
            return 128 + __builtin_clzll(lo_hi);
        }
        return 192 + __builtin_clzll((uint64_t)a.lo);
    }
    return 256;
}

static inline u256_t
u128_mul_u128(unsigned __int128 a, unsigned __int128 b)
{
    uint64_t a_lo = (uint64_t)a;
    uint64_t a_hi = (uint64_t)(a >> 64);
    uint64_t b_lo = (uint64_t)b;
    uint64_t b_hi = (uint64_t)(b >> 64);

    unsigned __int128 p0 = (unsigned __int128)a_lo * b_lo;
    unsigned __int128 p1 = (unsigned __int128)a_lo * b_hi;
    unsigned __int128 p2 = (unsigned __int128)a_hi * b_lo;
    unsigned __int128 p3 = (unsigned __int128)a_hi * b_hi;

    unsigned __int128 mid = p1 + (uint64_t)(p0 >> 64);
    unsigned __int128 carry_hi = 0;
    if (mid < p1) {
        carry_hi += ((unsigned __int128)1 << 64);
    }

    unsigned __int128 mid2 = mid + p2;
    if (mid2 < mid) {
        carry_hi += ((unsigned __int128)1 << 64);
    }

    u256_t r;
    r.lo = ((unsigned __int128)(uint64_t)mid2 << 64) | (uint64_t)p0;
    r.hi = p3 + (mid2 >> 64) + carry_hi;
    return r;
}

static void
u256_div_rem(u256_t num, u256_t den, u256_t* q_out, u256_t* rem_out)
{
    int cmp = u256_cmp(num, den);
    if (cmp < 0) {
        if (q_out) {
            *q_out = u256_zero();
        }
        if (rem_out) {
            *rem_out = num;
        }
        return;
    }
    if (cmp == 0) {
        if (q_out) {
            *q_out = u256_from_u128(1);
        }
        if (rem_out) {
            *rem_out = u256_zero();
        }
        return;
    }

    int    shift = u256_clz(den) - u256_clz(num);
    u256_t current_den = u256_shl(den, shift);
    u256_t q = u256_zero();

    for (int i = shift; i >= 0; i--) {
        if (u256_cmp(num, current_den) >= 0) {
            num = u256_sub(num, current_den);
            if (i >= 128) {
                q.hi |= ((unsigned __int128)1 << (i - 128));
            } else {
                q.lo |= ((unsigned __int128)1 << i);
            }
        }
        current_den = u256_shr1(current_den);
    }

    if (q_out) {
        *q_out = q;
    }
    if (rem_out) {
        *rem_out = num;
    }
}

decimal_t
decimal_zero(void)
{
    decimal_t d = {.mantissa = 0, .scale = 0};
    return d;
}

decimal_t
decimal_one(void)
{
    decimal_t d = {.mantissa = 1, .scale = 0};
    return d;
}

decimal_t
decimal_from_int(int64_t val)
{
    decimal_t d = {.mantissa = (__int128_t)val, .scale = 0};
    return d;
}

decimal_t
decimal_from_parts(int64_t integer_part, int64_t fractional_part, int32_t scale)
{
    if (scale < 0) {
        scale = 0;
    }
    if (scale > DECIMAL_MAX_SCALE) {
        scale = DECIMAL_MAX_SCALE;
    }

    __int128_t m = (__int128_t)integer_part * pow10_val(scale);
    if (integer_part >= 0) {
        m += (__int128_t)fractional_part;
    } else {
        m -= (__int128_t)fractional_part;
    }

    decimal_t d = {.mantissa = m, .scale = scale};
    return d;
}

decimal_err_t
decimal_from_string(const char* str, decimal_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    if (!str || !*str) {
        *out = decimal_zero();
        return DECIMAL_OK;
    }

    /* Skip leading whitespace */
    while (isspace((unsigned char)*str)) {
        str++;
    }

    if (!*str) {
        *out = decimal_zero();
        return DECIMAL_OK;
    }

    int sign = 1;
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }

    unsigned __int128 integer_part = 0;
    unsigned __int128 frac_part = 0;
    int32_t           scale = 0;
    bool              has_dot = false;
    bool              has_digits = false;

    while (*str) {
        if (*str >= '0' && *str <= '9') {
            has_digits = true;
            if (!has_dot) {
                u256_t p = u128_mul_u128(integer_part, 10);
                p = u256_add(p, u256_from_u128((unsigned __int128)(*str - '0')));
                if (p.hi != 0) {
                    return DECIMAL_ERR_OVERFLOW;
                }
                integer_part = p.lo;
            } else {
                if (scale < DECIMAL_MAX_SCALE) {
                    frac_part = frac_part * 10 + (unsigned __int128)(*str - '0');
                    scale++;
                }
            }
        } else if (*str == '.') {
            if (has_dot) {
                break; /* Second dot encountered */
            }
            has_dot = true;
        } else if (isspace((unsigned char)*str) || *str == ',' || *str == '%') {
            if (*str == ',') {
                /* ignore thousand comma separator */
            } else {
                break;
            }
        } else {
            break;
        }
        str++;
    }

    if (!has_digits) {
        *out = decimal_zero();
        return DECIMAL_ERR_PARSE;
    }

    u256_t scaled = u128_mul_u128(integer_part, pow10_u128(scale));
    scaled = u256_add(scaled, u256_from_u128(frac_part));
    if (scaled.hi != 0) {
        return DECIMAL_ERR_OVERFLOW;
    }

    if (sign < 0) {
        if (scaled.lo > ((unsigned __int128)1 << 127)) {
            return DECIMAL_ERR_OVERFLOW;
        }
        out->mantissa = -(__int128_t)scaled.lo;
    } else {
        if (scaled.lo > (((unsigned __int128)1 << 127) - 1)) {
            return DECIMAL_ERR_OVERFLOW;
        }
        out->mantissa = (__int128_t)scaled.lo;
    }

    out->scale = scale;
    return DECIMAL_OK;
}

decimal_err_t
decimal_from_double(double d, int32_t scale, decimal_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    if (isnan(d) || isinf(d)) {
        *out = decimal_zero();
        return DECIMAL_ERR_INVALID_ARG;
    }
    if (scale < 0) {
        scale = 0;
    }
    if (scale > DECIMAL_MAX_SCALE) {
        scale = DECIMAL_MAX_SCALE;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "%.*f", (int)scale, d);
    return decimal_from_string(buf, out);
}

double
decimal_to_double(decimal_t d)
{
    return (double)d.mantissa / (double)pow10_val(d.scale);
}

int
decimal_to_string(decimal_t d, char* buf, size_t buf_size)
{
    return decimal_to_string_fixed(d, d.scale, buf, buf_size);
}

int
decimal_to_string_fixed(decimal_t d, int32_t target_scale, char* buf, size_t buf_size)
{
    if (!buf || buf_size == 0) {
        return 0;
    }

    if (target_scale < 0) {
        target_scale = 0;
    }
    if (target_scale > DECIMAL_MAX_SCALE) {
        target_scale = DECIMAL_MAX_SCALE;
    }

    decimal_t rounded = d;
    if (d.scale > target_scale) {
        decimal_round(d, target_scale, ROUND_HALF_UP, &rounded);
    }

    bool              is_neg = (rounded.mantissa < 0);
    unsigned __int128 u = is_neg ? (unsigned __int128)(-(unsigned __int128)rounded.mantissa)
                                 : (unsigned __int128)rounded.mantissa;

    unsigned __int128 p = pow10_u128(rounded.scale);
    unsigned __int128 int_part = (p > 0) ? (u / p) : u;
    unsigned __int128 frac_part = (p > 0) ? (u % p) : 0;

    if (u == 0) {
        is_neg = false;
    }

    char int_str[64];
    char frac_str[64];

    /* Format int part */
    if (int_part == 0) {
        strcpy(int_str, "0");
    } else {
        int idx = 0;
        while (int_part > 0) {
            int_str[idx++] = (char)('0' + (int_part % 10));
            int_part /= 10;
        }
        int_str[idx] = '\0';
        /* Reverse */
        for (int i = 0, j = idx - 1; i < j; i++, j--) {
            char tmp = int_str[i];
            int_str[i] = int_str[j];
            int_str[j] = tmp;
        }
    }

    /* Format frac part */
    if (target_scale > 0) {
        int idx = rounded.scale;
        frac_str[idx] = '\0';
        while (idx > 0) {
            frac_str[--idx] = (char)('0' + (frac_part % 10));
            frac_part /= 10;
        }
        int current_len = rounded.scale;
        while (current_len < target_scale && current_len < (int)sizeof(frac_str) - 1) {
            frac_str[current_len++] = '0';
        }
        frac_str[current_len] = '\0';
        return snprintf(buf, buf_size, "%s%s.%s", is_neg ? "-" : "", int_str, frac_str);
    }

    return snprintf(buf, buf_size, "%s%s", is_neg ? "-" : "", int_str);
}

decimal_err_t
decimal_add(decimal_t a, decimal_t b, decimal_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    if (a.scale < 0 || a.scale > DECIMAL_MAX_SCALE || b.scale < 0 || b.scale > DECIMAL_MAX_SCALE) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    int32_t    max_scale = a.scale > b.scale ? a.scale : b.scale;
    __int128_t m_a = a.mantissa;
    __int128_t m_b = b.mantissa;

    if (a.scale < max_scale) {
        int32_t diff = max_scale - a.scale;
        if (__builtin_mul_overflow(m_a, pow10_val(diff), &m_a)) {
            return DECIMAL_ERR_OVERFLOW;
        }
    }
    if (b.scale < max_scale) {
        int32_t diff = max_scale - b.scale;
        if (__builtin_mul_overflow(m_b, pow10_val(diff), &m_b)) {
            return DECIMAL_ERR_OVERFLOW;
        }
    }

    if (__builtin_add_overflow(m_a, m_b, &out->mantissa)) {
        return DECIMAL_ERR_OVERFLOW;
    }
    out->scale = max_scale;
    return DECIMAL_OK;
}

decimal_err_t
decimal_sub(decimal_t a, decimal_t b, decimal_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    if (a.scale < 0 || a.scale > DECIMAL_MAX_SCALE || b.scale < 0 || b.scale > DECIMAL_MAX_SCALE) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    int32_t    max_scale = a.scale > b.scale ? a.scale : b.scale;
    __int128_t m_a = a.mantissa;
    __int128_t m_b = b.mantissa;

    if (a.scale < max_scale) {
        int32_t diff = max_scale - a.scale;
        if (__builtin_mul_overflow(m_a, pow10_val(diff), &m_a)) {
            return DECIMAL_ERR_OVERFLOW;
        }
    }
    if (b.scale < max_scale) {
        int32_t diff = max_scale - b.scale;
        if (__builtin_mul_overflow(m_b, pow10_val(diff), &m_b)) {
            return DECIMAL_ERR_OVERFLOW;
        }
    }

    if (__builtin_sub_overflow(m_a, m_b, &out->mantissa)) {
        return DECIMAL_ERR_OVERFLOW;
    }
    out->scale = max_scale;
    return DECIMAL_OK;
}

decimal_err_t
decimal_mul(decimal_t a, decimal_t b, decimal_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    if (a.scale < 0 || a.scale > DECIMAL_MAX_SCALE || b.scale < 0 || b.scale > DECIMAL_MAX_SCALE) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    int32_t scale = a.scale + b.scale;
    if (a.mantissa == 0 || b.mantissa == 0) {
        if (scale > DECIMAL_MAX_SCALE) {
            scale = DECIMAL_MAX_SCALE;
        }
        *out = (decimal_t){.mantissa = 0, .scale = scale};
        return DECIMAL_OK;
    }

    bool a_neg = (a.mantissa < 0);
    bool b_neg = (b.mantissa < 0);
    bool is_neg = (a_neg != b_neg);

    unsigned __int128 u_a =
        a_neg ? (unsigned __int128)(-(unsigned __int128)a.mantissa) : (unsigned __int128)a.mantissa;
    unsigned __int128 u_b =
        b_neg ? (unsigned __int128)(-(unsigned __int128)b.mantissa) : (unsigned __int128)b.mantissa;

    u256_t prod = u128_mul_u128(u_a, u_b);

    if (scale > DECIMAL_MAX_SCALE) {
        int32_t diff = scale - DECIMAL_MAX_SCALE;
        u256_t  D = u256_from_u128(pow10_u128(diff));
        u256_t  Q, Rem;
        u256_div_rem(prod, D, &Q, &Rem);

        int cmp_half = u256_cmp(u256_add(Rem, Rem), D);
        if (cmp_half >= 0) {
            Q = u256_add(Q, u256_from_u128(1));
        }

        if (Q.hi != 0) {
            return DECIMAL_ERR_OVERFLOW;
        }
        if (is_neg) {
            if (Q.lo > ((unsigned __int128)1 << 127)) {
                return DECIMAL_ERR_OVERFLOW;
            }
            out->mantissa = -(__int128_t)Q.lo;
        } else {
            if (Q.lo > (((unsigned __int128)1 << 127) - 1)) {
                return DECIMAL_ERR_OVERFLOW;
            }
            out->mantissa = (__int128_t)Q.lo;
        }
        out->scale = DECIMAL_MAX_SCALE;
        return DECIMAL_OK;
    }

    if (prod.hi != 0) {
        return DECIMAL_ERR_OVERFLOW;
    }
    if (is_neg) {
        if (prod.lo > ((unsigned __int128)1 << 127)) {
            return DECIMAL_ERR_OVERFLOW;
        }
        out->mantissa = -(__int128_t)prod.lo;
    } else {
        if (prod.lo > (((unsigned __int128)1 << 127) - 1)) {
            return DECIMAL_ERR_OVERFLOW;
        }
        out->mantissa = (__int128_t)prod.lo;
    }
    out->scale = scale;
    return DECIMAL_OK;
}

decimal_err_t
decimal_div(decimal_t a, decimal_t b, int32_t target_scale, round_mode_t mode, decimal_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    if (b.mantissa == 0) {
        *out = decimal_zero();
        return DECIMAL_ERR_DIV_BY_ZERO;
    }
    if (target_scale < 0 || target_scale > DECIMAL_MAX_SCALE) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    if (mode < ROUND_HALF_UP || mode > ROUND_FLOOR) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    if (a.scale < 0 || a.scale > DECIMAL_MAX_SCALE || b.scale < 0 || b.scale > DECIMAL_MAX_SCALE) {
        return DECIMAL_ERR_INVALID_ARG;
    }

    if (a.mantissa == 0) {
        *out = (decimal_t){.mantissa = 0, .scale = target_scale};
        return DECIMAL_OK;
    }

    bool a_neg = (a.mantissa < 0);
    bool b_neg = (b.mantissa < 0);
    bool is_neg = (a_neg != b_neg);

    unsigned __int128 u_a =
        a_neg ? (unsigned __int128)(-(unsigned __int128)a.mantissa) : (unsigned __int128)a.mantissa;
    unsigned __int128 u_b =
        b_neg ? (unsigned __int128)(-(unsigned __int128)b.mantissa) : (unsigned __int128)b.mantissa;

    int32_t shift = target_scale + b.scale - a.scale;
    u256_t  N, D;
    if (shift >= 0) {
        N = u128_mul_u128(u_a, pow10_u128(shift));
        D = u256_from_u128(u_b);
    } else {
        N = u256_from_u128(u_a);
        D = u128_mul_u128(u_b, pow10_u128(-shift));
    }

    u256_t Q, Rem;
    u256_div_rem(N, D, &Q, &Rem);

    bool has_rem = !u256_is_zero(Rem);
    int  cmp_half = u256_cmp(u256_add(Rem, Rem), D);

    bool round_up = false;
    switch (mode) {
    case ROUND_HALF_UP:
        if (cmp_half >= 0) {
            round_up = true;
        }
        break;
    case ROUND_HALF_EVEN:
        if (cmp_half > 0) {
            round_up = true;
        } else if (cmp_half == 0) {
            if ((Q.lo & 1) != 0) {
                round_up = true;
            }
        }
        break;
    case ROUND_DOWN:
        round_up = false;
        break;
    case ROUND_UP:
        if (has_rem) {
            round_up = true;
        }
        break;
    case ROUND_CEIL:
        if (!is_neg && has_rem) {
            round_up = true;
        }
        break;
    case ROUND_FLOOR:
        if (is_neg && has_rem) {
            round_up = true;
        }
        break;
    }

    if (round_up) {
        Q = u256_add(Q, u256_from_u128(1));
    }

    if (Q.hi != 0) {
        return DECIMAL_ERR_OVERFLOW;
    }
    if (is_neg) {
        if (Q.lo > ((unsigned __int128)1 << 127)) {
            return DECIMAL_ERR_OVERFLOW;
        }
        out->mantissa = -(__int128_t)Q.lo;
    } else {
        if (Q.lo > (((unsigned __int128)1 << 127) - 1)) {
            return DECIMAL_ERR_OVERFLOW;
        }
        out->mantissa = (__int128_t)Q.lo;
    }
    out->scale = target_scale;
    return DECIMAL_OK;
}

decimal_t
decimal_abs(decimal_t d)
{
    if (d.mantissa < 0 && d.mantissa != ((__int128_t)1 << 127)) {
        d.mantissa = -d.mantissa;
    }
    return d;
}

decimal_t
decimal_neg(decimal_t d)
{
    if (d.mantissa != ((__int128_t)1 << 127)) {
        d.mantissa = -d.mantissa;
    }
    return d;
}

int
decimal_cmp(decimal_t a, decimal_t b)
{
    if (a.mantissa == 0 && b.mantissa == 0) {
        return 0;
    }
    bool a_neg = (a.mantissa < 0);
    bool b_neg = (b.mantissa < 0);

    if (a.mantissa == 0) {
        return b_neg ? 1 : -1;
    }
    if (b.mantissa == 0) {
        return a_neg ? -1 : 1;
    }
    if (a_neg != b_neg) {
        return a_neg ? -1 : 1;
    }

    unsigned __int128 u_a =
        a_neg ? (unsigned __int128)(-(unsigned __int128)a.mantissa) : (unsigned __int128)a.mantissa;
    unsigned __int128 u_b =
        b_neg ? (unsigned __int128)(-(unsigned __int128)b.mantissa) : (unsigned __int128)b.mantissa;

    int32_t max_scale = a.scale > b.scale ? a.scale : b.scale;
    u256_t  a_val = u128_mul_u128(u_a, pow10_u128(max_scale - a.scale));
    u256_t  b_val = u128_mul_u128(u_b, pow10_u128(max_scale - b.scale));

    int cmp = u256_cmp(a_val, b_val);
    return a_neg ? -cmp : cmp;
}

bool
decimal_is_zero(decimal_t d)
{
    return d.mantissa == 0;
}

bool
decimal_is_negative(decimal_t d)
{
    return d.mantissa < 0;
}

bool
decimal_is_positive(decimal_t d)
{
    return d.mantissa > 0;
}

decimal_err_t
decimal_round(decimal_t d, int32_t target_scale, round_mode_t mode, decimal_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    if (target_scale < 0 || target_scale > DECIMAL_MAX_SCALE) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    if (mode < ROUND_HALF_UP || mode > ROUND_FLOOR) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    if (d.scale < 0 || d.scale > DECIMAL_MAX_SCALE) {
        return DECIMAL_ERR_INVALID_ARG;
    }

    if (d.mantissa == 0) {
        *out = (decimal_t){.mantissa = 0, .scale = target_scale};
        return DECIMAL_OK;
    }

    if (d.scale <= target_scale) {
        int32_t diff = target_scale - d.scale;
        if (diff == 0) {
            *out = d;
            return DECIMAL_OK;
        }
        __int128_t m;
        if (__builtin_mul_overflow(d.mantissa, pow10_val(diff), &m)) {
            return DECIMAL_ERR_OVERFLOW;
        }
        out->mantissa = m;
        out->scale = target_scale;
        return DECIMAL_OK;
    }

    int32_t           diff = d.scale - target_scale;
    unsigned __int128 p = pow10_u128(diff);

    bool              is_neg = (d.mantissa < 0);
    unsigned __int128 u = is_neg ? (unsigned __int128)(-(unsigned __int128)d.mantissa)
                                 : (unsigned __int128)d.mantissa;

    unsigned __int128 q = u / p;
    unsigned __int128 rem = u % p;
    bool              has_rem = (rem > 0);

    bool round_up = false;
    switch (mode) {
    case ROUND_HALF_UP:
        if (rem >= p - rem) {
            round_up = true;
        }
        break;
    case ROUND_HALF_EVEN:
        if (rem > p - rem) {
            round_up = true;
        } else if (rem == p - rem) {
            if ((q & 1) != 0) {
                round_up = true;
            }
        }
        break;
    case ROUND_DOWN:
        round_up = false;
        break;
    case ROUND_UP:
        if (has_rem) {
            round_up = true;
        }
        break;
    case ROUND_CEIL:
        if (!is_neg && has_rem) {
            round_up = true;
        }
        break;
    case ROUND_FLOOR:
        if (is_neg && has_rem) {
            round_up = true;
        }
        break;
    }

    if (round_up) {
        q += 1;
    }

    if (is_neg) {
        if (q > ((unsigned __int128)1 << 127)) {
            return DECIMAL_ERR_OVERFLOW;
        }
        out->mantissa = -(__int128_t)q;
    } else {
        if (q > (((unsigned __int128)1 << 127) - 1)) {
            return DECIMAL_ERR_OVERFLOW;
        }
        out->mantissa = (__int128_t)q;
    }
    out->scale = target_scale;
    return DECIMAL_OK;
}

decimal_t
decimal_rescale(decimal_t d, int32_t target_scale)
{
    decimal_t out;
    if (decimal_round(d, target_scale, ROUND_HALF_UP, &out) == DECIMAL_OK) {
        return out;
    }
    return d;
}

decimal_t
decimal_min(decimal_t a, decimal_t b)
{
    return decimal_cmp(a, b) <= 0 ? a : b;
}

decimal_t
decimal_max(decimal_t a, decimal_t b)
{
    return decimal_cmp(a, b) >= 0 ? a : b;
}
