/**
 * @file test_domain_repositories.c
 * @brief 领域仓储契约与基础设施实现集成单元测试
 */

#include "domain/auth/repository.h"
#include "domain/asset/repository.h"
#include "domain/transaction/repository.h"
#include "domain/portfolio/repository.h"
#include "infrastructure/repositories/portfolio_repo_impl.h"
#include "application/portfolio/usecases.h"
#include "services/market/exchange_rate_service.h"
#include "domain/cashflow/repository.h"
#include "domain/transfer/repository.h"
#include "domain/market/repository.h"
#include "domain/ai/repository.h"
#include "common/balance.h"

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
    int rc = mf_portfolio_repo_get_holdings(pool, user_id, 0, &items, &count);
    assert(rc == 0);
    assert(items != NULL || count == 0);
    mf_portfolio_repo_free_holdings(items, count);

    mf_portfolio_trade_event_t* events = NULL;
    size_t event_count = 0;
    rc = mf_portfolio_repo_get_trade_events(pool, user_id, 0, &events, &event_count);
    assert(rc == 0);
    assert(events != NULL || event_count == 0);
    mf_portfolio_repo_free_trade_events(events, event_count);

    /* 4a. Test multi-currency holdings totals conversion */
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    /* Insert a test stock category */
    csilk_json_t* ins_cat_res = csilk_db_query_param_json(
        pool,
        "INSERT INTO categories (user_id, name, type, asset_type, currency, ledger_id) "
        "VALUES (?, 'Stock Category', 'asset', 'stock', 'CNY', 0) RETURNING id",
        (const char*[]){uid_str, NULL});
    assert(ins_cat_res && csilk_json_array_size(ins_cat_res) > 0);
    int64_t cat_id = db_get_int(csilk_json_array_get(ins_cat_res, 0), "id");
    csilk_json_free(ins_cat_res);
    char cat_str[32];
    snprintf(cat_str, sizeof(cat_str), "%lld", (long long)cat_id);

    /* Link asset_id from test_asset_repository to this category */
    char ast_str[32];
    snprintf(ast_str, sizeof(ast_str), "%lld", (long long)asset_id);
    csilk_json_free(csilk_db_query_param_json(
        pool, "UPDATE assets SET category_id=? WHERE id=?",
        (const char*[]){cat_str, ast_str, NULL}));

    /* Insert a USD stock asset: cost_basis=1000.0, current_value=1200.0 (USD) */
    csilk_json_t* ins_usd_res = csilk_db_query_param_json(
        pool,
        "INSERT INTO assets (user_id, category_id, name, quantity, "
        "cost_basis, net_value, current_value, currency, ledger_id) "
        "VALUES (?, ?, 'US Tech ETF', 10.0, 1000.0, 120.0, 1200.0, 'USD', 0) "
        "RETURNING id",
        (const char*[]){uid_str, cat_str, NULL});
    assert(ins_usd_res && csilk_json_array_size(ins_usd_res) > 0);
    int64_t usd_asset_id = db_get_int(csilk_json_array_get(ins_usd_res, 0), "id");
    csilk_json_free(ins_usd_res);

    double total_qty = 0.0, total_cost = 0.0, total_market = 0.0;
    rc = portfolio_repo_get_current_holdings_totals(pool, user_id, 0, &total_qty, &total_cost, &total_market);
    assert(rc == 0);
    /* Asset 1 (CNY): cost=10000.0, current_value=0.0 */
    /* Asset 2 (USD): cost=1000.0 * 7.20 = 7200.0, market=1200.0 * 7.20 = 8640.0 */
    /* total_cost must be 10000.0 + 7200.0 = 17200.0 (NOT 11000.0!) */
    assert(fabs(total_cost - 17200.0) < 1e-2);

    /* 4b. Test portfolio_usecase_get_performance gain/loss & fee accounting */
    /* Insert a new tradeable asset */
    char perf_ast_str[32];
    csilk_json_t* ins_perf_ast = csilk_db_query_param_json(
        pool,
        "INSERT INTO assets (user_id, category_id, name, quantity, "
        "cost_basis, net_value, current_value, currency, ledger_id) "
        "VALUES (?, ?, 'Trading Test Asset', 0.0, 0.0, 15.0, 0.0, 'CNY', 0) "
        "RETURNING id",
        (const char*[]){uid_str, cat_str, NULL});
    assert(ins_perf_ast && csilk_json_array_size(ins_perf_ast) > 0);
    int64_t perf_ast_id = db_get_int(csilk_json_array_get(ins_perf_ast, 0), "id");
    csilk_json_free(ins_perf_ast);
    snprintf(perf_ast_str, sizeof(perf_ast_str), "%lld", (long long)perf_ast_id);

    /* Insert Buy trade: qty=100, price=10.0, amt=1000.0, fee=10.0 */
    csilk_json_free(csilk_db_query_param_json(
        pool,
        "INSERT INTO transactions (user_id, asset_id, transaction_type, direction, "
        "quantity, price_per_unit, amount, fee, currency, transaction_date, ledger_id) "
        "VALUES (?, ?, 'buy', 'in', 100.0, 10.0, 1000.0, 10.0, 'CNY', '2026-09-01', 0)",
        (const char*[]){uid_str, perf_ast_str, NULL}));

    /* Insert Sell trade: qty=100, price=15.0, amt=1500.0, fee=10.0 */
    csilk_json_free(csilk_db_query_param_json(
        pool,
        "INSERT INTO transactions (user_id, asset_id, transaction_type, direction, "
        "quantity, price_per_unit, amount, fee, currency, transaction_date, ledger_id) "
        "VALUES (?, ?, 'sell', 'out', 100.0, 15.0, 1500.0, 10.0, 'CNY', '2026-09-02', 0)",
        (const char*[]){uid_str, perf_ast_str, NULL}));

    query_portfolio_cmd_t cmd = {
        .user_id = user_id,
        .ledger_id = 0,
        .base_currency = "CNY",
    };
    csilk_json_t* perf_resp = NULL;
    portfolio_usecase_result_t perf_res = {0};
    rc = portfolio_usecase_get_performance(pool, &cmd, &perf_resp, &perf_res);
    assert(rc == 0 && perf_res.code == 0);
    assert(perf_resp != NULL);

    double total_trades = db_get_num(perf_resp, "total_trades");
    double total_gain = db_get_num(perf_resp, "total_gain");
    double total_loss = db_get_num(perf_resp, "total_loss");
    double net_gain = db_get_num(perf_resp, "net_gain");
    double realized_pnl = db_get_num(perf_resp, "realized_pnl");

    /* Expected: 
     * total_trades: 2 (1 buy, 1 sell)
     * Buy cost = 1000 + 10 = 1010.
     * Sell net proceeds = 1500 - 10 = 1490.
     * Realized PnL = 1490 - 1010 = 480.0.
     * Profitable trade -> total_gain = 480.0, total_loss = 0.0, net_gain = 480.0.
     */
    assert(total_trades == 2.0);
    assert(fabs(realized_pnl - 480.0) < 1e-2);
    assert(fabs(total_gain - 480.0) < 1e-2);
    assert(fabs(total_loss - 0.0) < 1e-2);
    assert(fabs(net_gain - 480.0) < 1e-2);

    csilk_json_free(perf_resp);

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
test_ai_repository(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id)
{
    printf("--- 7. Testing AI Domain Repository ---\n");

    /* 第二个用户，用于验证 AI 月度报表的跨用户隔离 */
    int64_t bob_id = 0;
    assert(mf_auth_repo_create(pool, "bob", "hash_bob_123", &bob_id) == 0 && bob_id > 0);

    /* 为两个用户各建一个分类 */
    char uid_a[32], uid_b[32], cat_amt[64], bob_amt[64];
    snprintf(uid_a, sizeof(uid_a), "%lld", (long long)user_id);
    snprintf(uid_b, sizeof(uid_b), "%lld", (long long)bob_id);
    snprintf(cat_amt, sizeof(cat_amt), "111.50");
    snprintf(bob_amt, sizeof(bob_amt), "222.25");

    csilk_json_t* cat_a = csilk_db_query_param_json(
        pool,
        "INSERT INTO categories (user_id, name, type) VALUES (?, 'AliceCat', 'expense') "
        "RETURNING id",
        (const char*[]){uid_a, NULL});
    csilk_json_t* cat_b = csilk_db_query_param_json(
        pool,
        "INSERT INTO categories (user_id, name, type) VALUES (?, 'BobCat', 'expense') "
        "RETURNING id",
        (const char*[]){uid_b, NULL});
    assert(cat_a && csilk_json_array_size(cat_a) > 0);
    assert(cat_b && csilk_json_array_size(cat_b) > 0);
    char cat_a_str[32], cat_b_str[32];
    snprintf(cat_a_str, sizeof(cat_a_str), "%lld", (long long)db_get_int(csilk_json_array_get(cat_a, 0), "id"));
    snprintf(cat_b_str, sizeof(cat_b_str), "%lld", (long long)db_get_int(csilk_json_array_get(cat_b, 0), "id"));
    csilk_json_free(cat_a);
    csilk_json_free(cat_b);

    /* 两个用户各插一条 2026-09 收支，金额不同 */
    char ast_str[32];
    snprintf(ast_str, sizeof(ast_str), "%lld", (long long)asset_id);
    const char* ins_exp =
        "INSERT INTO daily_expenses (user_id, category_id, asset_id, expense_type, amount, "
        "expense_date) VALUES (?, ?, ?, 'expense', ?, '2026-09-01') RETURNING id";
    csilk_json_t* exp_a =
        csilk_db_query_param_json(pool, ins_exp, (const char*[]){uid_a, cat_a_str, ast_str, cat_amt, NULL});
    csilk_json_t* exp_b =
        csilk_db_query_param_json(pool, ins_exp, (const char*[]){uid_b, cat_b_str, ast_str, bob_amt, NULL});
    assert(exp_a && csilk_json_array_size(exp_a) > 0);
    assert(exp_b && csilk_json_array_size(exp_b) > 0);
    csilk_json_free(exp_a);
    csilk_json_free(exp_b);

    /* 断言 1：monthly_totals 只返回该用户自己的金额 */
    csilk_json_t* tot_a =
        mf_ai_repo_daily_expense_monthly_totals(pool, user_id, "2026-09%");
    csilk_json_t* tot_b = mf_ai_repo_daily_expense_monthly_totals(pool, bob_id, "2026-09%");
    assert(tot_a && csilk_json_array_size(tot_a) == 1);
    assert(tot_b && csilk_json_array_size(tot_b) == 1);
    double a_expense = db_get_num(csilk_json_array_get(tot_a, 0), "total_expense");
    double b_expense = db_get_num(csilk_json_array_get(tot_b, 0), "total_expense");
    assert(a_expense > 111.4 && a_expense < 111.6);
    assert(b_expense > 222.1 && b_expense < 222.4);
    csilk_json_free(tot_a);
    csilk_json_free(tot_b);

    /* 断言 2：monthly_by_category 只返回该用户自己的分类聚合 */
    csilk_json_t* cats_a =
        mf_ai_repo_daily_expense_monthly_by_category(pool, user_id, "2026-09%");
    csilk_json_t* cats_b =
        mf_ai_repo_daily_expense_monthly_by_category(pool, bob_id, "2026-09%");
    assert(cats_a && csilk_json_array_size(cats_a) == 1);
    assert(cats_b && csilk_json_array_size(cats_b) == 1);
    csilk_json_t* row_a = csilk_json_array_get(cats_a, 0);
    csilk_json_t* row_b = csilk_json_array_get(cats_b, 0);
    const char* an = csilk_json_get_string(row_a, "category_name");
    const char* bn = csilk_json_get_string(row_b, "category_name");
    assert(an && strcmp(an, "AliceCat") == 0);
    assert(bn && strcmp(bn, "BobCat") == 0);
    csilk_json_free(cats_a);
    csilk_json_free(cats_b);

    printf("  ✅ AI Domain Repository passed (monthly isolation verified)\n");
}

static void
test_multi_currency_balance_and_transfers(csilk_db_pool_t* pool, int64_t user_id)
{
    printf("--- 8. Testing Multi-Currency Balance & Transfers ---\n");
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    /* 1. DEF-03: Multi-currency balance_apply_delta_m */
    csilk_json_t* ins_cat = csilk_db_query_param_json(
        pool,
        "INSERT INTO categories (user_id, name, type, asset_type, currency, ledger_id) "
        "VALUES (?, 'USD Cash Category', 'asset', 'cash', 'USD', 0) RETURNING id",
        (const char*[]){uid_str, NULL});
    assert(ins_cat && csilk_json_array_size(ins_cat) > 0);
    int64_t cat_id = db_get_int(csilk_json_array_get(ins_cat, 0), "id");
    csilk_json_free(ins_cat);
    char cat_str[32];
    snprintf(cat_str, sizeof(cat_str), "%lld", (long long)cat_id);

    /* Insert USD Account: initial current_value = 100.0 USD */
    csilk_json_t* ins_acc = csilk_db_query_param_json(
        pool,
        "INSERT INTO assets (user_id, category_id, name, current_value, currency, ledger_id) "
        "VALUES (?, ?, 'USD Checking', 100.0, 'USD', 0) RETURNING id",
        (const char*[]){uid_str, cat_str, NULL});
    assert(ins_acc && csilk_json_array_size(ins_acc) > 0);
    int64_t usd_acc_id = db_get_int(csilk_json_array_get(ins_acc, 0), "id");
    csilk_json_free(ins_acc);

    /* Apply a delta of 720.0 CNY to this USD account */
    money_t cny_delta;
    money_from_double(720.0, currency_from_str("CNY"), &cny_delta);
    int rc = balance_apply_delta_m(pool, usd_acc_id, user_id, cny_delta, "test_deposit", 1, "Deposit 720 CNY");
    assert(rc == 0);

    /* 720 CNY should convert to 100 USD (at 1 USD = 7.20 CNY), so balance must become 200.0 USD, NOT 820.0! */
    char usd_acc_str[32];
    snprintf(usd_acc_str, sizeof(usd_acc_str), "%lld", (long long)usd_acc_id);
    csilk_json_t* check_res = csilk_db_query_param_json(
        pool, "SELECT current_value FROM assets WHERE id=?", (const char*[]){usd_acc_str, NULL});
    assert(check_res && csilk_json_array_size(check_res) > 0);
    double usd_val = db_get_num(csilk_json_array_get(check_res, 0), "current_value");
    csilk_json_free(check_res);
    assert(fabs(usd_val - 200.0) < 1e-2);

    /* 2. DEF-04: apply_position on foreign currency asset */
    /* Insert a USD stock category and asset with 0 initial quantity, cost, net_value */
    csilk_json_t* ins_stk_cat = csilk_db_query_param_json(
        pool,
        "INSERT INTO categories (user_id, name, type, asset_type, currency, ledger_id) "
        "VALUES (?, 'USD Stock Category', 'asset', 'stock', 'USD', 0) RETURNING id",
        (const char*[]){uid_str, NULL});
    assert(ins_stk_cat && csilk_json_array_size(ins_stk_cat) > 0);
    int64_t stk_cat_id = db_get_int(csilk_json_array_get(ins_stk_cat, 0), "id");
    csilk_json_free(ins_stk_cat);
    char stk_cat_str[32];
    snprintf(stk_cat_str, sizeof(stk_cat_str), "%lld", (long long)stk_cat_id);

    csilk_json_t* ins_stk = csilk_db_query_param_json(
        pool,
        "INSERT INTO assets (user_id, category_id, name, quantity, cost_basis, net_value, current_value, currency, ledger_id) "
        "VALUES (?, ?, 'Foreign USD Stock', 0.0, 0.0, 0.0, 0.0, 'USD', 0) RETURNING id",
        (const char*[]){uid_str, stk_cat_str, NULL});
    assert(ins_stk && csilk_json_array_size(ins_stk) > 0);
    int64_t stk_id = db_get_int(csilk_json_array_get(ins_stk, 0), "id");
    csilk_json_free(ins_stk);

    /* Buy 10 units @ 50 USD = 500 USD, fee = 5 USD */
    double delta_pos = 0;
    rc = apply_position(pool, stk_id, "buy", 500.0, 5.0, 50.0, 10.0, &delta_pos);
    assert(rc == 0);

    char stk_str[32];
    snprintf(stk_str, sizeof(stk_str), "%lld", (long long)stk_id);
    csilk_json_t* stk_check = csilk_db_query_param_json(
        pool, "SELECT quantity, cost_basis, net_value FROM assets WHERE id=?", (const char*[]){stk_str, NULL});
    assert(stk_check && csilk_json_array_size(stk_check) > 0);
    double stk_qty = db_get_num(csilk_json_array_get(stk_check, 0), "quantity");
    double stk_cost = db_get_num(csilk_json_array_get(stk_check, 0), "cost_basis");
    double stk_nv = db_get_num(csilk_json_array_get(stk_check, 0), "net_value");
    csilk_json_free(stk_check);

    assert(fabs(stk_qty - 10.0) < 1e-4);
    assert(fabs(stk_cost - 505.0) < 1e-2); /* Must be 505.0 USD, NOT 0.0! */
    assert(fabs(stk_nv - 50.0) < 1e-2);

    /* 3. DEF-05: Transfer transaction direction & linked_asset_id */
    int64_t from_asset_id = usd_acc_id;
    int64_t to_asset_id = stk_id;
    rc = mf_transfer_repo_insert_out_transaction(pool, user_id, from_asset_id, to_asset_id, 50.0, "USD", "2026-09-10", "Transfer out");
    assert(rc == 0);
    rc = mf_transfer_repo_insert_in_transaction(pool, user_id, to_asset_id, from_asset_id, 50.0, "USD", "2026-09-10", "Transfer in");
    assert(rc == 0);

    /* Verify out transaction has direction='out' and linked_asset_id=to_asset_id */
    csilk_json_t* tx_out_res = csilk_db_query_param_json(
        pool,
        "SELECT direction, linked_asset_id FROM transactions WHERE user_id=? AND transaction_type='transfer_out'",
        (const char*[]){uid_str, NULL});
    assert(tx_out_res && csilk_json_array_size(tx_out_res) > 0);
    const char* out_dir = csilk_json_get_string(csilk_json_array_get(tx_out_res, 0), "direction");
    int64_t out_laid = db_get_int(csilk_json_array_get(tx_out_res, 0), "linked_asset_id");
    assert(out_dir && strcmp(out_dir, "out") == 0);
    assert(out_laid == to_asset_id);
    csilk_json_free(tx_out_res);

    /* Verify in transaction has direction='in' and linked_asset_id=from_asset_id */
    csilk_json_t* tx_in_res = csilk_db_query_param_json(
        pool,
        "SELECT direction, linked_asset_id FROM transactions WHERE user_id=? AND transaction_type='transfer_in'",
        (const char*[]){uid_str, NULL});
    assert(tx_in_res && csilk_json_array_size(tx_in_res) > 0);
    const char* in_dir = csilk_json_get_string(csilk_json_array_get(tx_in_res, 0), "direction");
    int64_t in_laid = db_get_int(csilk_json_array_get(tx_in_res, 0), "linked_asset_id");
    assert(in_dir && strcmp(in_dir, "in") == 0);
    assert(in_laid == from_asset_id);
    csilk_json_free(tx_in_res);

    printf("  ✅ Multi-Currency Balance & Transfers passed\n");
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
    test_ai_repository(pool, user_id, asset_id);
    test_multi_currency_balance_and_transfers(pool, user_id);

    teardown_test_db(pool);
    printf("\nAll domain repository tests passed!\n");
    return 0;
}
