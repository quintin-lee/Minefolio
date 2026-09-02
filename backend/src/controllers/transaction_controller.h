/**
 * @file transaction_controller.h
 * @brief 综合交易明细与投资交易流水控制器头文件
 *
 * 声明涵盖普通收支、账户资金划转以及证券/基金/加密货币买入卖出交易的
 * 增删改查、持仓成本联动（Position Linkage）、手续费子记录管理（Fee Children Rollback）
 * 与月度汇总统计相关的 HTTP 端点和路由注册函数。
 */

#pragma once
#include "csilk/csilk.h"

/**
 * @brief 分页查询交易明细记录
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/transactions
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          查询参数 (Query Parameters):
 *          - page: 页码 (int, 可选, 默认 1)
 *          - page_size: 每页条数 (int, 可选, 默认 20)
 *          - asset_id: 资产账户 ID (int64, 可选)
 *          - source_type: 交易大类 (string, 可选, "expense" | "income" | "transfer")
 *          - transaction_type: 交易具体类型 (string, 可选, 如 "buy", "sell", "dividend", "fee" 等)
 *          - start_date: 起始日期 (string, 可选, "YYYY-MM-DD")
 *          - end_date: 截止日期 (string, 可选, "YYYY-MM-DD")
 *          - keyword: 备注模糊搜索关键字 (string, 可选)
 *          - ledger_id: 账本 ID (int64, 可选)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"items": [...], "total": 42, "page": 1, "page_size": 20}}
 *          - 401 Unauthorized: 未登录 (code: 1001)
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void transactions_list(csilk_ctx_t* c);

/**
 * @brief 按月份汇总统计交易金额
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/transactions/monthly
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          查询参数 (Query Parameters):
 *          - year: 年份 (int, 可选, 默认当前年份)
 *          - ledger_id: 账本 ID (int64, 可选)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": [{"month": "2026-01", "expense": 100.0, "income": 200.0}, ...]}
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void transactions_monthly(csilk_ctx_t* c);

/**
 * @brief 创建新交易记录（包含投资买入/卖出/分红与手续费联动）
 *
 * @details HTTP 方法: POST
 *          REST 路径: /api/transactions
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          请求体 (JSON):
 *          - asset_id: 标的资产 ID (int64, 必填)
 *          - transaction_type: 交易类型 (string, 必填, "expense"|"income"|"buy"|"sell"|"dividend" 等)
 *          - amount: 总交易金额 (double, 必填, 需 > 0)
 *          - quantity: 交易份额/股数 (double, 投资类交易必填)
 *          - price_per_unit: 成交单价 (double, 投资类交易可选)
 *          - fee: 交易手续费 (double, 可选, 若 > 0 自动拆分子手续费记录)
 *          - fee_asset_id: 扣除手续费的资金账户 ID (int64, 可选)
 *          - funding_asset_id: 买入扣款或卖出回款的关联资金账户 ID (int64, 可选)
 *          - transaction_date: 交易发生日期 (string, 必填, "YYYY-MM-DD")
 *          - note: 备注说明 (string, 可选)
 *          - tags: 标签 ID 数组 (array of int64, 可选)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"id": 1, ...}}
 *          - 400 Bad Request: 参数错误或余额不足 (code: 1002)
 *
 *          业务联动逻辑：
 *          1. 写入 transactions 主表记录；
 *          2. 若为投资品买入/卖出，调用 apply_position 自动更新标的持仓数量与成本基准；
 *          3. 调用 balance_apply_delta 扣划或增加 funding_asset 资金账户余额；
 *          4. 若含有独立手续费，自动生成带 parent_tx_id 关联的手续费子记录并扣减手续费账户。
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void transactions_create(csilk_ctx_t* c);

/**
 * @brief 更新指定交易记录
 *
 * @details HTTP 方法: PUT
 *          REST 路径: /api/transactions/:id
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          路径参数 (Path Parameters):
 *          - id: 待更新交易 ID (int64)
 *          请求体 (JSON):
 *          - amount: 新总金额 (double, 可选)
 *          - quantity: 新份额 (double, 可选)
 *          - price_per_unit: 新单价 (double, 可选)
 *          - transaction_date: 新交易日期 (string, 可选)
 *          - note: 新备注 (string, 可选)
 *          - tags: 新标签列表 (array of int64, 可选)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": null}
 *          - 400 Bad Request: 数据错误或更新失败 (code: 1002)
 *          - 404 Not Found: 记录不存在 (code: 1003)
 *
 *          在事务中先完整回滚旧交易对持仓与资金账户的所有影响，再重新应用新交易参数。
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void transactions_update(csilk_ctx_t* c);

/**
 * @brief 删除指定交易记录（含手续费子记录回滚）
 *
 * @details HTTP 方法: DELETE
 *          REST 路径: /api/transactions/:id
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          路径参数 (Path Parameters):
 *          - id: 待删除交易 ID (int64)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": null}
 *          - 404 Not Found: 记录不存在 (code: 1003)
 *
 *          严格执行手续费回滚规范：
 *          1. 查询所有带有 parent_tx_id 的手续费子记录；
 *          2. 通过 balance_apply_delta 逆向回滚手续费扣除金额；
 *          3. 删除所有手续费子记录；
 *          4. 逆向回滚主交易的持仓变动与资金账户差额；
 *          5. 删除主交易记录。
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void transactions_delete(csilk_ctx_t* c);

#include "csilk/app/app.h"

/**
 * @brief 注册交易明细管理模块的所有 HTTP 路由
 *
 * @details 将交易列表检索、创建、更新、删除及月度汇总接口注册至 Csilk 应用实例。
 *
 * @param[in,out] app Csilk 应用实例指针 (csilk_app_t*)
 */
void register_transaction_routes(csilk_app_t* app);
