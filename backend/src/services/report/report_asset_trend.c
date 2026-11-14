/**
 * @file report_asset_trend.c
 * @brief 资产历史走势趋势报表计算服务
 */

#include "services/report_asset_service.h"
#include "application/report/commands.h"
#include "common/db.h"
#include "services/market/exchange_rate_service.h"
#include "core/financial/money.h"
#include "core/financial/currency.h"
#include "core/financial/decimal.h"
#include "csilk/drivers/db.h"
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
            atype && (strcmp(atype, "loan") == 0 || strcmp(atype, "credit_card") == 0 ||
                      strcmp(atype, "other_liability") == 0);
    }
    if (asset_rows) {
        csilk_json_free(asset_rows);
    }

    // 3. Query balance logs within the period (start_date <= log_date <= today)
    const char* start_date = dates[0];
    const char* log_sql =
        db_is_postgres() ? "SELECT asset_id, delta, to_char(created_at, 'YYYY-MM-DD') as log_date "
                           "FROM asset_balance_logs "
                           "WHERE user_id=? AND to_char(created_at, 'YYYY-MM-DD') >= ? "
                           "ORDER BY created_at ASC"
                         : "SELECT asset_id, delta, date(created_at) as log_date "
                           "FROM asset_balance_logs "
                           "WHERE user_id=? AND date(created_at) >= ? "
                           "ORDER BY created_at ASC";
    const char*   log_params[] = {uid_str, start_date, NULL};
    csilk_json_t* log_rows = csilk_db_query_param_json((csilk_db_pool_t*)pool, log_sql, log_params);

    size_t             num_logs = log_rows ? csilk_json_array_size(log_rows) : 0;
    asset_trend_log_t* logs =
        num_logs > 0 ? (asset_trend_log_t*)calloc(num_logs, sizeof(asset_trend_log_t)) : NULL;
    for (size_t i = 0; i < num_logs; i++) {
        csilk_json_t* r = csilk_json_array_get(log_rows, i);
        logs[i].asset_id = db_get_int(r, "asset_id");
        currency_t log_cur = currency_from_str("CNY");
        for (size_t a = 0; a < num_assets; a++) {
            if (assets[a].id == logs[i].asset_id) {
                log_cur = assets[a].currency;
                break;
            }
        }
        logs[i].delta = db_get_money(r, "delta", log_cur);
        const char* ld = csilk_json_get_string(r, "log_date");
        snprintf(logs[i].log_date, sizeof(logs[i].log_date), "%s", ld ? ld : "1970-01-01");
    }
    if (log_rows) {
        csilk_json_free(log_rows);
    }

    // 4. For each sample date k, compute asset/liability values by unwinding logs > dates[k]
    double     net_worth[5];
    double     total_assets[5];
    double     total_liabilities[5];
    currency_t cny_cur = currency_from_str("CNY");

    for (int k = 0; k < 5; k++) {
        money_t sum_assets_m = money_zero(cny_cur);
        money_t sum_liabs_m = money_zero(cny_cur);

        for (size_t a = 0; a < num_assets; a++) {
            if (strcmp(assets[a].created_date, dates[k]) > 0) {
                continue;
            }

            money_t unwound_m = assets[a].current_value;
            for (size_t l = 0; l < num_logs; l++) {
                if (logs[l].asset_id == assets[a].id && strcmp(logs[l].log_date, dates[k]) > 0) {
                    money_t next_unwound;
                    if (money_sub(unwound_m, logs[l].delta, &next_unwound) == DECIMAL_OK) {
                        unwound_m = next_unwound;
                    }
                }
            }

            double unwound_val = money_to_double(unwound_m);
            if (unwound_val < 0) {
                unwound_val = 0;
            }

            money_t safe_unwound;
            money_from_double(unwound_val, assets[a].currency, &safe_unwound);
            money_t cny_m = exchange_rate_convert_m(safe_unwound, cny_cur);

            if (assets[a].is_liability) {
                money_t next_sum;
                if (money_add(sum_liabs_m, cny_m, &next_sum) == DECIMAL_OK) {
                    sum_liabs_m = next_sum;
                }
            } else {
                money_t next_sum;
                if (money_add(sum_assets_m, cny_m, &next_sum) == DECIMAL_OK) {
                    sum_assets_m = next_sum;
                }
            }
        }

        total_assets[k] = money_to_double(sum_assets_m);
        total_liabilities[k] = money_to_double(sum_liabs_m);

        money_t nw_m;
        if (money_sub(sum_assets_m, sum_liabs_m, &nw_m) == DECIMAL_OK) {
            net_worth[k] = money_to_double(nw_m);
        } else {
            net_worth[k] = total_assets[k] - total_liabilities[k];
        }
    }

    if (assets) {
        free(assets);
    }
    if (logs) {
        free(logs);
    }

    // 5. Build JSON response
    csilk_json_t* resp = csilk_json_object();
    csilk_json_t* labels_arr = csilk_json_array();
    csilk_json_t* nw_arr = csilk_json_array();
    csilk_json_t* assets_arr = csilk_json_array();
    csilk_json_t* liabs_arr = csilk_json_array();

    for (int k = 0; k < 5; k++) {
        csilk_json_array_append(labels_arr, csilk_json_string_new(dates[k]));
        csilk_json_array_append(nw_arr, csilk_json_number(net_worth[k]));
        csilk_json_array_append(assets_arr, csilk_json_number(total_assets[k]));
        csilk_json_array_append(liabs_arr, csilk_json_number(total_liabilities[k]));
    }

    csilk_json_add_array(resp, "labels", labels_arr);
    csilk_json_add_array(resp, "net_worth", nw_arr);
    csilk_json_add_array(resp, "assets", assets_arr);
    csilk_json_add_array(resp, "liabilities", liabs_arr);

    out_res->code = 0;
    out_res->data_payload = resp;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
}
