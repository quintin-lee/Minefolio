/**
 * @file transfer_controller.h
 * @brief 账户间资金划转控制器头文件
 *
 * 声明账户间资金调拨/划转的 HTTP 处理函数和路由注册函数。
 * 支持在不同资产账户（如银行卡、现金钱包、证券账户等）之间进行原子转账，
 * 并双向联动更新转出方与转入方的账户余额及交易流水。
 */

#pragma once
#include "csilk/csilk.h"

/**
 * @brief 创建账户间资金划转记录
 *
 * @details HTTP 方法: POST
 *          REST 路径: /api/transfers
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          请求体 (JSON):
 *          - from_asset_id: 转出资产账户 ID (int64, 必填)
 *          - to_asset_id: 转入资产账户 ID (int64, 必填, 不能与 from_asset_id 相同)
 *          - amount: 划转金额 (double, 必填, 需 > 0)
 *          - transfer_date / transaction_date: 划转发生日期 (string, 必填, 格式 "YYYY-MM-DD")
 *          - currency: 币种 (string, 可选, 默认 "CNY")
 *          - note: 划转备注说明 (string, 可选)
 *          - ledger_id: 账本 ID (int64, 可选, 需具备 editor 权限)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": null}
 *          - 400 Bad Request: 参数错误、账户相同或余额更新失败 (code: 1002)
 *          - 404 Not Found: 转出或转入账户不存在 (code: 1003)
 *          - 500 Internal Error: 数据库事务提交失败 (code: 500)
 *
 *          事务内原子执行：
 *          1. 在 transfers 表中记录转账主体；
 *          2. 在 transactions 表中生成 transfer_out（转出支出）流水；
 *          3. 在 transactions 表中生成 transfer_in（转入收入）流水；
 *          4. 调用 balance_apply_delta 同时扣减转出账户、增加转入账户余额。
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void transfers_create(csilk_ctx_t* c);

#include "csilk/app/app.h"

/**
 * @brief 注册资金划转模块的相关 HTTP 路由
 *
 * @details 将 /api/transfers 创建端点注册至 Csilk 应用实例。
 *
 * @param[in,out] app Csilk 应用实例指针 (csilk_app_t*)
 */
void register_transfer_routes(csilk_app_t* app);
