#pragma once
#include "csilk/csilk.h"

/**
 * @file ledger_service.h
 * @brief 多账本空间 (Multi-Ledgers & Spaces) 隔离与协同成员 RBAC 权限管理服务
 */

/**
 * @brief 查询当前用户参与的所有账本空间列表 (GET /api/ledgers)
 * @param c HTTP 上下文
 */
void ledger_service_list(csilk_ctx_t* c);

/**
 * @brief 创建新的账本空间 (POST /api/ledgers)
 * 自动将创建者设为 owner 角色
 * @param c HTTP 上下文
 */
void ledger_service_create(csilk_ctx_t* c);

/**
 * @brief 获取指定账本空间的详细信息及统计概要 (GET /api/ledgers/:id)
 * @param c HTTP 上下文
 */
void ledger_service_get(csilk_ctx_t* c);

/**
 * @brief 修改账本空间配置（名称、货币、描述、图标）(PUT /api/ledgers/:id)
 * 需要 owner 权限
 * @param c HTTP 上下文
 */
void ledger_service_update(csilk_ctx_t* c);

/**
 * @brief 删除指定的账本空间 (DELETE /api/ledgers/:id)
 * 默认账本不可删除，非默认账本仅 owner 可执行删除
 * @param c HTTP 上下文
 */
void ledger_service_delete(csilk_ctx_t* c);

/**
 * @brief 列出指定账本空间的所有成员及其角色 (GET /api/ledgers/:id/members)
 * @param c HTTP 上下文
 */
void ledger_service_list_members(csilk_ctx_t* c);

/**
 * @brief 直接通过用户名添加成员至账本空间 (POST /api/ledgers/:id/members)
 * @param c HTTP 上下文
 */
void ledger_service_add_member(csilk_ctx_t* c);

/**
 * @brief 调整账本成员角色权限 (PUT /api/ledgers/:id/members/:uid)
 * 支持 owner / editor / viewer 角色流转
 * @param c HTTP 上下文
 */
void ledger_service_update_member(csilk_ctx_t* c);

/**
 * @brief 移除账本成员或成员主动退出账本 (DELETE /api/ledgers/:id/members/:uid)
 * @param c HTTP 上下文
 */
void ledger_service_remove_member(csilk_ctx_t* c);

/**
 * @brief 生成加入账本的一次性/限时邀请码 (POST /api/ledgers/:id/invite-code)
 * @param c HTTP 上下文
 */
void ledger_service_create_invite_code(csilk_ctx_t* c);

/**
 * @brief 使用邀请码加入指定账本空间 (POST /api/ledgers/join)
 * @param c HTTP 上下文
 */
void ledger_service_join_by_invite(csilk_ctx_t* c);
