#include "services/daily_expense_query.h"
#include "repositories/daily_expense_repo.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include "csilk/csilk.h"
#include <string.h>
#include <stdlib.h>

void
daily_expenses_list(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    int64_t page = 1, page_size = 20;
    parse_page_params(c, &page, &page_size);

    csilk_db_pool_t* pool = db_get_pool();
    const char*      type = csilk_get_query(c, "expense_type");
    const char*      cat_id = csilk_get_query(c, "category_id");
    const char*      tag_ids = csilk_get_query(c, "tag_ids");
    const char*      start = csilk_get_query(c, "start_date");
    const char*      end = csilk_get_query(c, "end_date");

    int64_t       total = 0;
    csilk_json_t* result =
        de_list(pool, user_id, page, page_size, type, cat_id, tag_ids, start, end, &total);
    if (!result) {
        respond_error(c, 500, "查询失败");
        return;
    }
    respond_page_ok(c, result, total, page, page_size);
}

void
daily_expenses_monthly(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* year_str = csilk_get_query(c, "year");
    const char* month_str = csilk_get_query(c, "month");
    if (!year_str || !month_str) {
        respond_bad_request(c, "year 和 month 参数为必填");
        return;
    }

    char date_pattern[32];
    snprintf(date_pattern, sizeof(date_pattern), "%s-%02d-%%", year_str, atoi(month_str));

    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    totals = de_monthly_totals(pool, user_id, date_pattern);
    double           income = 0, expense = 0;
    if (totals && csilk_json_array_size(totals) > 0) {
        const csilk_json_t* tr = csilk_json_array_get(totals, 0);
        income = db_get_num(tr, "total_income");
        expense = db_get_num(tr, "total_expense");
    }
    if (totals) {
        csilk_json_free(totals);
    }

    csilk_json_t* by_cat = de_monthly_by_category(pool, user_id, date_pattern);
    csilk_json_t* by_tag = de_monthly_by_tag(pool, user_id, date_pattern);
    csilk_json_t* daily = de_monthly_daily(pool, user_id, date_pattern);

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "year", atoll(year_str));
    csilk_json_add_number(resp, "month", atoll(month_str));
    csilk_json_add_number(resp, "total_income", income);
    csilk_json_add_number(resp, "total_expense", expense);
    csilk_json_add_number(resp, "balance", income - expense);
    csilk_json_add_array(resp, "by_category", by_cat ? by_cat : csilk_json_array());
    csilk_json_add_array(resp, "by_tag", by_tag ? by_tag : csilk_json_array());
    csilk_json_add_array(resp, "daily_breakdown", daily ? daily : csilk_json_array());
    respond_ok(c, resp);
}
