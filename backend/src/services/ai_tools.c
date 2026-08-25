#include "ai_tools.h"
#include "repositories/asset_repo.h"
#include "repositories/transaction_repo.h"
#include "repositories/daily_expense_repo.h"
#include "repositories/category_repo.h"
#include "common/db.h"
#include "common/ctx.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static csilk_json_t* schema_get_assets(void) {
    csilk_json_t* props = csilk_json_object();
    csilk_json_add_string(props, "page", "Page number, default 1");
    csilk_json_add_string(props, "page_size", "Items per page, default 50");
    csilk_json_t* s = csilk_json_object();
    csilk_json_add_string(s, "type", "object");
    csilk_json_add_array(s, "required", csilk_json_array());
    csilk_json_add_object(s, "properties", props);
    return s;
}

static csilk_json_t* schema_get_transactions(void) {
    csilk_json_t* props = csilk_json_object();
    csilk_json_add_string(props, "page", "Page number, default 1");
    csilk_json_add_string(props, "page_size", "Items per page, default 50");
    csilk_json_add_string(props, "start_date", "Filter start date (YYYY-MM-DD)");
    csilk_json_add_string(props, "end_date", "Filter end date (YYYY-MM-DD)");
    csilk_json_add_string(props, "type", "Transaction type filter");
    csilk_json_t* s = csilk_json_object();
    csilk_json_add_string(s, "type", "object");
    csilk_json_add_array(s, "required", csilk_json_array());
    csilk_json_add_object(s, "properties", props);
    return s;
}

static csilk_json_t* schema_get_daily_expenses(void) {
    csilk_json_t* props = csilk_json_object();
    csilk_json_add_string(props, "page", "Page number, default 1");
    csilk_json_add_string(props, "page_size", "Items per page, default 50");
    csilk_json_add_string(props, "start_date", "Filter start date (YYYY-MM-DD)");
    csilk_json_add_string(props, "end_date", "Filter end date (YYYY-MM-DD)");
    csilk_json_add_string(props, "expense_type", "Expense type filter");
    csilk_json_t* s = csilk_json_object();
    csilk_json_add_string(s, "type", "object");
    csilk_json_add_array(s, "required", csilk_json_array());
    csilk_json_add_object(s, "properties", props);
    return s;
}

static csilk_json_t* schema_get_categories(void) {
    csilk_json_t* props = csilk_json_object();
    csilk_json_add_string(props, "type", "Category type: asset, income, expense, transaction. Empty for all.");
    csilk_json_t* s = csilk_json_object();
    csilk_json_add_string(s, "type", "object");
    csilk_json_add_array(s, "required", csilk_json_array());
    csilk_json_add_object(s, "properties", props);
    return s;
}

static csilk_json_t* schema_get_summary(void) {
    csilk_json_t* s = csilk_json_object();
    csilk_json_add_string(s, "type", "object");
    csilk_json_add_array(s, "required", csilk_json_array());
    csilk_json_add_object(s, "properties", csilk_json_object());
    return s;
}

static csilk_json_t* schema_get_asset_detail(void) {
    csilk_json_t* props = csilk_json_object();
    csilk_json_add_string(props, "asset_id", "Asset ID to look up");
    csilk_json_t* s = csilk_json_object();
    csilk_json_add_string(s, "type", "object");
    csilk_json_add_array(s, "required", csilk_json_string_new("asset_id"));
    csilk_json_add_object(s, "properties", props);
    return s;
}

static csilk_ai_tool_t s_tools[] = {
    {
        .type = "function",
        .function = {
            .name = "get_assets",
            .description = "获取用户的资产列表（银行账户、投资、现金等），返回资产名称、余额、分类等信息",
            .parameters_json = NULL, /* filled in init */
        },
    },
    {
        .type = "function",
        .function = {
            .name = "get_asset_detail",
            .description = "获取单个资产的详细信息，包括持仓数量、成本、净值等",
            .parameters_json = NULL,
        },
    },
    {
        .type = "function",
        .function = {
            .name = "get_transactions",
            .description = "获取用户的交易记录列表，支持按日期、类型筛选。包括收入、支出、转账等交易",
            .parameters_json = NULL,
        },
    },
    {
        .type = "function",
        .function = {
            .name = "get_daily_expenses",
            .description = "获取用户的日常收支记录，支持按日期、类型筛选",
            .parameters_json = NULL,
        },
    },
    {
        .type = "function",
        .function = {
            .name = "get_categories",
            .description = "获取用户的分类列表（资产分类、收支分类、交易分类）",
            .parameters_json = NULL,
        },
    },
    {
        .type = "function",
        .function = {
            .name = "get_summary",
            .description = "获取用户的财务概览：总资产、总负债、净资产、本月收支等汇总数据",
            .parameters_json = NULL,
        },
    },
};

static int s_tools_initialized = 0;

static void ensure_tools_init(void) {
    if (s_tools_initialized) return;
    s_tools[0].function.parameters_json = schema_get_assets();
    s_tools[1].function.parameters_json = schema_get_asset_detail();
    s_tools[2].function.parameters_json = schema_get_transactions();
    s_tools[3].function.parameters_json = schema_get_daily_expenses();
    s_tools[4].function.parameters_json = schema_get_categories();
    s_tools[5].function.parameters_json = schema_get_summary();
    s_tools_initialized = 1;
}

const csilk_ai_tool_t* ai_tools_get_definitions(size_t* count) {
    ensure_tools_init();
    *count = sizeof(s_tools) / sizeof(s_tools[0]);
    return s_tools;
}

static char* json_to_str(csilk_json_t* obj) {
    if (!obj) return NULL;
    size_t len = 0;
    char* s = csilk_json_serialize(obj, &len);
    csilk_json_free(obj);
    return s;
}

static const char* arg_str(csilk_json_t* args, const char* key, const char* def) {
    const char* v = csilk_json_get_string(args, key);
    return (v && v[0]) ? v : def;
}

static int64_t arg_int(csilk_json_t* args, const char* key, int64_t def) {
    const csilk_json_t* v = csilk_json_get(args, key);
    if (!v) return def;
    if (csilk_json_is_number(v)) {
        return (int64_t)csilk_json_number_value(v);
    }
    if (csilk_json_is_string(v)) {
        return (int64_t)atoll(csilk_json_get_string(v, NULL));
    }
    return def;
}

static char* exec_get_assets(csilk_db_pool_t* pool, int64_t user_id, csilk_json_t* args) {
    int64_t page = arg_int(args, "page", 1);
    int64_t page_size = arg_int(args, "page_size", 50);
    int64_t total = 0;
    csilk_json_t* result = asset_list(pool, user_id, page, page_size, NULL, &total);
    if (!result) return strdup("{\"error\":\"failed to query assets\"}");
    /* Wrap with total count */
    csilk_json_t* wrapper = csilk_json_object();
    csilk_json_add_array(wrapper, "list", result);
    csilk_json_add_number(wrapper, "total", (double)total);
    csilk_json_add_number(wrapper, "page", (double)page);
    csilk_json_add_number(wrapper, "page_size", (double)page_size);
    return json_to_str(wrapper);
}

static char* exec_get_asset_detail(csilk_db_pool_t* pool, int64_t user_id, csilk_json_t* args) {
    int64_t asset_id = arg_int(args, "asset_id", 0);
    if (asset_id <= 0) return strdup("{\"error\":\"asset_id is required\"}");
    csilk_json_t* result = asset_get(pool, user_id, asset_id);
    if (!result) return strdup("{\"error\":\"asset not found\"}");
    return json_to_str(result);
}

static char* exec_get_transactions(csilk_db_pool_t* pool, int64_t user_id, csilk_json_t* args) {
    int64_t page = arg_int(args, "page", 1);
    int64_t page_size = arg_int(args, "page_size", 50);
    const char* start_date = arg_str(args, "start_date", NULL);
    const char* end_date = arg_str(args, "end_date", NULL);
    const char* type = arg_str(args, "type", NULL);
    int64_t total = 0;
    csilk_json_t* result = tx_list(pool, user_id, page, page_size,
                                   NULL, NULL, type, NULL, start_date, end_date, &total);
    if (!result) return strdup("{\"error\":\"failed to query transactions\"}");
    csilk_json_t* wrapper = csilk_json_object();
    csilk_json_add_array(wrapper, "list", result);
    csilk_json_add_number(wrapper, "total", (double)total);
    csilk_json_add_number(wrapper, "page", (double)page);
    csilk_json_add_number(wrapper, "page_size", (double)page_size);
    return json_to_str(wrapper);
}

static char* exec_get_daily_expenses(csilk_db_pool_t* pool, int64_t user_id, csilk_json_t* args) {
    int64_t page = arg_int(args, "page", 1);
    int64_t page_size = arg_int(args, "page_size", 50);
    const char* start_date = arg_str(args, "start_date", NULL);
    const char* end_date = arg_str(args, "end_date", NULL);
    const char* expense_type = arg_str(args, "expense_type", NULL);
    int64_t total = 0;
    csilk_json_t* result = de_list(pool, user_id, page, page_size,
                                   expense_type, NULL, NULL, start_date, end_date, &total);
    if (!result) return strdup("{\"error\":\"failed to query daily expenses\"}");
    csilk_json_t* wrapper = csilk_json_object();
    csilk_json_add_array(wrapper, "list", result);
    csilk_json_add_number(wrapper, "total", (double)total);
    csilk_json_add_number(wrapper, "page", (double)page);
    csilk_json_add_number(wrapper, "page_size", (double)page_size);
    return json_to_str(wrapper);
}

static char* exec_get_categories(csilk_db_pool_t* pool, int64_t user_id, csilk_json_t* args) {
    const char* type = arg_str(args, "type", NULL);
    csilk_json_t* result = category_list(pool, user_id, type);
    if (!result) return strdup("{\"error\":\"failed to query categories\"}");
    return json_to_str(result);
}

static char* exec_get_summary(csilk_db_pool_t* pool, int64_t user_id, csilk_json_t* args) {
    (void)args;
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);

    const char* sql_asset = "SELECT COALESCE(SUM(current_value), 0) as val FROM assets WHERE user_id = ?";
    csilk_json_t* r1 = csilk_db_query_param_json(pool, sql_asset, (const char*[]){uid, NULL});
    double total_assets = 0;
    if (r1 && csilk_json_array_size(r1) > 0) {
        total_assets = csilk_json_get_number(csilk_json_array_get(r1, 0), "val");
        csilk_json_free(r1);
    }

    const char* sql_liability =
        "SELECT COALESCE(SUM(a.current_value), 0) as val FROM assets a "
        "JOIN categories c ON a.category_id = c.id "
        "WHERE a.user_id = ? AND c.type = 'asset' "
        "AND c.name IN ('loan', 'credit_card', 'other_liability')";
    csilk_json_t* r2 = csilk_db_query_param_json(pool, sql_liability, (const char*[]){uid, NULL});
    double total_liabilities = 0;
    if (r2 && csilk_json_array_size(r2) > 0) {
        total_liabilities = csilk_json_get_number(csilk_json_array_get(r2, 0), "val");
        csilk_json_free(r2);
    }

    const char* sql_tx_count = "SELECT COUNT(*) as cnt FROM transactions WHERE user_id = ?";
    csilk_json_t* r3 = csilk_db_query_param_json(pool, sql_tx_count, (const char*[]){uid, NULL});
    int64_t tx_count = 0;
    if (r3 && csilk_json_array_size(r3) > 0) {
        tx_count = (int64_t)csilk_json_get_number(csilk_json_array_get(r3, 0), "cnt");
        csilk_json_free(r3);
    }

    const char* sql_de_count = "SELECT COUNT(*) as cnt FROM daily_expenses WHERE user_id = ?";
    csilk_json_t* r4 = csilk_db_query_param_json(pool, sql_de_count, (const char*[]){uid, NULL});
    int64_t de_count = 0;
    if (r4 && csilk_json_array_size(r4) > 0) {
        de_count = (int64_t)csilk_json_get_number(csilk_json_array_get(r4, 0), "cnt");
        csilk_json_free(r4);
    }

    csilk_json_t* summary = csilk_json_object();
    csilk_json_add_number(summary, "total_assets", total_assets);
    csilk_json_add_number(summary, "total_liabilities", total_liabilities);
    csilk_json_add_number(summary, "net_worth", total_assets - total_liabilities);
    csilk_json_add_number(summary, "transaction_count", (double)tx_count);
    csilk_json_add_number(summary, "daily_expense_count", (double)de_count);
    return json_to_str(summary);
}

char* ai_tools_execute(csilk_db_pool_t* pool, int64_t user_id,
                       const char* name, const char* arguments) {
    if (!name || !arguments) return NULL;

    csilk_json_t* args = csilk_json_parse(arguments);
    if (!args) {
        args = csilk_json_object();
    }

    char* result = NULL;
    if (strcmp(name, "get_assets") == 0) {
        result = exec_get_assets(pool, user_id, args);
    } else if (strcmp(name, "get_asset_detail") == 0) {
        result = exec_get_asset_detail(pool, user_id, args);
    } else if (strcmp(name, "get_transactions") == 0) {
        result = exec_get_transactions(pool, user_id, args);
    } else if (strcmp(name, "get_daily_expenses") == 0) {
        result = exec_get_daily_expenses(pool, user_id, args);
    } else if (strcmp(name, "get_categories") == 0) {
        result = exec_get_categories(pool, user_id, args);
    } else if (strcmp(name, "get_summary") == 0) {
        result = exec_get_summary(pool, user_id, args);
    } else {
        result = strdup("{\"error\":\"unknown tool\"}");
    }

    csilk_json_free(args);
    return result;
}
