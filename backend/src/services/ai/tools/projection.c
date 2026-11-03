#include "services/ai/tools/projection.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static void
copy_json_field(csilk_json_t*       dst,
                const csilk_json_t* src,
                const char*         src_key,
                const char*         dst_key)
{
    if (!dst || !src || !src_key) {
        return;
    }
    csilk_json_t* v = csilk_json_get(src, src_key);
    if (!v) {
        return;
    }
    const char* key = dst_key ? dst_key : src_key;
    if (csilk_json_is_string(v)) {
        csilk_json_add_string(dst, key, csilk_json_string_value(v));
    } else if (csilk_json_is_number(v)) {
        csilk_json_add_number(dst, key, csilk_json_number_value(v));
    } else if (csilk_json_is_bool(v)) {
        csilk_json_add_bool(dst, key, csilk_json_bool_value(v));
    } else if (csilk_json_is_null(v)) {
        csilk_json_add_null(dst, key);
    } else if (csilk_json_is_object(v)) {
        csilk_json_add_object(dst, key, csilk_json_copy(v));
    } else if (csilk_json_is_array(v)) {
        csilk_json_add_array(dst, key, csilk_json_copy(v));
    }
}

static void
copy_pagination_metadata(csilk_json_t* dst, const csilk_json_t* src)
{
    copy_json_field(dst, src, "total", "total");
    copy_json_field(dst, src, "page", "page");
    copy_json_field(dst, src, "page_size", "page_size");
}

static csilk_json_t*
project_asset_item(const csilk_json_t* item)
{
    csilk_json_t* out = csilk_json_object();
    copy_json_field(out, item, "id", "id");
    copy_json_field(out, item, "name", "name");
    copy_json_field(out, item, "asset_type", "asset_type");
    copy_json_field(out, item, "currency", "currency");
    copy_json_field(out, item, "balance", "balance");
    copy_json_field(out, item, "current_value", "current_value");
    copy_json_field(out, item, "cost_basis", "cost_basis");
    return out;
}

static csilk_json_t*
project_expense_item(const csilk_json_t* item)
{
    csilk_json_t* out = csilk_json_object();
    copy_json_field(out, item, "id", "id");
    if (csilk_json_get(item, "date")) {
        copy_json_field(out, item, "date", "date");
    } else if (csilk_json_get(item, "expense_date")) {
        copy_json_field(out, item, "expense_date", "date");
    }
    copy_json_field(out, item, "amount", "amount");
    copy_json_field(out, item, "category_name", "category_name");
    copy_json_field(out, item, "asset_name", "asset_name");
    if (csilk_json_get(item, "type")) {
        copy_json_field(out, item, "type", "type");
    } else if (csilk_json_get(item, "expense_type")) {
        copy_json_field(out, item, "expense_type", "type");
    }
    copy_json_field(out, item, "note", "note");
    return out;
}

static csilk_json_t*
project_transaction_item(const csilk_json_t* item)
{
    csilk_json_t* out = csilk_json_object();
    copy_json_field(out, item, "id", "id");
    if (csilk_json_get(item, "transaction_date")) {
        copy_json_field(out, item, "transaction_date", "transaction_date");
    } else if (csilk_json_get(item, "date")) {
        copy_json_field(out, item, "date", "transaction_date");
    }
    if (csilk_json_get(item, "transaction_type")) {
        copy_json_field(out, item, "transaction_type", "transaction_type");
    } else if (csilk_json_get(item, "type")) {
        copy_json_field(out, item, "type", "transaction_type");
    }
    copy_json_field(out, item, "amount", "amount");
    copy_json_field(out, item, "category_name", "category_name");
    copy_json_field(out, item, "asset_name", "asset_name");
    copy_json_field(out, item, "note", "note");
    return out;
}

char*
ai_project_tool_result(const char* tool_name, const char* raw_json)
{
    if (!raw_json) {
        return NULL;
    }
    if (!tool_name || strstr(raw_json, "\"error\":") != NULL) {
        return strdup(raw_json);
    }

    bool is_assets = (strcmp(tool_name, "get_assets") == 0);
    bool is_expenses = (strcmp(tool_name, "get_daily_expenses") == 0);
    bool is_transactions = (strcmp(tool_name, "get_transactions") == 0);

    if (!is_assets && !is_expenses && !is_transactions) {
        return strdup(raw_json);
    }

    csilk_json_t* parsed = csilk_json_parse(raw_json);
    if (!parsed) {
        return strdup(raw_json);
    }

    csilk_json_t* projected_root = NULL;

    if (csilk_json_is_array(parsed)) {
        /* Direct array root */
        projected_root = csilk_json_array();
        size_t total_items = csilk_json_array_size(parsed);
        size_t max_items = (is_expenses || is_transactions) ? 15 : total_items;
        size_t limit = total_items < max_items ? total_items : max_items;

        for (size_t i = 0; i < limit; i++) {
            const csilk_json_t* elem = csilk_json_array_get(parsed, i);
            if (!elem) {
                continue;
            }
            csilk_json_t* proj_item = NULL;
            if (is_assets) {
                proj_item = project_asset_item(elem);
            } else if (is_expenses) {
                proj_item = project_expense_item(elem);
            } else {
                proj_item = project_transaction_item(elem);
            }
            if (proj_item) {
                csilk_json_array_append(projected_root, proj_item);
            }
        }
    } else if (csilk_json_is_object(parsed)) {
        projected_root = csilk_json_object();
        copy_pagination_metadata(projected_root, parsed);

        const char*         array_key = NULL;
        const csilk_json_t* src_arr = NULL;

        if (is_assets) {
            array_key = "assets";
            src_arr = csilk_json_get(parsed, "assets");
        } else if (is_expenses) {
            array_key = "expenses";
            src_arr = csilk_json_get(parsed, "expenses");
        } else {
            array_key = "transactions";
            src_arr = csilk_json_get(parsed, "transactions");
        }

        if (!src_arr || !csilk_json_is_array(src_arr)) {
            /* Fallback to common array keys */
            const char* fallback_keys[] = {"list", "items", "data"};
            for (size_t k = 0; k < sizeof(fallback_keys) / sizeof(fallback_keys[0]); k++) {
                const csilk_json_t* cand = csilk_json_get(parsed, fallback_keys[k]);
                if (cand && csilk_json_is_array(cand)) {
                    src_arr = cand;
                    break;
                }
            }
        }

        csilk_json_t* out_arr = csilk_json_array();
        if (src_arr && csilk_json_is_array(src_arr)) {
            size_t total_items = csilk_json_array_size(src_arr);
            size_t max_items = (is_expenses || is_transactions) ? 15 : total_items;
            size_t limit = total_items < max_items ? total_items : max_items;

            for (size_t i = 0; i < limit; i++) {
                const csilk_json_t* elem = csilk_json_array_get(src_arr, i);
                if (!elem) {
                    continue;
                }
                csilk_json_t* proj_item = NULL;
                if (is_assets) {
                    proj_item = project_asset_item(elem);
                } else if (is_expenses) {
                    proj_item = project_expense_item(elem);
                } else {
                    proj_item = project_transaction_item(elem);
                }
                if (proj_item) {
                    csilk_json_array_append(out_arr, proj_item);
                }
            }
        }
        csilk_json_add_array(projected_root, array_key, out_arr);
    } else {
        /* Primitive root: return copy of raw_json */
        csilk_json_free(parsed);
        return strdup(raw_json);
    }

    size_t out_len = 0;
    char*  result_str = csilk_json_serialize(projected_root, &out_len);
    csilk_json_free(projected_root);
    csilk_json_free(parsed);

    return result_str ? result_str : strdup(raw_json);
}
