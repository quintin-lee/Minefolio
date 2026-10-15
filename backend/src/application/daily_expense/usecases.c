/**
 * @file usecases.c
 * @brief 日常收支用例编排实现 (Application Daily Expense Use Cases)
 */

#include "application/daily_expense/usecases.h"
#include "domain/daily_expense/rules.h"
#include "domain/daily_expense/repository.h"
#include "infrastructure/repositories/daily_expense_repo_impl.h"
#include "infrastructure/repositories/asset_repo_impl.h"
#include "common/balance.h"
#include "core/financial/money.h"
#include "core/financial/currency.h"
#include "core/ledger/ledger_engine.h"
#include "services/market/exchange_rate_service.h"
#include "common/db.h"
#include "common/response.h"
#include <stdio.h>
#include <string.h>

/* ===== Internal helpers ===== */

/* Asset existence pre-check inside an open transaction: rolls back and reports
 * 1002 on missing asset. Unifies SQLite/PostgreSQL behavior — PG enforces the
 * daily_expenses.asset_id FK at write time (500), while SQLite lets the write
 * through and fails later in apply_balance (1002). */
static int
check_asset_exists(void*                           pool,
                   int64_t                         user_id,
                   int64_t                         asset_id,
                   daily_expense_usecase_result_t* out_res)
{
    if (asset_exists((csilk_db_pool_t*)pool, user_id, asset_id)) {
        return 0;
    }
    csilk_db_exec(pool, "ROLLBACK");
    out_res->code = 1002;
    snprintf(out_res->message, sizeof(out_res->message), "资产无效");
    return -1;
}

static int
apply_balance(void*       pool,
              int64_t     user_id,
              int64_t     asset_id,
              double      amount,
              const char* currency,
              const char* type,
              int64_t     expense_id,
              const char* note)
{
    int        is_income = (strcmp(type, "income") == 0);
    currency_t cur = currency_from_str(currency);
    money_t    amt_m;
    money_from_double(amount, cur, &amt_m);
    return ledger_apply_expense(pool, user_id, asset_id, amt_m, is_income, expense_id, note);
}

static int
reverse_balance(void*       pool,
                int64_t     user_id,
                int64_t     asset_id,
                double      amount,
                const char* currency,
                const char* type,
                int64_t     expense_id,
                const char* note)
{
    int        is_income = (strcmp(type, "income") == 0);
    currency_t cur = currency_from_str(currency);
    money_t    amt_m;
    money_from_double(amount, cur, &amt_m);
    return ledger_reverse_expense(pool, user_id, asset_id, amt_m, is_income, expense_id, note);
}

static int
process_tags(void* pool, int64_t user_id, int64_t expense_id, const csilk_json_t* tags)
{
    if (!tags || !csilk_json_is_array(tags)) {
        return 0;
    }
    size_t n = csilk_json_array_size(tags);
    for (size_t i = 0; i < n; i++) {
        const csilk_json_t* tag_obj = csilk_json_array_get(tags, i);
        int64_t             tag_id = db_get_int(tag_obj, "id");
        const char*         name = csilk_json_get_string(tag_obj, "name");
        const char*         color = csilk_json_get_string(tag_obj, "color");

        int64_t tid = mf_daily_expense_repo_get_or_create_tag(pool, user_id, tag_id, name, color);
        if (tid <= 0) {
            continue;
        }
        if (mf_daily_expense_repo_tag_insert(pool, expense_id, tid) != 0) {
            return -1;
        }
    }
    return 0;
}

/* ===== Use case implementations ===== */

int
daily_expense_usecase_list(void*          pool,
                           int64_t        user_id,
                           int64_t        ledger_id,
                           int64_t        page,
                           int64_t        page_size,
                           const char*    expense_type,
                           const char*    category_id,
                           const char*    tag_ids,
                           const char*    start_date,
                           const char*    end_date,
                           csilk_json_t** out_json,
                           int64_t*       out_total)
{
    return mf_daily_expense_repo_list(pool,
                                      user_id,
                                      ledger_id,
                                      page,
                                      page_size,
                                      expense_type,
                                      category_id,
                                      tag_ids,
                                      start_date,
                                      end_date,
                                      out_json,
                                      out_total);
}

int
daily_expense_usecase_monthly(void*                           pool,
                              int64_t                         user_id,
                              int64_t                         ledger_id,
                              int64_t                         year,
                              int64_t                         month,
                              daily_expense_monthly_result_t* out_res,
                              csilk_json_t**                  out_by_category,
                              csilk_json_t**                  out_by_tag,
                              csilk_json_t**                  out_daily)
{
    char date_pattern[32];
    snprintf(date_pattern, sizeof(date_pattern), "%lld-%02d-%%", (long long)year, (int)month);

    /* 1. Monthly totals with currency conversion to base currency (CNY) */
    csilk_json_t* totals =
        mf_daily_expense_repo_monthly_totals(pool, user_id, ledger_id, date_pattern);
    double total_income = 0.0;
    double total_expense = 0.0;
    if (totals) {
        size_t n = csilk_json_array_size(totals);
        for (size_t i = 0; i < n; i++) {
            const csilk_json_t* tr = csilk_json_array_get(totals, i);
            const char*         type = csilk_json_get_string(tr, "expense_type");
            const char*         cur = csilk_json_get_string(tr, "currency");
            double              amt = db_get_num(tr, "amount") * exchange_rate_get_to_cny(cur);
            if (type && strcmp(type, "income") == 0) {
                total_income += amt;
            } else if (type && strcmp(type, "expense") == 0) {
                total_expense += amt;
            }
        }
        csilk_json_free(totals);
    }
    out_res->total_income = total_income;
    out_res->total_expense = total_expense;
    out_res->balance = total_income - total_expense;

    /* 2. By category breakdown with currency conversion */
    csilk_json_t* cat_rows =
        mf_daily_expense_repo_monthly_by_category(pool, user_id, ledger_id, date_pattern);
    csilk_json_t* cat_arr = csilk_json_array();
    if (cat_rows) {
        size_t n = csilk_json_array_size(cat_rows);
        typedef struct {
            char   name[128];
            char   expense_type[32];
            double amount;
        } cat_item_t;
        cat_item_t cats[128];
        int        cat_count = 0;
        for (size_t i = 0; i < n; i++) {
            const csilk_json_t* r = csilk_json_array_get(cat_rows, i);
            const char*         cname = csilk_json_get_string(r, "category_name");
            const char*         type = csilk_json_get_string(r, "expense_type");
            const char*         cur = csilk_json_get_string(r, "currency");
            double              amt = db_get_num(r, "amount") * exchange_rate_get_to_cny(cur);
            if (!cname || !type) {
                continue;
            }
            int found = -1;
            for (int k = 0; k < cat_count; k++) {
                if (strcmp(cats[k].name, cname) == 0 && strcmp(cats[k].expense_type, type) == 0) {
                    found = k;
                    break;
                }
            }
            if (found >= 0) {
                cats[found].amount += amt;
            } else if (cat_count < 128) {
                strncpy(cats[cat_count].name, cname, sizeof(cats[0].name) - 1);
                cats[cat_count].name[sizeof(cats[0].name) - 1] = '\0';
                strncpy(cats[cat_count].expense_type, type, sizeof(cats[0].expense_type) - 1);
                cats[cat_count].expense_type[sizeof(cats[0].expense_type) - 1] = '\0';
                cats[cat_count].amount = amt;
                cat_count++;
            }
        }
        csilk_json_free(cat_rows);

        /* Sort categories descending by amount */
        for (int i = 0; i < cat_count - 1; i++) {
            for (int j = i + 1; j < cat_count; j++) {
                if (cats[j].amount > cats[i].amount) {
                    cat_item_t tmp = cats[i];
                    cats[i] = cats[j];
                    cats[j] = tmp;
                }
            }
        }
        for (int k = 0; k < cat_count; k++) {
            csilk_json_t* item = csilk_json_object();
            csilk_json_add_string(item, "category_name", cats[k].name);
            csilk_json_add_string(item, "name", cats[k].name);
            csilk_json_add_string(item, "expense_type", cats[k].expense_type);
            csilk_json_add_number(item, "amount", cats[k].amount);
            csilk_json_array_append(cat_arr, item);
        }
    }
    *out_by_category = cat_arr;

    /* 3. By tag breakdown with currency conversion */
    csilk_json_t* tag_rows =
        mf_daily_expense_repo_monthly_by_tag(pool, user_id, ledger_id, date_pattern);
    csilk_json_t* tag_arr = csilk_json_array();
    if (tag_rows) {
        size_t n = csilk_json_array_size(tag_rows);
        typedef struct {
            char    name[128];
            double  amount;
            int64_t count;
        } tag_item_t;
        tag_item_t tags[128];
        int        tag_count = 0;
        for (size_t i = 0; i < n; i++) {
            const csilk_json_t* r = csilk_json_array_get(tag_rows, i);
            const char*         tname = csilk_json_get_string(r, "tag_name");
            const char*         cur = csilk_json_get_string(r, "currency");
            double              amt = db_get_num(r, "amount") * exchange_rate_get_to_cny(cur);
            int64_t             cnt = db_get_int(r, "count");
            if (!tname) {
                continue;
            }
            int found = -1;
            for (int k = 0; k < tag_count; k++) {
                if (strcmp(tags[k].name, tname) == 0) {
                    found = k;
                    break;
                }
            }
            if (found >= 0) {
                tags[found].amount += amt;
                tags[found].count += cnt;
            } else if (tag_count < 128) {
                strncpy(tags[tag_count].name, tname, sizeof(tags[0].name) - 1);
                tags[tag_count].name[sizeof(tags[0].name) - 1] = '\0';
                tags[tag_count].amount = amt;
                tags[tag_count].count = cnt;
                tag_count++;
            }
        }
        csilk_json_free(tag_rows);

        /* Sort tags descending by amount */
        for (int i = 0; i < tag_count - 1; i++) {
            for (int j = i + 1; j < tag_count; j++) {
                if (tags[j].amount > tags[i].amount) {
                    tag_item_t tmp = tags[i];
                    tags[i] = tags[j];
                    tags[j] = tmp;
                }
            }
        }
        for (int k = 0; k < tag_count; k++) {
            csilk_json_t* item = csilk_json_object();
            csilk_json_add_string(item, "tag_name", tags[k].name);
            csilk_json_add_number(item, "amount", tags[k].amount);
            csilk_json_add_number(item, "count", (double)tags[k].count);
            csilk_json_array_append(tag_arr, item);
        }
    }
    *out_by_tag = tag_arr;

    /* 4. Daily breakdown with currency conversion */
    csilk_json_t* daily_rows =
        mf_daily_expense_repo_monthly_daily(pool, user_id, ledger_id, date_pattern);
    csilk_json_t* daily_arr = csilk_json_array();
    if (daily_rows) {
        size_t n = csilk_json_array_size(daily_rows);
        typedef struct {
            char   date[32];
            double income;
            double expense;
        } daily_item_t;
        daily_item_t daily_items[64];
        int          daily_count = 0;
        for (size_t i = 0; i < n; i++) {
            const csilk_json_t* r = csilk_json_array_get(daily_rows, i);
            const char*         dt = csilk_json_get_string(r, "expense_date");
            const char*         type = csilk_json_get_string(r, "expense_type");
            const char*         cur = csilk_json_get_string(r, "currency");
            double              amt = db_get_num(r, "amount") * exchange_rate_get_to_cny(cur);
            if (!dt) {
                continue;
            }
            int d_idx = -1;
            for (int d = 0; d < daily_count; d++) {
                if (strcmp(daily_items[d].date, dt) == 0) {
                    d_idx = d;
                    break;
                }
            }
            if (d_idx < 0 && daily_count < 64) {
                d_idx = daily_count++;
                strncpy(daily_items[d_idx].date, dt, sizeof(daily_items[0].date) - 1);
                daily_items[d_idx].date[sizeof(daily_items[0].date) - 1] = '\0';
                daily_items[d_idx].income = 0;
                daily_items[d_idx].expense = 0;
            }
            if (d_idx >= 0) {
                if (type && strcmp(type, "income") == 0) {
                    daily_items[d_idx].income += amt;
                } else {
                    daily_items[d_idx].expense += amt;
                }
            }
        }
        csilk_json_free(daily_rows);

        for (int d = 0; d < daily_count; d++) {
            csilk_json_t* item = csilk_json_object();
            csilk_json_add_string(item, "expense_date", daily_items[d].date);
            csilk_json_add_number(item, "income", daily_items[d].income);
            csilk_json_add_number(item, "expense", daily_items[d].expense);
            csilk_json_array_append(daily_arr, item);
        }
    }
    *out_daily = daily_arr;

    out_res->code = 0;
    return 0;
}

int
daily_expense_usecase_create(void*                             pool,
                             const create_daily_expense_cmd_t* cmd,
                             const csilk_json_t*               tags,
                             daily_expense_usecase_result_t*   out_res)
{
    if (!mf_daily_expense_rule_validate_required(
            cmd->category_id, cmd->asset_id, cmd->expense_type, cmd->amount, cmd->expense_date)) {
        out_res->code = 1002;
        snprintf(out_res->message,
                 sizeof(out_res->message),
                 "asset_id、category_id、expense_type、amount、expense_date 为必填");
        return -1;
    }

    if (!mf_daily_expense_rule_validate_type(cmd->expense_type)) {
        out_res->code = 1002;
        snprintf(
            out_res->message, sizeof(out_res->message), "expense_type 必须为 income 或 expense");
        return -1;
    }

    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "数据库错误");
        return -1;
    }

    if (check_asset_exists(pool, cmd->user_id, cmd->asset_id, out_res) != 0) {
        return -1;
    }

    int64_t expense_id = mf_daily_expense_repo_insert(pool,
                                                      cmd->user_id,
                                                      cmd->ledger_id,
                                                      cmd->category_id,
                                                      cmd->asset_id,
                                                      cmd->expense_type,
                                                      cmd->amount,
                                                      cmd->currency,
                                                      cmd->expense_date,
                                                      cmd->note);
    if (expense_id <= 0) {
        csilk_db_exec(pool, "ROLLBACK");
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "创建失败");
        return -1;
    }

    const char* cur = cmd->currency && cmd->currency[0] ? cmd->currency : "CNY";
    if (apply_balance(pool,
                      cmd->user_id,
                      cmd->asset_id,
                      cmd->amount,
                      cur,
                      cmd->expense_type,
                      expense_id,
                      cmd->note) != 0) {
        csilk_db_exec(pool, "ROLLBACK");
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "资产无效");
        return -1;
    }

    if (process_tags(pool, cmd->user_id, expense_id, tags) != 0) {
        csilk_db_exec(pool, "ROLLBACK");
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "创建失败");
        return -1;
    }

    csilk_db_exec(pool, "COMMIT");
    out_res->code = 0;
    return 0;
}

int
daily_expense_usecase_update(void*                             pool,
                             const update_daily_expense_cmd_t* cmd,
                             const csilk_json_t*               tags,
                             daily_expense_usecase_result_t*   out_res)
{
    if (!mf_daily_expense_rule_validate_required(
            cmd->category_id, cmd->asset_id, cmd->expense_type, cmd->amount, cmd->expense_date)) {
        out_res->code = 1002;
        snprintf(out_res->message,
                 sizeof(out_res->message),
                 "asset_id、category_id、expense_type、amount、expense_date 为必填");
        return -1;
    }

    if (!mf_daily_expense_rule_validate_type(cmd->expense_type)) {
        out_res->code = 1002;
        snprintf(
            out_res->message, sizeof(out_res->message), "expense_type 必须为 income 或 expense");
        return -1;
    }

    /* Check existence */
    if (mf_daily_expense_repo_exists(pool, cmd->user_id, cmd->ledger_id, cmd->id) != 0) {
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "记录不存在");
        return -1;
    }

    /* Get old snapshot for balance rollback */
    mf_daily_expense_snapshot_t old = {0};
    if (mf_daily_expense_repo_get_snapshot(pool, cmd->user_id, cmd->ledger_id, cmd->id, &old) !=
        0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "查询失败");
        return -1;
    }

    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "数据库错误");
        return -1;
    }

    if (check_asset_exists(pool, cmd->user_id, cmd->asset_id, out_res) != 0) {
        return -1;
    }

    /* 1. Reverse old balance */
    if (reverse_balance(pool,
                        cmd->user_id,
                        old.asset_id,
                        old.amount,
                        old.currency,
                        old.expense_type,
                        cmd->id,
                        old.note) != 0) {
        csilk_db_exec(pool, "ROLLBACK");
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "回滚旧收支失败");
        return -1;
    }

    /* 2. Update record */
    const char* cur = cmd->currency && cmd->currency[0] ? cmd->currency : "CNY";
    if (mf_daily_expense_repo_update(pool,
                                     cmd->user_id,
                                     cmd->ledger_id,
                                     cmd->id,
                                     cmd->category_id,
                                     cmd->asset_id,
                                     cmd->expense_type,
                                     cmd->amount,
                                     cur,
                                     cmd->expense_date,
                                     cmd->note) != 0) {
        csilk_db_exec(pool, "ROLLBACK");
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "更新失败");
        return -1;
    }

    /* 3. Apply new balance */
    if (apply_balance(pool,
                      cmd->user_id,
                      cmd->asset_id,
                      cmd->amount,
                      cur,
                      cmd->expense_type,
                      cmd->id,
                      cmd->note) != 0) {
        csilk_db_exec(pool, "ROLLBACK");
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "资产无效");
        return -1;
    }

    /* 4. Rebind tags */
    mf_daily_expense_repo_tag_delete_all(pool, cmd->id);
    if (process_tags(pool, cmd->user_id, cmd->id, tags) != 0) {
        csilk_db_exec(pool, "ROLLBACK");
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "更新失败");
        return -1;
    }

    csilk_db_exec(pool, "COMMIT");
    out_res->code = 0;
    return 0;
}

int
daily_expense_usecase_delete(void*                           pool,
                             int64_t                         user_id,
                             int64_t                         ledger_id,
                             int64_t                         id,
                             daily_expense_usecase_result_t* out_res)
{
    /* Get snapshot for balance rollback */
    mf_daily_expense_snapshot_t old = {0};
    if (mf_daily_expense_repo_get_snapshot(pool, user_id, ledger_id, id, &old) != 0) {
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "记录不存在");
        return -1;
    }

    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "数据库错误");
        return -1;
    }

    /* 1. Delete tags */
    mf_daily_expense_repo_tag_delete_all(pool, id);

    /* 2. Reverse balance */
    if (reverse_balance(pool,
                        user_id,
                        old.asset_id,
                        old.amount,
                        old.currency,
                        old.expense_type,
                        id,
                        old.note) != 0) {
        csilk_db_exec(pool, "ROLLBACK");
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "删除失败");
        return -1;
    }

    /* 3. Delete record */
    if (mf_daily_expense_repo_delete(pool, user_id, ledger_id, id) != 0) {
        csilk_db_exec(pool, "ROLLBACK");
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "删除失败");
        return -1;
    }

    csilk_db_exec(pool, "COMMIT");
    out_res->code = 0;
    return 0;
}
