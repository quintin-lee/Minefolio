#include "infrastructure/repositories/account_repo_impl.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* PG 布尔列统一经 CAST(archived AS INTEGER) 读取；写参用 '0'/'1' 文本双方言通用 */
static const char*
account_archived_literal(int archived)
{
    return archived ? "1" : "0";
}

csilk_json_t*
account_list(csilk_db_pool_t* pool,
             int64_t          user_id,
             int64_t          ledger_id,
             bool             include_archived,
             int64_t*         total)
{
    char uid[32], lid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);

    char sql[1024];
    snprintf(
        sql,
        sizeof(sql),
        "SELECT ac.id, ac.name, ac.institution, ac.account_no, ac.currency, ac.icon, ac.note, "
        "ac.sort_order, CAST(ac.archived AS INTEGER) AS archived, ac.created_at, ac.updated_at, "
        "(SELECT COUNT(*) FROM assets a WHERE a.account_id = ac.id) AS asset_count, "
        "(SELECT COALESCE(SUM(a.current_value), 0) FROM assets a WHERE a.account_id = ac.id) AS "
        "total_value FROM accounts ac WHERE ac.user_id=? AND ac.ledger_id=? "
        "%s ORDER BY ac.sort_order, ac.id",
        include_archived ? "" : "AND ac.archived=0 ");

    const char*   params[3] = {uid, lid, NULL};
    csilk_json_t* rows = csilk_db_query_param_json(pool, sql, params);
    if (total) {
        *total = rows ? (int64_t)csilk_json_array_size(rows) : 0;
    }
    return rows;
}

csilk_json_t*
account_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    return csilk_db_query_param_json(
        pool,
        "SELECT ac.id, ac.ledger_id, ac.name, ac.institution, ac.account_no, ac.currency, ac.icon, "
        "ac.note, ac.sort_order, CAST(ac.archived AS INTEGER) AS archived, ac.created_at, "
        "ac.updated_at FROM accounts ac WHERE ac.id=? AND ac.user_id=?",
        (const char*[]){idstr, uid, NULL});
}

int64_t
account_insert(csilk_db_pool_t* pool,
               int64_t          user_id,
               int64_t          ledger_id,
               const char*      name,
               const char*      institution,
               const char*      account_no,
               const char*      currency,
               const char*      icon,
               const char*      note,
               int64_t          sort_order)
{
    char uid[32], lid[32], sort[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
    snprintf(sort, sizeof(sort), "%lld", (long long)sort_order);

    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "INSERT INTO accounts "
        "(user_id, ledger_id, name, institution, account_no, currency, icon, note, sort_order) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) RETURNING id",
        (const char*[]){uid,
                        lid,
                        name ? name : "",
                        institution ? institution : "other",
                        account_no ? account_no : "",
                        currency ? currency : "CNY",
                        icon ? icon : "",
                        note ? note : "",
                        sort,
                        NULL});
    int64_t id = 0;
    if (res && csilk_json_array_size(res) > 0) {
        id = db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) {
        csilk_json_free(res);
    }
    return id;
}

int
account_update(csilk_db_pool_t* pool,
               int64_t          user_id,
               int64_t          id,
               const char*      name,
               const char*      institution,
               const char*      account_no,
               const char*      currency,
               const char*      icon,
               const char*      note,
               int64_t          sort_order,
               int              has_sort_order,
               int              has_archived,
               int              archived)
{
    char uid[32], idstr[32], sort[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    snprintf(sort, sizeof(sort), "%lld", (long long)sort_order);

    char sql[1024];
    snprintf(sql,
             sizeof(sql),
             "UPDATE accounts SET name=?, institution=?, account_no=?, currency=?, icon=?, note=?, "
             "updated_at=CURRENT_TIMESTAMP%s%s WHERE id=? AND user_id=? RETURNING id",
             has_sort_order ? ", sort_order=?" : "",
             has_archived ? ", archived=?" : "");

    const char* params[12];
    int         pidx = 0;
    params[pidx++] = name ? name : "";
    params[pidx++] = institution ? institution : "other";
    params[pidx++] = account_no ? account_no : "";
    params[pidx++] = currency ? currency : "CNY";
    params[pidx++] = icon ? icon : "";
    params[pidx++] = note ? note : "";
    if (has_sort_order) {
        params[pidx++] = sort;
    }
    if (has_archived) {
        params[pidx++] = account_archived_literal(archived);
    }
    params[pidx++] = idstr;
    params[pidx++] = uid;
    params[pidx] = NULL;

    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    int           ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok ? 0 : 1;
}

int
account_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    csilk_json_t* res =
        csilk_db_query_param_json(pool,
                                  "DELETE FROM accounts WHERE id=? AND user_id=? RETURNING id",
                                  (const char*[]){idstr, uid, NULL});
    int ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok ? 0 : 1;
}

int
account_count_assets(csilk_db_pool_t* pool, int64_t user_id, int64_t account_id, int64_t* out_count)
{
    if (!pool || account_id <= 0 || !out_count) {
        return -1;
    }
    char uid[32], aid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(aid, sizeof(aid), "%lld", (long long)account_id);
    csilk_json_t* res =
        csilk_db_query_param_json(pool,
                                  "SELECT COUNT(*) AS cnt FROM assets WHERE account_id=? AND "
                                  "user_id=?",
                                  (const char*[]){aid, uid, NULL});
    *out_count = 0;
    if (res && csilk_json_array_size(res) > 0) {
        *out_count = db_get_int(csilk_json_array_get(res, 0), "cnt");
    }
    if (res) {
        csilk_json_free(res);
    }
    return 0;
}

int
account_has_transactions(csilk_db_pool_t* pool, int64_t user_id, int64_t account_id, int* out_flag)
{
    if (!pool || account_id <= 0 || !out_flag) {
        return -1;
    }
    char uid[32], aid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(aid, sizeof(aid), "%lld", (long long)account_id);
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "SELECT 1 AS hit FROM transactions "
        "WHERE user_id=? AND (asset_id IN (SELECT id FROM assets WHERE account_id=?) OR "
        "linked_asset_id IN (SELECT id FROM assets WHERE account_id=?)) LIMIT 1",
        (const char*[]){uid, aid, aid, NULL});
    *out_flag = (res && csilk_json_array_size(res) > 0) ? 1 : 0;
    if (res) {
        csilk_json_free(res);
    }
    return 0;
}

int64_t
account_find_default(csilk_db_pool_t* pool, int64_t user_id, int64_t ledger_id)
{
    char uid[32], lid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
    csilk_json_t* res =
        csilk_db_query_param_json(pool,
                                  "SELECT id FROM accounts WHERE user_id=? AND ledger_id=? AND "
                                  "name='默认账户' ORDER BY id LIMIT 1",
                                  (const char*[]){uid, lid, NULL});
    int64_t id = 0;
    if (res && csilk_json_array_size(res) > 0) {
        id = db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) {
        csilk_json_free(res);
    }
    return id;
}

int64_t
account_create_default(csilk_db_pool_t* pool, int64_t user_id, int64_t ledger_id)
{
    int64_t id = account_find_default(pool, user_id, ledger_id);
    if (id > 0) {
        return id;
    }
    /* 撞 UNIQUE(user_id, ledger_id, name) 时幂等返回既有 id */
    id = account_insert(
        pool, user_id, ledger_id, "默认账户", "cash_wallet", NULL, "CNY", "ph:wallet", NULL, 0);
    if (id <= 0) {
        id = account_find_default(pool, user_id, ledger_id);
    }
    return id;
}
