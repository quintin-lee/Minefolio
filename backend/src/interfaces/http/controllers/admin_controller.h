/**
 * @file admin_controller.h
 * @brief 系统管理与初始引导控制器头文件 (DDD 接口层)
 */

#pragma once
#include "csilk/csilk.h"
#include "csilk/app/app.h"

/**
 * @brief 获取系统初始化状态 (GET /api/system/status)
 */
void system_status(csilk_ctx_t* c);

/**
 * @brief 系统首次初始化设置引导 (POST /api/system/setup)
 */
void system_setup(csilk_ctx_t* c);

/**
 * @brief 获取数据库及灾备存储状态 (GET /api/system/backup/status)
 */
void system_backup_status(csilk_ctx_t* c);

/**
 * @brief 触发一次立即在线热备份 (POST /api/system/backup/create)
 */
void system_backup_create(csilk_ctx_t* c);

/**
 * @brief 安全流式下载备份数据库文件 (GET /api/system/backup/download/:filename)
 */
void system_backup_download(csilk_ctx_t* c);

/**
 * @brief 删除指定备份文件 (DELETE /api/system/backup/:filename)
 */
void system_backup_delete(csilk_ctx_t* c);

/**
 * @brief 管理员查询用户列表 (GET /api/admin/users)
 */
void api_admin_users_list(csilk_ctx_t* c);

/**
 * @brief 管理员创建新用户 (POST /api/admin/users)
 */
void api_admin_users_create(csilk_ctx_t* c);

/**
 * @brief 管理员更新用户角色或状态 (PUT /api/admin/users/:id)
 */
void api_admin_users_update(csilk_ctx_t* c);

/**
 * @brief 管理员重置指定用户密码 (POST /api/admin/users/:id/reset-password)
 */
void api_admin_users_reset_password(csilk_ctx_t* c);

/**
 * @brief 管理员关闭指定用户的两步验证 (POST /api/admin/users/:id/disable-2fa)
 */
void api_admin_users_disable_2fa(csilk_ctx_t* c);

/**
 * @brief 管理员删除指定用户 (DELETE /api/admin/users/:id)
 */
void api_admin_users_delete(csilk_ctx_t* c);

/**
 * @brief 注册系统管理相关 RESTful 路由
 */
void register_admin_routes(csilk_app_t* app);
