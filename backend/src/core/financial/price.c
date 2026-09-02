#include "core/financial/price.h"

price_t
price_zero(currency_t cur)
{
    price_t p = {.unit_price = decimal_zero(), .currency = cur};
    return p;
}

price_t
price_from_decimal(decimal_t d, currency_t cur)
{
    price_t p = {.unit_price = d, .currency = cur};
    return p;
}

decimal_err_t
price_from_string(const char* str, currency_t cur, price_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    decimal_t d;
    decimal_err_t err = decimal_from_string(str, &d);
    if (err != DECIMAL_OK) {
        return err;
    }
    out->unit_price = d;
    out->currency = cur;
    return DECIMAL_OK;
}

decimal_err_t
price_from_double(double d, int32_t scale, currency_t cur, price_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    decimal_t dec;
    decimal_err_t err = decimal_from_double(d, scale, &dec);
    if (err != DECIMAL_OK) {
        return err;
    }
    out->unit_price = dec;
    out->currency = cur;
    return DECIMAL_OK;
}

double
price_to_double(price_t p)
{
    return decimal_to_double(p.unit_price);
}

int
price_to_string(price_t p, char* buf, size_t buf_size)
{
    return decimal_to_string(p.unit_price, buf, buf_size);
}

int
price_to_string_fixed(price_t p, int32_t scale, char* buf, size_t buf_size)
{
    return decimal_to_string_fixed(p.unit_price, scale, buf, buf_size);
}

int
price_cmp(price_t a, price_t b)
{
    if (!currency_equals(a.currency, b.currency)) {
        return currency_equals(a.currency, CURRENCY_NONE) ? -1 : 1;
    }
    return decimal_cmp(a.unit_price, b.unit_price);
}

decimal_err_t
price_times_quantity(price_t p, quantity_t q, money_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    decimal_t total_amt;
    decimal_err_t err = decimal_mul(p.unit_price, q.units, &total_amt);
    if (err != DECIMAL_OK) {
        return err;
    }

    /* Round to currency standard precision */
    uint8_t prec = currency_precision(p.currency);
    decimal_t rounded;
    decimal_round(total_amt, (int32_t)prec, ROUND_HALF_UP, &rounded);

    out->amount = rounded;
    out->currency = p.currency;
    return DECIMAL_OK;
}

decimal_err_t
money_div_quantity(money_t m, quantity_t q, int32_t scale, round_mode_t mode, price_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    if (quantity_is_zero(q)) {
        *out = price_zero(m.currency);
        return DECIMAL_ERR_DIV_BY_ZERO;
    }
    if (scale <= 0) {
        scale = 4;
    }

    decimal_t res;
    decimal_err_t err = decimal_div(m.amount, q.units, scale, mode, &res);
    if (err != DECIMAL_OK) {
        return err;
    }

    out->unit_price = res;
    out->currency = m.currency;
    return DECIMAL_OK;
}

decimal_err_t
money_div_price(money_t m, price_t p, int32_t scale, round_mode_t mode, quantity_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    if (!currency_equals(m.currency, p.currency)) {
        *out = quantity_zero();
        return DECIMAL_ERR_INVALID_ARG; /* Currency mismatch! */
    }
    if (decimal_is_zero(p.unit_price)) {
        *out = quantity_zero();
        return DECIMAL_ERR_DIV_BY_ZERO;
    }
    if (scale <= 0) {
        scale = 4;
    }

    decimal_t res;
    decimal_err_t err = decimal_div(m.amount, p.unit_price, scale, mode, &res);
    if (err != DECIMAL_OK) {
        return err;
    }

    out->units = res;
    return DECIMAL_OK;
}
