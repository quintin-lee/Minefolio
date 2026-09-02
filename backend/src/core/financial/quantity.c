#include "core/financial/quantity.h"

quantity_t
quantity_zero(void)
{
    quantity_t q = {.units = decimal_zero()};
    return q;
}

quantity_t
quantity_from_decimal(decimal_t d)
{
    quantity_t q = {.units = d};
    return q;
}

decimal_err_t
quantity_from_string(const char* str, quantity_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    decimal_t d;
    decimal_err_t err = decimal_from_string(str, &d);
    if (err != DECIMAL_OK) {
        return err;
    }
    out->units = d;
    return DECIMAL_OK;
}

decimal_err_t
quantity_from_double(double d, int32_t scale, quantity_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    decimal_t dec;
    decimal_err_t err = decimal_from_double(d, scale, &dec);
    if (err != DECIMAL_OK) {
        return err;
    }
    out->units = dec;
    return DECIMAL_OK;
}

double
quantity_to_double(quantity_t q)
{
    return decimal_to_double(q.units);
}

int
quantity_to_string(quantity_t q, char* buf, size_t buf_size)
{
    return decimal_to_string(q.units, buf, buf_size);
}

int
quantity_to_string_fixed(quantity_t q, int32_t scale, char* buf, size_t buf_size)
{
    return decimal_to_string_fixed(q.units, scale, buf, buf_size);
}

decimal_err_t
quantity_add(quantity_t a, quantity_t b, quantity_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    decimal_t res;
    decimal_err_t err = decimal_add(a.units, b.units, &res);
    if (err != DECIMAL_OK) {
        return err;
    }
    out->units = res;
    return DECIMAL_OK;
}

decimal_err_t
quantity_sub(quantity_t a, quantity_t b, quantity_t* out)
{
    if (!out) {
        return DECIMAL_ERR_INVALID_ARG;
    }
    decimal_t res;
    decimal_err_t err = decimal_sub(a.units, b.units, &res);
    if (err != DECIMAL_OK) {
        return err;
    }
    out->units = res;
    return DECIMAL_OK;
}

int
quantity_cmp(quantity_t a, quantity_t b)
{
    return decimal_cmp(a.units, b.units);
}

bool
quantity_is_zero(quantity_t q)
{
    return decimal_is_zero(q.units);
}

bool
quantity_is_negative(quantity_t q)
{
    return decimal_is_negative(q.units);
}

bool
quantity_is_positive(quantity_t q)
{
    return decimal_is_positive(q.units);
}

quantity_t
quantity_abs(quantity_t q)
{
    q.units = decimal_abs(q.units);
    return q;
}

quantity_t
quantity_neg(quantity_t q)
{
    q.units = decimal_neg(q.units);
    return q;
}
