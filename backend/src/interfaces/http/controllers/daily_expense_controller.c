/**
 * @file daily_expense_controller.c
 * @brief 日常收支记账控制器实现 (DDD 接口层)
 */

#include "interfaces/http/controllers/daily_expense_controller.h"
#include "application/daily_expense/usecases.h"
#include "application/daily_expense/commands.h"
#include "domain/daily_expense/entity.h"
#include "infrastructure/repositories/daily_expense_repo_impl.h"
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

    const char* type = csilk_get_query(c, "expense_type");
    const char* cat_id = csilk_get_query(c, "category_id");
    const char* tag_ids = csilk_get_query(c, "tag_ids");
    const char* start = csilk_get_query(c, "start_date");
    const char* end = csilk_get_query(c, "end_date");

    int64_t       total = 0;
    csilk_json_t* result = NULL;
    int           rc = daily_expense_usecase_list(db_get_pool(),
                                                  user_id,
                                                  page,
                                                  page_size,
                                                  type,
                                                  cat_id,
                                                  tag_ids,
                                                  start,
                                                  end,
                                                  &result,
                                                  &total);
    if (rc != 0 || !result) {
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

    daily_expense_monthly_result_t res = {0};
    csilk_json_t*                  by_cat = NULL;
    csilk_json_t*                  by_tag = NULL;
    csilk_json_t*                  daily = NULL;

    int rc = daily_expense_usecase_monthly(
        db_get_pool(), user_id, atoll(year_str), atoll(month_str), &res, &by_cat, &by_tag, &daily);
    if (rc != 0) {
        respond_error(c, 500, "查询失败");
        return;
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "year", atoll(year_str));
    csilk_json_add_number(resp, "month", atoll(month_str));
    csilk_json_add_number(resp, "total_income", res.total_income);
    csilk_json_add_number(resp, "total_expense", res.total_expense);
    csilk_json_add_number(resp, "balance", res.balance);
    csilk_json_add_array(resp, "by_category", by_cat ? by_cat : csilk_json_array());
    csilk_json_add_array(resp, "by_tag", by_tag ? by_tag : csilk_json_array());
    csilk_json_add_array(resp, "daily_breakdown", daily ? daily : csilk_json_array());
    respond_ok(c, resp);
}

void
daily_expenses_create(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    int64_t ledger_id = ctx_ledger_id(c, user_id, "editor");
    if (ledger_id < 0) {
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    create_daily_expense_cmd_t cmd = {
        .user_id = user_id,
        .category_id = db_get_int(body, "category_id"),
        .asset_id = db_get_int(body, "asset_id"),
        .expense_type = csilk_json_get_string(body, "expense_type"),
        .amount = db_get_num(body, "amount"),
        .currency = csilk_json_get_string(body, "currency"),
        .expense_date = csilk_json_get_string(body, "expense_date"),
        .note = csilk_json_get_string(body, "note"),
    };

    const csilk_json_t* tags = csilk_json_get(body, "tags");

    daily_expense_usecase_result_t res = {0};
    int rc = daily_expense_usecase_create(db_get_pool(), &cmd, tags, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        respond_ok_null(c);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message[0] ? res.message : "创建失败");
    }
}

void
daily_expenses_update(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    int64_t ledger_id = ctx_ledger_id(c, user_id, "editor");
    if (ledger_id < 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) {
        respond_bad_request(c, "缺少 id");
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    update_daily_expense_cmd_t cmd = {
        .user_id = user_id,
        .id = atoll(id_str),
        .category_id = db_get_int(body, "category_id"),
        .asset_id = db_get_int(body, "asset_id"),
        .expense_type = csilk_json_get_string(body, "expense_type"),
        .amount = db_get_num(body, "amount"),
        .currency = csilk_json_get_string(body, "currency"),
        .expense_date = csilk_json_get_string(body, "expense_date"),
        .note = csilk_json_get_string(body, "note"),
    };

    const csilk_json_t* tags = csilk_json_get(body, "tags");

    daily_expense_usecase_result_t res = {0};
    int rc = daily_expense_usecase_update(db_get_pool(), &cmd, tags, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        respond_ok_null(c);
    } else if (res.code == 1003) {
        respond_not_found(c);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message[0] ? res.message : "更新失败");
    }
}

void
daily_expenses_delete(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    int64_t ledger_id = ctx_ledger_id(c, user_id, "editor");
    if (ledger_id < 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) {
        respond_bad_request(c, "缺少 id");
        return;
    }

    daily_expense_usecase_result_t res = {0};
    int rc = daily_expense_usecase_delete(db_get_pool(), user_id, atoll(id_str), &res);

    if (rc == 0 && res.code == 0) {
        respond_ok_null(c);
    } else if (res.code == 1003) {
        respond_not_found(c);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message[0] ? res.message : "删除失败");
    }
}

void
register_daily_expense_routes(csilk_app_t* app)
{
    csilk_app_get_ext(app,
                      "/api/daily-expenses",
                      daily_expenses_list,
                      nullptr,
                      "daily_expense_resp_t",
                      "List daily expenses",
                      "Returns paginated list of daily expense records");
    csilk_app_post_ext(app,
                       "/api/daily-expenses",
                       daily_expenses_create,
                       "daily_expense_req_t",
                       "daily_expense_resp_t",
                       "Create daily expense",
                       "Create a new daily expense entry with optional tags");
    csilk_app_put_ext(app,
                      "/api/daily-expenses/:id",
                      daily_expenses_update,
                      "daily_expense_req_t",
                      "daily_expense_resp_t",
                      "Update daily expense",
                      "Update an existing daily expense record by ID");
    csilk_app_delete_ext(app,
                         "/api/daily-expenses/:id",
                         daily_expenses_delete,
                         nullptr,
                         nullptr,
                         "Delete daily expense",
                         "Delete a daily expense record by ID");
    csilk_app_get_ext(app,
                      "/api/daily-expenses/monthly",
                      daily_expenses_monthly,
                      nullptr,
                      nullptr,
                      "Monthly daily expenses",
                      "Returns monthly aggregated daily expense data");
}
