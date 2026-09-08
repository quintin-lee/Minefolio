/**
 * @file import_export_controller.c
 * @brief 财务数据导入与导出控制器实现 (DDD 接口层)
 */

#include "interfaces/http/controllers/import_export_controller.h"
#include "application/import_export/usecases.h"

void
register_import_export_routes(csilk_app_t* app)
{
    csilk_app_get_ext(app,
                      "/api/export/transactions",
                      import_export_usecase_transactions_export,
                      nullptr,
                      nullptr,
                      "Export transactions as CSV",
                      "Downloads all user transactions as a CSV file");
    csilk_app_post_ext(app,
                       "/api/import/transactions",
                       import_export_usecase_transactions_import,
                       nullptr,
                       nullptr,
                       "Import transactions from CSV",
                       "Imports transactions from an uploaded CSV file");
    csilk_app_get_ext(app,
                      "/api/export/daily-expenses",
                      import_export_usecase_daily_expenses_export,
                      nullptr,
                      nullptr,
                      "Export daily expenses as CSV",
                      "Downloads all daily expenses as a CSV file");
    csilk_app_post_ext(app,
                       "/api/import/daily-expenses",
                       import_export_usecase_daily_expenses_import,
                       nullptr,
                       nullptr,
                       "Import daily expenses from CSV",
                       "Imports daily expenses from an uploaded CSV file");
}
