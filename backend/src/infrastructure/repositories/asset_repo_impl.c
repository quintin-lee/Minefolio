#include "infrastructure/repositories/asset_repo_impl.h"
#include "repositories/asset_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
mf_asset_repo_find_by_id(void* db_pool, int64_t user_id, int64_t id, mf_asset_t* out_asset)
{
    if (!out_asset || !db_pool) {
        return -1;
    }
    memset(out_asset, 0, sizeof(*out_asset));

    csilk_json_t* res = asset_get((csilk_db_pool_t*)db_pool, user_id, id);
    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return 1; /* 不存在 */
    }

    const csilk_json_t* row = csilk_json_array_get(res, 0);
    out_asset->id = db_get_int(row, "id");
    out_asset->user_id = user_id;
    out_asset->category_id = db_get_int(row, "category_id");

    const char* name = csilk_json_get_string(row, "name");
    if (name) {
        snprintf(out_asset->name, sizeof(out_asset->name), "%s", name);
    }

    const char* acc_no = csilk_json_get_string(row, "account_no");
    if (acc_no) {
        snprintf(out_asset->account_no, sizeof(out_asset->account_no), "%s", acc_no);
    }

    const char* atype = csilk_json_get_string(row, "asset_type");
    if (atype) {
        snprintf(out_asset->asset_type, sizeof(out_asset->asset_type), "%s", atype);
    }

    const char* cur = csilk_json_get_string(row, "currency");
    if (!cur) {
        cur = "CNY";
    }
    out_asset->currency = currency_from_str(cur);

    const char* note = csilk_json_get_string(row, "note");
    if (note) {
        snprintf(out_asset->note, sizeof(out_asset->note), "%s", note);
    }

    const char* sym = csilk_json_get_string(row, "symbol");
    if (sym) {
        snprintf(out_asset->symbol, sizeof(out_asset->symbol), "%s", sym);
    }

    const char* qs = csilk_json_get_string(row, "quote_source");
    if (qs) {
        snprintf(out_asset->quote_source, sizeof(out_asset->quote_source), "%s", qs);
    }

    out_asset->current_value = db_get_money(row, "current_value", out_asset->currency);
    out_asset->quantity = db_get_quantity(row, "quantity");
    out_asset->cost_basis = db_get_money(row, "cost_basis", out_asset->currency);
    out_asset->net_value = db_get_price(row, "net_value", out_asset->currency);

    const char* cat = csilk_json_get_string(row, "created_at");
    if (cat) {
        snprintf(out_asset->created_at, sizeof(out_asset->created_at), "%s", cat);
    }

    const char* uat = csilk_json_get_string(row, "updated_at");
    if (uat) {
        snprintf(out_asset->updated_at, sizeof(out_asset->updated_at), "%s", uat);
    }

    csilk_json_free(res);
    return 0;
}

int
mf_asset_repo_save(void* db_pool, const mf_asset_t* asset, int64_t* out_id)
{
    if (!asset || !db_pool) {
        return -1;
    }
    const char* cur_str = currency_code(&asset->currency);

    int64_t id = asset_insert((csilk_db_pool_t*)db_pool,
                              asset->user_id,
                              asset->category_id,
                              asset->name,
                              asset->account_no,
                              money_to_double(asset->current_value),
                              cur_str,
                              asset->note,
                              quantity_to_double(asset->quantity),
                              money_to_double(asset->cost_basis),
                              price_to_double(asset->net_value),
                              asset->symbol,
                              asset->quote_source);
    if (id <= 0) {
        return -1;
    }
    if (out_id) {
        *out_id = id;
    }
    return 0;
}

int
mf_asset_repo_update_basic(void* db_pool, const mf_asset_t* asset)
{
    if (!asset || !db_pool) {
        return -1;
    }
    const char* cur_str = currency_code(&asset->currency);

    return asset_update_basic((csilk_db_pool_t*)db_pool,
                              asset->user_id,
                              asset->id,
                              asset->name,
                              asset->account_no,
                              money_to_double(asset->current_value),
                              cur_str,
                              asset->note,
                              asset->symbol,
                              asset->quote_source)
               ? 0
               : -1;
}

int
mf_asset_repo_update_position(void*      db_pool,
                              int64_t    user_id,
                              int64_t    id,
                              price_t    net_value,
                              quantity_t quantity,
                              money_t    cost_basis)
{
    if (!db_pool) {
        return -1;
    }
    return asset_update_position((csilk_db_pool_t*)db_pool,
                                 user_id,
                                 id,
                                 price_to_double(net_value),
                                 quantity_to_double(quantity),
                                 money_to_double(cost_basis))
               ? 0
               : -1;
}

int
mf_asset_repo_delete(void* db_pool, int64_t user_id, int64_t id)
{
    if (!db_pool) {
        return -1;
    }
    return asset_delete((csilk_db_pool_t*)db_pool, user_id, id) ? 0 : -1;
}

int
mf_asset_repo_exists(void* db_pool, int64_t user_id, int64_t id)
{
    if (!db_pool) {
        return 0;
    }
    return asset_exists((csilk_db_pool_t*)db_pool, user_id, id);
}

int
mf_asset_repo_get_category_type(
    void* db_pool, int64_t user_id, int64_t category_id, char* out_type, size_t out_cap)
{
    if (!db_pool || user_id <= 0 || category_id <= 0 || !out_type || out_cap == 0) {
        return -1;
    }
    char* atype = asset_get_category_type((csilk_db_pool_t*)db_pool, user_id, category_id);
    if (!atype) {
        out_type[0] = '\0';
        return 1;
    }
    snprintf(out_type, out_cap, "%s", atype);
    free(atype);
    return 0;
}
