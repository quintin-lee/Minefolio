#include "services/market/exchange_rate_service.h"
#include "csilk/csilk.h"
#include "csilk/drivers/db.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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

    // 5. Historical or latest point-in-time FX lookup tests
    printf("--- Running Historical FX Lookup Tests ---\n");
    csilk_db_init();
    const char* test_db = "/tmp/test_fx_history.db";
    unlink(test_db);
    csilk_db_pool_t* pool = csilk_db_pool_new("sqlite", test_db);
    assert(pool != NULL);

    csilk_db_exec(pool,
        "CREATE TABLE IF NOT EXISTS exchange_rate_history ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "rate_date TEXT NOT NULL, "
        "base_currency TEXT NOT NULL DEFAULT 'CNY', "
        "target_currency TEXT NOT NULL, "
        "rate DECIMAL(18,6) NOT NULL, "
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
        "UNIQUE (rate_date, base_currency, target_currency)"
        ");");

    currency_t cny = currency_from_str("CNY");
    currency_t rmb = currency_from_str("RMB");
    currency_t usd = currency_from_str("USD");
    currency_t xyz = currency_from_str("XYZ");

    // 5.1 CNY / RMB must return 1.0 regardless of pool or date
    assert(exchange_rate_get_historical_or_latest(pool, cny, "2026-05-10") == 1.0);
    assert(exchange_rate_get_historical_or_latest(pool, rmb, "2026-05-10") == 1.0);
    assert(exchange_rate_get_historical_or_latest(NULL, cny, "2026-05-10") == 1.0);

    // 5.2 Date before any history smoothly falls back to latest rate (RF-2) without 0 or NaN
    double usd_latest = exchange_rate_get_to_cny("USD");
    double pre_hist_rate = exchange_rate_get_historical_or_latest(pool, usd, "2026-01-01");
    assert(!isnan(pre_hist_rate) && !isinf(pre_hist_rate) && pre_hist_rate > 0.0);
    assert(fabs(pre_hist_rate - usd_latest) < 1e-4);

    // 5.3 Insert historical snapshots
    csilk_db_exec(pool,
        "INSERT INTO exchange_rate_history (rate_date, base_currency, target_currency, rate) "
        "VALUES ('2026-05-10', 'CNY', 'USD', 7.150000);");
    csilk_db_exec(pool,
        "INSERT INTO exchange_rate_history (rate_date, base_currency, target_currency, rate) "
        "VALUES ('2026-05-15', 'CNY', 'USD', 7.250000);");

    // Before first record date -> fallback to latest rate
    double r_before = exchange_rate_get_historical_or_latest(pool, usd, "2026-05-09");
    assert(fabs(r_before - usd_latest) < 1e-4);

    // Exactly first record date -> 7.15
    double r_10 = exchange_rate_get_historical_or_latest(pool, usd, "2026-05-10");
    assert(fabs(r_10 - 7.15) < 1e-4);

    // Between records -> latest on or before target date -> 7.15
    double r_12 = exchange_rate_get_historical_or_latest(pool, usd, "2026-05-12");
    assert(fabs(r_12 - 7.15) < 1e-4);

    // Second record date -> 7.25
    double r_15 = exchange_rate_get_historical_or_latest(pool, usd, "2026-05-15");
    assert(fabs(r_15 - 7.25) < 1e-4);

    // After second record date -> 7.25
    double r_20 = exchange_rate_get_historical_or_latest(pool, usd, "2026-05-20");
    assert(fabs(r_20 - 7.25) < 1e-4);

    // Timestamp format prefix "2026-05-10 18:30:00" -> matches 2026-05-10 -> 7.15
    double r_time = exchange_rate_get_historical_or_latest(pool, usd, "2026-05-10 18:30:00");
    assert(fabs(r_time - 7.15) < 1e-4);

    // Missing/NULL date -> latest rate
    double r_null = exchange_rate_get_historical_or_latest(pool, usd, NULL);
    assert(fabs(r_null - usd_latest) < 1e-4);

    // Unknown currency -> fallback 1.0 (no 0 or NaN)
    double r_xyz = exchange_rate_get_historical_or_latest(pool, xyz, "2026-05-10");
    assert(!isnan(r_xyz) && !isinf(r_xyz) && r_xyz == 1.0);

    csilk_db_pool_free(pool);
    unlink(test_db);

    printf("PASS: test_fx_semantics\n");
    return 0;
}
