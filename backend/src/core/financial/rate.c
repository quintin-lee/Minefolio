#include "core/financial/rate.h"

rate_t
rate_one(currency_t from_cur, currency_t to_cur)
{
    rate_t r = {.factor = decimal_one(), .from_currency = from_cur, .to_currency = to_cur};
    return r;
}

rate_t
rate_from_decimal(decimal_t factor, currency_t from_cur, currency_t to_cur)
{
    rate_t r = {.factor = factor, .from_currency = from_cur, .to_currency = to_cur};
    return r;
}

decimal_err_t
rate_from_string(const char* str, currency_t from_cur, currency_t to_cur, rate_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    decimal_t d;
    decimal_err_t err = decimal_from_string(str, &d);
    if (err != DECIMAL_OK) {
        return err;
    }
    out->factor = d;
    out->from_currency = from_cur;
    out->to_currency = to_cur;
    return DECIMAL_OK;
}

decimal_err_t
rate_from_double(double d, int32_t scale, currency_t from_cur, currency_t to_cur, rate_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    decimal_t dec;
    decimal_err_t err = decimal_from_double(d, scale, &dec);
    if (err != DECIMAL_OK) {
        return err;
    }
    out->factor = dec;
    out->from_currency = from_cur;
    out->to_currency = to_cur;
    return DECIMAL_OK;
}

double
rate_to_double(rate_t r)
{
    return decimal_to_double(r.factor);
}

int
rate_to_string(rate_t r, char* buf, size_t buf_size)
{
    return decimal_to_string(r.factor, buf, buf_size);
}

int
rate_to_string_fixed(rate_t r, int32_t scale, char* buf, size_t buf_size)
{
    return decimal_to_string_fixed(r.factor, scale, buf, buf_size);
}

decimal_err_t
rate_convert_money(money_t in, rate_t r, money_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    if (!currency_equals(in.currency, r.from_currency)) {
        *out = money_zero(r.to_currency);
        return DECIMAL_ERR_INVALID_ARG; /* Input currency does not match rate source */
    }

    decimal_t total;
    decimal_err_t err = decimal_mul(in.amount, r.factor, &total);
    if (err != DECIMAL_OK) {
        return err;
    }

    uint8_t prec = currency_precision(r.to_currency);
    decimal_t rounded;
    decimal_round(total, (int32_t)prec, ROUND_HALF_UP, &rounded);

    out->amount = rounded;
    out->currency = r.to_currency;
    return DECIMAL_OK;
}

decimal_err_t
rate_invert(rate_t r, int32_t scale, rate_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    if (decimal_is_zero(r.factor)) {
        *out = rate_one(r.to_currency, r.from_currency);
        return DECIMAL_ERR_DIV_BY_ZERO;
    }
    if (scale <= 0) {
        scale = 6;
    }

    decimal_t one = decimal_one();
    decimal_t inv;
    decimal_err_t err = decimal_div(one, r.factor, scale, ROUND_HALF_UP, &inv);
    if (err != DECIMAL_OK) {
        return err;
    }

    out->factor = inv;
    out->from_currency = r.to_currency;
    out->to_currency = r.from_currency;
    return DECIMAL_OK;
}

decimal_err_t
rate_chain(rate_t a_to_b, rate_t b_to_c, int32_t scale, rate_t* out_a_to_c)
{
    if (!out_a_to_c) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    if (!currency_equals(a_to_b.to_currency, b_to_c.from_currency)) {
        *out_a_to_c = rate_one(a_to_b.from_currency, b_to_c.to_currency);
        return DECIMAL_ERR_INVALID_ARG; /* Incompatible currency chain */
    }
    if (scale <= 0) {
        scale = 6;
    }

    decimal_t product;
    decimal_err_t err = decimal_mul(a_to_b.factor, b_to_c.factor, &product);
    if (err != DECIMAL_OK) {
        return err;
    }

    decimal_t rounded;
    decimal_round(product, scale, ROUND_HALF_UP, &rounded);

    out_a_to_c->factor = rounded;
    out_a_to_c->from_currency = a_to_b.from_currency;
    out_a_to_c->to_currency = b_to_c.to_currency;
    return DECIMAL_OK;
}
