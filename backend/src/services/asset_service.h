#pragma once
#include "csilk/csilk.h"
#include "csilk/app/app.h"

/**
 * @file asset_service.h
 * @brief 资产与负债账户（现金、银行储蓄、信用卡、贷款、股票、公募基金、加密资产）核心管理服务
 */

/**
 * @brief 分页查询资产账户列表 (GET /api/assets)
 * 支持按分类类型 (asset_type)、账本 ID、关键字搜索及币种过滤
 * @param c HTTP 上下文
 */
void assets_list(csilk_ctx_t* c);

/**
 * @brief 创建新的资产/负债账户 (POST /api/assets)
 * @param c HTTP 上下文
 */
void assets_create(csilk_ctx_t* c);

/**
 * @brief 更新资产账户信息（名称、卡号、币种、备注、净值、持仓份额等）(PUT /api/assets/:id)
 * @param c HTTP 上下文
 */
void assets_update(csilk_ctx_t* c);

/**
 * @brief 删除资产账户 (DELETE /api/assets/:id)
 * 若存在关联交易流水将阻止删除或级联安全校验
 * @param c HTTP 上下文
 */
void assets_delete(csilk_ctx_t* c);

/**
 * @brief 获取单个资产账户详情与余额变动审计日志 (GET /api/assets/:id)
 * @param c HTTP 上下文
 */
void assets_detail(csilk_ctx_t* c);

/**
 * @brief 注册资产模块相关的 RESTful 路由
 * @param app Csilk 应用实例
 */
void register_asset_routes(csilk_app_t* app);
