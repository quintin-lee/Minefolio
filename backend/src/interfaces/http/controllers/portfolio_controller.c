#include "interfaces/http/controllers/portfolio_controller.h"
#include "application/portfolio/usecases.h"
#include "common/response.h"
#include "common/jwt.h"
#include "common/ctx.h"
#include "common/db.h"

void
api_portfolio_holdings(csilk_ctx_t* c)
{
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) {
        respond_unauthorized(c);
        return;
    }

    query_portfolio_cmd_t cmd = {
        .user_id = user_id,
        .base_currency = "CNY",
    };

    csilk_json_t*              resp = NULL;
    portfolio_usecase_result_t res = {0};
    int rc = portfolio_usecase_get_holdings(db_get_pool(), &cmd, &resp, &res);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, resp);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message[0] ? res.message : "查询失败");
    }
}

void
api_portfolio_performance(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    query_portfolio_cmd_t cmd = {
        .user_id = user_id,
        .base_currency = "CNY",
    };

    csilk_json_t*              resp = NULL;
    portfolio_usecase_result_t res = {0};
    int rc = portfolio_usecase_get_performance(db_get_pool(), &cmd, &resp, &res);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, resp);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message[0] ? res.message : "查询失败");
    }
}

void
api_portfolio_dashboard_summary(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    query_portfolio_cmd_t cmd = {
        .user_id = user_id,
        .base_currency = "CNY",
    };

    csilk_json_t*              resp = NULL;
    portfolio_usecase_result_t res = {0};
    int rc = portfolio_usecase_get_dashboard_summary(db_get_pool(), &cmd, &resp, &res);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, resp);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message[0] ? res.message : "查询失败");
    }
}

void
register_portfolio_routes(csilk_app_t* app)
{
    csilk_app_get_ext(app,
                      "/api/reports/holdings",
                      api_portfolio_holdings,
                      NULL,
                      NULL,
                      "Portfolio holdings",
                      "Returns current portfolio holdings for investment assets");
    csilk_app_get_ext(app,
                      "/api/reports/transaction/performance",
                      api_portfolio_performance,
                      NULL,
                      NULL,
                      "Transaction performance",
                      "Returns investment transaction performance/PnL report");
    csilk_app_get_ext(
        app,
        "/api/summary",
        api_portfolio_dashboard_summary,
        NULL,
        NULL,
        "Dashboard summary",
        "Returns dashboard aggregate: net worth, category breakdown, recent transactions");
}
