/**
 * @file cashflow_controller.c
 * @brief 被动现金流计划与日历预测控制器实现文件
 *
 * 遵循三层 C 架构规范，本控制器负责将 HTTP 请求路由映射并分发至
 * services/cashflow_service.c 中实现的各项现金流业务逻辑服务。
 */

#include "controllers/cashflow_controller.h"
#include "services/cashflow_service.h"

/**
 * @brief 注册现金流管理与日历模块的所有 HTTP 路由
 *
 * @details 详细端点定义与参数说明：
 *
 * 1. GET /api/cashflow/schedules
 *    - 功能: 获取用户的被动收入现金流计划列表
 *    - 鉴权: JWT (Bearer Token)
 *    - 响应: 200 OK {"code": 0, "data": [{"id": 1, "title": "招行理财付息", "asset_id": 2, "category_id": 3, "frequency": "monthly", "expected_amount": 500.0, "next_date": "2026-10-01", ...}]}
 *
 * 2. POST /api/cashflow/schedules
 *    - 功能: 创建新现金流计划
 *    - 鉴权: JWT (Bearer Token)
 *    - 请求体: {"title": "房屋租金", "asset_id": 1, "category_id": 2, "frequency": "monthly", "expected_amount": 3500.0, "start_date": "2026-09-01", "end_date": "2027-09-01", "day_of_month": 5}
 *    - 响应: 200 OK {"code": 0, "data": {"id": 1, ...}}
 *
 * 3. GET /api/cashflow/schedules/:id
 *    - 功能: 获取单个现金流计划详情
 *    - 鉴权: JWT (Bearer Token)
 *    - 响应: 200 OK {"code": 0, "data": {...}}
 *
 * 4. PUT /api/cashflow/schedules/:id
 *    - 功能: 更新现金流计划配置
 *    - 鉴权: JWT (Bearer Token)
 *    - 请求体: {"title": "...", "expected_amount": 3800.0, ...}
 *    - 响应: 200 OK {"code": 0, "data": null}
 *
 * 5. DELETE /api/cashflow/schedules/:id
 *    - 功能: 删除现金流计划
 *    - 鉴权: JWT (Bearer Token)
 *    - 响应: 200 OK {"code": 0, "data": null}
 *
 * 6. GET /api/cashflow/calendar
 *    - 功能: 获取指定年月的现金流日历事件预测与汇总总额
 *    - 鉴权: JWT (Bearer Token)
 *    - 查询参数: month (如 "2026-09")
 *    - 响应: 200 OK {"code": 0, "data": {"total_expected": 8200.0, "total_confirmed": 3500.0, "events": [{"date": "2026-09-05", "schedule_id": 1, "title": "房屋租金", "amount": 3500.0, "status": "confirmed"}, ...]}}
 *
 * 7. POST /api/cashflow/confirm
 *    - 功能: 确认现金流收入实际到账并自动入账生成交易明细
 *    - 鉴权: JWT (Bearer Token)
 *    - 请求体: {"schedule_id": 1, "asset_id": 1, "actual_amount": 3500.0, "received_date": "2026-09-05", "note": "9月房租到账"}
 *    - 响应: 200 OK {"code": 0, "data": {"transaction_id": 1001}}
 *
 * @param[in,out] app Csilk 应用实例指针 (csilk_app_t*)
 */
void
register_cashflow_routes(csilk_app_t* app)
{
    csilk_app_get_ext(app,
                      "/api/cashflow/schedules",
                      cashflow_service_list_schedules,
                      NULL,
                      NULL,
                      "List cashflow schedules",
                      "Get user's cashflow schedules");
    csilk_app_post_ext(app,
                       "/api/cashflow/schedules",
                       cashflow_service_create_schedule,
                       NULL,
                       NULL,
                       "Create cashflow schedule",
                       "Create a new cashflow schedule");
    csilk_app_get_ext(app,
                      "/api/cashflow/schedules/:id",
                      cashflow_service_get_schedule,
                      NULL,
                      NULL,
                      "Get cashflow schedule",
                      "Get cashflow schedule details");
    csilk_app_put_ext(app,
                      "/api/cashflow/schedules/:id",
                      cashflow_service_update_schedule,
                      NULL,
                      NULL,
                      "Update cashflow schedule",
                      "Update cashflow schedule configuration");
    csilk_app_delete_ext(app,
                         "/api/cashflow/schedules/:id",
                         cashflow_service_delete_schedule,
                         NULL,
                         NULL,
                         "Delete cashflow schedule",
                         "Delete cashflow schedule");

    csilk_app_get_ext(app,
                      "/api/cashflow/calendar",
                      cashflow_service_get_calendar,
                      NULL,
                      NULL,
                      "Get cashflow calendar",
                      "Get monthly cashflow events and summary");
    csilk_app_post_ext(app,
                       "/api/cashflow/confirm",
                       cashflow_service_confirm,
                       NULL,
                       NULL,
                       "Confirm cashflow income",
                       "Confirm passive income and create transaction");
}
