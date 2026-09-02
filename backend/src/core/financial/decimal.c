#include "core/financial/decimal.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const __int128_t k_pow10[19] = {1ULL,
                                       10ULL,
                                       100ULL,
                                       1000ULL,
                                       10000ULL,
                                       100000ULL,
                                       1000000ULL,
                                       10000000ULL,
                                       100000000ULL,
                                       1000000000ULL,
                                       10000000000ULL,
                                       100000000000ULL,
                                       1000000000000ULL,
                                       10000000000000ULL,
                                       100000000000000ULL,
                                       1000000000000000ULL,
                                       10000000000000000ULL,
                                       100000000000000000ULL,
                                       1000000000000000000ULL};

static __int128_t
pow10_val(int32_t scale)
{
    if (scale <= 0) {
        return 1;
    }
    if (scale <= 18) {
        return k_pow10[scale];
    }
    __int128_t res = k_pow10[18];
    for (int i = 18; i < scale; i++) {
        res *= 10;
    }
    return res;
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

    __int128_t integer_part = 0;
    __int128_t frac_part = 0;
    int32_t    scale = 0;
    bool       has_dot = false;
    bool       has_digits = false;

    while (*str) {
        if (*str >= '0' && *str <= '9') {
            has_digits = true;
            if (!has_dot) {
                integer_part = integer_part * 10 + (*str - '0');
            } else {
                if (scale < DECIMAL_MAX_SCALE) {
                    frac_part = frac_part * 10 + (*str - '0');
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

    __int128_t total = integer_part * pow10_val(scale) + frac_part;
    if (sign < 0) {
        total = -total;
    }

    out->mantissa = total;
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

    decimal_t rounded;
    decimal_round(d, target_scale, ROUND_HALF_UP, &rounded);

    __int128_t m = rounded.mantissa;
    bool       is_neg = (m < 0);
    if (is_neg) {
        m = -m;
    }

    __int128_t p = pow10_val(rounded.scale);
    __int128_t int_part = (p > 0) ? (m / p) : m;
    __int128_t frac_part = (p > 0) ? (m % p) : 0;

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
    if (rounded.scale > 0) {
        int idx = rounded.scale;
        frac_str[idx] = '\0';
        while (idx > 0) {
            frac_str[--idx] = (char)('0' + (frac_part % 10));
            frac_part /= 10;
        }
        return snprintf(buf, buf_size, "%s%s.%s", is_neg ? "-" : "", int_str, frac_str);
    }

    return snprintf(buf, buf_size, "%s%s", is_neg ? "-" : "", int_str);
}

static decimal_t
align_scale(decimal_t d, int32_t target_scale)
{
    if (d.scale == target_scale) {
        return d;
    }
    if (d.scale < target_scale) {
        int32_t diff = target_scale - d.scale;
        d.mantissa *= pow10_val(diff);
        d.scale = target_scale;
        return d;
    }
    /* d.scale > target_scale */
    decimal_t out;
    decimal_round(d, target_scale, ROUND_HALF_UP, &out);
    return out;
}

decimal_err_t
decimal_add(decimal_t a, decimal_t b, decimal_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    int32_t   max_scale = a.scale > b.scale ? a.scale : b.scale;
    decimal_t a_aligned = align_scale(a, max_scale);
    decimal_t b_aligned = align_scale(b, max_scale);

    out->mantissa = a_aligned.mantissa + b_aligned.mantissa;
    out->scale = max_scale;
    return DECIMAL_OK;
}

decimal_err_t
decimal_sub(decimal_t a, decimal_t b, decimal_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    int32_t   max_scale = a.scale > b.scale ? a.scale : b.scale;
    decimal_t a_aligned = align_scale(a, max_scale);
    decimal_t b_aligned = align_scale(b, max_scale);

    out->mantissa = a_aligned.mantissa - b_aligned.mantissa;
    out->scale = max_scale;
    return DECIMAL_OK;
}

decimal_err_t
decimal_mul(decimal_t a, decimal_t b, decimal_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    __int128_t m = a.mantissa * b.mantissa;
    int32_t    scale = a.scale + b.scale;

    if (scale > DECIMAL_MAX_SCALE) {
        decimal_t tmp = {.mantissa = m, .scale = scale};
        return decimal_round(tmp, DECIMAL_MAX_SCALE, ROUND_HALF_UP, out);
    }

    out->mantissa = m;
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
    if (target_scale < 0) {
        target_scale = 0;
    }
    if (target_scale > DECIMAL_MAX_SCALE) {
        target_scale = DECIMAL_MAX_SCALE;
    }

    /* Compute with 1 extra digit for precision rounding */
    int32_t calc_scale = target_scale + 1;
    int32_t shift = calc_scale + b.scale - a.scale;

    __int128_t num = a.mantissa;
    __int128_t den = b.mantissa;

    if (shift >= 0) {
        num *= pow10_val(shift);
    } else {
        den *= pow10_val(-shift);
    }

    __int128_t q = num / den;
    __int128_t rem = num % den;

    decimal_t tmp = {.mantissa = q, .scale = calc_scale};
    (void)rem;
    return decimal_round(tmp, target_scale, mode, out);
}

decimal_t
decimal_abs(decimal_t d)
{
    if (d.mantissa < 0) {
        d.mantissa = -d.mantissa;
    }
    return d;
}

decimal_t
decimal_neg(decimal_t d)
{
    d.mantissa = -d.mantissa;
    return d;
}

int
decimal_cmp(decimal_t a, decimal_t b)
{
    int32_t   max_scale = a.scale > b.scale ? a.scale : b.scale;
    decimal_t a_aligned = align_scale(a, max_scale);
    decimal_t b_aligned = align_scale(b, max_scale);

    if (a_aligned.mantissa < b_aligned.mantissa) {
        return -1;
    }
    if (a_aligned.mantissa > b_aligned.mantissa) {
        return 1;
    }
    return 0;
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

    if (d.scale <= target_scale) {
        int32_t diff = target_scale - d.scale;
        out->mantissa = d.mantissa * pow10_val(diff);
        out->scale = target_scale;
        return DECIMAL_OK;
    }

    int32_t    diff = d.scale - target_scale;
    __int128_t p = pow10_val(diff);

    bool       is_neg = (d.mantissa < 0);
    __int128_t m = is_neg ? -d.mantissa : d.mantissa;

    __int128_t q = m / p;
    __int128_t rem = m % p;
    __int128_t half = p / 2;

    switch (mode) {
    case ROUND_HALF_UP:
        if (rem >= half) {
            q += 1;
        }
        break;
    case ROUND_HALF_EVEN:
        if (rem > half) {
            q += 1;
        } else if (rem == half) {
            if (q % 2 != 0) {
                q += 1;
            }
        }
        break;
    case ROUND_DOWN:
        /* Truncate magnitude */
        break;
    case ROUND_UP:
        if (rem > 0) {
            q += 1;
        }
        break;
    case ROUND_CEIL:
        if (!is_neg && rem > 0) {
            q += 1;
        }
        break;
    case ROUND_FLOOR:
        if (is_neg && rem > 0) {
            q += 1;
        }
        break;
    }

    if (is_neg) {
        q = -q;
    }

    out->mantissa = q;
    out->scale = target_scale;
    return DECIMAL_OK;
}

decimal_t
decimal_rescale(decimal_t d, int32_t target_scale)
{
    decimal_t out;
    decimal_round(d, target_scale, ROUND_HALF_UP, &out);
    return out;
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
