#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include "common/jwt.h"
#include "services/market/exchange_rate_service.h"
#include "interfaces/http/controllers/portfolio_controller.h"
#include "csilk/csilk.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {

    int64_t asset_id;

    double cost_for_pnl;

    double qty;

    double realized;

} holding_pnl_t;

typedef struct {
    int64_t id;
    double  current_value;
    char    currency[16];
    char    created_date[16];
    int     is_liability;
} asset_trend_item_t;

typedef struct {
    int64_t asset_id;
    double  delta;
    char    log_date[16];
} asset_trend_log_t;

void
report_asset_trend(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }
    int64_t ledger_id = ctx_ledger_id(c, user_id, "viewer");
    if (ledger_id < 0) {
        return;
    }
    const char* period_str = csilk_get_query(c, "period");
    int         days = 30;
    if (period_str) {
        if (strcmp(period_str, "90d") == 0) {
            days = 90;
        } else if (strcmp(period_str, "365d") == 0 || strcmp(period_str, "1y") == 0) {
            days = 365;
        } else {
            days = atoi(period_str);
        }
    }
    if (days <= 0 || days > 365) {
        days = 30;
    }

    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);

    // 1. Generate 5 sample points (labels) across the period
    time_t    now = time(NULL);
    struct tm cur_tm = *localtime(&now);
    char      dates[5][16];
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
            ? (ledger_id > 0 ? "SELECT a.id, a.current_value, a.currency, c.asset_type, "
                               "to_char(a.created_at, 'YYYY-MM-DD') as created_date "
                               "FROM assets a JOIN categories c ON a.category_id=c.id "
                               "WHERE a.user_id=? AND a.ledger_id=?"
                             : "SELECT a.id, a.current_value, a.currency, c.asset_type, "
                               "to_char(a.created_at, 'YYYY-MM-DD') as created_date "
                               "FROM assets a JOIN categories c ON a.category_id=c.id "
                               "WHERE a.user_id=?")
            : (ledger_id > 0 ? "SELECT a.id, a.current_value, a.currency, c.asset_type, "
                               "date(a.created_at) as created_date "
                               "FROM assets a JOIN categories c ON a.category_id=c.id "
                               "WHERE a.user_id=? AND a.ledger_id=?"
                             : "SELECT a.id, a.current_value, a.currency, c.asset_type, "
                               "date(a.created_at) as created_date "
                               "FROM assets a JOIN categories c ON a.category_id=c.id "
                               "WHERE a.user_id=?");
    const char*      asset_params[3] = {uid_str, (ledger_id > 0 ? lid_str : NULL), NULL};
    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    asset_rows = csilk_db_query_param_json(pool, asset_sql, asset_params);

    size_t              num_assets = asset_rows ? csilk_json_array_size(asset_rows) : 0;
    asset_trend_item_t* assets =
        num_assets > 0 ? (asset_trend_item_t*)calloc(num_assets, sizeof(asset_trend_item_t)) : NULL;
    for (size_t i = 0; i < num_assets; i++) {
        csilk_json_t* r = csilk_json_array_get(asset_rows, i);
        assets[i].id = db_get_int(r, "id");
        assets[i].current_value = db_get_num(r, "current_value");
        const char* cur = csilk_json_get_string(r, "currency");
        snprintf(assets[i].currency, sizeof(assets[i].currency), "%s", cur ? cur : "CNY");
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
            ? (ledger_id > 0
                   ? "SELECT abl.asset_id, abl.delta, to_char(abl.created_at, 'YYYY-MM-DD') as "
                     "log_date "
                     "FROM asset_balance_logs abl JOIN assets a ON abl.asset_id=a.id "
                     "WHERE abl.user_id=? AND a.ledger_id=? AND abl.created_at::date >= ?::date"
                   : "SELECT abl.asset_id, abl.delta, to_char(abl.created_at, 'YYYY-MM-DD') as "
                     "log_date "
                     "FROM asset_balance_logs abl "
                     "WHERE abl.user_id=? AND abl.created_at::date >= ?::date")
            : (ledger_id > 0 ? "SELECT abl.asset_id, abl.delta, date(abl.created_at) as log_date "
                               "FROM asset_balance_logs abl JOIN assets a ON abl.asset_id=a.id "
                               "WHERE abl.user_id=? AND a.ledger_id=? AND date(abl.created_at) >= ?"
                             : "SELECT abl.asset_id, abl.delta, date(abl.created_at) as log_date "
                               "FROM asset_balance_logs abl "
                               "WHERE abl.user_id=? AND date(abl.created_at) >= ?");
    const char* log_params[4] = {
        uid_str, (ledger_id > 0 ? lid_str : dates[0]), (ledger_id > 0 ? dates[0] : NULL), NULL};
    csilk_json_t* log_rows = csilk_db_query_param_json(pool, log_sql, log_params);

    size_t             num_logs = log_rows ? csilk_json_array_size(log_rows) : 0;
    asset_trend_log_t* logs =
        num_logs > 0 ? (asset_trend_log_t*)calloc(num_logs, sizeof(asset_trend_log_t)) : NULL;
    for (size_t i = 0; i < num_logs; i++) {
        csilk_json_t* r = csilk_json_array_get(log_rows, i);
        logs[i].asset_id = db_get_int(r, "asset_id");
        logs[i].delta = db_get_num(r, "delta");
        const char* ld = csilk_json_get_string(r, "log_date");
        snprintf(logs[i].log_date, sizeof(logs[i].log_date), "%s", ld ? ld : "");
    }
    if (log_rows) {
        csilk_json_free(log_rows);
    }

    // 4. Reconstruct historical balances for each sample point
    double pt_assets[5] = {0};
    double pt_liabs[5] = {0};

    for (int k = 0; k < 5; k++) {
        const char* target_date = dates[k];
        for (size_t a = 0; a < num_assets; a++) {
            if (strcmp(assets[a].created_date, target_date) > 0) {
                continue;
            }
            double val = assets[a].current_value;
            for (size_t l = 0; l < num_logs; l++) {
                if (logs[l].asset_id == assets[a].id && strcmp(logs[l].log_date, target_date) > 0) {
                    val -= logs[l].delta;
                }
            }
            double conv_val = val * exchange_rate_get_to_cny(assets[a].currency);
            if (assets[a].is_liability) {
                pt_liabs[k] += conv_val;
            } else {
                pt_assets[k] += conv_val;
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
    csilk_json_add_string(resp, "period", period_str ? period_str : "30d");

    csilk_json_t* labels_arr = csilk_json_array();
    csilk_json_t* nw_arr = csilk_json_array();
    csilk_json_t* assets_arr = csilk_json_array();
    csilk_json_t* liabs_arr = csilk_json_array();

    for (int k = 0; k < 5; k++) {
        csilk_json_array_append(labels_arr, csilk_json_string_new(dates[k]));
        csilk_json_array_append(nw_arr, csilk_json_number(pt_assets[k] - pt_liabs[k]));
        csilk_json_array_append(assets_arr, csilk_json_number(pt_assets[k]));
        csilk_json_array_append(liabs_arr, csilk_json_number(pt_liabs[k]));
    }

    csilk_json_add_array(resp, "labels", labels_arr);
    csilk_json_add_array(resp, "net_worth", nw_arr);
    csilk_json_add_array(resp, "assets", assets_arr);
    csilk_json_add_array(resp, "liabilities", liabs_arr);

    respond_ok(c, resp);
}

void
report_asset_breakdown(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }
    int64_t ledger_id = ctx_ledger_id(c, user_id, "viewer");
    if (ledger_id < 0) {
        return;
    }
    csilk_db_pool_t* pool = db_get_pool();

    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);
    const char* params[] = {uid_str, lid_str, NULL};

    csilk_json_t* assets = csilk_db_query_param_json(
        pool,
        "SELECT c.name as name, a.current_value as value, a.currency as currency "
        "FROM assets a JOIN categories c ON a.category_id=c.id "
        "WHERE a.user_id=? AND a.ledger_id=? AND c.asset_type NOT IN "
        "('loan','credit_card','other_liability')",
        params);
    csilk_json_t* liabs = csilk_db_query_param_json(
        pool,
        "SELECT c.name as name, a.current_value as value, a.currency as currency "
        "FROM assets a JOIN categories c ON a.category_id=c.id "
        "WHERE a.user_id=? AND a.ledger_id=? AND c.asset_type IN "
        "('loan','credit_card','other_liability')",
        params);

    typedef struct {
        char   name[128];
        double value;
    } cat_acc_t;

    cat_acc_t asset_cats[128];
    int       asset_cats_count = 0;
    double    total_assets = 0;

    if (assets) {
        size_t n = csilk_json_array_size(assets);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* row = csilk_json_array_get(assets, i);
            const char*   name = csilk_json_get_string(row, "name");
            const char*   cur = csilk_json_get_string(row, "currency");
            double        v = db_get_num(row, "value") * exchange_rate_get_to_cny(cur);
            total_assets += v;
            if (name) {
                int found = -1;
                for (int k = 0; k < asset_cats_count; k++) {
                    if (strcmp(asset_cats[k].name, name) == 0) {
                        found = k;
                        break;
                    }
                }
                if (found >= 0) {
                    asset_cats[found].value += v;
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
    double    total_liabs = 0;

    if (liabs) {
        size_t n = csilk_json_array_size(liabs);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* row = csilk_json_array_get(liabs, i);
            const char*   name = csilk_json_get_string(row, "name");
            const char*   cur = csilk_json_get_string(row, "currency");
            double        v = db_get_num(row, "value") * exchange_rate_get_to_cny(cur);
            total_liabs += v;
            if (name) {
                int found = -1;
                for (int k = 0; k < liab_cats_count; k++) {
                    if (strcmp(liab_cats[k].name, name) == 0) {
                        found = k;
                        break;
                    }
                }
                if (found >= 0) {
                    liab_cats[found].value += v;
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
        csilk_json_add_number(item, "value", asset_cats[i].value);
        csilk_json_add_number(
            item, "pct", total_assets > 0 ? (asset_cats[i].value / total_assets * 100) : 0);
        csilk_json_array_append(asset_items, item);
    }
    csilk_json_add_array(resp, "assets", asset_items);

    csilk_json_t* liab_items = csilk_json_array();
    for (int i = 0; i < liab_cats_count; i++) {
        csilk_json_t* item = csilk_json_object();
        csilk_json_add_string(item, "name", liab_cats[i].name);
        csilk_json_add_number(item, "value", liab_cats[i].value);
        csilk_json_add_number(
            item, "pct", total_liabs > 0 ? (liab_cats[i].value / total_liabs * 100) : 0);
        csilk_json_array_append(liab_items, item);
    }
    csilk_json_add_array(resp, "liabilities", liab_items);
    csilk_json_add_number(resp, "total_assets", total_assets);
    csilk_json_add_number(resp, "total_liabilities", total_liabs);
    csilk_json_add_number(resp, "net_worth", total_assets - total_liabs);
    respond_ok(c, resp);
}

void
report_transaction_performance(csilk_ctx_t* c)
{
    api_portfolio_performance(c);
}

void
report_asset_summary(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }
    int64_t ledger_id = ctx_ledger_id(c, user_id, "viewer");
    if (ledger_id < 0) {
        return;
    }
    csilk_db_pool_t* pool = db_get_pool();

    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);
    const char* params[] = {uid_str, lid_str, NULL};

    csilk_json_t* rows = csilk_db_query_param_json(
        pool,
        "SELECT a.current_value, a.currency, c.name as name, c.asset_type "
        "FROM assets a JOIN categories c ON a.category_id=c.id "
        "WHERE a.user_id=? AND a.ledger_id=?",
        params);
    double        current_assets = 0, current_liabs = 0;
    csilk_json_t* by_cat = csilk_json_array();

    typedef struct {
        char   name[128];
        char   asset_type[32];
        double value;
        int    is_liab;
    } cat_summary_item_t;

    cat_summary_item_t cat_items[128];
    int                cat_item_count = 0;

    if (rows) {
        size_t n = csilk_json_array_size(rows);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* row = csilk_json_array_get(rows, i);
            const char*   cur = csilk_json_get_string(row, "currency");
            double        v = db_get_num(row, "current_value") * exchange_rate_get_to_cny(cur);
            const char*   name = csilk_json_get_string(row, "name");
            const char*   atype = csilk_json_get_string(row, "asset_type");
            int           is_liab =
                (atype && (strcmp(atype, "loan") == 0 || strcmp(atype, "credit_card") == 0 ||
                           strcmp(atype, "other_liability") == 0));
            if (is_liab) {
                current_liabs += v;
            } else {
                current_assets += v;
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
                cat_items[found].value += v;
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
        csilk_json_add_number(item, "value", cat_items[k].value);
        csilk_json_add_bool(item, "is_liability", cat_items[k].is_liab);
        csilk_json_array_append(by_cat, item);
    }

    // 30-day net wealth change calculated from multi-currency balance audit logs
    csilk_json_t* logs = csilk_db_query_param_json(
        pool,
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
    double change_30d = 0;
    if (logs) {
        size_t n = csilk_json_array_size(logs);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* r = csilk_json_array_get(logs, i);
            double        delta = db_get_num(r, "delta");
            const char*   cur = csilk_json_get_string(r, "currency");
            const char*   atype = csilk_json_get_string(r, "asset_type");
            double        conv_delta = delta * exchange_rate_get_to_cny(cur);
            int           is_liab =
                (atype && (strcmp(atype, "loan") == 0 || strcmp(atype, "credit_card") == 0 ||
                           strcmp(atype, "other_liability") == 0));
            if (is_liab) {
                change_30d -= conv_delta;
            } else {
                change_30d += conv_delta;
            }
        }
        csilk_json_free(logs);
    }

    double nw = current_assets - current_liabs;
    double prev_nw = nw - change_30d;
    double change_30d_pct = 0.0;
    if (prev_nw > 0) {
        change_30d_pct = (change_30d / prev_nw) * 100.0;
    } else if (nw != 0) {
        change_30d_pct = (change_30d / fabs(nw)) * 100.0;
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "current_value", nw);
    csilk_json_add_number(resp, "total_assets", current_assets);
    csilk_json_add_number(resp, "total_liabilities", current_liabs);
    csilk_json_add_number(resp, "change_30d", change_30d);
    csilk_json_add_number(resp, "change_30d_pct", change_30d_pct);
    csilk_json_add_array(resp, "by_category", by_cat);
    respond_ok(c, resp);
}

/**
 * @brief 生成多币种资产构成分布及基准币种统一折算报表
 *
 * 核心逻辑：
 * 1. 扫描当前用户所有的有效资产与负债，按 original currency 聚合为资产池桶 (currency buckets)；
 * 2. 统计每个币种的原币总资产 (original_assets)、总负债 (original_liabilities) 及净额；
 * 3. 调用外汇转换引擎 exchange_rate_convert，将各币种资产统一折合为请求指定的基准币种 (默认 CNY)；
 * 4. 计算各币种在折算总净资产中的占比 (percentage)，构建结构化报表响应。
 *
 * @param c HTTP 上下文（支持 query 参数 base_currency，默认 "CNY"）
 */
void
report_multi_currency_summary(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        respond_unauthorized(c);
        return;
    }
    int64_t ledger_id = ctx_ledger_id(c, user_id, "viewer");
    if (ledger_id < 0) {
        return;
    }
    const char* base_cur = csilk_get_query(c, "base_currency");
    if (!base_cur || !base_cur[0]) {
        base_cur = "CNY";
    }

    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);
    const char* params[] = {uid_str, lid_str, NULL};

    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    rows = csilk_db_query_param_json(
        pool,
        "SELECT a.id, a.name, a.currency, a.net_value, a.quantity, a.current_value, "
        "CASE WHEN a.quantity > 0 AND a.net_value > 0 THEN a.quantity * a.net_value ELSE "
        "a.current_value END as val, "
        "c.asset_type "
        "FROM assets a "
        "JOIN categories c ON a.category_id = c.id "
        "WHERE a.user_id = ? AND a.ledger_id = ?",
        params);

    typedef struct {
        char   currency[16];
        double assets;
        double liabilities;
        int    count;
    } cur_bucket_t;

    cur_bucket_t buckets[32];
    int          bucket_count = 0;
    memset(buckets, 0, sizeof(buckets));

    double grand_total_assets = 0;
    double grand_total_liabs = 0;

    if (rows) {
        size_t n = csilk_json_array_size(rows);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* r = csilk_json_array_get(rows, i);
            const char*   cur = csilk_json_get_string(r, "currency");
            if (!cur || !cur[0]) {
                cur = "CNY";
            }
            double      val = db_get_num(r, "val");
            const char* atype = csilk_json_get_string(r, "asset_type");
            int         is_liab =
                (atype && (strcmp(atype, "loan") == 0 || strcmp(atype, "credit_card") == 0 ||
                           strcmp(atype, "other_liability") == 0));

            int b_idx = -1;
            for (int b = 0; b < bucket_count; b++) {
                if (strcasecmp(buckets[b].currency, cur) == 0) {
                    b_idx = b;
                    break;
                }
            }
            if (b_idx < 0 && bucket_count < 32) {
                b_idx = bucket_count++;
                strncpy(buckets[b_idx].currency, cur, sizeof(buckets[b_idx].currency) - 1);
            }

            if (b_idx >= 0) {
                buckets[b_idx].count++;
                if (is_liab) {
                    buckets[b_idx].liabilities += val;
                } else {
                    buckets[b_idx].assets += val;
                }
            }

            double converted_val = exchange_rate_convert(val, cur, base_cur);
            if (is_liab) {
                grand_total_liabs += converted_val;
            } else {
                grand_total_assets += converted_val;
            }
        }
        csilk_json_free(rows);
    }

    double grand_net_worth = grand_total_assets - grand_total_liabs;

    csilk_json_t* cur_arr = csilk_json_array();
    for (int b = 0; b < bucket_count; b++) {
        double b_net = buckets[b].assets - buckets[b].liabilities;
        double converted_net = exchange_rate_convert(b_net, buckets[b].currency, base_cur);
        double rate_to_base = exchange_rate_convert(1.0, buckets[b].currency, base_cur);

        csilk_json_t* b_obj = csilk_json_object();
        csilk_json_add_string(b_obj, "currency", buckets[b].currency);
        csilk_json_add_number(b_obj, "asset_count", buckets[b].count);
        csilk_json_add_number(b_obj, "original_assets", buckets[b].assets);
        csilk_json_add_number(b_obj, "original_liabilities", buckets[b].liabilities);
        csilk_json_add_number(b_obj, "original_net_worth", b_net);
        csilk_json_add_number(b_obj, "rate_to_base", rate_to_base);
        csilk_json_add_number(b_obj, "converted_net_worth", converted_net);
        double pct = (grand_net_worth > 0) ? (converted_net / grand_net_worth * 100.0) : 0.0;
        csilk_json_add_number(b_obj, "percentage", pct);
        csilk_json_array_append(cur_arr, b_obj);
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "base_currency", base_cur);
    csilk_json_add_number(resp, "total_net_worth", grand_net_worth);
    csilk_json_add_number(resp, "total_assets", grand_total_assets);
    csilk_json_add_number(resp, "total_liabilities", grand_total_liabs);
    csilk_json_add_array(resp, "currencies", cur_arr);

    respond_ok(c, resp);
}

/**
 * @brief 境外外币资产投资回报之「标的价格涨跌」与「纯汇率波动损益 (FX PnL)」双因子剥离分析
 *
 * 金融归因数学模型：
 * 设某外币资产 A（计价币种 C，基准币种 B）：
 * - 原币当前市值：P_current = current_value 或 quantity * net_value
 * - 原币持仓成本：C_basis = cost_basis
 * - 当前实时汇率 (C -> B)：R_current
 * - 买入历史加权汇率 (C -> B)：R_cost（从 exchange_rate_history 或买入时快照获取）
 *
 * 收益拆解归因：
 * 1. 标的价格盈亏（折合基准币）：
 *    Asset_Price_PnL_in_Base = (P_current - C_basis) * R_current
 * 2. 纯汇率波动损益（折合基准币）：
 *    FX_PnL_in_Base = C_basis * (R_current - R_cost)
 * 3. 综合总回报（基准币总收益）：
 *    Combined_PnL_in_Base = Asset_Price_PnL_in_Base + FX_PnL_in_Base
 *                         = P_current * R_current - C_basis * R_cost
 *
 * @param c HTTP 上下文（支持 query 参数 base_currency，默认 "CNY"）
 */
void
report_fx_pnl(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        respond_unauthorized(c);
        return;
    }
    int64_t ledger_id = ctx_ledger_id(c, user_id, "viewer");
    if (ledger_id < 0) {
        return;
    }

    const char* base_cur = csilk_get_query(c, "base_currency");
    if (!base_cur || !base_cur[0]) {
        base_cur = "CNY";
    }

    csilk_db_pool_t* pool = db_get_pool();
    char             uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);

    const char* sql =
        db_is_postgres()
            ? "SELECT a.id, a.name, a.currency, a.quantity, a.cost_basis, a.net_value, "
              "a.current_value, "
              "       c.name as category_name, c.asset_type, "
              "       COALESCE("
              "           (SELECT rate FROM exchange_rate_history h WHERE h.target_currency = "
              "a.currency "
              "            AND h.rate_date <= COALESCE((SELECT MIN(t.transaction_date) FROM "
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
              "            AND h.rate_date <= COALESCE((SELECT MIN(t.transaction_date) FROM "
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

    csilk_json_t* rows =
        csilk_db_query_param_json(pool, sql, (const char*[]){uid_str, lid_str, base_cur, NULL});

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

            quantity_t q = db_get_quantity(r, "quantity");
            money_t    cost = db_get_money(r, "cost_basis", asset_cur);
            price_t    nv = db_get_price(r, "net_value", asset_cur);
            money_t    cv = db_get_money(r, "current_value", asset_cur);

            money_t current_val_orig;
            if (!money_is_zero(cv)) {
                current_val_orig = cv;
            } else if (!quantity_is_zero(q) && !decimal_is_zero(nv.unit_price)) {
                price_times_quantity(nv, q, &current_val_orig);
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
                // If historical exchange rate is unavailable, default cost rate to current rate (0 FX fluctuation gain)
                cost_fx_r = curr_fx_r;
            }

            // Asset Price PnL: (curr_val_orig - cost) * curr_fx_r
            money_t orig_pnl;
            money_sub(current_val_orig, cost, &orig_pnl);
            money_t asset_pnl_base;
            rate_convert_money(orig_pnl, curr_fx_r, &asset_pnl_base);

            // FX PnL: cost * (curr_fx_r - cost_fx_r)
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

            // Combined PnL: asset_pnl_base + fx_pnl_base
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

    respond_ok(c, resp);
}
