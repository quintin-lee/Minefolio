/**
 * @file tag_controller.h
 * @brief 标签管理与输入补全控制器头文件
 *
 * 声明自定义记账标签的增删改查、列表检索、颜色维护以及
 * 记账时关键词自动补全建议（Tag Suggestions）相关的 HTTP 端点和路由注册函数。
 */

#pragma once
#include "csilk/csilk.h"

/**
 * @brief 获取当前用户的全部标签列表
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/tags
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          请求参数: 无
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": [{"id": 1, "name": "餐饮", "color": "#FF5722", "use_count": 15}, ...]}
 *          - 401 Unauthorized: 未登录 (code: 1001)
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void tags_list(csilk_ctx_t* c);

/**
 * @brief 创建新标签
 *
 * @details HTTP 方法: POST
 *          REST 路径: /api/tags
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          请求体 (JSON):
 *          - name: 标签名称 (string, 必填)
 *          - color: 标签展示颜色十六进制字符串 (string, 可选, 如 "#4CAF50")
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"id": 1, ...}}
 *          - 400 Bad Request: 标签名为空或格式不合规 (code: 1002)
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void tags_create(csilk_ctx_t* c);

/**
 * @brief 更新已有标签
 *
 * @details HTTP 方法: PUT
 *          REST 路径: /api/tags/:id
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          路径参数 (Path Parameters):
 *          - id: 标签 ID (int64)
 *          请求体 (JSON):
 *          - name: 新标签名称 (string, 可选)
 *          - color: 新颜色值 (string, 可选)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": null}
 *          - 404 Not Found: 标签不存在或无权操作 (code: 1003)
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void tags_update(csilk_ctx_t* c);

/**
 * @brief 删除指定标签
 *
 * @details HTTP 方法: DELETE
 *          REST 路径: /api/tags/:id
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          路径参数 (Path Parameters):
 *          - id: 待删除标签 ID (int64)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": null}
 *          - 404 Not Found: 标签不存在 (code: 1003)
 *
 *          删除标签时将自动清理 daily_expense_tags 等关联表中的中间映射记录。
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void tags_delete(csilk_ctx_t* c);

/**
 * @brief 根据关键词获取标签补全建议
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/tags/suggestions
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          查询参数 (Query Parameters):
 *          - q: 搜索前缀或关键词 (string, 可选)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": [{"id": 1, "name": "餐饮", "color": "#FF5722"}, ...]}
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void tags_suggestions(csilk_ctx_t* c);

#include "csilk/app/app.h"

/**
 * @brief 注册标签管理模块相关的所有 HTTP 路由
 *
 * @details 注册标签列表、新增、修改、删除及自动联想补全端点。
 *
 * @param[in,out] app Csilk 应用实例指针 (csilk_app_t*)
 */
void register_tag_routes(csilk_app_t* app);
