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
#include <time.h>
#include <math.h>
#include <ctype.h>
#include <curl/curl.h>

/* ========================================================================= */
/*  JSON Schema Helpers                                                      */
/* ========================================================================= */

static void
add_prop(csilk_json_t* props, const char* name, const char* type, const char* desc)
{
    csilk_json_t* p = csilk_json_object();
    csilk_json_add_string(p, "type", type);
    csilk_json_add_string(p, "description", desc);
    csilk_json_add_object(props, name, p);
}

static csilk_json_t*
make_schema(csilk_json_t* props, const char** required_names, int req_count)
{
    csilk_json_t* s = csilk_json_object();
    csilk_json_add_string(s, "type", "object");
    csilk_json_add_object(s, "properties", props);
    csilk_json_t* req = csilk_json_array();
    for (int i = 0; i < req_count; i++) {
        csilk_json_array_append(req, csilk_json_string_new(required_names[i]));
    }
    csilk_json_add_array(s, "required", req);
    return s;
}

/* ========================================================================= */
/*  Tool Schema Definitions                                                  */
/* ========================================================================= */

static csilk_json_t*
schema_get_assets(void)
{
    csilk_json_t* props = csilk_json_object();
    add_prop(props, "page", "integer", "Page number, default 1");
    add_prop(props, "page_size", "integer", "Items per page, default 50");
    return make_schema(props, NULL, 0);
}

static csilk_json_t*
schema_get_asset_detail(void)
{
    csilk_json_t* props = csilk_json_object();
    add_prop(props, "asset_id", "integer", "Asset ID to look up");
    const char* req[] = {"asset_id"};
    return make_schema(props, req, 1);
}

static csilk_json_t*
schema_get_transactions(void)
{
    csilk_json_t* props = csilk_json_object();
    add_prop(props, "page", "integer", "Page number, default 1");
    add_prop(props, "page_size", "integer", "Items per page, default 50");
    add_prop(props, "start_date", "string", "Filter start date (YYYY-MM-DD)");
    add_prop(props, "end_date", "string", "Filter end date (YYYY-MM-DD)");
    add_prop(props, "type", "string", "Transaction type filter");
    return make_schema(props, NULL, 0);
}

static csilk_json_t*
schema_get_daily_expenses(void)
{
    csilk_json_t* props = csilk_json_object();
    add_prop(props, "page", "integer", "Page number, default 1");
    add_prop(props, "page_size", "integer", "Items per page, default 50");
    add_prop(props, "start_date", "string", "Filter start date (YYYY-MM-DD)");
    add_prop(props, "end_date", "string", "Filter end date (YYYY-MM-DD)");
    add_prop(props, "expense_type", "string", "Expense type filter: income or expense");
    return make_schema(props, NULL, 0);
}

static csilk_json_t*
schema_get_categories(void)
{
    csilk_json_t* props = csilk_json_object();
    add_prop(props,
             "type",
             "string",
             "Category type: asset, income, expense, transaction. Empty for all.");
    return make_schema(props, NULL, 0);
}

static csilk_json_t*
schema_get_summary(void)
{
    csilk_json_t* props = csilk_json_object();
    return make_schema(props, NULL, 0);
}

static csilk_json_t*
schema_get_current_time(void)
{
    csilk_json_t* props = csilk_json_object();
    add_prop(props, "timezone", "string", "Optional timezone name, default server local timezone");
    return make_schema(props, NULL, 0);
}

static csilk_json_t*
schema_calculate_date_range(void)
{
    csilk_json_t* props = csilk_json_object();
    add_prop(props,
             "range_type",
             "string",
             "Relative time range identifier: today, yesterday, this_week, last_week, this_month, "
             "last_month, this_quarter, last_quarter, this_year, last_year, last_7_days, "
             "last_30_days, last_90_days, last_365_days");
    const char* req[] = {"range_type"};
    return make_schema(props, req, 1);
}

static csilk_json_t*
schema_calculate_compound_interest(void)
{
    csilk_json_t* props = csilk_json_object();
    add_prop(props, "principal", "number", "Initial investment principal amount (e.g. 100000)");
    add_prop(props,
             "regular_contribution",
             "number",
             "Regular contribution amount per period (default 0)");
    add_prop(props,
             "contribution_frequency",
             "string",
             "Contribution frequency: 'monthly' (default) or 'yearly'");
    add_prop(props,
             "annual_rate_pct",
             "number",
             "Expected annual interest/return rate in percentage, e.g. 6.5 for 6.5%");
    add_prop(props, "years", "number", "Investment horizon in years, e.g. 10");
    const char* req[] = {"principal", "annual_rate_pct", "years"};
    return make_schema(props, req, 3);
}

static csilk_json_t*
schema_calculate_loan_repayment(void)
{
    csilk_json_t* props = csilk_json_object();
    add_prop(props, "loan_amount", "number", "Total loan principal amount (e.g. 1000000)");
    add_prop(
        props, "annual_rate_pct", "number", "Annual interest rate percentage, e.g. 3.45 for 3.45%");
    add_prop(props, "term_years", "integer", "Loan duration in years (e.g. 30)");
    add_prop(props,
             "term_months",
             "integer",
             "Loan duration in months (takes precedence over term_years)");
    add_prop(props,
             "repayment_type",
             "string",
             "Repayment method: 'equal_installment' (等额本息) or 'equal_principal' (等额本金)");
    const char* req[] = {"loan_amount", "annual_rate_pct", "repayment_type"};
    return make_schema(props, req, 3);
}

static csilk_json_t*
schema_web_search(void)
{
    csilk_json_t* props = csilk_json_object();
    add_prop(props, "query", "string", "Search keywords, financial question, or topic");
    add_prop(props,
             "max_results",
             "integer",
             "Maximum number of search results to return (1-10, default 5)");
    const char* req[] = {"query"};
    return make_schema(props, req, 1);
}

static csilk_json_t*
schema_get_exchange_rate(void)
{
    csilk_json_t* props = csilk_json_object();
    add_prop(props,
             "base_currency",
             "string",
             "Base currency code, e.g. 'USD', 'CNY', 'EUR', 'JPY', 'HKD', 'GBP', 'SGD', 'AUD', "
             "'CAD' (default: 'USD')");
    add_prop(
        props,
        "target_currency",
        "string",
        "Target currency code to convert to, e.g. 'CNY', 'USD', 'JPY', 'EUR', 'HKD' (optional)");
    add_prop(props, "amount", "number", "Amount of base currency to convert (default: 1.0)");
    return make_schema(props, NULL, 0);
}

static csilk_json_t*
schema_propose_daily_expense(void)
{
    csilk_json_t* props = csilk_json_object();
    add_prop(props,
             "type",
             "string",
             "Expense type: 'expense' (支出) or 'income' (收入), default 'expense'");
    add_prop(props, "amount", "number", "Transaction amount (must be positive number > 0)");
    add_prop(props,
             "category_name",
             "string",
             "Category name or intent, e.g. '餐饮', '打车', '交通', '购物', '工资', etc.");
    add_prop(props,
             "asset_name",
             "string",
             "Asset/account name, e.g. '招行', '微信', '支付宝', '现金', '工商银行'");
    add_prop(props, "date", "string", "Date YYYY-MM-DD (defaults to today if omitted)");
    add_prop(props, "note", "string", "Remark or note description");
    const char* req[] = {"amount"};
    return make_schema(props, req, 1);
}

static csilk_json_t*
schema_propose_transfer(void)
{
    csilk_json_t* props = csilk_json_object();
    add_prop(props, "amount", "number", "Transfer amount (must be positive number > 0)");
    add_prop(props,
             "from_asset_name",
             "string",
             "Source asset/account name, e.g. '招商银行', '工资卡', '微信钱包'");
    add_prop(props,
             "to_asset_name",
             "string",
             "Target asset/account name, e.g. '信用卡', '支付宝', '储蓄卡'");
    add_prop(props, "date", "string", "Transfer date YYYY-MM-DD");
    add_prop(props, "fee", "number", "Transfer fee (optional, default 0)");
    add_prop(props, "note", "string", "Transfer note");
    const char* req[] = {"amount"};
    return make_schema(props, req, 1);
}

static csilk_json_t*
schema_analyze_financial_health(void)
{
    csilk_json_t* props = csilk_json_object();
    return make_schema(props, NULL, 0);
}

/* ========================================================================= */
/*  Tool Registry Array                                                      */
/* ========================================================================= */

static csilk_ai_tool_t s_tools[] = {
    {
     .type = "function",
     .function =
            {
                .name = "get_assets",
                .description =
                    "获取用户的资产列表（银行账户、投资、现金等），返回资产名称、余额、分类等信息",
                .parameters_json = NULL, /* filled in init */
            }, },
    {
     .type = "function",
     .function =
            {
                .name = "get_asset_detail",
                .description = "获取单个资产的详细信息，包括持仓数量、成本、净值等",
                .parameters_json = NULL,
            }, },
    {
     .type = "function",
     .function =
            {
                .name = "get_transactions",
                .description =
                    "获取用户的交易记录列表，支持按日期、类型筛选。包括收入、支出、转账等交易",
                .parameters_json = NULL,
            }, },
    {
     .type = "function",
     .function =
            {
                .name = "get_daily_expenses",
                .description = "获取用户的日常收支记录，支持按日期、类型筛选",
                .parameters_json = NULL,
            }, },
    {
     .type = "function",
     .function =
            {
                .name = "get_categories",
                .description = "获取用户的分类列表（资产分类、收支分类、交易分类）",
                .parameters_json = NULL,
            }, },
    {
     .type = "function",
     .function =
            {
                .name = "get_summary",
                .description = "获取用户的财务概览：总资产、总负债、净资产、本月收支等汇总数据",
                .parameters_json = NULL,
            }, },
    {
     .type = "function",
     .function =
            {
                .name = "get_current_time",
                .description =
                    "获取当前服务器精确时间、年月日、星期几、时区和当前季度，用于准确定位时间基准",
                .parameters_json = NULL,
            }, },
    {
     .type = "function",
     .function =
            {
                .name = "calculate_date_range",
                .description = "根据自然语言相对时间范围（如本周、上月、今年、近30天等）精确计算并"
                               "换算为标准的 YYYY-MM-DD 起止日期",
                .parameters_json = NULL,
            }, },
    {
     .type = "function",
     .function =
            {
                .name = "calculate_compound_interest",
                .description = "高精度复利与定投计算器，测算一次性投资或定期定额投入的终值、累计本"
                               "金与收益明细",
                .parameters_json = NULL,
            }, },
    {
     .type = "function",
     .function =
            {
                .name = "calculate_loan_repayment",
                .description = "房贷与分期还款计算器，支持等额本息与等额本金两种还款方式测算月供、"
                               "总利息与总还款额",
                .parameters_json = NULL,
            }, },
    {
     .type = "function",
     .function =
            {
                .name = "web_search",
                .description =
                    "联网搜索工具，获取最新的金融市场行情、宏观经济数据、政策新闻与网络知识",
                .parameters_json = NULL,
            }, },
    {
     .type = "function",
     .function =
            {
                .name = "get_exchange_rate",
                .description = "实时获取外汇汇率和货币兑换计算结果（如 "
                               "USD/CNY、EUR/CNY、JPY/CNY、HKD/CNY、GBP/CNY 等最新行情）",
                .parameters_json = NULL,
            }, },
    {
     .type = "function",
     .function =
            {
                .name = "propose_daily_expense",
                .description = "自然语言记账智能拟录入草稿（收入或支出），自动模糊匹配账户与分类，"
                               "返回拟录入卡片数据供用户两步确认",
                .parameters_json = NULL,
            }, },
    {
     .type = "function",
     .function =
            {
                .name = "propose_transfer",
                .description = "账户间转账智能拟录入草稿，自动模糊匹配转出与转入资产账户，返回转账"
                               "卡片数据供用户两步确认",
                .parameters_json = NULL,
            }, },
    {
     .type = "function",
     .function =
            {
                .name = "analyze_financial_health",
                .description = "全套核心财务健康体检诊断，聚合计算应急备用金流动性月数、净储蓄率、"
                               "资产负债率与生息投资资产占比",
                .parameters_json = NULL,
            }, },
};

static int s_tools_initialized = 0;

static void
ensure_tools_init(void)
{
    if (s_tools_initialized) {
        return;
    }
    s_tools[0].function.parameters_json = schema_get_assets();
    s_tools[1].function.parameters_json = schema_get_asset_detail();
    s_tools[2].function.parameters_json = schema_get_transactions();
    s_tools[3].function.parameters_json = schema_get_daily_expenses();
    s_tools[4].function.parameters_json = schema_get_categories();
    s_tools[5].function.parameters_json = schema_get_summary();
    s_tools[6].function.parameters_json = schema_get_current_time();
    s_tools[7].function.parameters_json = schema_calculate_date_range();
    s_tools[8].function.parameters_json = schema_calculate_compound_interest();
    s_tools[9].function.parameters_json = schema_calculate_loan_repayment();
    s_tools[10].function.parameters_json = schema_web_search();
    s_tools[11].function.parameters_json = schema_get_exchange_rate();
    s_tools[12].function.parameters_json = schema_propose_daily_expense();
    s_tools[13].function.parameters_json = schema_propose_transfer();
    s_tools[14].function.parameters_json = schema_analyze_financial_health();
    s_tools_initialized = 1;
}

const csilk_ai_tool_t*
ai_tools_get_definitions(size_t* count)
{
    ensure_tools_init();
    *count = sizeof(s_tools) / sizeof(s_tools[0]);
    return s_tools;
}

/* ========================================================================= */
/*  Argument Parsing & JSON Helpers                                          */
/* ========================================================================= */

static char*
json_to_str(csilk_json_t* obj)
{
    if (!obj) {
        return NULL;
    }
    size_t len = 0;
    char*  s = csilk_json_serialize(obj, &len);
    csilk_json_free(obj);
    return s;
}

static const char*
arg_str(csilk_json_t* args, const char* key, const char* def)
{
    const char* v = csilk_json_get_string(args, key);
    return (v && v[0]) ? v : def;
}

static int64_t
arg_int(csilk_json_t* args, const char* key, int64_t def)
{
    const csilk_json_t* v = csilk_json_get(args, key);
    if (!v) {
        return def;
    }
    if (csilk_json_is_number(v)) {
        return (int64_t)csilk_json_number_value(v);
    }
    if (csilk_json_is_string(v)) {
        return (int64_t)atoll(csilk_json_get_string(v, NULL));
    }
    return def;
}

static double
arg_double(csilk_json_t* args, const char* key, double def)
{
    const csilk_json_t* v = csilk_json_get(args, key);
    if (!v) {
        return def;
    }
    if (csilk_json_is_number(v)) {
        return csilk_json_number_value(v);
    }
    if (csilk_json_is_string(v)) {
        return atof(csilk_json_get_string(v, "0"));
    }
    return def;
}

static double
round_to_2(double val)
{
    return round(val * 100.0) / 100.0;
}

/* ========================================================================= */
/*  Existing Tool Implementations (Assets, Transactions, Daily Expenses)      */
/* ========================================================================= */

static char*
exec_get_assets(csilk_db_pool_t* pool, int64_t user_id, csilk_json_t* args)
{
    int64_t       page = arg_int(args, "page", 1);
    int64_t       page_size = arg_int(args, "page_size", 50);
    int64_t       total = 0;
    csilk_json_t* result = asset_list(pool, user_id, page, page_size, NULL, &total);
    if (!result) {
        return strdup("{\"error\":\"failed to query assets\"}");
    }
    csilk_json_t* wrapper = csilk_json_object();
    csilk_json_add_array(wrapper, "list", result);
    csilk_json_add_number(wrapper, "total", (double)total);
    csilk_json_add_number(wrapper, "page", (double)page);
    csilk_json_add_number(wrapper, "page_size", (double)page_size);
    return json_to_str(wrapper);
}

static char*
exec_get_asset_detail(csilk_db_pool_t* pool, int64_t user_id, csilk_json_t* args)
{
    int64_t asset_id = arg_int(args, "asset_id", 0);
    if (asset_id <= 0) {
        return strdup("{\"error\":\"asset_id is required\"}");
    }
    csilk_json_t* result = asset_get(pool, user_id, asset_id);
    if (!result) {
        return strdup("{\"error\":\"asset not found\"}");
    }
    return json_to_str(result);
}

static char*
exec_get_transactions(csilk_db_pool_t* pool, int64_t user_id, csilk_json_t* args)
{
    int64_t       page = arg_int(args, "page", 1);
    int64_t       page_size = arg_int(args, "page_size", 50);
    const char*   start_date = arg_str(args, "start_date", NULL);
    const char*   end_date = arg_str(args, "end_date", NULL);
    const char*   type = arg_str(args, "type", NULL);
    int64_t       total = 0;
    csilk_json_t* result = tx_list(
        pool, user_id, page, page_size, NULL, NULL, type, NULL, start_date, end_date, &total);
    if (!result) {
        return strdup("{\"error\":\"failed to query transactions\"}");
    }
    csilk_json_t* wrapper = csilk_json_object();
    csilk_json_add_array(wrapper, "list", result);
    csilk_json_add_number(wrapper, "total", (double)total);
    csilk_json_add_number(wrapper, "page", (double)page);
    csilk_json_add_number(wrapper, "page_size", (double)page_size);
    return json_to_str(wrapper);
}

static char*
exec_get_daily_expenses(csilk_db_pool_t* pool, int64_t user_id, csilk_json_t* args)
{
    int64_t       page = arg_int(args, "page", 1);
    int64_t       page_size = arg_int(args, "page_size", 50);
    const char*   start_date = arg_str(args, "start_date", NULL);
    const char*   end_date = arg_str(args, "end_date", NULL);
    const char*   expense_type = arg_str(args, "expense_type", NULL);
    int64_t       total = 0;
    csilk_json_t* result = de_list(
        pool, user_id, page, page_size, expense_type, NULL, NULL, start_date, end_date, &total);
    if (!result) {
        return strdup("{\"error\":\"failed to query daily expenses\"}");
    }
    csilk_json_t* wrapper = csilk_json_object();
    csilk_json_add_array(wrapper, "list", result);
    csilk_json_add_number(wrapper, "total", (double)total);
    csilk_json_add_number(wrapper, "page", (double)page);
    csilk_json_add_number(wrapper, "page_size", (double)page_size);
    return json_to_str(wrapper);
}

static char*
exec_get_categories(csilk_db_pool_t* pool, int64_t user_id, csilk_json_t* args)
{
    const char*   type = arg_str(args, "type", NULL);
    csilk_json_t* result = category_list(pool, user_id, type);
    if (!result) {
        return strdup("{\"error\":\"failed to query categories\"}");
    }
    return json_to_str(result);
}

static char*
exec_get_summary(csilk_db_pool_t* pool, int64_t user_id, csilk_json_t* args)
{
    (void)args;
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);

    /* Single query with correlated subqueries: preserves original semantics */
    const char* sql =
        "SELECT "
        "  (SELECT COALESCE(SUM(current_value), 0) FROM assets WHERE user_id = ?) AS total_assets, "
        "  (SELECT COALESCE(SUM(a.current_value), 0) "
        "   FROM assets a JOIN categories c ON a.category_id = c.id "
        "   WHERE a.user_id = ? AND c.type = 'asset' "
        "   AND c.name IN ('loan','credit_card','other_liability')) AS total_liabilities, "
        "  (SELECT COUNT(*) FROM transactions WHERE user_id = ?) AS tx_count, "
        "  (SELECT COUNT(*) FROM daily_expenses WHERE user_id = ?) AS de_count";

    csilk_json_t* r =
        csilk_db_query_param_json(pool, sql, (const char*[]){uid, uid, uid, uid, NULL});
    double  total_assets = 0, total_liabilities = 0;
    int64_t tx_count = 0, de_count = 0;
    if (r && csilk_json_array_size(r) > 0) {
        csilk_json_t* row = csilk_json_array_get(r, 0);
        total_assets = db_get_num(row, "total_assets");
        total_liabilities = db_get_num(row, "total_liabilities");
        tx_count = db_get_int(row, "tx_count");
        de_count = db_get_int(row, "de_count");
    }
    csilk_json_free(r);

    csilk_json_t* summary = csilk_json_object();
    csilk_json_add_number(summary, "total_assets", total_assets);
    csilk_json_add_number(summary, "total_liabilities", total_liabilities);
    csilk_json_add_number(summary, "net_worth", total_assets - total_liabilities);
    csilk_json_add_number(summary, "transaction_count", (double)tx_count);
    csilk_json_add_number(summary, "daily_expense_count", (double)de_count);
    return json_to_str(summary);
}

/* ========================================================================= */
/*  Task 1: Time & Date Tools Implementation                                 */
/* ========================================================================= */

static char*
exec_get_current_time(csilk_db_pool_t* pool, int64_t user_id, csilk_json_t* args)
{
    (void)pool;
    (void)user_id;
    (void)args;

    time_t    now = time(NULL);
    struct tm tm_val;
    localtime_r(&now, &tm_val);

    char dt_buf[64], d_buf[32], t_buf[32], tz_buf[64];
    strftime(dt_buf, sizeof(dt_buf), "%Y-%m-%d %H:%M:%S", &tm_val);
    strftime(d_buf, sizeof(d_buf), "%Y-%m-%d", &tm_val);
    strftime(t_buf, sizeof(t_buf), "%H:%M:%S", &tm_val);
    strftime(tz_buf, sizeof(tz_buf), "%z %Z", &tm_val);

    static const char* weekdays_en[] = {
        "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    static const char* weekdays_cn[] = {
        "星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"};
    int wday = (tm_val.tm_wday >= 0 && tm_val.tm_wday < 7) ? tm_val.tm_wday : 0;

    int  quarter = (tm_val.tm_mon / 3) + 1;
    char q_buf[8];
    snprintf(q_buf, sizeof(q_buf), "Q%d", quarter);

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_string(res, "datetime", dt_buf);
    csilk_json_add_string(res, "date", d_buf);
    csilk_json_add_string(res, "time", t_buf);
    csilk_json_add_number(res, "year", (double)(tm_val.tm_year + 1900));
    csilk_json_add_number(res, "month", (double)(tm_val.tm_mon + 1));
    csilk_json_add_number(res, "day", (double)tm_val.tm_mday);
    csilk_json_add_string(res, "weekday", weekdays_en[wday]);
    csilk_json_add_string(res, "weekday_cn", weekdays_cn[wday]);
    csilk_json_add_string(res, "quarter", q_buf);
    csilk_json_add_string(res, "timezone", tz_buf);
    csilk_json_add_number(res, "timestamp", (double)now);

    return json_to_str(res);
}

static int
is_leap_year(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static int
days_in_month(int y, int m)
{
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && is_leap_year(y)) {
        return 29;
    }
    if (m >= 1 && m <= 12) {
        return d[m - 1];
    }
    return 30;
}

static char*
exec_calculate_date_range(csilk_db_pool_t* pool, int64_t user_id, csilk_json_t* args)
{
    (void)pool;
    (void)user_id;

    const char* range_type = arg_str(args, "range_type", "this_month");
    time_t      now = time(NULL);
    struct tm   tm_val;
    localtime_r(&now, &tm_val);

    int cur_year = tm_val.tm_year + 1900;
    int cur_mon = tm_val.tm_mon + 1; // 1-12
    int cur_day = tm_val.tm_mday;

    char start_date[32] = {0};
    char end_date[32] = {0};
    char label[64] = {0};
    int  days_count = 0;

    if (strcmp(range_type, "today") == 0) {
        snprintf(start_date, sizeof(start_date), "%04d-%02d-%02d", cur_year, cur_mon, cur_day);
        snprintf(end_date, sizeof(end_date), "%04d-%02d-%02d", cur_year, cur_mon, cur_day);
        snprintf(label, sizeof(label), "今天");
        days_count = 1;
    } else if (strcmp(range_type, "yesterday") == 0) {
        time_t    yest = now - 86400;
        struct tm tm_y;
        localtime_r(&yest, &tm_y);
        snprintf(start_date,
                 sizeof(start_date),
                 "%04d-%02d-%02d",
                 tm_y.tm_year + 1900,
                 tm_y.tm_mon + 1,
                 tm_y.tm_mday);
        snprintf(end_date,
                 sizeof(end_date),
                 "%04d-%02d-%02d",
                 tm_y.tm_year + 1900,
                 tm_y.tm_mon + 1,
                 tm_y.tm_mday);
        snprintf(label, sizeof(label), "昨天");
        days_count = 1;
    } else if (strcmp(range_type, "this_week") == 0) {
        int       day_offset = (tm_val.tm_wday == 0) ? 6 : (tm_val.tm_wday - 1);
        time_t    mon_t = now - (time_t)day_offset * 86400;
        time_t    sun_t = mon_t + 6 * 86400;
        struct tm tm_m, tm_s;
        localtime_r(&mon_t, &tm_m);
        localtime_r(&sun_t, &tm_s);
        snprintf(start_date,
                 sizeof(start_date),
                 "%04d-%02d-%02d",
                 tm_m.tm_year + 1900,
                 tm_m.tm_mon + 1,
                 tm_m.tm_mday);
        snprintf(end_date,
                 sizeof(end_date),
                 "%04d-%02d-%02d",
                 tm_s.tm_year + 1900,
                 tm_s.tm_mon + 1,
                 tm_s.tm_mday);
        snprintf(label, sizeof(label), "本周");
        days_count = 7;
    } else if (strcmp(range_type, "last_week") == 0) {
        int       day_offset = (tm_val.tm_wday == 0) ? 6 : (tm_val.tm_wday - 1);
        time_t    mon_t = now - (time_t)(day_offset + 7) * 86400;
        time_t    sun_t = mon_t + 6 * 86400;
        struct tm tm_m, tm_s;
        localtime_r(&mon_t, &tm_m);
        localtime_r(&sun_t, &tm_s);
        snprintf(start_date,
                 sizeof(start_date),
                 "%04d-%02d-%02d",
                 tm_m.tm_year + 1900,
                 tm_m.tm_mon + 1,
                 tm_m.tm_mday);
        snprintf(end_date,
                 sizeof(end_date),
                 "%04d-%02d-%02d",
                 tm_s.tm_year + 1900,
                 tm_s.tm_mon + 1,
                 tm_s.tm_mday);
        snprintf(label, sizeof(label), "上周");
        days_count = 7;
    } else if (strcmp(range_type, "this_month") == 0) {
        int dim = days_in_month(cur_year, cur_mon);
        snprintf(start_date, sizeof(start_date), "%04d-%02d-01", cur_year, cur_mon);
        snprintf(end_date, sizeof(end_date), "%04d-%02d-%02d", cur_year, cur_mon, dim);
        snprintf(label, sizeof(label), "本月");
        days_count = dim;
    } else if (strcmp(range_type, "last_month") == 0) {
        int prev_year = cur_year;
        int prev_mon = cur_mon - 1;
        if (prev_mon < 1) {
            prev_mon = 12;
            prev_year--;
        }
        int dim = days_in_month(prev_year, prev_mon);
        snprintf(start_date, sizeof(start_date), "%04d-%02d-01", prev_year, prev_mon);
        snprintf(end_date, sizeof(end_date), "%04d-%02d-%02d", prev_year, prev_mon, dim);
        snprintf(label, sizeof(label), "上个月");
        days_count = dim;
    } else if (strcmp(range_type, "this_quarter") == 0) {
        int q = (cur_mon - 1) / 3; // 0, 1, 2, 3
        int start_m = q * 3 + 1;
        int end_m = start_m + 2;
        int dim = days_in_month(cur_year, end_m);
        snprintf(start_date, sizeof(start_date), "%04d-%02d-01", cur_year, start_m);
        snprintf(end_date, sizeof(end_date), "%04d-%02d-%02d", cur_year, end_m, dim);
        snprintf(label, sizeof(label), "本季度 (Q%d)", q + 1);
        days_count = days_in_month(cur_year, start_m) + days_in_month(cur_year, start_m + 1) + dim;
    } else if (strcmp(range_type, "last_quarter") == 0) {
        int q = (cur_mon - 1) / 3;
        int lq_year = cur_year;
        int lq = q - 1;
        if (lq < 0) {
            lq = 3;
            lq_year--;
        }
        int start_m = lq * 3 + 1;
        int end_m = start_m + 2;
        int dim = days_in_month(lq_year, end_m);
        snprintf(start_date, sizeof(start_date), "%04d-%02d-01", lq_year, start_m);
        snprintf(end_date, sizeof(end_date), "%04d-%02d-%02d", lq_year, end_m, dim);
        snprintf(label, sizeof(label), "上个季度 (Q%d)", lq + 1);
        days_count = days_in_month(lq_year, start_m) + days_in_month(lq_year, start_m + 1) + dim;
    } else if (strcmp(range_type, "this_year") == 0) {
        snprintf(start_date, sizeof(start_date), "%04d-01-01", cur_year);
        snprintf(end_date, sizeof(end_date), "%04d-12-31", cur_year);
        snprintf(label, sizeof(label), "今年 (%d年)", cur_year);
        days_count = is_leap_year(cur_year) ? 366 : 365;
    } else if (strcmp(range_type, "last_year") == 0) {
        int ly = cur_year - 1;
        snprintf(start_date, sizeof(start_date), "%04d-01-01", ly);
        snprintf(end_date, sizeof(end_date), "%04d-12-31", ly);
        snprintf(label, sizeof(label), "去年 (%d年)", ly);
        days_count = is_leap_year(ly) ? 366 : 365;
    } else {
        /* Rolling day windows */
        int roll_days = 30;
        if (strcmp(range_type, "last_7_days") == 0) {
            roll_days = 7;
            snprintf(label, sizeof(label), "近7天");
        } else if (strcmp(range_type, "last_90_days") == 0) {
            roll_days = 90;
            snprintf(label, sizeof(label), "近90天");
        } else if (strcmp(range_type, "last_365_days") == 0) {
            roll_days = 365;
            snprintf(label, sizeof(label), "近365天 (近一年)");
        } else {
            roll_days = 30;
            snprintf(label, sizeof(label), "近30天");
        }
        time_t    st = now - (time_t)(roll_days - 1) * 86400;
        struct tm tm_st;
        localtime_r(&st, &tm_st);
        snprintf(start_date,
                 sizeof(start_date),
                 "%04d-%02d-%02d",
                 tm_st.tm_year + 1900,
                 tm_st.tm_mon + 1,
                 tm_st.tm_mday);
        snprintf(end_date, sizeof(end_date), "%04d-%02d-%02d", cur_year, cur_mon, cur_day);
        days_count = roll_days;
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_string(res, "range_type", range_type);
    csilk_json_add_string(res, "label", label);
    csilk_json_add_string(res, "start_date", start_date);
    csilk_json_add_string(res, "end_date", end_date);
    csilk_json_add_number(res, "days_count", (double)days_count);
    return json_to_str(res);
}

/* ========================================================================= */
/*  Task 2: Financial Calculators Implementation                             */
/* ========================================================================= */

static char*
exec_calculate_compound_interest(csilk_db_pool_t* pool, int64_t user_id, csilk_json_t* args)
{
    (void)pool;
    (void)user_id;

    double      principal = arg_double(args, "principal", 0.0);
    double      regular_contrib = arg_double(args, "regular_contribution", 0.0);
    const char* freq = arg_str(args, "contribution_frequency", "monthly");
    double      annual_rate_pct = arg_double(args, "annual_rate_pct", 0.0);
    double      years_input = arg_double(args, "years", 1.0);

    if (principal < 0 || regular_contrib < 0 || years_input <= 0) {
        return strdup("{\"error\":\"principal, contribution, and years must be positive\"}");
    }

    int years = (int)years_input;
    if (years < 1) {
        years = 1;
    }
    if (years > 100) {
        years = 100;
    }

    int    is_monthly = (strcmp(freq, "yearly") != 0);
    double r_annual = annual_rate_pct / 100.0;
    double r_period = is_monthly ? (r_annual / 12.0) : r_annual;
    int    periods_per_year = is_monthly ? 12 : 1;

    double balance = principal;
    double total_principal = principal;

    csilk_json_t* yearly_arr = csilk_json_array();

    for (int y = 1; y <= years; y++) {
        for (int p = 0; p < periods_per_year; p++) {
            balance = balance * (1.0 + r_period) + regular_contrib;
            total_principal += regular_contrib;
        }
        csilk_json_t* y_item = csilk_json_object();
        csilk_json_add_number(y_item, "year", (double)y);
        csilk_json_add_number(y_item, "principal", round_to_2(total_principal));
        csilk_json_add_number(y_item, "balance", round_to_2(balance));
        csilk_json_add_number(y_item, "interest", round_to_2(balance - total_principal));
        csilk_json_array_append(yearly_arr, y_item);
    }

    double total_interest = balance - total_principal;
    double return_pct = total_principal > 0 ? (total_interest / total_principal * 100.0) : 0.0;

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "initial_principal", round_to_2(principal));
    csilk_json_add_number(res, "regular_contribution", round_to_2(regular_contrib));
    csilk_json_add_string(res, "contribution_frequency", is_monthly ? "monthly" : "yearly");
    csilk_json_add_number(res, "annual_rate_pct", annual_rate_pct);
    csilk_json_add_number(res, "years", (double)years);
    csilk_json_add_number(res, "total_principal", round_to_2(total_principal));
    csilk_json_add_number(res, "total_interest", round_to_2(total_interest));
    csilk_json_add_number(res, "future_value", round_to_2(balance));
    csilk_json_add_number(res, "effective_total_return_pct", round_to_2(return_pct));
    csilk_json_add_array(res, "yearly_summary", yearly_arr);

    return json_to_str(res);
}

static char*
exec_calculate_loan_repayment(csilk_db_pool_t* pool, int64_t user_id, csilk_json_t* args)
{
    (void)pool;
    (void)user_id;

    double  loan_amount = arg_double(args, "loan_amount", 0.0);
    double  annual_rate_pct = arg_double(args, "annual_rate_pct", 0.0);
    int64_t term_months = arg_int(args, "term_months", 0);
    if (term_months <= 0) {
        int64_t term_years = arg_int(args, "term_years", 30);
        term_months = term_years * 12;
    }
    const char* rep_type = arg_str(args, "repayment_type", "equal_installment");

    if (loan_amount <= 0 || term_months <= 0) {
        return strdup("{\"error\":\"loan_amount and term must be positive numbers\"}");
    }

    double i_month = (annual_rate_pct / 100.0) / 12.0;
    int    N = (int)term_months;

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "loan_amount", round_to_2(loan_amount));
    csilk_json_add_number(res, "annual_rate_pct", annual_rate_pct);
    csilk_json_add_number(res, "term_months", (double)N);
    csilk_json_add_number(res, "term_years", round_to_2((double)N / 12.0));

    if (strcmp(rep_type, "equal_principal") == 0) {
        /* Equal Principal (等额本金) */
        double p_month = loan_amount / N;
        double first_month_pay = p_month + loan_amount * i_month;
        double last_month_pay = p_month + (loan_amount - (N - 1) * p_month) * i_month;
        double total_interest = (N + 1) * loan_amount * i_month / 2.0;
        double total_repay = loan_amount + total_interest;
        double monthly_decrease = p_month * i_month;

        csilk_json_add_string(res, "repayment_type", "equal_principal");
        csilk_json_add_string(res, "repayment_type_cn", "等额本金");
        csilk_json_add_number(res, "first_month_payment", round_to_2(first_month_pay));
        csilk_json_add_number(res, "last_month_payment", round_to_2(last_month_pay));
        csilk_json_add_number(res, "monthly_decrease", round_to_2(monthly_decrease));
        csilk_json_add_number(res, "total_interest", round_to_2(total_interest));
        csilk_json_add_number(res, "total_repayment", round_to_2(total_repay));
    } else {
        /* Equal Installment (等额本息) */
        double monthly_pay = 0.0;
        if (i_month <= 0) {
            monthly_pay = loan_amount / N;
        } else {
            double factor = pow(1.0 + i_month, N);
            monthly_pay = loan_amount * (i_month * factor) / (factor - 1.0);
        }
        double total_repay = monthly_pay * N;
        double total_interest = total_repay - loan_amount;

        csilk_json_add_string(res, "repayment_type", "equal_installment");
        csilk_json_add_string(res, "repayment_type_cn", "等额本息");
        csilk_json_add_number(res, "monthly_payment", round_to_2(monthly_pay));
        csilk_json_add_number(res, "first_month_payment", round_to_2(monthly_pay));
        csilk_json_add_number(res, "last_month_payment", round_to_2(monthly_pay));
        csilk_json_add_number(res, "total_interest", round_to_2(total_interest));
        csilk_json_add_number(res, "total_repayment", round_to_2(total_repay));
    }

    return json_to_str(res);
}

/* ========================================================================= */
/*  Task 3: Web Search Tool Implementation                                   */
/* ========================================================================= */

typedef struct {
    char*  data;
    size_t size;
    size_t cap;
} curl_buf_t;

static size_t
curl_write_cb(void* ptr, size_t size, size_t nmemb, void* userdata)
{
    size_t      total = size * nmemb;
    curl_buf_t* b = (curl_buf_t*)userdata;
    if (b->size + total + 1 > b->cap) {
        size_t new_cap = (b->cap + total + 8192) * 2;
        char*  new_data = realloc(b->data, new_cap);
        if (!new_data) {
            return 0;
        }
        b->data = new_data;
        b->cap = new_cap;
    }
    memcpy(b->data + b->size, ptr, total);
    b->size += total;
    b->data[b->size] = '\0';
    return total;
}

static char*
url_encode(CURL* curl, const char* str)
{
    if (!str) {
        return strdup("");
    }
    char* enc = curl_easy_escape(curl, str, (int)strlen(str));
    if (!enc) {
        return strdup(str);
    }
    char* res = strdup(enc);
    curl_free(enc);
    return res;
}

static char*
exec_get_exchange_rate(csilk_db_pool_t* pool, int64_t user_id, csilk_json_t* args)
{
    (void)pool;
    (void)user_id;

    const char* base_in = arg_str(args, "base_currency", "USD");
    const char* target_in = arg_str(args, "target_currency", NULL);
    double      amount = arg_double(args, "amount", 1.0);
    if (amount <= 0) {
        amount = 1.0;
    }

    char base[16] = "USD";
    if (base_in && base_in[0]) {
        size_t blen = strlen(base_in);
        if (blen > 15) {
            blen = 15;
        }
        for (size_t i = 0; i < blen; i++) {
            base[i] = (char)toupper((unsigned char)base_in[i]);
        }
        base[blen] = '\0';
    }

    char target[16] = "";
    if (target_in && target_in[0]) {
        size_t tlen = strlen(target_in);
        if (tlen > 15) {
            tlen = 15;
        }
        for (size_t i = 0; i < tlen; i++) {
            target[i] = (char)toupper((unsigned char)target_in[i]);
        }
        target[tlen] = '\0';
    }

    static int s_curl_inited = 0;
    if (!s_curl_inited) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        s_curl_inited = 1;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        return strdup("{\"error\":\"failed to initialize HTTP client\"}");
    }

    char url[256];
    snprintf(url, sizeof(url), "https://open.er-api.com/v6/latest/%s", base);

    curl_buf_t buf = {.data = malloc(8192), .size = 0, .cap = 8192};
    if (buf.data) {
        buf.data[0] = '\0';
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 6000L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 Minefolio/1.0");

    CURLcode      rc = curl_easy_perform(curl);
    csilk_json_t* res_obj = csilk_json_object();

    if (rc == CURLE_OK && buf.size > 0) {
        csilk_json_t* fx_json = csilk_json_parse(buf.data);
        if (fx_json) {
            const char* res_code = csilk_json_get_string(fx_json, "result");
            if (res_code && strcmp(res_code, "success") == 0) {
                csilk_json_add_string(res_obj, "status", "success");
                csilk_json_add_string(res_obj, "base_currency", base);
                csilk_json_add_string(res_obj,
                                      "updated_time_utc",
                                      csilk_json_get_string(fx_json, "time_last_update_utc") ?: "");
                csilk_json_add_number(res_obj, "amount", amount);

                const csilk_json_t* rates = csilk_json_get(fx_json, "rates");
                if (rates) {
                    if (target[0] != '\0') {
                        const csilk_json_t* tr = csilk_json_get(rates, target);
                        if (tr && csilk_json_is_number(tr)) {
                            double tr_val = csilk_json_number_value(tr);
                            csilk_json_add_string(res_obj, "target_currency", target);
                            csilk_json_add_number(res_obj, "rate", tr_val);
                            csilk_json_add_number(
                                res_obj, "converted_amount", round_to_2(amount * tr_val));
                        }
                    }

                    /* Major key rates list */
                    csilk_json_t* major = csilk_json_object();
                    const char*   major_keys[] = {"CNY",
                                                  "USD",
                                                  "EUR",
                                                  "JPY",
                                                  "HKD",
                                                  "GBP",
                                                  "SGD",
                                                  "AUD",
                                                  "CAD",
                                                  "CHF",
                                                  "KRW"};
                    for (size_t k = 0; k < sizeof(major_keys) / sizeof(major_keys[0]); k++) {
                        const csilk_json_t* kr = csilk_json_get(rates, major_keys[k]);
                        if (kr && csilk_json_is_number(kr)) {
                            csilk_json_add_number(
                                major, major_keys[k], csilk_json_number_value(kr));
                        }
                    }
                    csilk_json_add_object(res_obj, "major_rates", major);
                }

                csilk_json_free(fx_json);
                free(buf.data);
                curl_easy_cleanup(curl);
                return json_to_str(res_obj);
            }
            csilk_json_free(fx_json);
        }
    }

    free(buf.data);
    curl_easy_cleanup(curl);
    csilk_json_add_string(res_obj, "status", "error");
    csilk_json_add_string(
        res_obj, "message", "failed to fetch real-time exchange rates from server");
    return json_to_str(res_obj);
}

static char*
exec_web_search(csilk_db_pool_t* pool, int64_t user_id, csilk_json_t* args)
{
    (void)pool;
    (void)user_id;

    const char* query = arg_str(args, "query", NULL);
    int64_t     max_results = arg_int(args, "max_results", 5);
    if (max_results < 1) {
        max_results = 1;
    }
    if (max_results > 10) {
        max_results = 10;
    }

    if (!query || !query[0]) {
        return strdup("{\"error\":\"search query is required\"}");
    }

    static int s_curl_inited = 0;
    if (!s_curl_inited) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        s_curl_inited = 1;
    }

    const char* tavily_key = getenv("TAVILY_API_KEY");
    const char* bocha_key = getenv("BOCHA_API_KEY");
    const char* serper_key = getenv("SERPER_API_KEY");

    /* If query is asking about foreign exchange / currency rates, handle automatically via live FX API */
    int is_fx_query = 0;
    if (strstr(query, "汇率") || strstr(query, "外汇") || strstr(query, "exchange rate") ||
        strstr(query, "rate") || strstr(query, "CNY") || strstr(query, "USD") ||
        strstr(query, "JPY") || strstr(query, "EUR") || strstr(query, "HKD") ||
        strstr(query, "GBP") || strstr(query, "兑换")) {
        is_fx_query = 1;
    }

    if (is_fx_query && (!tavily_key || !tavily_key[0]) && (!bocha_key || !bocha_key[0]) &&
        (!serper_key || !serper_key[0])) {
        const char* base_curr = "USD";
        if (strstr(query, "CNY") || strstr(query, "人民币")) {
            base_curr = "USD";
        } else if (strstr(query, "EUR") || strstr(query, "欧元")) {
            base_curr = "EUR";
        } else if (strstr(query, "JPY") || strstr(query, "日元")) {
            base_curr = "JPY";
        } else if (strstr(query, "GBP") || strstr(query, "英镑")) {
            base_curr = "GBP";
        } else if (strstr(query, "HKD") || strstr(query, "港币") || strstr(query, "港元")) {
            base_curr = "HKD";
        }

        csilk_json_t* fx_args = csilk_json_object();
        csilk_json_add_string(fx_args, "base_currency", base_curr);
        csilk_json_add_string(fx_args, "target_currency", "CNY");
        char* fx_res = exec_get_exchange_rate(pool, user_id, fx_args);
        csilk_json_free(fx_args);

        if (fx_res) {
            csilk_json_t* fx_obj = csilk_json_parse(fx_res);
            free(fx_res);
            if (fx_obj) {
                csilk_json_t* s_res = csilk_json_object();
                csilk_json_add_string(s_res, "query", query);
                csilk_json_add_string(s_res, "provider", "realtime_forex_feed");
                csilk_json_add_number(s_res, "results_count", 1.0);
                csilk_json_t* arr = csilk_json_array();
                csilk_json_t* it = csilk_json_object();
                csilk_json_add_string(it, "title", "实时外汇汇率行情与中间价数据");
                csilk_json_add_string(it, "url", "https://open.er-api.com");
                size_t jlen = 0;
                char*  jtxt = csilk_json_serialize(fx_obj, &jlen);
                csilk_json_add_string(it, "snippet", jtxt ?: "");
                free(jtxt);
                csilk_json_array_append(arr, it);
                csilk_json_add_array(s_res, "results", arr);
                csilk_json_free(fx_obj);
                return json_to_str(s_res);
            }
        }
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        return strdup("{\"error\":\"failed to initialize HTTP client\"}");
    }

    curl_buf_t buf = {.data = malloc(4096), .size = 0, .cap = 4096};
    if (buf.data) {
        buf.data[0] = '\0';
    }

    csilk_json_t* res_obj = csilk_json_object();
    csilk_json_add_string(res_obj, "query", query);

    if (tavily_key && tavily_key[0]) {
        /* 1. Tavily Search API */
        csilk_json_t* req_body = csilk_json_object();
        csilk_json_add_string(req_body, "api_key", tavily_key);
        csilk_json_add_string(req_body, "query", query);
        csilk_json_add_number(req_body, "max_results", (double)max_results);
        csilk_json_add_string(req_body, "search_depth", "basic");
        csilk_json_add_bool(req_body, "include_answer", 1);

        size_t post_len = 0;
        char*  post_data = csilk_json_serialize(req_body, &post_len);
        csilk_json_free(req_body);

        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, "https://api.tavily.com/search");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 8000L);

        CURLcode rc = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        free(post_data);

        if (rc == CURLE_OK && buf.size > 0) {
            csilk_json_t* tv_res = csilk_json_parse(buf.data);
            if (tv_res) {
                csilk_json_add_string(res_obj, "provider", "tavily");
                const char* ans = csilk_json_get_string(tv_res, "answer");
                if (ans && ans[0]) {
                    csilk_json_add_string(res_obj, "direct_answer", ans);
                }
                const csilk_json_t* rlist = csilk_json_get(tv_res, "results");
                if (rlist && csilk_json_is_array(rlist)) {
                    csilk_json_t* parsed_results = csilk_json_array();
                    size_t        cnt = csilk_json_array_size(rlist);
                    for (size_t i = 0; i < cnt && (int64_t)i < max_results; i++) {
                        const csilk_json_t* it = csilk_json_array_get(rlist, i);
                        csilk_json_t*       out_it = csilk_json_object();
                        csilk_json_add_string(
                            out_it, "title", csilk_json_get_string(it, "title") ?: "");
                        csilk_json_add_string(
                            out_it, "url", csilk_json_get_string(it, "url") ?: "");
                        csilk_json_add_string(
                            out_it, "snippet", csilk_json_get_string(it, "content") ?: "");
                        csilk_json_array_append(parsed_results, out_it);
                    }
                    csilk_json_add_number(
                        res_obj, "results_count", (double)csilk_json_array_size(parsed_results));
                    csilk_json_add_array(res_obj, "results", parsed_results);
                }
                csilk_json_free(tv_res);
                free(buf.data);
                curl_easy_cleanup(curl);
                return json_to_str(res_obj);
            }
        }
    } else if (bocha_key && bocha_key[0]) {
        /* 2. Bocha AI Search API */
        csilk_json_t* req_body = csilk_json_object();
        csilk_json_add_string(req_body, "query", query);
        csilk_json_add_string(req_body, "freshness", "noLimit");
        csilk_json_add_number(req_body, "count", (double)max_results);

        size_t post_len = 0;
        char*  post_data = csilk_json_serialize(req_body, &post_len);
        csilk_json_free(req_body);

        char auth_hdr[256];
        snprintf(auth_hdr, sizeof(auth_hdr), "Authorization: Bearer %s", bocha_key);

        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, auth_hdr);

        curl_easy_setopt(curl, CURLOPT_URL, "https://api.bochaai.com/v1/web-search");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 8000L);

        CURLcode rc = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        free(post_data);

        if (rc == CURLE_OK && buf.size > 0) {
            csilk_json_t* bc_res = csilk_json_parse(buf.data);
            if (bc_res) {
                csilk_json_add_string(res_obj, "provider", "bocha");
                const csilk_json_t* data_obj = csilk_json_get(bc_res, "data");
                const csilk_json_t* web_pages =
                    data_obj ? csilk_json_get(data_obj, "webPages") : NULL;
                const csilk_json_t* val_list =
                    web_pages ? csilk_json_get(web_pages, "value") : NULL;

                if (val_list && csilk_json_is_array(val_list)) {
                    csilk_json_t* parsed_results = csilk_json_array();
                    size_t        cnt = csilk_json_array_size(val_list);
                    for (size_t i = 0; i < cnt && (int64_t)i < max_results; i++) {
                        const csilk_json_t* it = csilk_json_array_get(val_list, i);
                        csilk_json_t*       out_it = csilk_json_object();
                        csilk_json_add_string(
                            out_it, "title", csilk_json_get_string(it, "name") ?: "");
                        csilk_json_add_string(
                            out_it, "url", csilk_json_get_string(it, "url") ?: "");
                        csilk_json_add_string(
                            out_it, "snippet", csilk_json_get_string(it, "snippet") ?: "");
                        csilk_json_array_append(parsed_results, out_it);
                    }
                    csilk_json_add_number(
                        res_obj, "results_count", (double)csilk_json_array_size(parsed_results));
                    csilk_json_add_array(res_obj, "results", parsed_results);
                }
                csilk_json_free(bc_res);
                free(buf.data);
                curl_easy_cleanup(curl);
                return json_to_str(res_obj);
            }
        }
    } else if (serper_key && serper_key[0]) {
        /* 3. Serper Google Search API */
        csilk_json_t* req_body = csilk_json_object();
        csilk_json_add_string(req_body, "q", query);
        csilk_json_add_number(req_body, "num", (double)max_results);

        size_t post_len = 0;
        char*  post_data = csilk_json_serialize(req_body, &post_len);
        csilk_json_free(req_body);

        char auth_hdr[256];
        snprintf(auth_hdr, sizeof(auth_hdr), "X-API-KEY: %s", serper_key);

        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, auth_hdr);

        curl_easy_setopt(curl, CURLOPT_URL, "https://google.serper.dev/search");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 8000L);

        CURLcode rc = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        free(post_data);

        if (rc == CURLE_OK && buf.size > 0) {
            csilk_json_t* sp_res = csilk_json_parse(buf.data);
            if (sp_res) {
                csilk_json_add_string(res_obj, "provider", "serper");
                const csilk_json_t* org = csilk_json_get(sp_res, "organic");
                if (org && csilk_json_is_array(org)) {
                    csilk_json_t* parsed_results = csilk_json_array();
                    size_t        cnt = csilk_json_array_size(org);
                    for (size_t i = 0; i < cnt && (int64_t)i < max_results; i++) {
                        const csilk_json_t* it = csilk_json_array_get(org, i);
                        csilk_json_t*       out_it = csilk_json_object();
                        csilk_json_add_string(
                            out_it, "title", csilk_json_get_string(it, "title") ?: "");
                        csilk_json_add_string(
                            out_it, "url", csilk_json_get_string(it, "link") ?: "");
                        csilk_json_add_string(
                            out_it, "snippet", csilk_json_get_string(it, "snippet") ?: "");
                        csilk_json_array_append(parsed_results, out_it);
                    }
                    csilk_json_add_number(
                        res_obj, "results_count", (double)csilk_json_array_size(parsed_results));
                    csilk_json_add_array(res_obj, "results", parsed_results);
                }
                csilk_json_free(sp_res);
                free(buf.data);
                curl_easy_cleanup(curl);
                return json_to_str(res_obj);
            }
        }
    }

    /* 4. OpenSearch (Bing + Baidu) Free Multi-Source Search */
    char*         enc = url_encode(curl, query);
    csilk_json_t* results_arr = csilk_json_array();

    if (enc && enc[0]) {
        /* 4a. Bing OpenSearch */
        char url[512];
        snprintf(url, sizeof(url), "https://api.bing.com/osjson.aspx?query=%s", enc);

        buf.size = 0;
        if (buf.data) {
            buf.data[0] = '\0';
        }

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 2500L);
        curl_easy_setopt(
            curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/120.0.0.0");

        CURLcode rc = curl_easy_perform(curl);
        if (rc == CURLE_OK && buf.size > 0) {
            csilk_json_t* arr = csilk_json_parse(buf.data);
            if (arr && csilk_json_is_array(arr) && csilk_json_array_size(arr) >= 2) {
                csilk_json_t* items = csilk_json_array_get(arr, 1);
                if (items && csilk_json_is_array(items)) {
                    size_t cnt = csilk_json_array_size(items);
                    for (size_t i = 0;
                         i < cnt && (int64_t)csilk_json_array_size(results_arr) < max_results;
                         i++) {
                        const char* text = csilk_json_string_value(csilk_json_array_get(items, i));
                        if (text && text[0]) {
                            csilk_json_t* it = csilk_json_object();
                            csilk_json_add_string(it, "title", text);
                            char link[512];
                            snprintf(link, sizeof(link), "https://www.bing.com/search?q=%s", enc);
                            csilk_json_add_string(it, "url", link);
                            char snip[256];
                            snprintf(snip, sizeof(snip), "搜索实时热点与资讯: %s", text);
                            csilk_json_add_string(it, "snippet", snip);
                            csilk_json_array_append(results_arr, it);
                        }
                    }
                }
            }
            if (arr) {
                csilk_json_free(arr);
            }
        }

        /* 4b. If still need results, query Baidu OpenSearch */
        if ((int64_t)csilk_json_array_size(results_arr) < max_results) {
            buf.size = 0;
            if (buf.data) {
                buf.data[0] = '\0';
            }
            snprintf(url,
                     sizeof(url),
                     "https://suggestion.baidu.com/su?wd=%s&action=opensearch&ie=utf-8",
                     enc);
            curl_easy_setopt(curl, CURLOPT_URL, url);
            rc = curl_easy_perform(curl);
            if (rc == CURLE_OK && buf.size > 0) {
                csilk_json_t* arr = csilk_json_parse(buf.data);
                if (arr && csilk_json_is_array(arr) && csilk_json_array_size(arr) >= 2) {
                    csilk_json_t* items = csilk_json_array_get(arr, 1);
                    if (items && csilk_json_is_array(items)) {
                        size_t cnt = csilk_json_array_size(items);
                        for (size_t i = 0;
                             i < cnt && (int64_t)csilk_json_array_size(results_arr) < max_results;
                             i++) {
                            const char* text =
                                csilk_json_string_value(csilk_json_array_get(items, i));
                            if (text && text[0]) {
                                csilk_json_t* it = csilk_json_object();
                                csilk_json_add_string(it, "title", text);
                                char link[512];
                                snprintf(link, sizeof(link), "https://www.baidu.com/s?wd=%s", enc);
                                csilk_json_add_string(it, "url", link);
                                char snip[256];
                                snprintf(snip, sizeof(snip), "搜索实时资讯与行情: %s", text);
                                csilk_json_add_string(it, "snippet", snip);
                                csilk_json_array_append(results_arr, it);
                            }
                        }
                    }
                }
                if (arr) {
                    csilk_json_free(arr);
                }
            }
        }
        free(enc);
    }

    csilk_json_add_string(res_obj, "provider", "opensearch");

    /* 5. Fallback context if network returns 0 results */
    if (csilk_json_array_size(results_arr) == 0) {
        time_t    now = time(NULL);
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        char time_str[64];
        snprintf(time_str,
                 sizeof(time_str),
                 "%04d-%02d-%02d %02d:%02d:%02d",
                 tm_now.tm_year + 1900,
                 tm_now.tm_mon + 1,
                 tm_now.tm_mday,
                 tm_now.tm_hour,
                 tm_now.tm_min,
                 tm_now.tm_sec);

        csilk_json_t* fallback_it = csilk_json_object();
        csilk_json_add_string(fallback_it, "title", query);
        csilk_json_add_string(fallback_it, "url", "https://www.bing.com");
        char fallback_snip[512];
        snprintf(fallback_snip,
                 sizeof(fallback_snip),
                 "当前时间为 %s。请基于当前日期及您的知识库与用户财务账单数据为用户详细解答。",
                 time_str);
        csilk_json_add_string(fallback_it, "snippet", fallback_snip);
        csilk_json_array_append(results_arr, fallback_it);
    }

    csilk_json_add_number(res_obj, "results_count", (double)csilk_json_array_size(results_arr));
    csilk_json_add_array(res_obj, "results", results_arr);

    free(buf.data);
    curl_easy_cleanup(curl);
    return json_to_str(res_obj);
}

static int
str_icontains(const char* haystack, const char* needle)
{
    if (!haystack || !needle) {
        return 0;
    }
    if (!needle[0]) {
        return 1;
    }
    char   h[256], n[256];
    size_t hlen = strlen(haystack), nlen = strlen(needle);
    if (hlen >= sizeof(h)) {
        hlen = sizeof(h) - 1;
    }
    if (nlen >= sizeof(n)) {
        nlen = sizeof(n) - 1;
    }
    for (size_t i = 0; i < hlen; i++) {
        h[i] = (char)tolower((unsigned char)haystack[i]);
    }
    h[hlen] = '\0';
    for (size_t i = 0; i < nlen; i++) {
        n[i] = (char)tolower((unsigned char)needle[i]);
    }
    n[nlen] = '\0';
    return strstr(h, n) != NULL || strstr(n, h) != NULL;
}

static char*
exec_propose_daily_expense(csilk_db_pool_t* pool, int64_t user_id, csilk_json_t* args)
{
    const char* type = arg_str(args, "type", "expense");
    double      amount = arg_double(args, "amount", 0.0);
    const char* cat_name = arg_str(args, "category_name", "");
    const char* asset_name = arg_str(args, "asset_name", "");
    const char* date = arg_str(args, "date", NULL);
    const char* note = arg_str(args, "note", "");

    if (amount <= 0) {
        return strdup("{\"error\":\"amount must be positive\"}");
    }

    char date_buf[32];
    if (!date || !date[0]) {
        time_t    now = time(NULL);
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        snprintf(date_buf,
                 sizeof(date_buf),
                 "%04d-%02d-%02d",
                 tm_now.tm_year + 1900,
                 tm_now.tm_mon + 1,
                 tm_now.tm_mday);
        date = date_buf;
    }

    /* 1. Match asset */
    int64_t       matched_asset_id = 0;
    char          matched_asset_name[128] = "";
    int64_t       total_assets = 0;
    csilk_json_t* assets = asset_list(pool, user_id, 1, 500, NULL, &total_assets);
    if (assets && csilk_json_is_array(assets)) {
        size_t asz = csilk_json_array_size(assets);
        for (size_t i = 0; i < asz; i++) {
            csilk_json_t* it = csilk_json_array_get(assets, i);
            const char*   name = csilk_json_get_string(it, "name");
            int64_t       id = (int64_t)csilk_json_get_number(it, "id");
            if (i == 0 || (asset_name[0] && str_icontains(name, asset_name))) {
                matched_asset_id = id;
                strncpy(matched_asset_name, name ? name : "", sizeof(matched_asset_name) - 1);
                if (asset_name[0] && str_icontains(name, asset_name)) {
                    break;
                }
            }
        }
        csilk_json_free(assets);
    }

    /* 2. Match category */
    int64_t       matched_cat_id = 0;
    char          matched_cat_name[128] = "";
    csilk_json_t* cats = category_list(pool, user_id, type);
    if (cats && csilk_json_is_array(cats)) {
        size_t csz = csilk_json_array_size(cats);
        for (size_t i = 0; i < csz; i++) {
            csilk_json_t* it = csilk_json_array_get(cats, i);
            const char*   name = csilk_json_get_string(it, "name");
            int64_t       id = (int64_t)csilk_json_get_number(it, "id");
            if (i == 0 || (cat_name[0] && str_icontains(name, cat_name))) {
                matched_cat_id = id;
                strncpy(matched_cat_name, name ? name : "", sizeof(matched_cat_name) - 1);
                if (cat_name[0] && str_icontains(name, cat_name)) {
                    break;
                }
            }
        }
        csilk_json_free(cats);
    }

    csilk_json_t* root = csilk_json_object();
    csilk_json_add_string(root, "action_type", "daily_expense");
    csilk_json_add_string(root, "status", "proposed");

    csilk_json_t* data = csilk_json_object();
    csilk_json_add_string(data, "type", type);
    csilk_json_add_number(data, "amount", round_to_2(amount));
    csilk_json_add_number(data, "category_id", (double)matched_cat_id);
    csilk_json_add_string(data,
                          "category_name",
                          matched_cat_name[0] ? matched_cat_name
                                              : (cat_name[0] ? cat_name : "日常收支"));
    csilk_json_add_number(data, "asset_id", (double)matched_asset_id);
    csilk_json_add_string(data,
                          "asset_name",
                          matched_asset_name[0] ? matched_asset_name
                                                : (asset_name[0] ? asset_name : "默认账户"));
    csilk_json_add_string(data, "date", date);
    csilk_json_add_string(data, "note", note);

    csilk_json_add_object(root, "data", data);
    return json_to_str(root);
}

static char*
exec_propose_transfer(csilk_db_pool_t* pool, int64_t user_id, csilk_json_t* args)
{
    double      amount = arg_double(args, "amount", 0.0);
    const char* from_name = arg_str(args, "from_asset_name", "");
    const char* to_name = arg_str(args, "to_asset_name", "");
    const char* date = arg_str(args, "date", NULL);
    double      fee = arg_double(args, "fee", 0.0);
    const char* note = arg_str(args, "note", "");

    if (amount <= 0) {
        return strdup("{\"error\":\"amount must be positive\"}");
    }

    char date_buf[32];
    if (!date || !date[0]) {
        time_t    now = time(NULL);
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        snprintf(date_buf,
                 sizeof(date_buf),
                 "%04d-%02d-%02d",
                 tm_now.tm_year + 1900,
                 tm_now.tm_mon + 1,
                 tm_now.tm_mday);
        date = date_buf;
    }

    int64_t       from_id = 0, to_id = 0;
    char          from_matched[128] = "", to_matched[128] = "";
    int64_t       total_assets = 0;
    csilk_json_t* assets = asset_list(pool, user_id, 1, 500, NULL, &total_assets);
    if (assets && csilk_json_is_array(assets)) {
        size_t asz = csilk_json_array_size(assets);
        for (size_t i = 0; i < asz; i++) {
            csilk_json_t* it = csilk_json_array_get(assets, i);
            const char*   name = csilk_json_get_string(it, "name");
            int64_t       id = (int64_t)csilk_json_get_number(it, "id");
            if (from_name[0] && str_icontains(name, from_name)) {
                from_id = id;
                strncpy(from_matched, name ? name : "", sizeof(from_matched) - 1);
            }
            if (to_name[0] && str_icontains(name, to_name)) {
                to_id = id;
                strncpy(to_matched, name ? name : "", sizeof(to_matched) - 1);
            }
        }
        if (from_id == 0 && asz > 0) {
            csilk_json_t* first = csilk_json_array_get(assets, 0);
            from_id = (int64_t)csilk_json_get_number(first, "id");
            strncpy(
                from_matched, csilk_json_get_string(first, "name") ?: "", sizeof(from_matched) - 1);
        }
        if (to_id == 0 && asz > 1) {
            csilk_json_t* second = csilk_json_array_get(assets, 1);
            to_id = (int64_t)csilk_json_get_number(second, "id");
            strncpy(
                to_matched, csilk_json_get_string(second, "name") ?: "", sizeof(to_matched) - 1);
        }
        csilk_json_free(assets);
    }

    csilk_json_t* root = csilk_json_object();
    csilk_json_add_string(root, "action_type", "transfer");
    csilk_json_add_string(root, "status", "proposed");

    csilk_json_t* data = csilk_json_object();
    csilk_json_add_number(data, "amount", round_to_2(amount));
    csilk_json_add_number(data, "from_asset_id", (double)from_id);
    csilk_json_add_string(data, "from_asset_name", from_matched[0] ? from_matched : from_name);
    csilk_json_add_number(data, "to_asset_id", (double)to_id);
    csilk_json_add_string(data, "to_asset_name", to_matched[0] ? to_matched : to_name);
    csilk_json_add_string(data, "date", date);
    csilk_json_add_number(data, "fee", round_to_2(fee));
    csilk_json_add_string(data, "note", note);

    csilk_json_add_object(root, "data", data);
    return json_to_str(root);
}

static char*
exec_analyze_financial_health(csilk_db_pool_t* pool, int64_t user_id, csilk_json_t* args)
{
    (void)args;

    int64_t       total_assets_count = 0;
    csilk_json_t* assets = asset_list(pool, user_id, 1, 500, NULL, &total_assets_count);

    double total_assets = 0.0;
    double total_liabilities = 0.0;
    double liquid_assets = 0.0;
    double invest_assets = 0.0;

    if (assets && csilk_json_is_array(assets)) {
        size_t cnt = csilk_json_array_size(assets);
        for (size_t i = 0; i < cnt; i++) {
            csilk_json_t* it = csilk_json_array_get(assets, i);
            double        val = db_get_num(it, "current_value");
            const char*   atype = csilk_json_get_string(it, "asset_type");
            if (!atype) {
                atype = "cash";
            }

            if (strcmp(atype, "loan") == 0 || strcmp(atype, "credit_card") == 0 ||
                strcmp(atype, "other_liability") == 0) {
                total_liabilities += val;
            } else {
                total_assets += val;
                if (strcmp(atype, "cash") == 0 || strcmp(atype, "bank") == 0) {
                    liquid_assets += val;
                } else if (strcmp(atype, "stock") == 0 || strcmp(atype, "fund") == 0 ||
                           strcmp(atype, "bond") == 0 || strcmp(atype, "crypto") == 0 ||
                           strcmp(atype, "investment") == 0) {
                    invest_assets += val;
                }
            }
        }
        csilk_json_free(assets);
    }

    double net_worth = total_assets - total_liabilities;

    /* Query recent 30-day cash flow */
    time_t    now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char cur_month[16];
    snprintf(cur_month, sizeof(cur_month), "%04d-%02d", tm_now.tm_year + 1900, tm_now.tm_mon + 1);

    csilk_json_t* monthly = tx_monthly(pool, user_id, cur_month);
    double        monthly_inflows = 0.0;
    double        monthly_outflows = 0.0;
    if (monthly && csilk_json_is_array(monthly) && csilk_json_array_size(monthly) > 0) {
        csilk_json_t* m = csilk_json_array_get(monthly, 0);
        monthly_inflows = db_get_num(m, "inflows");
        monthly_outflows = db_get_num(m, "outflows");
    }
    if (monthly) {
        csilk_json_free(monthly);
    }

    /* Fallback estimation if monthly outflows is 0 */
    double effective_monthly_outflow = monthly_outflows > 0 ? monthly_outflows : 3000.0;
    double liquidity_months = liquid_assets / effective_monthly_outflow;

    double net_savings = monthly_inflows - monthly_outflows;
    double savings_rate = monthly_inflows > 0 ? (net_savings / monthly_inflows * 100.0) : 0.0;
    if (savings_rate < 0) {
        savings_rate = 0.0;
    }

    double debt_ratio = total_assets > 0 ? (total_liabilities / total_assets * 100.0) : 0.0;
    double invest_ratio = total_assets > 0 ? (invest_assets / total_assets * 100.0) : 0.0;

    csilk_json_t* root = csilk_json_object();
    csilk_json_add_string(root, "status", "success");

    csilk_json_t* metrics = csilk_json_object();
    csilk_json_add_number(metrics, "liquidity_months", round_to_2(liquidity_months));
    csilk_json_add_string(
        metrics, "liquidity_status", liquidity_months >= 3.0 ? "adequate" : "low");
    csilk_json_add_number(metrics, "savings_rate_pct", round_to_2(savings_rate));
    csilk_json_add_number(metrics, "debt_to_asset_pct", round_to_2(debt_ratio));
    csilk_json_add_number(metrics, "invest_asset_pct", round_to_2(invest_ratio));
    csilk_json_add_object(root, "metrics", metrics);

    csilk_json_t* summary = csilk_json_object();
    csilk_json_add_number(summary, "total_assets", round_to_2(total_assets));
    csilk_json_add_number(summary, "total_liabilities", round_to_2(total_liabilities));
    csilk_json_add_number(summary, "net_worth", round_to_2(net_worth));
    csilk_json_add_number(summary, "liquid_assets", round_to_2(liquid_assets));
    csilk_json_add_number(summary, "invest_assets", round_to_2(invest_assets));
    csilk_json_add_number(summary, "monthly_inflows", round_to_2(monthly_inflows));
    csilk_json_add_number(summary, "monthly_outflows", round_to_2(monthly_outflows));
    csilk_json_add_object(root, "summary", summary);

    csilk_json_t* rules = csilk_json_object();
    csilk_json_add_string(rules, "liquidity_target", "3~6个月必要支出作为应急备用金");
    csilk_json_add_string(rules, "savings_rate_target", "月度净储蓄率建议 >= 30%");
    csilk_json_add_string(rules, "debt_safe_line", "资产负债率建议 <= 40%");
    csilk_json_add_string(rules, "invest_target", "生息投资资产占比建议保持在 40%~70%");
    csilk_json_add_object(root, "evaluation_rules", rules);

    return json_to_str(root);
}

/* ========================================================================= */
/*  Idempotent Tool Cache                                                    */
/* ========================================================================= */
/* Pure-function tools (no DB/user state) — cache result keyed by tool name  */
/* to avoid re-execution when the model calls them repeatedly.               */

typedef struct {
    char*  key; /* "tool_name" or "tool_name:arg_value" */
    char*  result;
    time_t cached_at;
} idem_cache_entry_t;

#define IDEM_CACHE_SIZE 8
static idem_cache_entry_t s_idem_cache[IDEM_CACHE_SIZE];

static char*
idem_cache_get(const char* key)
{
    time_t now = time(NULL);
    for (int i = 0; i < IDEM_CACHE_SIZE; i++) {
        if (s_idem_cache[i].key && strcmp(s_idem_cache[i].key, key) == 0 &&
            now - s_idem_cache[i].cached_at < 60) {
            return s_idem_cache[i].result;
        }
    }
    return NULL;
}

static void
idem_cache_set(const char* key, char* result)
{
    if (!key || !result) {
        return;
    }
    /* Find existing entry or evict oldest (index 0) */
    int slot = -1;
    for (int i = 0; i < IDEM_CACHE_SIZE; i++) {
        if (s_idem_cache[i].key && strcmp(s_idem_cache[i].key, key) == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        slot = 0; /* LRU-approx: always overwrite slot 0 */
        free(s_idem_cache[slot].key);
        free(s_idem_cache[slot].result);
    }
    s_idem_cache[slot].result = strdup(result); /* own the copy */
    s_idem_cache[slot].cached_at = time(NULL);
}

/* ========================================================================= */
/* ========================================================================= */
/*  Internal dispatcher (caller owns args; no parse)                       */
/* ========================================================================= */

char*
ai_tools_execute_parsed(csilk_db_pool_t* pool,
                        int64_t          user_id,
                        csilk_json_t*    args,
                        const char*      name)
{
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
    } else if (strcmp(name, "get_current_time") == 0) {
        char* cached = idem_cache_get("get_current_time");
        if (cached) {
            return strdup(cached);
        }
        result = exec_get_current_time(pool, user_id, args);
        if (result) {
            idem_cache_set("get_current_time", result);
        }
    } else if (strcmp(name, "calculate_date_range") == 0) {
        const char* range_type = arg_str(args, "range_type", "this_month");
        char        cache_key[128];
        snprintf(cache_key, sizeof(cache_key), "calculate_date_range:%s", range_type);
        char* cached = idem_cache_get(cache_key);
        if (cached) {
            return strdup(cached);
        }
        result = exec_calculate_date_range(pool, user_id, args);
        if (result) {
            idem_cache_set(cache_key, result);
        }
    } else if (strcmp(name, "calculate_compound_interest") == 0) {
        result = exec_calculate_compound_interest(pool, user_id, args);
    } else if (strcmp(name, "calculate_loan_repayment") == 0) {
        result = exec_calculate_loan_repayment(pool, user_id, args);
    } else if (strcmp(name, "web_search") == 0) {
        result = exec_web_search(pool, user_id, args);
    } else if (strcmp(name, "get_exchange_rate") == 0) {
        const char* base = arg_str(args, "base_currency", "USD");
        const char* target = arg_str(args, "target_currency", "CNY");
        char        fx_key[128];
        snprintf(fx_key, sizeof(fx_key), "fx:%s:%s", base, target);
        char* cached = idem_cache_get(fx_key);
        if (cached) {
            return strdup(cached);
        }
        result = exec_get_exchange_rate(pool, user_id, args);
        if (result) {
            idem_cache_set(fx_key, result);
        }
    } else if (strcmp(name, "propose_daily_expense") == 0) {
        result = exec_propose_daily_expense(pool, user_id, args);
    } else if (strcmp(name, "propose_transfer") == 0) {
        result = exec_propose_transfer(pool, user_id, args);
    } else if (strcmp(name, "analyze_financial_health") == 0) {
        result = exec_analyze_financial_health(pool, user_id, args);
    } else {
        result = strdup("{\"error\":\"unknown tool\"}");
    }
    return result;
}

/* ========================================================================= */
/*  Public API: ai_tools_execute (parses arguments JSON)                    */
/* ========================================================================= */

char*
ai_tools_execute(csilk_db_pool_t* pool, int64_t user_id, const char* name, const char* arguments)
{
    if (!name || !arguments) {
        return NULL;
    }
    csilk_json_t* args = csilk_json_parse(arguments);
    if (!args) {
        args = csilk_json_object();
    }
    char* result = ai_tools_execute_parsed(pool, user_id, args, name);
    csilk_json_free(args);
    return result;
}
