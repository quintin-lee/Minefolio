#include "core/financial/percentage.h"

percentage_t
percentage_zero(void)
{
    percentage_t p = {.percent = decimal_zero()};
    return p;
}

percentage_t
percentage_from_decimal(decimal_t pct)
{
    percentage_t p = {.percent = pct};
    return p;
}

decimal_err_t
percentage_from_string(const char* str, percentage_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    decimal_t     d;
    decimal_err_t err = decimal_from_string(str, &d);
    if (err != DECIMAL_OK) {
        return err;
    }
    out->percent = d;
    return DECIMAL_OK;
}

decimal_err_t
percentage_from_double(double d, int32_t scale, percentage_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    decimal_t     dec;
    decimal_err_t err = decimal_from_double(d, scale, &dec);
    if (err != DECIMAL_OK) {
        return err;
    }
    out->percent = dec;
    return DECIMAL_OK;
}

double
percentage_to_double(percentage_t p)
{
    return decimal_to_double(p.percent);
}

int
percentage_to_string(percentage_t p, char* buf, size_t buf_size)
{
    return decimal_to_string(p.percent, buf, buf_size);
}

int
percentage_to_string_fixed(percentage_t p, int32_t scale, char* buf, size_t buf_size)
{
    return decimal_to_string_fixed(p.percent, scale, buf, buf_size);
}

int
percentage_cmp(percentage_t a, percentage_t b)
{
    return decimal_cmp(a.percent, b.percent);
}

decimal_err_t
percentage_apply(money_t in, percentage_t p, money_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    decimal_t     hundred = decimal_from_int(100);
    decimal_t     ratio;
    decimal_err_t err = decimal_div(p.percent, hundred, 8, ROUND_HALF_UP, &ratio);
    if (err != DECIMAL_OK) {
        return err;
    }

    decimal_t total;
    err = decimal_mul(in.amount, ratio, &total);
    if (err != DECIMAL_OK) {
        return err;
    }

    uint8_t   prec = currency_precision(in.currency);
    decimal_t rounded;
    decimal_round(total, (int32_t)prec, ROUND_HALF_UP, &rounded);

    out->amount = rounded;
    out->currency = in.currency;
    return DECIMAL_OK;
}

decimal_err_t
percentage_calc(money_t part, money_t whole, int32_t scale, round_mode_t mode, percentage_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    if (money_is_zero(whole)) {
        *out = percentage_zero();
        return DECIMAL_ERR_DIV_BY_ZERO;
    }
    if (scale <= 0) {
        scale = 2;
    }

    /* Compute ratio = (part / whole) */
    decimal_t     ratio;
    int32_t       calc_scale = scale + 2; /* Calculate extra 2 digits because we multiply by 100 */
    decimal_err_t err = decimal_div(part.amount, whole.amount, calc_scale, mode, &ratio);
    if (err != DECIMAL_OK) {
        return err;
    }

    decimal_t hundred = decimal_from_int(100);
    decimal_t pct;
    err = decimal_mul(ratio, hundred, &pct);
    if (err != DECIMAL_OK) {
        return err;
    }

    decimal_t rounded;
    decimal_round(pct, scale, mode, &rounded);

    out->percent = rounded;
    return DECIMAL_OK;
}
