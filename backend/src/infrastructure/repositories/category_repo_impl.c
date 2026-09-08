#include "infrastructure/repositories/category_repo_impl.h"
#include "common/db.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int
mf_category_repo_list(
    void* db_pool, int64_t user_id, const char* type, mf_category_t** out_list, size_t* out_count)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    csilk_json_t* rows = NULL;
    if (type && type[0]) {
        rows = csilk_db_query_param_json(
            pool,
            "SELECT c.id, c.name, c.parent_id, c.type, c.asset_type, c.currency, "
            "c.icon, c.sort_order, c.created_at "
            "FROM categories c WHERE c.user_id=? AND c.type=? "
            "ORDER BY c.parent_id, c.sort_order, c.name",
            (const char*[]){uid_str, type, NULL});
    } else {
        rows = csilk_db_query_param_json(
            pool,
            "SELECT c.id, c.name, c.parent_id, c.type, c.asset_type, c.currency, "
            "c.icon, c.sort_order, c.created_at "
            "FROM categories c WHERE c.user_id=? "
            "ORDER BY c.parent_id, c.sort_order, c.name",
            (const char*[]){uid_str, NULL});
    }

    if (!rows) {
        *out_list = NULL;
        *out_count = 0;
        return 0;
    }

    size_t         n = csilk_json_array_size(rows);
    mf_category_t* list = n > 0 ? (mf_category_t*)malloc(sizeof(mf_category_t) * n) : NULL;
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* r = csilk_json_array_get(rows, i);
        list[i].id = db_get_int(r, "id");
        list[i].user_id = user_id;
        strncpy(list[i].name, csilk_json_get_string(r, "name") ?: "", sizeof(list[i].name) - 1);
        list[i].parent_id = db_get_int(r, "parent_id");
        strncpy(list[i].type, csilk_json_get_string(r, "type") ?: "", sizeof(list[i].type) - 1);
        strncpy(list[i].asset_type,
                csilk_json_get_string(r, "asset_type") ?: "",
                sizeof(list[i].asset_type) - 1);
        strncpy(list[i].currency,
                csilk_json_get_string(r, "currency") ?: "",
                sizeof(list[i].currency) - 1);
        strncpy(list[i].icon, csilk_json_get_string(r, "icon") ?: "", sizeof(list[i].icon) - 1);
        list[i].sort_order = (int)db_get_num(r, "sort_order");
        strncpy(list[i].created_at,
                csilk_json_get_string(r, "created_at") ?: "",
                sizeof(list[i].created_at) - 1);
    }
    csilk_json_free(rows);
    *out_list = list;
    *out_count = n;
    return 0;
}

int
mf_category_repo_children(
    void* db_pool, int64_t user_id, int64_t parent_id, mf_category_t** out_list, size_t* out_count)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid_str[32], pid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(pid_str, sizeof(pid_str), "%lld", (long long)parent_id);

    csilk_json_t* rows = csilk_db_query_param_json(
        pool,
        "SELECT c.id, c.name, c.parent_id, c.type, c.asset_type, c.currency, "
        "c.icon, c.sort_order, c.created_at "
        "FROM categories c WHERE c.parent_id=? AND c.user_id=? "
        "ORDER BY c.sort_order, c.name",
        (const char*[]){pid_str, uid_str, NULL});

    if (!rows) {
        *out_list = NULL;
        *out_count = 0;
        return 0;
    }

    size_t         n = csilk_json_array_size(rows);
    mf_category_t* list = n > 0 ? (mf_category_t*)malloc(sizeof(mf_category_t) * n) : NULL;
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* r = csilk_json_array_get(rows, i);
        list[i].id = db_get_int(r, "id");
        list[i].user_id = user_id;
        strncpy(list[i].name, csilk_json_get_string(r, "name") ?: "", sizeof(list[i].name) - 1);
        list[i].parent_id = db_get_int(r, "parent_id");
        strncpy(list[i].type, csilk_json_get_string(r, "type") ?: "", sizeof(list[i].type) - 1);
        strncpy(list[i].asset_type,
                csilk_json_get_string(r, "asset_type") ?: "",
                sizeof(list[i].asset_type) - 1);
        strncpy(list[i].currency,
                csilk_json_get_string(r, "currency") ?: "",
                sizeof(list[i].currency) - 1);
        strncpy(list[i].icon, csilk_json_get_string(r, "icon") ?: "", sizeof(list[i].icon) - 1);
        list[i].sort_order = (int)db_get_num(r, "sort_order");
        strncpy(list[i].created_at,
                csilk_json_get_string(r, "created_at") ?: "",
                sizeof(list[i].created_at) - 1);
    }
    csilk_json_free(rows);
    *out_list = list;
    *out_count = n;
    return 0;
}

int
mf_category_repo_count_children(void*    db_pool,
                                int64_t  user_id,
                                int64_t  parent_id,
                                int64_t* out_count)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid_str[32], pid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(pid_str, sizeof(pid_str), "%lld", (long long)parent_id);

    csilk_json_t* rows = csilk_db_query_param_json(
        pool,
        "SELECT COUNT(*) as cnt FROM categories WHERE parent_id=? AND user_id=?",
        (const char*[]){pid_str, uid_str, NULL});

    if (!rows || csilk_json_array_size(rows) == 0) {
        if (rows) {
            csilk_json_free(rows);
        }
        *out_count = 0;
        return 0;
    }
    *out_count = (int64_t)db_get_num(csilk_json_array_get(rows, 0), "cnt");
    csilk_json_free(rows);
    return 0;
}

int
mf_category_repo_create(void* db_pool, int64_t user_id, const mf_category_t* cat, int64_t* out_id)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid_str[32], pid_str[32], sort_str[16];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(pid_str, sizeof(pid_str), "%lld", (long long)cat->parent_id);
    snprintf(sort_str, sizeof(sort_str), "%d", cat->sort_order);

    const char* params[] = {uid_str,
                            cat->name,
                            cat->parent_id > 0 ? pid_str : "NULL",
                            cat->type,
                            cat->asset_type,
                            cat->currency,
                            cat->icon,
                            sort_str,
                            NULL};

    csilk_json_t* res = NULL;
    if (cat->parent_id > 0) {
        res = csilk_db_query_param_json(pool,
                                        "INSERT INTO categories (user_id, name, parent_id, type, "
                                        "asset_type, currency, icon, sort_order) "
                                        "VALUES (?, ?, ?, ?, ?, ?, ?, ?) RETURNING id",
                                        params);
    } else {
        res = csilk_db_query_param_json(
            pool,
            "INSERT INTO categories (user_id, name, type, asset_type, currency, icon, sort_order) "
            "VALUES (?, ?, ?, ?, ?, ?, ?) RETURNING id",
            (const char*[]){uid_str,
                            cat->name,
                            cat->type,
                            cat->asset_type,
                            cat->currency,
                            cat->icon,
                            sort_str,
                            NULL});
    }

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
mf_category_repo_update(void* db_pool, int64_t user_id, int64_t id, const mf_category_t* cat)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid_str[32], id_str[32], sort_str[16];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(id_str, sizeof(id_str), "%lld", (long long)id);
    snprintf(sort_str, sizeof(sort_str), "%d", cat->sort_order);

    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "UPDATE categories SET name=?, type=?, asset_type=?, currency=?, icon=?, sort_order=? "
        "WHERE id=? AND user_id=? RETURNING id",
        (const char*[]){cat->name,
                        cat->type,
                        cat->asset_type,
                        cat->currency,
                        cat->icon,
                        sort_str,
                        id_str,
                        uid_str,
                        NULL});

    int ok = (res && csilk_json_array_size(res) > 0) ? 0 : 1;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

int
mf_category_repo_delete(void* db_pool, int64_t user_id, int64_t id)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid_str[32], id_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(id_str, sizeof(id_str), "%lld", (long long)id);

    csilk_json_t* res =
        csilk_db_query_param_json(pool,
                                  "DELETE FROM categories WHERE id=? AND user_id=? RETURNING id",
                                  (const char*[]){id_str, uid_str, NULL});

    int ok = (res && csilk_json_array_size(res) > 0) ? 0 : 1;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

int64_t
mf_category_repo_find_or_create(void*       db_pool,
                                int64_t     user_id,
                                const char* name,
                                int64_t     parent_id,
                                const char* type,
                                const char* asset_type,
                                const char* icon,
                                int         sort_order)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid_str[32], pid_str[32], sort_str[16];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(pid_str, sizeof(pid_str), "%lld", (long long)parent_id);
    snprintf(sort_str, sizeof(sort_str), "%d", sort_order);

    /* Check if exists */
    csilk_json_t* chk = NULL;
    if (parent_id > 0) {
        chk = csilk_db_query_param_json(
            pool,
            "SELECT id FROM categories WHERE user_id=? AND name=? AND parent_id=?",
            (const char*[]){uid_str, name, pid_str, NULL});
    } else {
        chk = csilk_db_query_param_json(
            pool,
            "SELECT id FROM categories WHERE user_id=? AND name=? AND parent_id IS NULL",
            (const char*[]){uid_str, name, NULL});
    }
    if (chk && csilk_json_array_size(chk) > 0) {
        int64_t id = db_get_int(csilk_json_array_get(chk, 0), "id");
        csilk_json_free(chk);
        return id;
    }
    if (chk) {
        csilk_json_free(chk);
    }

    /* Create new */
    const char* ic = icon && icon[0] ? icon : "";
    const char* at = asset_type && asset_type[0] ? asset_type : "cash";

    csilk_json_t* res = NULL;
    if (parent_id > 0) {
        res = csilk_db_query_param_json(
            pool,
            "INSERT INTO categories (user_id, name, parent_id, type, asset_type, currency, icon, "
            "sort_order) "
            "VALUES (?, ?, ?, ?, ?, 'CNY', ?, ?) RETURNING id",
            (const char*[]){uid_str, name, pid_str, type, at, ic, sort_str, NULL});
    } else {
        res = csilk_db_query_param_json(
            pool,
            "INSERT INTO categories (user_id, name, type, asset_type, currency, icon, sort_order) "
            "VALUES (?, ?, ?, ?, 'CNY', ?, ?) RETURNING id",
            (const char*[]){uid_str, name, type, at, ic, sort_str, NULL});
    }

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
mf_category_repo_exists(void* db_pool, int64_t user_id, int64_t id)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid_str[32], id_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(id_str, sizeof(id_str), "%lld", (long long)id);

    csilk_json_t* res =
        csilk_db_query_param_json(pool,
                                  "SELECT id FROM categories WHERE id=? AND user_id=?",
                                  (const char*[]){id_str, uid_str, NULL});

    int ok = (res && csilk_json_array_size(res) > 0) ? 1 : 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

int
mf_category_repo_is_seeded(void* db_pool, int64_t user_id)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    csilk_json_t* res = csilk_db_query_param_json(
        pool, "SELECT 1 FROM category_seed_state WHERE user_id=?", (const char*[]){uid_str, NULL});

    int seeded = (res && csilk_json_array_size(res) > 0) ? 1 : 0;
    if (res) {
        csilk_json_free(res);
    }
    return seeded;
}

void
mf_category_repo_mark_seeded(void* db_pool, int64_t user_id)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    csilk_json_t* res =
        csilk_db_query_param_json(pool,
                                  "INSERT OR IGNORE INTO category_seed_state (user_id) VALUES (?)",
                                  (const char*[]){uid_str, NULL});
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
