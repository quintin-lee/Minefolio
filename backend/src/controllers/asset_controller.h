/**
 * @file asset_controller.h
 * @brief 资产管理与持仓流水控制器头文件
 *
 * 声明资产（现金账户、银行卡、证券、基金、加密货币、借贷与固定资产等）的增删改查、
 * 详情查询、持仓净值调整以及余额变动日志（Asset Balance Logs）等 HTTP 端点和路由注册函数。
 */

#pragma once
#include "csilk/csilk.h"

/**
 * @brief 获取当前用户的资产列表（分页/过滤）
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/assets
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          查询参数 (Query Parameters):
 *          - page: 页码 (int, 可选, 默认 1)
 *          - page_size: 每页条数 (int, 可选, 默认 20)
 *          - category_id: 分类 ID (int64, 可选, 过滤特定分类下的资产)
 *          - ledger_id: 账本 ID (int64, 可选, 过滤特定账本)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"items": [...], "total": 10, "page": 1, "page_size": 20}}
 *          - 401 Unauthorized: 未登录 (code: 1001)
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void assets_list(csilk_ctx_t* c);

/**
 * @brief 创建新资产
 *
 * @details HTTP 方法: POST
 *          REST 路径: /api/assets
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          请求体 (JSON):
 *          - name: 资产名称 (string, 必填, 如 "招商银行储蓄卡", "贵州茅台")
 *          - category_id: 所属分类 ID (int64, 必填)
 *          - account_no: 账号/卡号/代号 (string, 可选)
 *          - current_value: 初始金额/现值 (double, 可选, 默认 0.0)
 *          - currency: 币种 (string, 可选, 默认 "CNY")
 *          - note: 备注说明 (string, 可选)
 *          - quantity: 持仓份额/数量 (double, 仅投资类资产适用)
 *          - cost_basis: 持仓总成本 (double, 仅投资类资产适用)
 *          - net_value: 单位净值/当前单价 (double, 仅投资类资产适用)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"id": 1, ...}}
 *          - 400 Bad Request: 参数缺失或格式错误 (code: 1002)
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void assets_create(csilk_ctx_t* c);

/**
 * @brief 更新指定资产信息
 *
 * @details HTTP 方法: PUT
 *          REST 路径: /api/assets/:id
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          路径参数 (Path Parameters):
 *          - id: 资产 ID (int64)
 *          请求体 (JSON):
 *          - name: 资产名称 (string, 可选)
 *          - category_id: 分类 ID (int64, 可选)
 *          - account_no: 账号 (string, 可选)
 *          - current_value: 当前余额/现值 (double, 可选, 若变更会自动记录余额变动日志)
 *          - currency: 币种代码 (string, 可选)
 *          - note: 备注 (string, 可选)
 *          - net_value: 最新单位净值 (double, 可选, 投资品净值变动将自动重算 current_value = quantity * net_value)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": null}
 *          - 400 Bad Request: 参数校验失败 (code: 1002)
 *          - 404 Not Found: 资产不存在或无权访问 (code: 1003)
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void assets_update(csilk_ctx_t* c);

/**
 * @brief 删除指定资产
 *
 * @details HTTP 方法: DELETE
 *          REST 路径: /api/assets/:id
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          路径参数 (Path Parameters):
 *          - id: 待删除资产 ID (int64)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": null}
 *          - 404 Not Found: 资产不存在 (code: 1003)
 *
 *          删除资产时将级联清理该资产关联的所有交易明细与余额变动日志。
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void assets_delete(csilk_ctx_t* c);

/**
 * @brief 获取资产详细信息及关联交易记录
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/assets/:id
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          路径参数 (Path Parameters):
 *          - id: 资产 ID (int64)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"id": 1, "name": "...", "transactions": [...]}}
 *          - 404 Not Found: 资产不存在 (code: 1003)
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void assets_detail(csilk_ctx_t* c);

/**
 * @brief 分页查询资产余额变动流水日志
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/asset-balance-logs
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          查询参数 (Query Parameters):
 *          - page: 页码 (int, 可选, 默认 1)
 *          - page_size: 每页条数 (int, 可选, 默认 20)
 *          - asset_id: 资产 ID (int64, 可选, 筛选特定资产的变动历史)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"items": [{"id": 1, "delta": 100.0, "balance_after": 1100.0, "source_type": "expense", ...}], "total": 5}}
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void asset_logs_list(csilk_ctx_t* c);

/**
 * @brief 根据交易事实重建单个资产状态
 *
 * @details HTTP 方法: POST
 *          REST 路径: /api/assets/:id/rebuild
 *          鉴权要求: JWT 认证 (Bearer Token)
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void assets_rebuild_single(csilk_ctx_t* c);

/**
 * @brief 根据全部历史交易事实全量重建用户所有资产与投资组合状态
 *
 * @details HTTP 方法: POST
 *          REST 路径: /api/assets/rebuild
 *          鉴权要求: JWT 认证 (Bearer Token)
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void assets_rebuild_all(csilk_ctx_t* c);

#include "csilk/app/app.h"

/**
 * @brief 注册资产管理模块相关的所有 HTTP 路由
 *
 * @details 将资产增删改查、详情以及余额变动日志接口挂载到 Csilk 应用实例。
 *
 * @param[in,out] app Csilk 应用实例指针 (csilk_app_t*)
 */
void register_asset_routes(csilk_app_t* app);
