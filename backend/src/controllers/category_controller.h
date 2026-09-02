/**
 * @file category_controller.h
 * @brief 分类管理与预设分类播种控制器头文件
 *
 * 声明支出、收入、交易、资产分类的树形结构查询、增删改查、子分类查询以及
 * 新用户预设分类自动播种（Seed Defaults）相关的 HTTP 处理函数和路由注册函数。
 */

#pragma once
#include "csilk/csilk.h"
#include "csilk/drivers/db.h"

/**
 * @brief 为指定用户播种默认预设分类树
 *
 * @details 当新用户注册或首次初始化系统时调用。
 *          为用户创建包括餐饮、交通、日常购物、职业收入、投资理财、流动资产等预设层级分类体系。
 *          通过 category_seed_state 幂等表防止重复播种。
 *
 * @param[in,out] pool 数据库连接池指针 (csilk_db_pool_t*)
 * @param[in]     user_id 用户 ID (int64_t)
 */
void categories_seed_defaults(csilk_db_pool_t* pool, int64_t user_id);

/**
 * @brief 获取当前用户的分类树列表
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/categories
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          查询参数 (Query Parameters):
 *          - type: 分类大类类型 (string, 可选, 如 "expense", "income", "transaction", "asset" 或逗号分隔如 "expense,income")
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": [{"id": 1, "name": "餐饮美食", "children": [...]}, ...]}
 *          - 401 Unauthorized: 未登录 (code: 1001)
 *
 *          返回数据由后端自动聚合成多级树形结构，各节点包含 id, name, parent_id, type, asset_type, icon, sort_order 等字段。
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void categories_list(csilk_ctx_t* c);

/**
 * @brief 创建新分类
 *
 * @details HTTP 方法: POST
 *          REST 路径: /api/categories
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          请求体 (JSON):
 *          - name: 分类名称 (string, 必填)
 *          - type: 分类大类类型 (string, 可选, "expense"|"income"|"transaction"|"asset", 默认 "asset")
 *          - asset_type: 资产类型细分 (string, 可选, 如 "cash"|"stock"|"fund" 等)
 *          - parent_id: 父级分类 ID (int64, 可选, 若为空或 0 则为一级顶级分类)
 *          - currency: 币种 (string, 可选, 默认 "CNY")
 *          - icon: 图标标识 (string, 可选, 如 Emoji 或 Iconify 字符串)
 *          - sort_order: 排序序号 (int, 可选, 默认 0)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": null}
 *          - 400 Bad Request: 参数错误 (code: 1002)
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void categories_create(csilk_ctx_t* c);

/**
 * @brief 更新指定分类信息
 *
 * @details HTTP 方法: PUT
 *          REST 路径: /api/categories/:id
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          路径参数 (Path Parameters):
 *          - id: 分类 ID (int64)
 *          请求体 (JSON):
 *          - name: 分类名称 (string, 可选)
 *          - type: 类型 (string, 可选)
 *          - asset_type: 资产子类型 (string, 可选)
 *          - currency: 币种代码 (string, 可选)
 *          - icon: 图标 (string, 可选)
 *          - sort_order: 排序序号 (int, 可选)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": null}
 *          - 404 Not Found: 分类不存在或无权操作 (code: 1003)
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void categories_update(csilk_ctx_t* c);

/**
 * @brief 删除指定分类
 *
 * @details HTTP 方法: DELETE
 *          REST 路径: /api/categories/:id
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          路径参数 (Path Parameters):
 *          - id: 待删除分类 ID (int64)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": null}
 *          - 403 Forbidden: 该分类下存在子分类，禁止直接删除 (code: 1004)
 *          - 404 Not Found: 分类不存在 (code: 1003)
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void categories_delete(csilk_ctx_t* c);

/**
 * @brief 获取指定分类的直接子分类列表
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/categories/:id/children
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          路径参数 (Path Parameters):
 *          - id: 父级分类 ID (int64)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": [{"id": 2, "name": "...", "parent_id": 1}, ...]}
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void categories_children(csilk_ctx_t* c);

#include "csilk/app/app.h"

/**
 * @brief 注册分类管理相关的所有 HTTP 路由
 *
 * @details 注册分类树列表、创建、更新、删除及直接子节点查询路由。
 *
 * @param[in,out] app Csilk 应用实例指针 (csilk_app_t*)
 */
void register_category_routes(csilk_app_t* app);
