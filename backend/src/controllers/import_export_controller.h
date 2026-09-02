/**
 * @file import_export_controller.h
 * @brief 财务数据导入与导出控制器头文件
 *
 * 声明交易记录（Transactions）与日常记账（Daily Expenses）数据的
 * CSV 文件格式导出与批量解析导入相关的 HTTP 端点和路由注册函数。
 */

#pragma once
#include "csilk/csilk.h"

/**
 * @brief 导出交易记录为 CSV 格式文件
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/export/transactions
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          查询参数 (Query Parameters):
 *          - start_date: 起始日期 (string, 可选, "YYYY-MM-DD")
 *          - end_date: 截止日期 (string, 可选, "YYYY-MM-DD")
 *          - ledger_id: 账本 ID (int64, 可选)
 *          返回包格式:
 *          - 200 OK: 响应体为 CSV 二进制/文本流 (Content-Type: text/csv; charset=utf-8)
 *            Content-Disposition: attachment; filename="transactions_YYYYMMDD.csv"
 *          - 401 Unauthorized: 未登录 (code: 1001)
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void transactions_export_csv(csilk_ctx_t* c);

/**
 * @brief 从 CSV 文件批量导入交易记录
 *
 * @details HTTP 方法: POST
 *          REST 路径: /api/import/transactions
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          请求体:
 *          - Multipart 表单或原始 CSV 文本内容 (text/csv / application/octet-stream)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"imported": 25, "skipped": 2, "errors": []}}
 *          - 400 Bad Request: CSV 格式错误或关键列缺失 (code: 1002)
 *
 *          导入过程将逐行解析交易类型、发生日期、标的资产、金额、手续费及备注，并应用资产与持仓联动。
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void transactions_import_csv(csilk_ctx_t* c);

/**
 * @brief 导出日常收支记账明细为 CSV 文件
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/export/daily-expenses
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          查询参数 (Query Parameters):
 *          - start_date: 起始日期 (string, 可选)
 *          - end_date: 截止日期 (string, 可选)
 *          - ledger_id: 账本 ID (int64, 可选)
 *          返回包格式:
 *          - 200 OK: CSV 文本流 (Content-Type: text/csv; charset=utf-8)
 *            Content-Disposition: attachment; filename="daily_expenses_YYYYMMDD.csv"
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void daily_expenses_export_csv(csilk_ctx_t* c);

/**
 * @brief 从 CSV 文件批量导入日常收支记账
 *
 * @details HTTP 方法: POST
 *          REST 路径: /api/import/daily-expenses
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          请求体:
 *          - CSV 文件内容或 Multipart 数据
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"imported": 50, "failed": 0}}
 *          - 400 Bad Request: 文件内容解析失败 (code: 1002)
 *
 *          配合导入匹配规则（Import Rules）可自动根据商户名称或交易对手推断分类与标签。
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void daily_expenses_import_csv(csilk_ctx_t* c);

#include "csilk/app/app.h"

/**
 * @brief 注册数据导入与导出相关的所有 HTTP 路由
 *
 * @details 注册交易与日常收支的 CSV 文件导出及批量导入端点。
 *
 * @param[in,out] app Csilk 应用实例指针 (csilk_app_t*)
 */
void register_import_export_routes(csilk_app_t* app);
