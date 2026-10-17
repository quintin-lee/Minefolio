#include "services/market/exchange_rate_service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    // 1. Known currencies must succeed with code 0
    double rate = 0.0;
    int rc = exchange_rate_get_to_cny_safe("CNY", &rate);
    assert(rc == 0 && rate == 1.0);

    rc = exchange_rate_get_to_cny_safe("USD", &rate);
    assert(rc == 0 && rate > 0.0);

    rc = exchange_rate_get_to_cny_safe("HKD", &rate);
    assert(rc == 0 && rate > 0.0);

    // 2. Unknown currency must return -1 in safe mode
    rc = exchange_rate_get_to_cny_safe("XYZ_UNKNOWN", &rate);
    assert(rc == -1);

    // 3. NULL or empty currency must return -1 in safe mode
    rc = exchange_rate_get_to_cny_safe(NULL, &rate);
    assert(rc == -1);
    rc = exchange_rate_get_to_cny_safe("", &rate);
    assert(rc == -1);

    // 4. Legacy fallback function still returns 1.0 for backwards compatibility
    double fallback_rate = exchange_rate_get_to_cny("XYZ_UNKNOWN");
    assert(fallback_rate == 1.0);

    printf("PASS: test_fx_semantics\n");
    return 0;
}
