/**
 * @file daily_expense_repo_impl.c
 * @brief 日常收支仓储 SQL 实现 (Infrastructure Daily Expense Repository)
 *
 * 包装 repositories/daily_expense_repo.c 中的现有 SQL 函数。
 */

#include "infrastructure/repositories/daily_expense_repo_impl.h"
#include "repositories/daily_expense_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <string.h>

int
mf_daily_expense_repo_list(void*          db_pool,
                           int64_t        user_id,
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
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    *out_json = de_list(pool,
                        user_id,
                        page,
                        page_size,
                        expense_type,
                        category_id,
                        tag_ids,
                        start_date,
                        end_date,
                        out_total);
    return *out_json ? 0 : -1;
}

csilk_json_t*
mf_daily_expense_repo_monthly_totals(void* db_pool, int64_t user_id, const char* pattern)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    return de_monthly_totals(pool, user_id, pattern);
}

csilk_json_t*
mf_daily_expense_repo_monthly_by_category(void* db_pool, int64_t user_id, const char* pattern)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    return de_monthly_by_category(pool, user_id, pattern);
}

csilk_json_t*
mf_daily_expense_repo_monthly_by_tag(void* db_pool, int64_t user_id, const char* pattern)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    return de_monthly_by_tag(pool, user_id, pattern);
}

csilk_json_t*
mf_daily_expense_repo_monthly_daily(void* db_pool, int64_t user_id, const char* pattern)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    return de_monthly_daily(pool, user_id, pattern);
}

int64_t
mf_daily_expense_repo_insert(void*       db_pool,
                             int64_t     user_id,
                             int64_t     category_id,
                             int64_t     asset_id,
                             const char* expense_type,
                             double      amount,
                             const char* currency,
                             const char* date,
                             const char* note)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    return de_insert(
        pool, user_id, category_id, asset_id, expense_type, amount, currency, date, note);
}

int
mf_daily_expense_repo_get_snapshot(void*                        db_pool,
                                   int64_t                      user_id,
                                   int64_t                      id,
                                   mf_daily_expense_snapshot_t* out)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    csilk_json_t*    row = de_get(pool, user_id, id);
    if (!row || csilk_json_array_size(row) == 0) {
        if (row) {
            csilk_json_free(row);
        }
        return -1;
    }

    const csilk_json_t* r = csilk_json_array_get(row, 0);
    out->amount = db_get_num(r, "amount");
    strncpy(out->expense_type,
            csilk_json_get_string(r, "expense_type") ?: "",
            sizeof(out->expense_type) - 1);
    strncpy(
        out->currency, csilk_json_get_string(r, "currency") ?: "CNY", sizeof(out->currency) - 1);
    out->asset_id = (int64_t)db_get_int(r, "asset_id");
    strncpy(out->note, csilk_json_get_string(r, "note") ?: "", sizeof(out->note) - 1);

    csilk_json_free(row);
    return 0;
}

int
mf_daily_expense_repo_update(void*       db_pool,
                             int64_t     user_id,
                             int64_t     id,
                             int64_t     category_id,
                             int64_t     asset_id,
                             const char* expense_type,
                             double      amount,
                             const char* currency,
                             const char* date,
                             const char* note)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    return de_update(
               pool, user_id, id, category_id, asset_id, expense_type, amount, currency, date, note)
               ? 0
               : -1;
}

int
mf_daily_expense_repo_delete(void* db_pool, int64_t user_id, int64_t id)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    return de_delete(pool, user_id, id) ? 0 : -1;
}

int
mf_daily_expense_repo_exists(void* db_pool, int64_t user_id, int64_t id)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid_str[32], id_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(id_str, sizeof(id_str), "%lld", (long long)id);

    csilk_json_t* res =
        csilk_db_query_param_json(pool,
                                  "SELECT id FROM daily_expenses WHERE id=? AND user_id=?",
                                  (const char*[]){id_str, uid_str, NULL});
    int ok = (res && csilk_json_array_size(res) > 0) ? 0 : 1;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

int
mf_daily_expense_repo_tag_insert(void* db_pool, int64_t expense_id, int64_t tag_id)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    return de_tag_insert(pool, expense_id, tag_id) ? 0 : -1;
}

int
mf_daily_expense_repo_tag_delete_all(void* db_pool, int64_t expense_id)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    return de_tag_delete_all(pool, expense_id) ? 0 : -1;
}

int64_t
mf_daily_expense_repo_get_or_create_tag(
    void* db_pool, int64_t user_id, int64_t tag_id, const char* tag_name, const char* tag_color)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    /* Try by ID first */
    if (tag_id > 0) {
        char tid_str[32];
        snprintf(tid_str, sizeof(tid_str), "%lld", (long long)tag_id);
        const char*   params[] = {tid_str, uid_str, NULL};
        csilk_json_t* chk =
            csilk_db_query_param_json(pool, "SELECT id FROM tags WHERE id=? AND user_id=?", params);
        if (chk && csilk_json_array_size(chk) > 0) {
            int64_t id = db_get_int(csilk_json_array_get(chk, 0), "id");
            csilk_json_free(chk);
            return id;
        }
        if (chk) {
            csilk_json_free(chk);
        }
    }

    /* Try by name */
    if (tag_name && tag_name[0]) {
        const char*   q_params[] = {uid_str, tag_name, NULL};
        csilk_json_t* q_res = csilk_db_query_param_json(
            pool, "SELECT id FROM tags WHERE user_id=? AND name=?", q_params);
        if (q_res && csilk_json_array_size(q_res) > 0) {
            int64_t existing_id = db_get_int(csilk_json_array_get(q_res, 0), "id");
            csilk_json_free(q_res);
            return existing_id;
        }
        if (q_res) {
            csilk_json_free(q_res);
        }

        const char*   color = tag_color && tag_color[0] ? tag_color : "#3b82f6";
        const char*   ins_params[] = {uid_str, tag_name, color, NULL};
        csilk_json_t* ins_res = csilk_db_query_param_json(
            pool,
            "INSERT INTO tags (user_id, name, color) VALUES (?, ?, ?) RETURNING id",
            ins_params);
        if (ins_res && csilk_json_array_size(ins_res) > 0) {
            int64_t new_id = db_get_int(csilk_json_array_get(ins_res, 0), "id");
            csilk_json_free(ins_res);
            return new_id;
        }
        if (ins_res) {
            csilk_json_free(ins_res);
        }
    }

    return 0;
}
