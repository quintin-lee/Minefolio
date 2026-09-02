#include "core/financial/money.h"
#include <stdio.h>
#include <string.h>

money_t
money_zero(currency_t cur)
{
    money_t m = {.amount = decimal_zero(), .currency = cur};
    return m;
}

money_t
money_from_decimal(decimal_t amt, currency_t cur)
{
    money_t m = {.amount = amt, .currency = cur};
    return m;
}

decimal_err_t
money_from_string(const char* str, currency_t cur, money_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    decimal_t     amt;
    decimal_err_t err = decimal_from_string(str, &amt);
    if (err != DECIMAL_OK) {
        return err;
    }
    out->amount = amt;
    out->currency = cur;
    return DECIMAL_OK;
}

decimal_err_t
money_from_int(int64_t val, currency_t cur, money_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    out->amount = decimal_from_int(val);
    out->currency = cur;
    return DECIMAL_OK;
}

decimal_err_t
money_from_double(double d, currency_t cur, money_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    uint8_t       prec = currency_precision(cur);
    decimal_t     amt;
    decimal_err_t err = decimal_from_double(d, (int32_t)prec, &amt);
    if (err != DECIMAL_OK) {
        return err;
    }
    out->amount = amt;
    out->currency = cur;
    return DECIMAL_OK;
}

double
money_to_double(money_t m)
{
    return decimal_to_double(m.amount);
}

int
money_to_string(money_t m, char* buf, size_t buf_size)
{
    uint8_t prec = currency_precision(m.currency);
    return decimal_to_string_fixed(m.amount, (int32_t)prec, buf, buf_size);
}

decimal_err_t
money_add(money_t a, money_t b, money_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    if (!currency_equals(a.currency, b.currency)) {
        *out = money_zero(a.currency);
        return DECIMAL_ERR_INVALID_ARG; /* Currency mismatch! */
    }

    decimal_t     res;
    decimal_err_t err = decimal_add(a.amount, b.amount, &res);
    if (err != DECIMAL_OK) {
        return err;
    }

    out->amount = res;
    out->currency = a.currency;
    return DECIMAL_OK;
}

decimal_err_t
money_sub(money_t a, money_t b, money_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    if (!currency_equals(a.currency, b.currency)) {
        *out = money_zero(a.currency);
        return DECIMAL_ERR_INVALID_ARG; /* Currency mismatch! */
    }

    decimal_t     res;
    decimal_err_t err = decimal_sub(a.amount, b.amount, &res);
    if (err != DECIMAL_OK) {
        return err;
    }

    out->amount = res;
    out->currency = a.currency;
    return DECIMAL_OK;
}

int
money_cmp(money_t a, money_t b)
{
    if (!currency_equals(a.currency, b.currency)) {
        return strcasecmp(currency_code(&a.currency), currency_code(&b.currency));
    }
    return decimal_cmp(a.amount, b.amount);
}

money_t
money_abs(money_t m)
{
    m.amount = decimal_abs(m.amount);
    return m;
}

money_t
money_neg(money_t m)
{
    m.amount = decimal_neg(m.amount);
    return m;
}

bool
money_is_zero(money_t m)
{
    return decimal_is_zero(m.amount);
}

bool
money_is_negative(money_t m)
{
    return decimal_is_negative(m.amount);
}

bool
money_is_positive(money_t m)
{
    return decimal_is_positive(m.amount);
}

decimal_err_t
money_round(money_t m, round_mode_t mode, money_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    uint8_t       prec = currency_precision(m.currency);
    decimal_t     rounded;
    decimal_err_t err = decimal_round(m.amount, (int32_t)prec, mode, &rounded);
    if (err != DECIMAL_OK) {
        return err;
    }
    out->amount = rounded;
    out->currency = m.currency;
    return DECIMAL_OK;
}
