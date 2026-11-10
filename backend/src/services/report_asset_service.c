/**
 * @file report_asset_service.c
 * @brief 资产报表查询实现（Application Layer - 报表域）
 *
 * 依赖：pool（controller 通过 db_get_pool() 传入）+ report_query_t，
 * 不依赖 csilk_ctx_t / ctx_* / respond_*。结果通过 out_res->data_payload 承载。
 */

#include "services/report_asset_service.h"
#include "application/report/commands.h"
#include "common/db.h"
#include "services/market/exchange_rate_service.h"
#include "domain/portfolio/fx_table.h"
#include "core/financial/price.h"
#include "core/financial/quantity.h"
#include "core/financial/percentage.h"
#include "core/financial/money.h"
#include "core/financial/currency.h"
#include "core/financial/decimal.h"
#include "csilk/drivers/db.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    int64_t    id;
    money_t    current_value;
    currency_t currency;
    char       created_date[16];
    int        is_liability;
} asset_trend_item_t;

typedef struct {
    int64_t asset_id;
    money_t delta;
    char    log_date[16];
} asset_trend_log_t;

void
report_asset_trend(void* pool, const report_query_t* q, report_result_t* out_res)
{
    if (!pool || !q || !out_res) {
        if (out_res) {
            out_res->code = 1002;
            snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        }
        return;
    }
    memset(out_res, 0, sizeof(*out_res));

    int days = (q->days > 0) ? q->days : 30;
    if (q->period) {
        if (strcmp(q->period, "90d") == 0) {
            days = 90;
        } else if (strcmp(q->period, "365d") == 0 || strcmp(q->period, "1y") == 0) {
            days = 365;
        } else {
            days = atoi(q->period);
        }
    }
    if (days <= 0 || days > 365) {
        days = 30;
    }

    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)q->user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)q->ledger_id);

    // 1. Generate 5 sample points (labels) across the period
    time_t    now = time(NULL);
    struct tm cur_tm;
    localtime_r(&now, &cur_tm);
    char dates[5][16];
    for (int k = 0; k < 5; k++) {
        struct tm tm = cur_tm;
        int       mday_offset = (4 - k) * (days - 1) / 4;
        tm.tm_mday -= mday_offset;
        mktime(&tm);
        strftime(dates[k], sizeof(dates[k]), "%Y-%m-%d", &tm);
    }

    // 2. Query all assets for the user / ledger
    const char* asset_sql =
        db_is_postgres()
            ? (q->ledger_id > 0 ? "SELECT a.id, a.current_value, a.currency, c.asset_type, "
                                  "to_char(a.created_at, 'YYYY-MM-DD') as created_date "
                                  "FROM assets a JOIN categories c ON a.category_id=c.id "
                                  "WHERE a.user_id=? AND a.ledger_id=?"
                                : "SELECT a.id, a.current_value, a.currency, c.asset_type, "
                                  "to_char(a.created_at, 'YYYY-MM-DD') as created_date "
                                  "FROM assets a JOIN categories c ON a.category_id=c.id "
                                  "WHERE a.user_id=?")
            : (q->ledger_id > 0 ? "SELECT a.id, a.current_value, a.currency, c.asset_type, "
                                  "date(a.created_at) as created_date "
                                  "FROM assets a JOIN categories c ON a.category_id=c.id "
                                  "WHERE a.user_id=? AND a.ledger_id=?"
                                : "SELECT a.id, a.current_value, a.currency, c.asset_type, "
                                  "date(a.created_at) as created_date "
                                  "FROM assets a JOIN categories c ON a.category_id=c.id "
                                  "WHERE a.user_id=?");
    const char*   asset_params[3] = {uid_str, (q->ledger_id > 0 ? lid_str : NULL), NULL};
    csilk_json_t* asset_rows =
        csilk_db_query_param_json((csilk_db_pool_t*)pool, asset_sql, asset_params);

    size_t              num_assets = asset_rows ? csilk_json_array_size(asset_rows) : 0;
    asset_trend_item_t* assets =
        num_assets > 0 ? (asset_trend_item_t*)calloc(num_assets, sizeof(asset_trend_item_t)) : NULL;
    for (size_t i = 0; i < num_assets; i++) {
        csilk_json_t* r = csilk_json_array_get(asset_rows, i);
        assets[i].id = db_get_int(r, "id");
        const char* cur_s = csilk_json_get_string(r, "currency");
        assets[i].currency = currency_from_str(cur_s ? cur_s : "CNY");
        assets[i].current_value = db_get_money(r, "current_value", assets[i].currency);
        const char* cd = csilk_json_get_string(r, "created_date");
        snprintf(
            assets[i].created_date, sizeof(assets[i].created_date), "%s", cd ? cd : "1970-01-01");
        const char* atype = csilk_json_get_string(r, "asset_type");
        assets[i].is_liability =
            (atype && (strcmp(atype, "loan") == 0 || strcmp(atype, "credit_card") == 0 ||
                       strcmp(atype, "other_liability") == 0));
    }
    if (asset_rows) {
        csilk_json_free(asset_rows);
    }

    // 3. Query all balance delta logs on or after dates[0]
    const char* log_sql =
        db_is_postgres()
            ? (q->ledger_id > 0
                   ? "SELECT abl.asset_id, abl.delta, a.currency, "
                     "to_char(abl.created_at, 'YYYY-MM-DD') as log_date "
                     "FROM asset_balance_logs abl JOIN assets a ON abl.asset_id=a.id "
                     "WHERE abl.user_id=? AND a.ledger_id=? AND abl.created_at::date >= ?::date"
                   : "SELECT abl.asset_id, abl.delta, a.currency, "
                     "to_char(abl.created_at, 'YYYY-MM-DD') as log_date "
                     "FROM asset_balance_logs abl JOIN assets a ON abl.asset_id=a.id "
                     "WHERE abl.user_id=? AND abl.created_at::date >= ?::date")
            : (q->ledger_id > 0
                   ? "SELECT abl.asset_id, abl.delta, a.currency, "
                     "date(abl.created_at) as log_date "
                     "FROM asset_balance_logs abl JOIN assets a ON abl.asset_id=a.id "
                     "WHERE abl.user_id=? AND a.ledger_id=? AND date(abl.created_at) >= ?"
                   : "SELECT abl.asset_id, abl.delta, a.currency, "
                     "date(abl.created_at) as log_date "
                     "FROM asset_balance_logs abl JOIN assets a ON abl.asset_id=a.id "
                     "WHERE abl.user_id=? AND date(abl.created_at) >= ?");
    const char*   log_params[4] = {uid_str,
                                   (q->ledger_id > 0 ? lid_str : dates[0]),
                                   (q->ledger_id > 0 ? dates[0] : NULL),
                                   NULL};
    csilk_json_t* log_rows = csilk_db_query_param_json((csilk_db_pool_t*)pool, log_sql, log_params);

    size_t             num_logs = log_rows ? csilk_json_array_size(log_rows) : 0;
    asset_trend_log_t* logs =
        num_logs > 0 ? (asset_trend_log_t*)calloc(num_logs, sizeof(asset_trend_log_t)) : NULL;
    for (size_t i = 0; i < num_logs; i++) {
        csilk_json_t* r = csilk_json_array_get(log_rows, i);
        logs[i].asset_id = db_get_int(r, "asset_id");
        const char* log_cur = csilk_json_get_string(r, "currency");
        logs[i].delta = db_get_money(r, "delta", currency_from_str(log_cur ? log_cur : "CNY"));
        const char* ld = csilk_json_get_string(r, "log_date");
        snprintf(logs[i].log_date, sizeof(logs[i].log_date), "%s", ld ? ld : "");
    }
    if (log_rows) {
        csilk_json_free(log_rows);
    }

    // 4. Reconstruct historical balances for each sample point
    currency_t base_cur = currency_from_str("CNY");
    money_t    pt_assets[5] = {0};
    money_t    pt_liabs[5] = {0};
    for (int k = 0; k < 5; k++) {
        pt_assets[k] = money_zero(base_cur);
        pt_liabs[k] = money_zero(base_cur);
    }

    for (int k = 0; k < 5; k++) {
        const char* target_date = dates[k];
        for (size_t a = 0; a < num_assets; a++) {
            if (strcmp(assets[a].created_date, target_date) > 0) {
                continue;
            }
            money_t val = assets[a].current_value;
            for (size_t l = 0; l < num_logs; l++) {
                if (logs[l].asset_id == assets[a].id && strcmp(logs[l].log_date, target_date) > 0) {
                    money_t neg_delta = money_neg(logs[l].delta);
                    money_t acc2;
                    if (money_add(val, neg_delta, &acc2) == DECIMAL_OK) {
                        val = acc2;
                    }
                }
            }
            money_t conv_val = exchange_rate_convert_m(val, base_cur);
            money_t acc;
            if (assets[a].is_liability) {
                if (money_add(pt_liabs[k], conv_val, &acc) == DECIMAL_OK) {
                    pt_liabs[k] = acc;
                }
            } else {
                if (money_add(pt_assets[k], conv_val, &acc) == DECIMAL_OK) {
                    pt_assets[k] = acc;
                }
            }
        }
    }

    if (assets) {
        free(assets);
    }
    if (logs) {
        free(logs);
    }

    // 5. Build response object
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "period", q->period ? q->period : "30d");

    csilk_json_t* labels_arr = csilk_json_array();
    csilk_json_t* nw_arr = csilk_json_array();
    csilk_json_t* assets_arr = csilk_json_array();
    csilk_json_t* liabs_arr = csilk_json_array();

    for (int k = 0; k < 5; k++) {
        money_t nw;
        if (money_sub(pt_assets[k], pt_liabs[k], &nw) != DECIMAL_OK) {
            nw = pt_assets[k];
        }
        csilk_json_array_append(labels_arr, csilk_json_string_new(dates[k]));
        csilk_json_array_append(nw_arr, csilk_json_number(money_to_double(nw)));
        csilk_json_array_append(assets_arr, csilk_json_number(money_to_double(pt_assets[k])));
        csilk_json_array_append(liabs_arr, csilk_json_number(money_to_double(pt_liabs[k])));
    }

    csilk_json_add_array(resp, "labels", labels_arr);
    csilk_json_add_array(resp, "net_worth", nw_arr);
    csilk_json_add_array(resp, "assets", assets_arr);
    csilk_json_add_array(resp, "liabilities", liabs_arr);

    out_res->code = 0;
    out_res->data_payload = resp;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
}

void
report_asset_breakdown(void* pool, const report_query_t* q, report_result_t* out_res)
{
    if (!pool || !q || !out_res) {
        if (out_res) {
            out_res->code = 1002;
            snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        }
        return;
    }
    memset(out_res, 0, sizeof(*out_res));

    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)q->user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)q->ledger_id);
    const char* params[] = {uid_str, lid_str, NULL};

    csilk_json_t* assets = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT c.name as name, a.current_value as value, a.currency as currency "
        "FROM assets a JOIN categories c ON a.category_id=c.id "
        "WHERE a.user_id=? AND a.ledger_id=? AND c.asset_type NOT IN "
        "('loan','credit_card','other_liability')",
        params);
    csilk_json_t* liabs = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT c.name as name, a.current_value as value, a.currency as currency "
        "FROM assets a JOIN categories c ON a.category_id=c.id "
        "WHERE a.user_id=? AND a.ledger_id=? AND c.asset_type IN "
        "('loan','credit_card','other_liability')",
        params);

    currency_t base_cur = currency_from_str("CNY");
    typedef struct {
        char    name[128];
        money_t value;
    } cat_acc_t;

    cat_acc_t asset_cats[128];
    int       asset_cats_count = 0;
    money_t   total_assets = money_zero(base_cur);

    if (assets) {
        size_t n = csilk_json_array_size(assets);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* row = csilk_json_array_get(assets, i);
            const char*   name = csilk_json_get_string(row, "name");
            const char*   cur_s = csilk_json_get_string(row, "currency");
            currency_t    cur = currency_from_str(cur_s ? cur_s : "CNY");
            money_t       v = exchange_rate_convert_m(db_get_money(row, "value", cur), base_cur);
            money_t       acc;
            if (money_add(total_assets, v, &acc) == DECIMAL_OK) {
                total_assets = acc;
            }
            if (name) {
                int found = -1;
                for (int k = 0; k < asset_cats_count; k++) {
                    if (strcmp(asset_cats[k].name, name) == 0) {
                        found = k;
                        break;
                    }
                }
                if (found >= 0) {
                    if (money_add(asset_cats[found].value, v, &acc) == DECIMAL_OK) {
                        asset_cats[found].value = acc;
                    }
                } else if (asset_cats_count < 128) {
                    strncpy(
                        asset_cats[asset_cats_count].name, name, sizeof(asset_cats[0].name) - 1);
                    asset_cats[asset_cats_count].name[sizeof(asset_cats[0].name) - 1] = '\0';
                    asset_cats[asset_cats_count].value = v;
                    asset_cats_count++;
                }
            }
        }
        csilk_json_free(assets);
    }

    cat_acc_t liab_cats[128];
    int       liab_cats_count = 0;
    money_t   total_liabs = money_zero(base_cur);

    if (liabs) {
        size_t n = csilk_json_array_size(liabs);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* row = csilk_json_array_get(liabs, i);
            const char*   name = csilk_json_get_string(row, "name");
            const char*   cur_s = csilk_json_get_string(row, "currency");
            currency_t    cur = currency_from_str(cur_s ? cur_s : "CNY");
            money_t       v = exchange_rate_convert_m(db_get_money(row, "value", cur), base_cur);
            money_t       acc;
            if (money_add(total_liabs, v, &acc) == DECIMAL_OK) {
                total_liabs = acc;
            }
            if (name) {
                int found = -1;
                for (int k = 0; k < liab_cats_count; k++) {
                    if (strcmp(liab_cats[k].name, name) == 0) {
                        found = k;
                        break;
                    }
                }
                if (found >= 0) {
                    if (money_add(liab_cats[found].value, v, &acc) == DECIMAL_OK) {
                        liab_cats[found].value = acc;
                    }
                } else if (liab_cats_count < 128) {
                    strncpy(liab_cats[liab_cats_count].name, name, sizeof(liab_cats[0].name) - 1);
                    liab_cats[liab_cats_count].name[sizeof(liab_cats[0].name) - 1] = '\0';
                    liab_cats[liab_cats_count].value = v;
                    liab_cats_count++;
                }
            }
        }
        csilk_json_free(liabs);
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_t* asset_items = csilk_json_array();
    for (int i = 0; i < asset_cats_count; i++) {
        csilk_json_t* item = csilk_json_object();
        csilk_json_add_string(item, "name", asset_cats[i].name);
        csilk_json_add_number(item, "value", money_to_double(asset_cats[i].value));
        percentage_t pct;
        if (decimal_is_zero(total_assets.amount)) {
            pct = percentage_zero();
        } else if (percentage_calc(asset_cats[i].value, total_assets, 2, ROUND_HALF_UP, &pct) !=
                   DECIMAL_OK) {
            pct = percentage_zero();
        }
        csilk_json_add_number(item, "pct", percentage_to_double(pct));
        csilk_json_array_append(asset_items, item);
    }
    csilk_json_add_array(resp, "assets", asset_items);

    csilk_json_t* liab_items = csilk_json_array();
    for (int i = 0; i < liab_cats_count; i++) {
        csilk_json_t* item = csilk_json_object();
        csilk_json_add_string(item, "name", liab_cats[i].name);
        csilk_json_add_number(item, "value", money_to_double(liab_cats[i].value));
        percentage_t pct;
        if (decimal_is_zero(total_liabs.amount)) {
            pct = percentage_zero();
        } else if (percentage_calc(liab_cats[i].value, total_liabs, 2, ROUND_HALF_UP, &pct) !=
                   DECIMAL_OK) {
            pct = percentage_zero();
        }
        csilk_json_add_number(item, "pct", percentage_to_double(pct));
        csilk_json_array_append(liab_items, item);
    }
    csilk_json_add_array(resp, "liabilities", liab_items);
    money_t net_worth_m;
    if (money_sub(total_assets, total_liabs, &net_worth_m) != DECIMAL_OK) {
        net_worth_m = total_assets;
    }
    csilk_json_add_number(resp, "total_assets", money_to_double(total_assets));
    csilk_json_add_number(resp, "total_liabilities", money_to_double(total_liabs));
    csilk_json_add_number(resp, "net_worth", money_to_double(net_worth_m));

    out_res->code = 0;
    out_res->data_payload = resp;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
}

void
report_asset_summary(void* pool, const report_query_t* q, report_result_t* out_res)
{
    if (!pool || !q || !out_res) {
        if (out_res) {
            out_res->code = 1002;
            snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        }
        return;
    }
    memset(out_res, 0, sizeof(*out_res));

    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)q->user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)q->ledger_id);
    const char* params[] = {uid_str, lid_str, NULL};

    csilk_json_t* rows = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT a.current_value, a.currency, c.name as name, c.asset_type "
        "FROM assets a JOIN categories c ON a.category_id=c.id "
        "WHERE a.user_id=? AND a.ledger_id=?",
        params);
    currency_t    base_cur = currency_from_str("CNY");
    money_t       current_assets = money_zero(base_cur), current_liabs = money_zero(base_cur);
    csilk_json_t* by_cat = csilk_json_array();

    typedef struct {
        char    name[128];
        char    asset_type[32];
        money_t value;
        int     is_liab;
    } cat_summary_item_t;

    cat_summary_item_t cat_items[128];
    int                cat_item_count = 0;

    if (rows) {
        size_t n = csilk_json_array_size(rows);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* row = csilk_json_array_get(rows, i);
            const char*   cur_s = csilk_json_get_string(row, "currency");
            currency_t    cur = currency_from_str(cur_s ? cur_s : "CNY");
            money_t v = exchange_rate_convert_m(db_get_money(row, "current_value", cur), base_cur);
            const char* name = csilk_json_get_string(row, "name");
            const char* atype = csilk_json_get_string(row, "asset_type");
            int         is_liab =
                (atype && (strcmp(atype, "loan") == 0 || strcmp(atype, "credit_card") == 0 ||
                           strcmp(atype, "other_liability") == 0));
            money_t acc;
            if (is_liab) {
                if (money_add(current_liabs, v, &acc) == DECIMAL_OK) {
                    current_liabs = acc;
                }
            } else {
                if (money_add(current_assets, v, &acc) == DECIMAL_OK) {
                    current_assets = acc;
                }
            }

            int found = -1;
            for (int k = 0; k < cat_item_count; k++) {
                if (name && strcmp(cat_items[k].name, name) == 0 &&
                    cat_items[k].is_liab == is_liab) {
                    found = k;
                    break;
                }
            }
            if (found >= 0) {
                if (money_add(cat_items[found].value, v, &acc) == DECIMAL_OK) {
                    cat_items[found].value = acc;
                }
            } else if (cat_item_count < 128) {
                strncpy(cat_items[cat_item_count].name,
                        name ? name : "",
                        sizeof(cat_items[0].name) - 1);
                cat_items[cat_item_count].name[sizeof(cat_items[0].name) - 1] = '\0';
                strncpy(cat_items[cat_item_count].asset_type,
                        atype ? atype : "",
                        sizeof(cat_items[0].asset_type) - 1);
                cat_items[cat_item_count].value = v;
                cat_items[cat_item_count].is_liab = is_liab;
                cat_item_count++;
            }
        }
        csilk_json_free(rows);
    }

    for (int k = 0; k < cat_item_count; k++) {
        csilk_json_t* item = csilk_json_object();
        csilk_json_add_string(item, "name", cat_items[k].name);
        csilk_json_add_number(item, "value", money_to_double(cat_items[k].value));
        csilk_json_add_bool(item, "is_liability", cat_items[k].is_liab);
        csilk_json_array_append(by_cat, item);
    }

    // 30-day net wealth change calculated from multi-currency balance audit logs
    csilk_json_t* logs = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        db_is_postgres() ? "SELECT abl.delta, a.currency, c.asset_type "
                           "FROM asset_balance_logs abl "
                           "JOIN assets a ON abl.asset_id = a.id "
                           "JOIN categories c ON a.category_id = c.id "
                           "WHERE abl.user_id = ? AND a.ledger_id = ? "
                           "AND abl.created_at::date >= CURRENT_DATE - 30"
                         : "SELECT abl.delta, a.currency, c.asset_type "
                           "FROM asset_balance_logs abl "
                           "JOIN assets a ON abl.asset_id = a.id "
                           "JOIN categories c ON a.category_id = c.id "
                           "WHERE abl.user_id = ? AND a.ledger_id = ? "
                           "AND date(abl.created_at) >= date('now', '-30 days')",
        params);
    money_t change_30d = money_zero(base_cur);
    if (logs) {
        size_t n = csilk_json_array_size(logs);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* r = csilk_json_array_get(logs, i);
            const char*   cur_s = csilk_json_get_string(r, "currency");
            currency_t    cur = currency_from_str(cur_s ? cur_s : "CNY");
            money_t       delta = db_get_money(r, "delta", cur);
            const char*   atype = csilk_json_get_string(r, "asset_type");
            money_t       conv_delta = exchange_rate_convert_m(delta, base_cur);
            int           is_liab =
                (atype && (strcmp(atype, "loan") == 0 || strcmp(atype, "credit_card") == 0 ||
                           strcmp(atype, "other_liability") == 0));
            money_t acc;
            if (is_liab) {
                money_t neg_delta = money_neg(conv_delta);
                if (money_add(change_30d, neg_delta, &acc) == DECIMAL_OK) {
                    change_30d = acc;
                }
            } else {
                if (money_add(change_30d, conv_delta, &acc) == DECIMAL_OK) {
                    change_30d = acc;
                }
            }
        }
        csilk_json_free(logs);
    }

    money_t nw;
    if (money_sub(current_assets, current_liabs, &nw) != DECIMAL_OK) {
        nw = current_assets;
    }
    money_t prev_nw;
    if (money_sub(nw, change_30d, &prev_nw) != DECIMAL_OK) {
        prev_nw = nw;
    }
    percentage_t change_30d_pct;
    { /* 30-day net worth percentage change */
        money_t denom = prev_nw;
        if (money_is_negative(denom) || decimal_is_zero(denom.amount)) {
            denom = money_abs(nw);
        }
        if (decimal_is_zero(denom.amount)) {
            change_30d_pct = percentage_zero();
        } else if (percentage_calc(change_30d, denom, 2, ROUND_HALF_UP, &change_30d_pct) !=
                   DECIMAL_OK) {
            change_30d_pct = percentage_zero();
        }
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "current_value", money_to_double(nw));
    csilk_json_add_number(resp, "total_assets", money_to_double(current_assets));
    csilk_json_add_number(resp, "total_liabilities", money_to_double(current_liabs));
    csilk_json_add_number(resp, "change_30d", money_to_double(change_30d));
    csilk_json_add_number(resp, "change_30d_pct", percentage_to_double(change_30d_pct));
    csilk_json_add_array(resp, "by_category", by_cat);

    out_res->code = 0;
    out_res->data_payload = resp;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
}

void
report_multi_currency_summary(void* pool, const report_query_t* q, report_result_t* out_res)
{
    if (!pool || !q || !out_res) {
        if (out_res) {
            out_res->code = 1002;
            snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        }
        return;
    }
    memset(out_res, 0, sizeof(*out_res));

    const char* base_cur = (q->base_currency && q->base_currency[0]) ? q->base_currency : "CNY";

    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)q->user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)q->ledger_id);
    const char* params[] = {uid_str, lid_str, NULL};

    csilk_json_t* rows = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT a.id, a.name, a.currency, a.net_value, a.quantity, a.current_value, "
        "CASE WHEN a.quantity > 0 AND a.net_value > 0 THEN a.quantity * a.net_value ELSE "
        "a.current_value END as val, "
        "c.asset_type "
        "FROM assets a "
        "JOIN categories c ON a.category_id = c.id "
        "WHERE a.user_id = ? AND a.ledger_id = ?",
        params);

    currency_t base_currency_t = currency_from_str(base_cur);

    typedef struct {
        char       currency[16];
        currency_t cur;
        money_t    assets;
        money_t    liabilities;
        int        count;
    } cur_bucket_t;

    cur_bucket_t buckets[32];
    int          bucket_count = 0;
    memset(buckets, 0, sizeof(buckets));

    money_t grand_total_assets = money_zero(base_currency_t);
    money_t grand_total_liabs = money_zero(base_currency_t);

    if (rows) {
        size_t n = csilk_json_array_size(rows);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* r = csilk_json_array_get(rows, i);
            const char*   cur = csilk_json_get_string(r, "currency");
            if (!cur || !cur[0]) {
                cur = "CNY";
            }
            currency_t  cur_t = currency_from_str(cur);
            money_t     val = db_get_money(r, "val", cur_t);
            const char* atype = csilk_json_get_string(r, "asset_type");
            int         is_liab =
                (atype && (strcmp(atype, "loan") == 0 || strcmp(atype, "credit_card") == 0 ||
                           strcmp(atype, "other_liability") == 0));

            int b_idx = -1;
            for (int b = 0; b < bucket_count; b++) {
                if (currency_equals(buckets[b].cur, cur_t)) {
                    b_idx = b;
                    break;
                }
            }
            if (b_idx < 0 && bucket_count < 32) {
                b_idx = bucket_count++;
                buckets[b_idx].cur = cur_t;
                strncpy(buckets[b_idx].currency,
                        currency_code(&cur_t),
                        sizeof(buckets[b_idx].currency) - 1);
                buckets[b_idx].assets = money_zero(cur_t);
                buckets[b_idx].liabilities = money_zero(cur_t);
            }

            money_t acc;
            if (b_idx >= 0) {
                buckets[b_idx].count++;
                if (is_liab) {
                    if (money_add(buckets[b_idx].liabilities, val, &acc) == DECIMAL_OK) {
                        buckets[b_idx].liabilities = acc;
                    }
                } else {
                    if (money_add(buckets[b_idx].assets, val, &acc) == DECIMAL_OK) {
                        buckets[b_idx].assets = acc;
                    }
                }
            }

            money_t converted_val = exchange_rate_convert_m(val, base_currency_t);
            if (is_liab) {
                if (money_add(grand_total_liabs, converted_val, &acc) == DECIMAL_OK) {
                    grand_total_liabs = acc;
                }
            } else {
                if (money_add(grand_total_assets, converted_val, &acc) == DECIMAL_OK) {
                    grand_total_assets = acc;
                }
            }
        }
        csilk_json_free(rows);
    }

    money_t grand_net_worth;
    if (money_sub(grand_total_assets, grand_total_liabs, &grand_net_worth) != DECIMAL_OK) {
        grand_net_worth = grand_total_assets;
    }

    csilk_json_t* cur_arr = csilk_json_array();
    for (int b = 0; b < bucket_count; b++) {
        money_t b_net;
        if (money_sub(buckets[b].assets, buckets[b].liabilities, &b_net) != DECIMAL_OK) {
            b_net = buckets[b].assets;
        }
        money_t converted_net = exchange_rate_convert_m(b_net, base_currency_t);
        double  rate_to_base =
            rate_to_double(exchange_rate_get_rate(buckets[b].cur, base_currency_t));

        csilk_json_t* b_obj = csilk_json_object();
        csilk_json_add_string(b_obj, "currency", buckets[b].currency);
        csilk_json_add_number(b_obj, "asset_count", buckets[b].count);
        csilk_json_add_number(b_obj, "original_assets", money_to_double(buckets[b].assets));
        csilk_json_add_number(
            b_obj, "original_liabilities", money_to_double(buckets[b].liabilities));
        csilk_json_add_number(b_obj, "original_net_worth", money_to_double(b_net));
        csilk_json_add_number(b_obj, "rate_to_base", rate_to_base);
        csilk_json_add_number(b_obj, "converted_net_worth", money_to_double(converted_net));
        percentage_t pct;
        if (decimal_is_zero(grand_net_worth.amount)) {
            pct = percentage_zero();
        } else if (percentage_calc(converted_net, grand_net_worth, 2, ROUND_HALF_UP, &pct) !=
                   DECIMAL_OK) {
            pct = percentage_zero();
        }
        csilk_json_add_number(b_obj, "percentage", percentage_to_double(pct));
        csilk_json_array_append(cur_arr, b_obj);
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "base_currency", base_cur);
    csilk_json_add_number(resp, "total_net_worth", money_to_double(grand_net_worth));
    csilk_json_add_number(resp, "total_assets", money_to_double(grand_total_assets));
    csilk_json_add_number(resp, "total_liabilities", money_to_double(grand_total_liabs));
    csilk_json_add_array(resp, "currencies", cur_arr);

    out_res->code = 0;
    out_res->data_payload = resp;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
}

void
report_fx_pnl(void* pool, const report_query_t* q, report_result_t* out_res)
{
    if (!pool || !q || !out_res) {
        if (out_res) {
            out_res->code = 1002;
            snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        }
        return;
    }
    memset(out_res, 0, sizeof(*out_res));

    const char* base_cur = (q->base_currency && q->base_currency[0]) ? q->base_currency : "CNY";

    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)q->user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)q->ledger_id);

    const char* sql =
        db_is_postgres()
            ? "SELECT a.id, a.name, a.currency, a.quantity, a.cost_basis, a.net_value, "
              "a.current_value, "
              "       c.name as category_name, c.asset_type, "
              "       COALESCE("
              "           (SELECT rate FROM exchange_rate_history h WHERE h.target_currency = "
              "a.currency "
              "            AND h.rate_date <= COALESCE((SELECT to_char(MIN(t.transaction_date), "
              "'YYYY-MM-DD') FROM "
              "transactions t WHERE t.asset_id = a.id), to_char(a.created_at, 'YYYY-MM-DD')) "
              "            ORDER BY h.rate_date DESC LIMIT 1), "
              "           (SELECT rate FROM exchange_rate_history h WHERE h.target_currency = "
              "a.currency "
              "            ORDER BY h.rate_date ASC LIMIT 1) "
              "       ) as initial_rate "
              "FROM assets a "
              "JOIN categories c ON a.category_id = c.id "
              "WHERE a.user_id = ? AND a.ledger_id = ? AND a.currency IS NOT NULL AND a.currency "
              "!= ? AND a.currency "
              "!= ''"
            : "SELECT a.id, a.name, a.currency, a.quantity, a.cost_basis, a.net_value, "
              "a.current_value, "
              "       c.name as category_name, c.asset_type, "
              "       COALESCE("
              "           (SELECT rate FROM exchange_rate_history h WHERE h.target_currency = "
              "a.currency "
              "            AND h.rate_date <= COALESCE((SELECT date(MIN(t.transaction_date)) FROM "
              "transactions t WHERE t.asset_id = a.id), date(a.created_at)) "
              "            ORDER BY h.rate_date DESC LIMIT 1), "
              "           (SELECT rate FROM exchange_rate_history h WHERE h.target_currency = "
              "a.currency "
              "            ORDER BY h.rate_date ASC LIMIT 1) "
              "       ) as initial_rate "
              "FROM assets a "
              "JOIN categories c ON a.category_id = c.id "
              "WHERE a.user_id = ? AND a.ledger_id = ? AND a.currency IS NOT NULL AND a.currency "
              "!= ? AND a.currency "
              "!= ''";

    csilk_json_t* rows = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool, sql, (const char*[]){uid_str, lid_str, base_cur, NULL});

    csilk_json_t* items = csilk_json_array();
    currency_t    base_currency = currency_from_str(base_cur);

    money_t total_foreign_cost_base = money_zero(base_currency);
    money_t total_foreign_market_base = money_zero(base_currency);
    money_t total_asset_pnl_base = money_zero(base_currency);
    money_t total_fx_pnl_base = money_zero(base_currency);
    money_t total_combined_pnl_base = money_zero(base_currency);

    if (rows) {
        size_t n = csilk_json_array_size(rows);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* r = csilk_json_array_get(rows, i);
            int64_t       aid = db_get_int(r, "id");
            const char*   aname = csilk_json_get_string(r, "name");
            const char*   cur = csilk_json_get_string(r, "currency");
            const char*   cname = csilk_json_get_string(r, "category_name");
            currency_t    asset_cur = currency_from_str(cur);

            quantity_t q_qty = db_get_quantity(r, "quantity");
            money_t    cost = db_get_money(r, "cost_basis", asset_cur);
            price_t    nv = db_get_price(r, "net_value", asset_cur);
            money_t    cv = db_get_money(r, "current_value", asset_cur);

            money_t current_val_orig;
            if (!money_is_zero(cv)) {
                current_val_orig = cv;
            } else if (!quantity_is_zero(q_qty) && !decimal_is_zero(nv.unit_price)) {
                price_times_quantity(nv, q_qty, &current_val_orig);
            } else {
                current_val_orig = money_zero(asset_cur);
            }

            if (money_is_zero(current_val_orig) && money_is_zero(cost)) {
                continue;
            }

            rate_t    curr_fx_r = exchange_rate_get_rate(asset_cur, base_currency);
            rate_t    cost_fx_r;
            decimal_t init_rate_d = db_get_decimal(r, "initial_rate");
            if (!decimal_is_zero(init_rate_d)) {
                cost_fx_r = rate_from_decimal(init_rate_d, asset_cur, base_currency);
            } else {
                cost_fx_r = curr_fx_r;
            }

            money_t orig_pnl;
            money_sub(current_val_orig, cost, &orig_pnl);
            money_t asset_pnl_base;
            rate_convert_money(orig_pnl, curr_fx_r, &asset_pnl_base);

            rate_t r_diff;
            r_diff.from_currency = asset_cur;
            r_diff.to_currency = base_currency;
            decimal_sub(curr_fx_r.factor, cost_fx_r.factor, &r_diff.factor);

            money_t fx_pnl_base;
            if (!money_is_zero(cost)) {
                rate_convert_money(cost, r_diff, &fx_pnl_base);
            } else {
                rate_convert_money(current_val_orig, r_diff, &fx_pnl_base);
            }

            money_t combined_pnl_base;
            money_add(asset_pnl_base, fx_pnl_base, &combined_pnl_base);

            money_t cost_in_base, val_in_base;
            rate_convert_money(cost, cost_fx_r, &cost_in_base);
            rate_convert_money(current_val_orig, curr_fx_r, &val_in_base);

            money_add(total_foreign_cost_base, cost_in_base, &total_foreign_cost_base);
            money_add(total_foreign_market_base, val_in_base, &total_foreign_market_base);
            money_add(total_asset_pnl_base, asset_pnl_base, &total_asset_pnl_base);
            money_add(total_fx_pnl_base, fx_pnl_base, &total_fx_pnl_base);
            money_add(total_combined_pnl_base, combined_pnl_base, &total_combined_pnl_base);

            csilk_json_t* it = csilk_json_object();
            csilk_json_add_number(it, "asset_id", (double)aid);
            csilk_json_add_string(it, "asset_name", aname ? aname : "");
            csilk_json_add_string(it, "currency", cur ? cur : "");
            csilk_json_add_string(it, "category_name", cname ? cname : "");
            csilk_json_add_number(it, "cost_basis_orig", money_to_double(cost));
            csilk_json_add_number(it, "current_value_orig", money_to_double(current_val_orig));
            csilk_json_add_number(it, "current_fx_rate", rate_to_double(curr_fx_r));
            csilk_json_add_number(it, "cost_fx_rate", rate_to_double(cost_fx_r));
            csilk_json_add_number(it, "asset_pnl_base", money_to_double(asset_pnl_base));
            csilk_json_add_number(it, "fx_pnl_base", money_to_double(fx_pnl_base));
            csilk_json_add_number(it, "combined_pnl_base", money_to_double(combined_pnl_base));

            percentage_t fx_pct;
            if (!decimal_is_zero(cost_fx_r.factor)) {
                decimal_t diff_pct, hundred;
                hundred = decimal_from_int(100);
                decimal_div(r_diff.factor, cost_fx_r.factor, 4, ROUND_HALF_UP, &diff_pct);
                decimal_mul(diff_pct, hundred, &diff_pct);
                fx_pct = percentage_from_decimal(diff_pct);
            } else {
                fx_pct = percentage_zero();
            }
            csilk_json_add_number(it, "fx_return_rate", percentage_to_double(fx_pct));
            csilk_json_array_append(items, it);
        }
        csilk_json_free(rows);
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "base_currency", base_cur);
    csilk_json_add_number(
        resp, "total_foreign_cost_base", money_to_double(total_foreign_cost_base));
    csilk_json_add_number(
        resp, "total_foreign_market_base", money_to_double(total_foreign_market_base));
    csilk_json_add_number(resp, "total_asset_pnl_base", money_to_double(total_asset_pnl_base));
    csilk_json_add_number(resp, "total_fx_pnl_base", money_to_double(total_fx_pnl_base));
    csilk_json_add_number(
        resp, "total_combined_pnl_base", money_to_double(total_combined_pnl_base));
    csilk_json_add_array(resp, "assets", items);

    out_res->code = 0;
    out_res->data_payload = resp;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
}
