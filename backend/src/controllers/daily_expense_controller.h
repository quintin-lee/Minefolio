/**
 * @file daily_expense_controller.h
 * @brief 日常收支记账控制器头文件
 *
 * 声明日常消费与收入记账的增删改查、分页检索、标签关联、月度统计以及
 * 资产账户余额联动更新相关的 HTTP 端点和路由注册函数。
 */

#pragma once
#include "csilk/csilk.h"

/**
 * @brief 分页查询日常记账明细列表
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/daily-expenses
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          查询参数 (Query Parameters):
 *          - page: 页码 (int, 可选, 默认 1)
 *          - page_size: 每页条数 (int, 可选, 默认 20)
 *          - type: 收支类型 (string, 可选, "expense" | "income")
 *          - category_id: 分类 ID (int64, 可选)
 *          - asset_id: 关联资金账户 ID (int64, 可选)
 *          - tag_id: 关联标签 ID (int64, 可选)
 *          - start_date: 起始日期 (string, 可选, 格式 "YYYY-MM-DD")
 *          - end_date: 截止日期 (string, 可选, 格式 "YYYY-MM-DD")
 *          - keyword: 备注关键字模糊搜索 (string, 可选)
 *          - ledger_id: 账本 ID (int64, 可选)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"items": [...], "total": 100, "page": 1, "page_size": 20}}
 *          - 401 Unauthorized: 未登录 (code: 1001)
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void daily_expenses_list(csilk_ctx_t* c);

/**
 * @brief 创建日常记账记录
 *
 * @details HTTP 方法: POST
 *          REST 路径: /api/daily-expenses
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          请求体 (JSON):
 *          - amount: 记账金额 (double, 必填, 需 > 0)
 *          - type: 收支大类 (string, 必填, "expense" | "income")
 *          - category_id: 分类 ID (int64, 必填)
 *          - asset_id: 扣款/入账资产账户 ID (int64, 必填)
 *          - expense_date / date: 交易发生日期 (string, 必填, 格式 "YYYY-MM-DD")
 *          - note: 备注说明 (string, 可选)
 *          - tags: 标签 ID 数组 (array of int64, 可选, 如 [1, 2])
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"id": 1, ...}}
 *          - 400 Bad Request: 必填字段缺失或账户不存在 (code: 1002)
 *
 *          在事务中执行记账记录插入、标签关联绑定，并通过 balance_apply_delta 自动扣减/增加关联账户余额。
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void daily_expenses_create(csilk_ctx_t* c);

/**
 * @brief 更新指定日常记账记录
 *
 * @details HTTP 方法: PUT
 *          REST 路径: /api/daily-expenses/:id
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          路径参数 (Path Parameters):
 *          - id: 记账记录 ID (int64)
 *          请求体 (JSON):
 *          - amount: 新金额 (double, 可选)
 *          - type: 收支类型 (string, 可选)
 *          - category_id: 新分类 ID (int64, 可选)
 *          - asset_id: 新资产账户 ID (int64, 可选)
 *          - expense_date / date: 新交易日期 (string, 可选)
 *          - note: 新备注 (string, 可选)
 *          - tags: 更新后的标签 ID 列表 (array of int64, 可选)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": null}
 *          - 400 Bad Request: 数据错误或余额更新失败 (code: 1002)
 *          - 404 Not Found: 记录不存在 (code: 1003)
 *
 *          更新时会自动回滚原账户历史金额变动，并将新差额重新计入目标账户余额。
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void daily_expenses_update(csilk_ctx_t* c);

/**
 * @brief 删除日常记账记录
 *
 * @details HTTP 方法: DELETE
 *          REST 路径: /api/daily-expenses/:id
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          路径参数 (Path Parameters):
 *          - id: 待删除记录 ID (int64)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": null}
 *          - 404 Not Found: 记录不存在 (code: 1003)
 *
 *          删除记录时将级联解除标签关联，并自动回滚该笔交易对关联资产账户余额造成的影响。
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void daily_expenses_delete(csilk_ctx_t* c);

/**
 * @brief 按月份聚合日常支出与收入统计
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/daily-expenses/monthly
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          查询参数 (Query Parameters):
 *          - year: 查询年份 (int, 可选, 默认当前年份)
 *          - ledger_id: 账本 ID (int64, 可选)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": [{"month": "2026-01", "expense": 1234.5, "income": 5678.0}, ...]}
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void daily_expenses_monthly(csilk_ctx_t* c);

#include "csilk/app/app.h"

/**
 * @brief 注册日常记账模块的所有 HTTP 路由
 *
 * @details 将日常收支增删改查、列表查询及月度汇总接口注册至 Csilk 应用实例。
 *
 * @param[in,out] app Csilk 应用实例指针 (csilk_app_t*)
 */
void register_daily_expense_routes(csilk_app_t* app);
