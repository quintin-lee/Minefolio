#include "core/financial/currency.h"
#include <ctype.h>
#include <string.h>

const currency_t CURRENCY_CNY = {.code = "CNY", .precision = 2};
const currency_t CURRENCY_USD = {.code = "USD", .precision = 2};
const currency_t CURRENCY_EUR = {.code = "EUR", .precision = 2};
const currency_t CURRENCY_HKD = {.code = "HKD", .precision = 2};
const currency_t CURRENCY_JPY = {.code = "JPY", .precision = 0};
const currency_t CURRENCY_GBP = {.code = "GBP", .precision = 2};
const currency_t CURRENCY_AUD = {.code = "AUD", .precision = 2};
const currency_t CURRENCY_CAD = {.code = "CAD", .precision = 2};
const currency_t CURRENCY_SGD = {.code = "SGD", .precision = 2};
const currency_t CURRENCY_BTC = {.code = "BTC", .precision = 8};
const currency_t CURRENCY_ETH = {.code = "ETH", .precision = 8};
const currency_t CURRENCY_USDT = {.code = "USDT", .precision = 2};
const currency_t CURRENCY_NONE = {.code = "", .precision = 2};

currency_t
currency_from_str(const char* code)
{
    if (!code || !*code) {
        return CURRENCY_NONE;
    }

    currency_t cur;
    memset(&cur, 0, sizeof(cur));

    size_t len = 0;
    while (code[len] && len < CURRENCY_CODE_LEN - 1) {
        cur.code[len] = (char)toupper((unsigned char)code[len]);
        len++;
    }
    cur.code[len] = '\0';

    /* Standard precision resolution */
    if (strcmp(cur.code, "JPY") == 0 || strcmp(cur.code, "KRW") == 0 ||
        strcmp(cur.code, "VND") == 0 || strcmp(cur.code, "IDR") == 0) {
        cur.precision = 0;
    } else if (strcmp(cur.code, "BHD") == 0 || strcmp(cur.code, "KWD") == 0 ||
               strcmp(cur.code, "OMR") == 0 || strcmp(cur.code, "JOD") == 0) {
        cur.precision = 3;
    } else if (strcmp(cur.code, "BTC") == 0 || strcmp(cur.code, "ETH") == 0 ||
               strcmp(cur.code, "SOL") == 0 || strcmp(cur.code, "BNB") == 0) {
        cur.precision = 8;
    } else {
        cur.precision = 2; /* Default 2 for CNY, USD, EUR, HKD, GBP, etc. */
    }

    return cur;
}

const char*
currency_code(currency_t cur)
{
    return cur.code[0] != '\0' ? cur.code : "CNY";
}

bool
currency_equals(currency_t a, currency_t b)
{
    const char* ca = a.code[0] != '\0' ? a.code : "CNY";
    const char* cb = b.code[0] != '\0' ? b.code : "CNY";
    return strcasecmp(ca, cb) == 0;
}

bool
currency_is_valid(currency_t cur)
{
    return cur.code[0] != '\0';
}

uint8_t
currency_precision(currency_t cur)
{
    return cur.precision;
}
