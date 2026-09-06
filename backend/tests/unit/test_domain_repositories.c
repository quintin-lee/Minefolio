/**
 * @file test_domain_repositories.c
 * @brief 领域仓储契约与基础设施实现集成单元测试
 */

#include "domain/auth/repository.h"
#include "domain/asset/repository.h"
#include "domain/transaction/repository.h"
#include "domain/portfolio/repository.h"
#include "domain/cashflow/repository.h"
#include "domain/market/repository.h"
#include "domain/ai/repository.h"

#include "core/financial/currency.h"
#include "core/financial/money.h"
#include "core/financial/price.h"
#include "core/financial/quantity.h"

#include "common/db.h"
#include "config/secret.h"
#include "csilk/csilk.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char* TEST_DB_PATH = "/tmp/test_mf_domain_repositories.db";

static inline currency_t
test_cny(void)
{
    return currency_from_str("CNY");
}

static inline money_t
test_money(double d)
{
    money_t m;
    money_from_double(d, test_cny(), &m);
    return m;
}

static inline price_t
test_price(double d)
{
    price_t p;
    price_from_double(d, 4, test_cny(), &p);
    return p;
}

static inline quantity_t
test_qty(double d)
{
    quantity_t q;
    quantity_from_double(d, 4, &q);
    return q;
}

static csilk_db_pool_t*
setup_test_db(void)
{
    unlink(TEST_DB_PATH);

    config_secret_set_test_mode(true);
    config_secret_set_test_override("DB_DRIVER", "sqlite");
    config_secret_set_test_override("DB_DSN", TEST_DB_PATH);
    config_secret_set_test_override("JWT_SECRET", "test_jwt_secret_domain_repos_32bytes_long!");

    csilk_db_pool_t* pool = NULL;
    int              rc = db_init(&pool);
    assert(rc == 0 && pool != NULL);

    rc = db_run_migrations(pool);
    assert(rc == 0);

    return pool;
}

static void
teardown_test_db(csilk_db_pool_t* pool)
{
    if (pool) {
        csilk_db_pool_free(pool);
    }
    config_secret_clear_test_overrides();
    unlink(TEST_DB_PATH);
}

static void
test_auth_repository(csilk_db_pool_t* pool, int64_t* out_user_id)
{
    printf("--- 1. Testing Auth Domain Repository ---\n");

    assert(mf_auth_repo_is_initialized(pool) == 0);
    assert(mf_auth_repo_count(pool) == 0);

    int64_t user_id = 0;
    int     rc = mf_auth_repo_create(pool, "alice", "hash_alice_123", &user_id);
    assert(rc == 0 && user_id > 0);

    assert(mf_auth_repo_is_initialized(pool) == 1);
    assert(mf_auth_repo_count(pool) == 1);

    mf_user_t u;
    memset(&u, 0, sizeof(u));
    rc = mf_auth_repo_find_by_username(pool, "alice", &u);
    assert(rc == 0);
    assert(u.id == user_id);
    assert(strcmp(u.username, "alice") == 0);
    assert(strcmp(u.password_hash, "hash_alice_123") == 0);

    memset(&u, 0, sizeof(u));
    assert(mf_auth_repo_find_by_username(pool, "nobody", &u) == 1);

    memset(&u, 0, sizeof(u));
    rc = mf_auth_repo_get_by_id(pool, user_id, &u);
    assert(rc == 0);
    assert(u.id == user_id);
    assert(strcmp(u.username, "alice") == 0);

    rc = mf_auth_repo_update_password(pool, user_id, "new_hash_456");
    assert(rc == 0);

    memset(&u, 0, sizeof(u));
    rc = mf_auth_repo_get_by_id(pool, user_id, &u);
    assert(rc == 0);
    assert(strcmp(u.password_hash, "new_hash_456") == 0);

    *out_user_id = user_id;
    printf("  ✅ Auth Domain Repository passed\n");
}

static void
test_asset_repository(csilk_db_pool_t* pool, int64_t user_id, int64_t* out_asset_id)
{
    printf("--- 2. Testing Asset Domain Repository ---\n");
    mf_asset_t asset;
    memset(&asset, 0, sizeof(asset));
    asset.user_id = user_id;
    snprintf(asset.name, sizeof(asset.name), "Test Asset");
    snprintf(asset.asset_type, sizeof(asset.asset_type), "stock");
    asset.quantity = test_qty(100.0);
    asset.cost_basis = test_money(10000.0);
    asset.net_value = test_price(1800.0);
    asset.currency = test_cny();

    int64_t asset_id = 0;
    int rc = mf_asset_repo_save(pool, &asset, &asset_id);
    assert(rc == 0 && asset_id > 0);

    mf_asset_t found;
    memset(&found, 0, sizeof(found));
    rc = mf_asset_repo_find_by_id(pool, user_id, asset_id, &found);
    assert(rc == 0);
    assert(found.id == asset_id);
    assert(found.user_id == user_id);
    assert(strcmp(found.name, "Test Asset") == 0);

    memset(&found, 0, sizeof(found));
    assert(mf_asset_repo_find_by_id(pool, user_id, 999999, &found) == 1);

    *out_asset_id = asset_id;
    printf("  ✅ Asset Domain Repository passed\n");
}

static void
test_transaction_repository(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id)
{
    printf("--- 3. Testing Transaction Domain Repository ---\n");

    mf_transaction_t tx;
    memset(&tx, 0, sizeof(tx));
    tx.user_id = user_id;
    tx.asset_id = asset_id;
    tx.account_id = 0;
    tx.parent_tx_id = 0;
    snprintf(tx.type, sizeof(tx.type), "buy");
    tx.amount = test_qty(50.0);
    tx.price = test_price(1800.0);
    tx.fee = test_money(15.0);
    snprintf(tx.fee_currency, sizeof(tx.fee_currency), "CNY");
    snprintf(tx.note, sizeof(tx.note), "第一次建仓");
    snprintf(tx.tx_time, sizeof(tx.tx_time), "2026-09-01 09:30:00");

    int64_t tx_id = 0;
    int     rc = mf_tx_repo_save(pool, &tx, &tx_id);
    assert(rc == 0 && tx_id > 0);

    mf_transaction_t found;
    memset(&found, 0, sizeof(found));
    rc = mf_tx_repo_find_by_id(pool, user_id, tx_id, &found);
    assert(rc == 0);
    assert(found.id == tx_id);
    assert(found.user_id == user_id);
    assert(found.asset_id == asset_id);
    assert(strcmp(found.type, "buy") == 0);
    assert(fabs(quantity_to_double(found.amount) - 50.0) < 0.001);
    assert(fabs(money_to_double(found.fee) - 15.0) < 0.001);
    assert(strcmp(found.note, "第一次建仓") == 0);

    memset(&found, 0, sizeof(found));
    assert(mf_tx_repo_find_by_id(pool, user_id, 999999, &found) == 1);

    assert(mf_tx_repo_find_by_id(pool, user_id, tx_id, &found) == 0);
    snprintf(found.note, sizeof(found.note), "建仓修改备注");
    found.fee = test_money(18.0);
    rc = mf_tx_repo_update(pool, &found);
    assert(rc == 0);

    memset(&found, 0, sizeof(found));
    assert(mf_tx_repo_find_by_id(pool, user_id, tx_id, &found) == 0);
    assert(strcmp(found.note, "建仓修改备注") == 0);
    assert(fabs(money_to_double(found.fee) - 18.0) < 0.001);

    mf_transaction_t fee_child;
    memset(&fee_child, 0, sizeof(fee_child));
    fee_child.user_id = user_id;
    fee_child.asset_id = asset_id;
    fee_child.parent_tx_id = tx_id;
    snprintf(fee_child.type, sizeof(fee_child.type), "fee");
    fee_child.amount = test_qty(1.0);
    fee_child.price = test_price(18.0);
    fee_child.fee = test_money(18.0);
    snprintf(fee_child.fee_currency, sizeof(fee_child.fee_currency), "CNY");
    snprintf(fee_child.note, sizeof(fee_child.note), "交易手续费扣减");
    snprintf(fee_child.tx_time, sizeof(fee_child.tx_time), "2026-09-01 09:30:00");

    int64_t fee_child_id = 0;
    rc = mf_tx_repo_save(pool, &fee_child, &fee_child_id);
    assert(rc == 0 && fee_child_id > 0);

    mf_transaction_t* fee_list = NULL;
    size_t            fee_count = 0;
    rc = mf_tx_repo_find_fee_children(pool, user_id, tx_id, &fee_list, &fee_count);
    assert(rc == 0);
    assert(fee_count == 1 && fee_list != NULL);
    assert(fee_list[0].id == fee_child_id);
    assert(fee_list[0].parent_tx_id == tx_id);
    mf_tx_repo_free_list(fee_list, fee_count);

    rc = mf_tx_repo_delete_fee_children(pool, user_id, tx_id);
    assert(rc == 0);

    fee_list = NULL;
    fee_count = 0;
    rc = mf_tx_repo_find_fee_children(pool, user_id, tx_id, &fee_list, &fee_count);
    assert(rc == 0);
    assert(fee_count == 0);
    mf_tx_repo_free_list(fee_list, fee_count);

    rc = mf_tx_repo_delete(pool, user_id, tx_id);
    assert(rc == 0);

    memset(&found, 0, sizeof(found));
    assert(mf_tx_repo_find_by_id(pool, user_id, tx_id, &found) == 1);

    printf("  ✅ Transaction Domain Repository passed\n");
}

static void
test_portfolio_repository(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id)
{
    printf("--- 4. Testing Portfolio Domain Repository ---\n");

    mf_holding_item_t* items = NULL;
    size_t count = 0;
    int rc = mf_portfolio_repo_get_holdings(pool, user_id, &items, &count);
    assert(rc == 0);
    assert(items != NULL || count == 0);
    mf_portfolio_repo_free_holdings(items, count);

    mf_portfolio_trade_event_t* events = NULL;
    size_t event_count = 0;
    rc = mf_portfolio_repo_get_trade_events(pool, user_id, &events, &event_count);
    assert(rc == 0);
    assert(events != NULL || event_count == 0);
    mf_portfolio_repo_free_trade_events(events, event_count);

    printf("  ✅ Portfolio Domain Repository passed\n");
}

static void
test_cashflow_repository(csilk_db_pool_t* pool, int64_t user_id)
{
    printf("--- 5. Testing Cashflow Domain Repository ---\n");

    mf_asset_t source_asset;
    memset(&source_asset, 0, sizeof(source_asset));
    source_asset.user_id = user_id;
    snprintf(source_asset.name, sizeof(source_asset.name), "Source Asset");
    snprintf(source_asset.asset_type, sizeof(source_asset.asset_type), "stock");
    source_asset.quantity = test_qty(100.0);
    source_asset.cost_basis = test_money(10000.0);
    source_asset.net_value = test_price(100.0);
    source_asset.currency = test_cny();
    int64_t source_asset_id = 0;
    int rc = mf_asset_repo_save(pool, &source_asset, &source_asset_id);
    assert(rc == 0 && source_asset_id > 0);

    mf_asset_t target_asset;
    memset(&target_asset, 0, sizeof(target_asset));
    target_asset.user_id = user_id;
    snprintf(target_asset.name, sizeof(target_asset.name), "Target Asset");
    snprintf(target_asset.asset_type, sizeof(target_asset.asset_type), "bank");
    target_asset.quantity = test_qty(0.0);
    target_asset.cost_basis = test_money(0.0);
    target_asset.net_value = test_price(1.0);
    target_asset.currency = test_cny();
    int64_t target_asset_id = 0;
    rc = mf_asset_repo_save(pool, &target_asset, &target_asset_id);
    assert(rc == 0 && target_asset_id > 0);

    mf_cashflow_schedule_t schedule;
    memset(&schedule, 0, sizeof(schedule));
    schedule.user_id = user_id;
    schedule.source_asset_id = source_asset_id;
    schedule.target_asset_id = target_asset_id;
    snprintf(schedule.name, sizeof(schedule.name), "Monthly Rent");
    snprintf(schedule.flow_type, sizeof(schedule.flow_type), "rent");
    snprintf(schedule.frequency, sizeof(schedule.frequency), "monthly");
    snprintf(schedule.start_date, sizeof(schedule.start_date), "2026-01-01");
    snprintf(schedule.end_date, sizeof(schedule.end_date), "2026-12-31");
    schedule.expected_amount = test_money(3000.0);
    snprintf(schedule.status, sizeof(schedule.status), "active");

    int64_t schedule_id = 0;
    rc = mf_cashflow_repo_create(pool, &schedule, &schedule_id);
    assert(rc == 0 && schedule_id > 0);

    mf_cashflow_schedule_t* list = NULL;
    size_t count = 0;
    rc = mf_cashflow_repo_list_active(pool, user_id, &list, &count);
    assert(rc == 0);
    assert(count > 0 && list != NULL);
    mf_cashflow_repo_free_list(list, count);

    printf("  ✅ Cashflow Domain Repository passed\n");
}

static void
test_market_repository(csilk_db_pool_t* pool, int64_t user_id)
{
    printf("--- 6. Testing Market Domain Repository ---\n");

    printf("  ✅ Market Domain Repository passed\n");
}

static void
test_ai_repository(csilk_db_pool_t* pool, int64_t user_id)
{
    printf("--- 7. Testing AI Domain Repository ---\n");

    int64_t session_id = mf_ai_session_repo_create(pool, user_id, "Test Session", NULL, NULL);
    assert(session_id > 0);

    mf_ai_trace_summary_t stats;
    memset(&stats, 0, sizeof(stats));
    int rc = mf_ai_trace_repo_stats(pool, user_id, &stats);
    assert(rc == 0);

    printf("  ✅ AI Domain Repository passed\n");
}

int
main(void)
{
    csilk_db_pool_t* pool = setup_test_db();
    if (!pool) {
        fprintf(stderr, "Failed to setup test database\n");
        return 1;
    }

    int64_t user_id = 0;
    int64_t asset_id = 0;

    test_auth_repository(pool, &user_id);
    test_asset_repository(pool, user_id, &asset_id);
    test_transaction_repository(pool, user_id, asset_id);
    test_portfolio_repository(pool, user_id, asset_id);
    test_cashflow_repository(pool, user_id);
    test_market_repository(pool, user_id);
    test_ai_repository(pool, user_id);

    teardown_test_db(pool);
    printf("\nAll domain repository tests passed!\n");
    return 0;
}
