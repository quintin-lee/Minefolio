/**
 * @file usecases.c
 * @brief 导入导出用例实现 (Application Layer)
 *
 * 用例层将跨域依赖隔离，委托给现有的 service 函数执行。
 */

#include "application/import_export/usecases.h"
#include "services/export_service.h"
#include "services/import_service.h"

void
import_export_usecase_transactions_export(csilk_ctx_t* c)
{
    transactions_export_csv(c);
}

void
import_export_usecase_transactions_import(csilk_ctx_t* c)
{
    transactions_import_csv(c);
}

void
import_export_usecase_daily_expenses_export(csilk_ctx_t* c)
{
    daily_expenses_export_csv(c);
}

void
import_export_usecase_daily_expenses_import(csilk_ctx_t* c)
{
    daily_expenses_import_csv(c);
}
