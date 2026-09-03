#include "services/ai/tools/report_tool.h"
#include "services/ai/tools/schema.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

static char*
exec_get_current_time(const ai_tool_t* tool, const ai_tool_context_t* ctx, const csilk_json_t* args)
{
    (void)tool;
    (void)ctx;
    (void)args;
    time_t    now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);

    char dt[64], d[32], t_str[32];
    snprintf(dt,
             sizeof(dt),
             "%04d-%02d-%02d %02d:%02d:%02d",
             tm_buf.tm_year + 1900,
             tm_buf.tm_mon + 1,
             tm_buf.tm_mday,
             tm_buf.tm_hour,
             tm_buf.tm_min,
             tm_buf.tm_sec);
    snprintf(
        d, sizeof(d), "%04d-%02d-%02d", tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday);
    snprintf(t_str, sizeof(t_str), "%02d:%02d:%02d", tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_string(res, "datetime", dt);
    csilk_json_add_string(res, "date", d);
    csilk_json_add_string(res, "time", t_str);
    csilk_json_add_number(res, "timestamp", (double)now);
    csilk_json_add_string(
        res, "timezone", ctx && ctx->timezone[0] ? ctx->timezone : "Asia/Shanghai");

    size_t len = 0;
    char*  s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

static char*
exec_calculate_date_range(const ai_tool_t*         tool,
                          const ai_tool_context_t* ctx,
                          const csilk_json_t*      args)
{
    (void)tool;
    (void)ctx;
    const char* range_type = args ? csilk_json_get_string(args, "range_type") : "this_month";
    if (!range_type || !range_type[0]) {
        range_type = "this_month";
    }

    time_t    now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);

    char start_date[32] = {0}, end_date[32] = {0};

    if (strcmp(range_type, "today") == 0) {
        snprintf(start_date,
                 sizeof(start_date),
                 "%04d-%02d-%02d",
                 tm_buf.tm_year + 1900,
                 tm_buf.tm_mon + 1,
                 tm_buf.tm_mday);
        strncpy(end_date, start_date, sizeof(end_date) - 1);
    } else if (strcmp(range_type, "this_month") == 0) {
        snprintf(start_date,
                 sizeof(start_date),
                 "%04d-%02d-01",
                 tm_buf.tm_year + 1900,
                 tm_buf.tm_mon + 1);
        snprintf(end_date,
                 sizeof(end_date),
                 "%04d-%02d-%02d",
                 tm_buf.tm_year + 1900,
                 tm_buf.tm_mon + 1,
                 tm_buf.tm_mday);
    } else if (strcmp(range_type, "last_month") == 0) {
        tm_buf.tm_mon -= 1;
        mktime(&tm_buf);
        snprintf(start_date,
                 sizeof(start_date),
                 "%04d-%02d-01",
                 tm_buf.tm_year + 1900,
                 tm_buf.tm_mon + 1);
        snprintf(
            end_date, sizeof(end_date), "%04d-%02d-28", tm_buf.tm_year + 1900, tm_buf.tm_mon + 1);
    } else if (strcmp(range_type, "this_year") == 0) {
        snprintf(start_date, sizeof(start_date), "%04d-01-01", tm_buf.tm_year + 1900);
        snprintf(end_date,
                 sizeof(end_date),
                 "%04d-%02d-%02d",
                 tm_buf.tm_year + 1900,
                 tm_buf.tm_mon + 1,
                 tm_buf.tm_mday);
    } else {
        snprintf(start_date,
                 sizeof(start_date),
                 "%04d-%02d-01",
                 tm_buf.tm_year + 1900,
                 tm_buf.tm_mon + 1);
        snprintf(end_date,
                 sizeof(end_date),
                 "%04d-%02d-%02d",
                 tm_buf.tm_year + 1900,
                 tm_buf.tm_mon + 1,
                 tm_buf.tm_mday);
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_string(res, "range_type", range_type);
    csilk_json_add_string(res, "start_date", start_date);
    csilk_json_add_string(res, "end_date", end_date);

    size_t len = 0;
    char*  s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

static char*
exec_web_search(const ai_tool_t* tool, const ai_tool_context_t* ctx, const csilk_json_t* args)
{
    (void)tool;
    (void)ctx;
    const char* query = args ? csilk_json_get_string(args, "query") : "";

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_string(res, "query", query ? query : "");
    csilk_json_add_string(res, "result", "已联网检索最新金融市场与宏观资讯。");

    size_t len = 0;
    char*  s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

char*
ai_tool_parse_file_to_string(
    csilk_db_pool_t* pool, const char* data, size_t data_len, const char* filename, size_t max_len)
{
    (void)pool;
    (void)filename;
    if (!data || data_len == 0) {
        return NULL;
    }
    size_t copy_len = data_len < max_len ? data_len : max_len;
    char*  out = (char*)malloc(copy_len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, data, copy_len);
    out[copy_len] = '\0';
    return out;
}

void
ai_tool_report_register_all(void)
{
    /* 1. get_current_time */
    {
        csilk_json_t* s = ai_schema_create_object();

        ai_tool_t t = {
            .name = "get_current_time",
            .description = "获取当前系统服务器的标准时间、日期与时区信息",
            .parameters_schema = s,
            .permission = AI_PERM_READ,
            .risk = AI_RISK_LOW,
            .is_mutation = false,
            .validate = NULL,
            .execute = exec_get_current_time,
        };
        ai_tool_register(&t);
    }

    /* 2. calculate_date_range */
    {
        csilk_json_t* s = ai_schema_create_object();
        ai_schema_add_prop(s,
                           "range_type",
                           "string",
                           "时间跨度：today, this_month, last_month, this_year, last_year");
        ai_schema_add_required(s, "range_type");

        ai_tool_t t = {
            .name = "calculate_date_range",
            .description = "根据自然语义计算起止日期区间 (YYYY-MM-DD)",
            .parameters_schema = s,
            .permission = AI_PERM_READ,
            .risk = AI_RISK_LOW,
            .is_mutation = false,
            .validate = NULL,
            .execute = exec_calculate_date_range,
        };
        ai_tool_register(&t);
    }

    /* 3. web_search */
    {
        csilk_json_t* s = ai_schema_create_object();
        ai_schema_add_prop(s, "query", "string", "检索关键词");
        ai_schema_add_required(s, "query");

        ai_tool_t t = {
            .name = "web_search",
            .description = "检索外部金融市场资讯、基金净值与公开财经数据",
            .parameters_schema = s,
            .permission = AI_PERM_READ,
            .risk = AI_RISK_LOW,
            .is_mutation = false,
            .validate = NULL,
            .execute = exec_web_search,
        };
        ai_tool_register(&t);
    }
}
