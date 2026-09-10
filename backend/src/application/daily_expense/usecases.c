/**
 * @file usecases.c
 * @brief 日常收支用例编排实现 (Application Daily Expense Use Cases)
 */

#include "application/daily_expense/usecases.h"
#include "domain/daily_expense/rules.h"
#include "domain/daily_expense/repository.h"
#include "infrastructure/repositories/daily_expense_repo_impl.h"
#include "common/balance.h"
#include "core/financial/money.h"
#include "core/financial/currency.h"
#include "core/ledger/ledger_engine.h"
#include "common/db.h"
#include "common/response.h"
#include <stdio.h>
#include <string.h>

/* ===== Internal helpers ===== */

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

    csilk_json_t* totals =
        mf_daily_expense_repo_monthly_totals(pool, user_id, ledger_id, date_pattern);
    if (totals && csilk_json_array_size(totals) > 0) {
        const csilk_json_t* tr = csilk_json_array_get(totals, 0);
        out_res->total_income = db_get_num(tr, "total_income");
        out_res->total_expense = db_get_num(tr, "total_expense");
        out_res->balance = out_res->total_income - out_res->total_expense;
    }
    if (totals) {
        csilk_json_free(totals);
    }

    *out_by_category =
        mf_daily_expense_repo_monthly_by_category(pool, user_id, ledger_id, date_pattern);
    *out_by_tag = mf_daily_expense_repo_monthly_by_tag(pool, user_id, ledger_id, date_pattern);
    *out_daily = mf_daily_expense_repo_monthly_daily(pool, user_id, ledger_id, date_pattern);

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
