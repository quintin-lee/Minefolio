#include "infrastructure/repositories/category_repo_impl.h"
#include "common/db.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int
mf_category_repo_list(void*           db_pool,
                      int64_t         user_id,
                      int64_t         ledger_id,
                      const char*     type,
                      mf_category_t** out_list,
                      size_t*         out_count)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);

    const char* params[4] = {uid_str, lid_str, NULL, NULL};
    const char* sql = "SELECT c.id, c.name, c.parent_id, c.type, c.asset_type, c.currency, "
                      "c.icon, c.sort_order, c.created_at FROM categories c "
                      "WHERE c.user_id=? AND c.ledger_id=?";
    if (type && type[0]) {
        sql = "SELECT c.id, c.name, c.parent_id, c.type, c.asset_type, c.currency, "
              "c.icon, c.sort_order, c.created_at FROM categories c "
              "WHERE c.user_id=? AND c.ledger_id=? AND c.type=?";
        params[2] = type;
    }
    csilk_json_t* rows = csilk_db_query_param_json(pool, sql, params);
    if (!rows) {
        *out_list = NULL;
        *out_count = 0;
        return 0;
    }

    size_t         n = csilk_json_array_size(rows);
    mf_category_t* list = n > 0 ? malloc(sizeof(mf_category_t) * n) : NULL;
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* r = csilk_json_array_get(rows, i);
        list[i].id = db_get_int(r, "id");
        list[i].user_id = user_id;
        list[i].ledger_id = ledger_id;
        snprintf(list[i].name, sizeof(list[i].name), "%s", csilk_json_get_string(r, "name") ?: "");
        list[i].parent_id = db_get_int(r, "parent_id");
        snprintf(list[i].type, sizeof(list[i].type), "%s", csilk_json_get_string(r, "type") ?: "");
        snprintf(list[i].asset_type,
                 sizeof(list[i].asset_type),
                 "%s",
                 csilk_json_get_string(r, "asset_type") ?: "");
        snprintf(list[i].currency,
                 sizeof(list[i].currency),
                 "%s",
                 csilk_json_get_string(r, "currency") ?: "");
        snprintf(list[i].icon, sizeof(list[i].icon), "%s", csilk_json_get_string(r, "icon") ?: "");
        list[i].sort_order = (int)db_get_num(r, "sort_order");
        snprintf(list[i].created_at,
                 sizeof(list[i].created_at),
                 "%s",
                 csilk_json_get_string(r, "created_at") ?: "");
    }
    csilk_json_free(rows);
    *out_list = list;
    *out_count = n;
    return 0;
}

int
mf_category_repo_children(void*           db_pool,
                          int64_t         user_id,
                          int64_t         ledger_id,
                          int64_t         parent_id,
                          mf_category_t** out_list,
                          size_t*         out_count)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid_str[32], lid_str[32], pid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);
    snprintf(pid_str, sizeof(pid_str), "%lld", (long long)parent_id);
    csilk_json_t* rows = csilk_db_query_param_json(
        pool,
        "SELECT c.id, c.name, c.parent_id, c.type, c.asset_type, c.currency, c.icon, c.sort_order, "
        "c.created_at "
        "FROM categories c WHERE c.parent_id=? AND c.user_id=? AND c.ledger_id=? "
        "ORDER BY c.sort_order, c.name",
        (const char*[]){pid_str, uid_str, lid_str, NULL});
    if (!rows) {
        *out_list = NULL;
        *out_count = 0;
        return 0;
    }
    size_t         n = csilk_json_array_size(rows);
    mf_category_t* list = n > 0 ? malloc(sizeof(mf_category_t) * n) : NULL;
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* r = csilk_json_array_get(rows, i);
        list[i].id = db_get_int(r, "id");
        list[i].user_id = user_id;
        list[i].ledger_id = ledger_id;
        snprintf(list[i].name, sizeof(list[i].name), "%s", csilk_json_get_string(r, "name") ?: "");
        list[i].parent_id = db_get_int(r, "parent_id");
        snprintf(list[i].type, sizeof(list[i].type), "%s", csilk_json_get_string(r, "type") ?: "");
        snprintf(list[i].asset_type,
                 sizeof(list[i].asset_type),
                 "%s",
                 csilk_json_get_string(r, "asset_type") ?: "");
        snprintf(list[i].currency,
                 sizeof(list[i].currency),
                 "%s",
                 csilk_json_get_string(r, "currency") ?: "");
        snprintf(list[i].icon, sizeof(list[i].icon), "%s", csilk_json_get_string(r, "icon") ?: "");
        list[i].sort_order = (int)db_get_num(r, "sort_order");
        snprintf(list[i].created_at,
                 sizeof(list[i].created_at),
                 "%s",
                 csilk_json_get_string(r, "created_at") ?: "");
    }
    csilk_json_free(rows);
    *out_list = list;
    *out_count = n;
    return 0;
}

int
mf_category_repo_count_children(
    void* db_pool, int64_t user_id, int64_t ledger_id, int64_t parent_id, int64_t* out_count)
{
    char uid_str[32], lid_str[32], pid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);
    snprintf(pid_str, sizeof(pid_str), "%lld", (long long)parent_id);
    csilk_json_t* rows = csilk_db_query_param_json(
        (csilk_db_pool_t*)db_pool,
        "SELECT COUNT(*) AS cnt FROM categories WHERE parent_id=? AND user_id=? AND ledger_id=?",
        (const char*[]){pid_str, uid_str, lid_str, NULL});
    *out_count = (rows && csilk_json_array_size(rows) > 0)
                     ? (int64_t)db_get_num(csilk_json_array_get(rows, 0), "cnt")
                     : 0;
    if (rows) {
        csilk_json_free(rows);
    }
    return 0;
}

int
mf_category_repo_create(
    void* db_pool, int64_t user_id, int64_t ledger_id, const mf_category_t* cat, int64_t* out_id)
{
    char uid[32], lid[32], pid[32], sort[16];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
    snprintf(pid, sizeof(pid), "%lld", (long long)cat->parent_id);
    snprintf(sort, sizeof(sort), "%d", cat->sort_order);
    const char* params_parent[] = {
        uid, lid, cat->name, pid, cat->type, cat->asset_type, cat->currency, cat->icon, sort, NULL};
    const char* params_root[] = {
        uid, lid, cat->name, cat->type, cat->asset_type, cat->currency, cat->icon, sort, NULL};
    const char* sql =
        cat->parent_id > 0
            ? "INSERT INTO categories (user_id, ledger_id, name, parent_id, type, asset_type, "
              "currency, icon, sort_order) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) RETURNING id"
            : "INSERT INTO categories (user_id, ledger_id, name, type, asset_type, currency, icon, "
              "sort_order) VALUES (?, ?, ?, ?, ?, ?, ?, ?) RETURNING id";
    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)db_pool, sql, cat->parent_id > 0 ? params_parent : params_root);
    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return -1;
    }
    *out_id = db_get_int(csilk_json_array_get(res, 0), "id");
    csilk_json_free(res);
    return 0;
}

int
mf_category_repo_update(
    void* db_pool, int64_t user_id, int64_t ledger_id, int64_t id, const mf_category_t* cat)
{
    char uid[32], lid[32], ident[32], sort[16];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
    snprintf(ident, sizeof(ident), "%lld", (long long)id);
    snprintf(sort, sizeof(sort), "%d", cat->sort_order);
    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)db_pool,
        "UPDATE categories SET name=?, type=?, asset_type=?, currency=?, icon=?, sort_order=? "
        "WHERE id=? AND user_id=? AND ledger_id=? RETURNING id",
        (const char*[]){cat->name,
                        cat->type,
                        cat->asset_type,
                        cat->currency,
                        cat->icon,
                        sort,
                        ident,
                        uid,
                        lid,
                        NULL});
    int ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok ? 0 : 1;
}

int
mf_category_repo_delete(void* db_pool, int64_t user_id, int64_t ledger_id, int64_t id)
{
    char uid[32], lid[32], ident[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
    snprintf(ident, sizeof(ident), "%lld", (long long)id);
    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)db_pool,
        "DELETE FROM categories WHERE id=? AND user_id=? AND ledger_id=? RETURNING id",
        (const char*[]){ident, uid, lid, NULL});
    int ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok ? 0 : 1;
}

int64_t
mf_category_repo_find_or_create(void*       db_pool,
                                int64_t     user_id,
                                int64_t     ledger_id,
                                const char* name,
                                int64_t     parent_id,
                                const char* type,
                                const char* asset_type,
                                const char* icon,
                                int         sort_order)
{
    char uid[32], lid[32], pid[32], sort[16];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
    snprintf(pid, sizeof(pid), "%lld", (long long)parent_id);
    snprintf(sort, sizeof(sort), "%d", sort_order);
    csilk_json_t* chk =
        parent_id > 0 ? csilk_db_query_param_json((csilk_db_pool_t*)db_pool,
                                                  "SELECT id FROM categories WHERE user_id=? AND "
                                                  "ledger_id=? AND name=? AND parent_id=?",
                                                  (const char*[]){uid, lid, name, pid, NULL})
                      : csilk_db_query_param_json((csilk_db_pool_t*)db_pool,
                                                  "SELECT id FROM categories WHERE user_id=? AND "
                                                  "ledger_id=? AND name=? AND parent_id IS NULL",
                                                  (const char*[]){uid, lid, name, NULL});
    if (chk && csilk_json_array_size(chk) > 0) {
        int64_t id = db_get_int(csilk_json_array_get(chk, 0), "id");
        csilk_json_free(chk);
        return id;
    }
    if (chk) {
        csilk_json_free(chk);
    }
    const char*   at = asset_type && asset_type[0] ? asset_type : "cash";
    const char*   ic = icon && icon[0] ? icon : "";
    csilk_json_t* res =
        parent_id > 0
            ? csilk_db_query_param_json(
                  (csilk_db_pool_t*)db_pool,
                  "INSERT INTO categories (user_id, ledger_id, name, parent_id, type, asset_type, "
                  "currency, icon, sort_order) VALUES (?, ?, ?, ?, ?, ?, 'CNY', ?, ?) RETURNING id",
                  (const char*[]){uid, lid, name, pid, type, at, ic, sort, NULL})
            : csilk_db_query_param_json(
                  (csilk_db_pool_t*)db_pool,
                  "INSERT INTO categories (user_id, ledger_id, name, type, asset_type, currency, "
                  "icon, sort_order) VALUES (?, ?, ?, ?, ?, 'CNY', ?, ?) RETURNING id",
                  (const char*[]){uid, lid, name, type, at, ic, sort, NULL});
    int64_t id = (res && csilk_json_array_size(res) > 0)
                     ? db_get_int(csilk_json_array_get(res, 0), "id")
                     : 0;
    if (res) {
        csilk_json_free(res);
    }
    return id;
}

int
mf_category_repo_exists(void* db_pool, int64_t user_id, int64_t ledger_id, int64_t id)
{
    char uid[32], lid[32], ident[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
    snprintf(ident, sizeof(ident), "%lld", (long long)id);
    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)db_pool,
        "SELECT id FROM categories WHERE id=? AND user_id=? AND ledger_id=?",
        (const char*[]){ident, uid, lid, NULL});
    int ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

int
mf_category_repo_is_seeded(void* db_pool, int64_t user_id, int64_t ledger_id)
{
    char uid[32], lid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)db_pool,
        "SELECT 1 FROM category_seed_state WHERE user_id=? AND ledger_id=?",
        (const char*[]){uid, lid, NULL});
    int ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

void
mf_category_repo_mark_seeded(void* db_pool, int64_t user_id, int64_t ledger_id)
{
    char uid[32], lid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)db_pool,
        "INSERT OR IGNORE INTO category_seed_state (user_id, ledger_id) VALUES (?, ?)",
        (const char*[]){uid, lid, NULL});
    if (res) {
        csilk_json_free(res);
    }
}

void
mf_category_repo_free_list(mf_category_t* list, size_t count)
{
    (void)count;
    free(list);
}
