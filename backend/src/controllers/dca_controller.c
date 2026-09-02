/**
 * @file dca_controller.c
 * @brief 定投策略与计划执行控制器实现文件
 *
 * 遵循三层 C 架构规范，本控制器负责定投（DCA）模块的 HTTP 路由映射与调度，
 * 将请求分发至 services/dca_service.c 中实现的各项定投业务服务。
 */

#include "controllers/dca_controller.h"
#include "services/dca_service.h"

/**
 * @brief 注册定投策略模块的所有 HTTP 路由
 *
 * @details 详细端点定义与参数说明：
 *
 * 1. GET /api/dca/plans
 *    - 功能: 获取当前用户的所有定投计划列表
 *    - 鉴权: JWT (Bearer Token)
 *    - 响应: 200 OK {"code": 0, "data": [{"id": 1, "target_asset_id": 10, "funding_asset_id": 2, "amount": 1000.0, "frequency": "monthly", "status": "active", ...}]}
 *
 * 2. POST /api/dca/plans
 *    - 功能: 创建新定投计划
 *    - 鉴权: JWT (Bearer Token)
 *    - 请求体: {"target_asset_id": 10, "funding_asset_id": 2, "amount": 1000.0, "frequency": "monthly", "day_of_month": 15, "start_date": "2026-09-15"}
 *    - 响应: 200 OK {"code": 0, "data": {"id": 1, ...}}
 *
 * 3. GET /api/dca/plans/:id
 *    - 功能: 获取定投计划详情
 *    - 鉴权: JWT (Bearer Token)
 *    - 响应: 200 OK {"code": 0, "data": {...}}
 *
 * 4. PUT /api/dca/plans/:id
 *    - 功能: 更新定投计划参数
 *    - 鉴权: JWT (Bearer Token)
 *    - 请求体: {"amount": 1500.0, "frequency": "biweekly", ...}
 *    - 响应: 200 OK {"code": 0, "data": null}
 *
 * 5. PUT /api/dca/plans/:id/status
 *    - 功能: 变更定投计划状态
 *    - 鉴权: JWT (Bearer Token)
 *    - 请求体: {"status": "paused" | "active" | "completed"}
 *    - 响应: 200 OK {"code": 0, "data": null}
 *
 * 6. DELETE /api/dca/plans/:id
 *    - 功能: 删除定投计划
 *    - 鉴权: JWT (Bearer Token)
 *    - 响应: 200 OK {"code": 0, "data": null}
 *
 * 7. GET /api/dca/plans/:id/executions
 *    - 功能: 获取指定定投计划的历史执行流水
 *    - 鉴权: JWT (Bearer Token)
 *    - 响应: 200 OK {"code": 0, "data": [{"id": 101, "execution_date": "2026-08-15", "amount": 1000.0, "price": 50.2, "shares": 19.92, "status": "success"}, ...]}
 *
 * 8. GET /api/dca/executions/pending
 *    - 功能: 获取当前用户已到达执行日期待确认的定投任务
 *    - 鉴权: JWT (Bearer Token)
 *    - 响应: 200 OK {"code": 0, "data": [...]}
 *
 * 9. POST /api/dca/executions/:id/confirm
 *    - 功能: 确认定投买入并入账生成交易明细
 *    - 鉴权: JWT (Bearer Token)
 *    - 请求体: {"actual_price": 50.2, "fee": 5.0, ...}
 *    - 响应: 200 OK {"code": 0, "data": {"transaction_id": 501}}
 *
 * 10. POST /api/dca/executions/:id/skip
 *     - 功能: 跳过当期定投买入
 *     - 鉴权: JWT (Bearer Token)
 *     - 请求体: {"reason": "资金不足"}
 *     - 响应: 200 OK {"code": 0, "data": null}
 *
 * @param[in,out] app Csilk 应用实例指针 (csilk_app_t*)
 */
void
register_dca_routes(csilk_app_t* app)

{
    csilk_app_get_ext(app,
                      "/api/dca/plans",
                      dca_service_list_plans,
                      NULL,
                      NULL,
                      "List DCA plans",
                      "Get user's DCA plans");
    csilk_app_post_ext(app,
                       "/api/dca/plans",
                       dca_service_create_plan,
                       NULL,
                       NULL,
                       "Create DCA plan",
                       "Create a new DCA plan");
    csilk_app_get_ext(app,
                      "/api/dca/plans/:id",
                      dca_service_get_plan,
                      NULL,
                      NULL,
                      "Get DCA plan",
                      "Get DCA plan details");
    csilk_app_put_ext(app,
                      "/api/dca/plans/:id",
                      dca_service_update_plan,
                      NULL,
                      NULL,
                      "Update DCA plan",
                      "Update DCA plan configuration");
    csilk_app_put_ext(app,
                      "/api/dca/plans/:id/status",
                      dca_service_set_plan_status,
                      NULL,
                      NULL,
                      "Set DCA plan status",
                      "Pause, resume, or complete DCA plan");
    csilk_app_delete_ext(app,
                         "/api/dca/plans/:id",
                         dca_service_delete_plan,
                         NULL,
                         NULL,
                         "Delete DCA plan",
                         "Delete DCA plan");

    csilk_app_get_ext(app,
                      "/api/dca/plans/:id/executions",
                      dca_service_list_executions,
                      NULL,
                      NULL,
                      "List DCA executions",
                      "Get execution history for plan");
    csilk_app_get_ext(app,
                      "/api/dca/executions/pending",
                      dca_service_list_pending_executions,
                      NULL,
                      NULL,
                      "List pending executions",
                      "Get pending DCA tasks for user");
    csilk_app_post_ext(app,
                       "/api/dca/executions/:id/confirm",
                       dca_service_confirm_execution,
                       NULL,
                       NULL,
                       "Confirm DCA execution",
                       "Confirm and execute DCA buy transaction");
    csilk_app_post_ext(app,
                       "/api/dca/executions/:id/skip",
                       dca_service_skip_execution,
                       NULL,
                       NULL,
                       "Skip DCA execution",
                       "Skip DCA execution task");
}
