#include "services/ai/tools/projection.h"
#include "csilk/csilk.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
test_get_assets_projection(void)
{
    printf("[Test Compact] Testing get_assets projection...\n");

    /* Construct a bloated assets JSON payload with multiple items */
    csilk_json_t* root = csilk_json_object();
    csilk_json_add_number(root, "total", 5.0);
    csilk_json_add_number(root, "page", 1.0);
    csilk_json_add_number(root, "page_size", 20.0);

    csilk_json_t* assets_arr = csilk_json_array();
    for (int i = 1; i <= 5; i++) {
        csilk_json_t* item = csilk_json_object();
        /* Essential financial fields */
        csilk_json_add_number(item, "id", (double)i);
        csilk_json_add_string(item, "name", "招商银行储蓄卡账户资金");
        csilk_json_add_string(item, "asset_type", "cash");
        csilk_json_add_string(item, "currency", "CNY");
        csilk_json_add_number(item, "balance", 50000.0 + i * 100.0);
        csilk_json_add_number(item, "current_value", 50000.0 + i * 100.0);
        csilk_json_add_number(item, "cost_basis", 50000.0 + i * 100.0);

        /* Bloated technical metadata that MUST be stripped */
        csilk_json_add_number(item, "user_id", 1001.0);
        csilk_json_add_number(item, "category_id", 42.0);
        csilk_json_add_string(item, "account_no", "62258801234567890123");
        csilk_json_add_string(item, "symbol", "CMB_DEBIT_SAVINGS_CARD");
        csilk_json_add_string(item, "quote_source", "manual_sync_v2_internal");
        csilk_json_add_string(item, "last_sync_at", "2026-11-03T08:30:00.123456Z");
        csilk_json_add_string(item, "note", "工资主卡长期备用金日常开销与房贷扣款专用");
        csilk_json_add_string(item, "icon", "ph:credit-card-bold-dark-mode");
        csilk_json_add_number(item, "sort_order", 10.0);
        csilk_json_add_string(item, "created_at", "2026-01-01T12:00:00.000000Z");
        csilk_json_add_string(item, "updated_at", "2026-11-02T18:45:00.000000Z");
        csilk_json_add_number(item, "account_id", 9.0);
        csilk_json_add_string(item, "account_name", "默认总管账户平台分类");
        csilk_json_add_string(item, "institution", "bank_card_institutional_provider");
        csilk_json_add_string(item, "category_name", "主要日常储蓄活期卡");
        csilk_json_add_number(item, "quantity", 50000.0 + i * 100.0);
        csilk_json_add_number(item, "net_value", 1.0);
        csilk_json_add_string(item, "cost_method", "first_in_first_out_method");

        csilk_json_array_append(assets_arr, item);
    }
    csilk_json_add_array(root, "assets", assets_arr);

    size_t raw_len = 0;
    char* raw_json = csilk_json_serialize(root, &raw_len);
    csilk_json_free(root);
    assert(raw_json != NULL);

    char* projected = ai_project_tool_result("get_assets", raw_json);
    assert(projected != NULL);

    size_t proj_len = strlen(projected);
    double reduction = 1.0 - ((double)proj_len / (double)raw_len);
    printf("  Original payload: %zu bytes, Projected payload: %zu bytes (%.2f%% reduction)\n",
           raw_len, proj_len, reduction * 100.0);
    assert(reduction >= 0.60);

    /* Verify fields in projected JSON */
    csilk_json_t* proj = csilk_json_parse(projected);
    assert(proj != NULL);
    assert(csilk_json_get_number(proj, "total") == 5.0);
    assert(csilk_json_get_number(proj, "page") == 1.0);
    assert(csilk_json_get_number(proj, "page_size") == 20.0);

    csilk_json_t* proj_arr = csilk_json_get(proj, "assets");
    assert(proj_arr != NULL);
    assert(csilk_json_is_array(proj_arr));
    assert(csilk_json_array_size(proj_arr) == 5);

    csilk_json_t* first = csilk_json_array_get(proj_arr, 0);
    assert(first != NULL);

    /* Essential financial fields must be preserved */
    assert(csilk_json_get(first, "id") != NULL);
    assert(csilk_json_get(first, "name") != NULL);
    assert(csilk_json_get(first, "asset_type") != NULL);
    assert(csilk_json_get(first, "currency") != NULL);
    assert(csilk_json_get(first, "current_value") != NULL || csilk_json_get(first, "balance") != NULL);
    assert(csilk_json_get(first, "cost_basis") != NULL);

    /* Non-essential technical metadata must be completely stripped */
    assert(csilk_json_get(first, "user_id") == NULL);
    assert(csilk_json_get(first, "account_no") == NULL);
    assert(csilk_json_get(first, "symbol") == NULL);
    assert(csilk_json_get(first, "quote_source") == NULL);
    assert(csilk_json_get(first, "last_sync_at") == NULL);
    assert(csilk_json_get(first, "note") == NULL);
    assert(csilk_json_get(first, "icon") == NULL);
    assert(csilk_json_get(first, "sort_order") == NULL);
    assert(csilk_json_get(first, "created_at") == NULL);
    assert(csilk_json_get(first, "updated_at") == NULL);
    assert(csilk_json_get(first, "account_id") == NULL);
    assert(csilk_json_get(first, "account_name") == NULL);
    assert(csilk_json_get(first, "institution") == NULL);
    assert(csilk_json_get(first, "cost_method") == NULL);

    csilk_json_free(proj);
    free(raw_json);
    free(projected);
    printf("  ✓ get_assets projection passed.\n");
}

static void
test_get_daily_expenses_projection(void)
{
    printf("[Test Compact] Testing get_daily_expenses projection...\n");

    csilk_json_t* root = csilk_json_object();
    csilk_json_add_number(root, "total", 30.0);
    csilk_json_add_number(root, "page", 1.0);
    csilk_json_add_number(root, "page_size", 30.0);

    /* Populate 25 items to test truncation at 15 */
    csilk_json_t* arr = csilk_json_array();
    for (int i = 1; i <= 25; i++) {
        csilk_json_t* item = csilk_json_object();
        csilk_json_add_number(item, "id", (double)i);
        csilk_json_add_string(item, "date", "2026-11-01");
        csilk_json_add_number(item, "amount", 35.50 + i);
        csilk_json_add_string(item, "category_name", "餐饮外卖");
        csilk_json_add_string(item, "asset_name", "招商银行");
        csilk_json_add_string(item, "type", "expense");
        csilk_json_add_string(item, "note", "午餐日式牛肉饭套餐");

        /* Metadata to strip */
        csilk_json_add_number(item, "user_id", 1001.0);
        csilk_json_add_number(item, "category_id", 3.0);
        csilk_json_add_number(item, "asset_id", 5.0);
        csilk_json_add_string(item, "created_at", "2026-11-01T12:30:00Z");
        csilk_json_add_string(item, "updated_at", "2026-11-01T12:30:00Z");

        csilk_json_array_append(arr, item);
    }
    csilk_json_add_array(root, "expenses", arr);

    size_t raw_len = 0;
    char* raw_json = csilk_json_serialize(root, &raw_len);
    csilk_json_free(root);
    assert(raw_json != NULL);

    char* projected = ai_project_tool_result("get_daily_expenses", raw_json);
    assert(projected != NULL);

    csilk_json_t* proj = csilk_json_parse(projected);
    assert(proj != NULL);

    /* Pagination metadata preserved */
    assert(csilk_json_get_number(proj, "total") == 30.0);
    assert(csilk_json_get_number(proj, "page") == 1.0);
    assert(csilk_json_get_number(proj, "page_size") == 30.0);

    /* Array must be capped at 15 items */
    csilk_json_t* proj_arr = csilk_json_get(proj, "expenses");
    assert(proj_arr != NULL);
    assert(csilk_json_is_array(proj_arr));
    size_t count = csilk_json_array_size(proj_arr);
    printf("  Truncated row count: %zu (expected 15)\n", count);
    assert(count == 15);

    /* Verify essential fields retained and technical fields stripped */
    csilk_json_t* first = csilk_json_array_get(proj_arr, 0);
    assert(first != NULL);
    assert(csilk_json_get(first, "id") != NULL);
    assert(csilk_json_get(first, "date") != NULL);
    assert(csilk_json_get(first, "amount") != NULL);
    assert(csilk_json_get(first, "category_name") != NULL);
    assert(csilk_json_get(first, "asset_name") != NULL);
    assert(csilk_json_get(first, "type") != NULL);
    assert(csilk_json_get(first, "note") != NULL);

    assert(csilk_json_get(first, "user_id") == NULL);
    assert(csilk_json_get(first, "category_id") == NULL);
    assert(csilk_json_get(first, "asset_id") == NULL);
    assert(csilk_json_get(first, "created_at") == NULL);
    assert(csilk_json_get(first, "updated_at") == NULL);

    csilk_json_free(proj);
    free(raw_json);
    free(projected);
    printf("  ✓ get_daily_expenses projection passed.\n");
}

static void
test_get_transactions_projection(void)
{
    printf("[Test Compact] Testing get_transactions projection...\n");

    csilk_json_t* root = csilk_json_object();
    csilk_json_add_number(root, "total", 40.0);
    csilk_json_add_number(root, "page", 1.0);
    csilk_json_add_number(root, "page_size", 20.0);

    /* Populate 20 transactions to test truncation at 15 */
    csilk_json_t* arr = csilk_json_array();
    for (int i = 1; i <= 20; i++) {
        csilk_json_t* item = csilk_json_object();
        csilk_json_add_number(item, "id", (double)i);
        csilk_json_add_string(item, "transaction_date", "2026-11-02");
        csilk_json_add_string(item, "transaction_type", "buy");
        csilk_json_add_number(item, "amount", 1000.0 * i);
        csilk_json_add_string(item, "category_name", "股票投资");
        csilk_json_add_string(item, "asset_name", "贵州茅台");
        csilk_json_add_string(item, "note", "定投买入一手");

        /* Metadata to strip */
        csilk_json_add_number(item, "user_id", 1001.0);
        csilk_json_add_number(item, "ledger_id", 2.0);
        csilk_json_add_number(item, "asset_id", 10.0);
        csilk_json_add_number(item, "linked_asset_id", 11.0);
        csilk_json_add_number(item, "category_id", 6.0);
        csilk_json_add_string(item, "source_type", "manual");
        csilk_json_add_number(item, "direction", 1.0);
        csilk_json_add_number(item, "linked_direction", -1.0);
        csilk_json_add_number(item, "price_per_unit", 1700.0);
        csilk_json_add_number(item, "quantity", 100.0);
        csilk_json_add_string(item, "currency", "CNY");
        csilk_json_add_string(item, "created_at", "2026-11-02T10:00:00Z");
        csilk_json_add_string(item, "updated_at", "2026-11-02T10:00:00Z");

        csilk_json_array_append(arr, item);
    }
    csilk_json_add_array(root, "transactions", arr);

    size_t raw_len = 0;
    char* raw_json = csilk_json_serialize(root, &raw_len);
    csilk_json_free(root);
    assert(raw_json != NULL);

    char* projected = ai_project_tool_result("get_transactions", raw_json);
    assert(projected != NULL);

    csilk_json_t* proj = csilk_json_parse(projected);
    assert(proj != NULL);

    /* Pagination metadata preserved */
    assert(csilk_json_get_number(proj, "total") == 40.0);
    assert(csilk_json_get_number(proj, "page") == 1.0);
    assert(csilk_json_get_number(proj, "page_size") == 20.0);

    /* Array must be capped at 15 items */
    csilk_json_t* proj_arr = csilk_json_get(proj, "transactions");
    assert(proj_arr != NULL);
    assert(csilk_json_is_array(proj_arr));
    size_t count = csilk_json_array_size(proj_arr);
    printf("  Truncated row count: %zu (expected 15)\n", count);
    assert(count == 15);

    /* Verify essential fields retained and internal fields stripped */
    csilk_json_t* first = csilk_json_array_get(proj_arr, 0);
    assert(first != NULL);
    assert(csilk_json_get(first, "id") != NULL);
    assert(csilk_json_get(first, "transaction_date") != NULL);
    assert(csilk_json_get(first, "transaction_type") != NULL);
    assert(csilk_json_get(first, "amount") != NULL);
    assert(csilk_json_get(first, "category_name") != NULL);
    assert(csilk_json_get(first, "asset_name") != NULL);
    assert(csilk_json_get(first, "note") != NULL);

    assert(csilk_json_get(first, "user_id") == NULL);
    assert(csilk_json_get(first, "ledger_id") == NULL);
    assert(csilk_json_get(first, "asset_id") == NULL);
    assert(csilk_json_get(first, "linked_asset_id") == NULL);
    assert(csilk_json_get(first, "category_id") == NULL);
    assert(csilk_json_get(first, "direction") == NULL);
    assert(csilk_json_get(first, "linked_direction") == NULL);
    assert(csilk_json_get(first, "source_type") == NULL);
    assert(csilk_json_get(first, "created_at") == NULL);
    assert(csilk_json_get(first, "updated_at") == NULL);

    csilk_json_free(proj);
    free(raw_json);
    free(projected);
    printf("  ✓ get_transactions projection passed.\n");
}

static void
test_pass_through(void)
{
    printf("[Test Compact] Testing pass-through on error payloads and non-projected tools...\n");

    /* 1. Error payload pass-through */
    const char* err_json = "{\"error\":\"database query timeout on cluster replica\"}";
    char* proj_err = ai_project_tool_result("get_assets", err_json);
    assert(proj_err != NULL);
    assert(strcmp(proj_err, err_json) == 0);
    free(proj_err);

    /* 2. Non-projected tool pass-through (web_search) */
    const char* search_json = "{\"results\":[{\"title\":\"Test Title\",\"url\":\"https://example.com\"}]}";
    char* proj_search = ai_project_tool_result("web_search", search_json);
    assert(proj_search != NULL);
    assert(strcmp(proj_search, search_json) == 0);
    free(proj_search);

    /* 3. Unknown tool pass-through */
    const char* custom_json = "{\"status\":\"custom_ok\",\"data\":[1,2,3]}";
    char* proj_custom = ai_project_tool_result("custom_unknown_tool", custom_json);
    assert(proj_custom != NULL);
    assert(strcmp(proj_custom, custom_json) == 0);
    free(proj_custom);

    /* 4. Null safety */
    char* proj_null_tool = ai_project_tool_result(NULL, search_json);
    assert(proj_null_tool != NULL);
    free(proj_null_tool);
    assert(ai_project_tool_result("get_assets", NULL) == NULL);

    printf("  ✓ pass-through cases passed.\n");
}

int
main(void)
{
    printf("=== Running Unit Test: AI Token Compact Projection ===\n");

    test_get_assets_projection();
    test_get_daily_expenses_projection();
    test_get_transactions_projection();
    test_pass_through();

    printf("✓ All AI token compact projection tests passed successfully!\n");
    return 0;
}
