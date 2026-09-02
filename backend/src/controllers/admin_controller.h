/**
 * @file admin_controller.h
 * @brief 系统管理与初始引导控制器头文件
 *
 * 定义系统状态探测、初始化设置引导等管理端点声明及路由注册函数。
 * 遵循三层架构规范，对外暴露 HTTP 路由处理入口。
 */

#pragma once
#include "csilk/csilk.h"

/**
 * @brief 获取系统初始化状态
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/system/status
 *          鉴权要求: 公开访问（无需 JWT）
 *          请求参数: 无
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"initialized": true/false}}
 *
 *          通过查询用户数量判断系统是否已完成初次安装初始化。
 *          为防止用户名爆破与信息泄露，不返回具体注册用户数。
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void system_status(csilk_ctx_t* c);

/**
 * @brief 系统首次初始化设置引导
 *
 * @details HTTP 方法: POST
 *          REST 路径: /api/system/setup
 *          鉴权要求: 公开访问（仅当系统未初始化时允许一次性调用，受限流保护）
 *          请求体 (JSON):
 *          - username: 管理员用户名 (string, 长度 >= 2)
 *          - password_enc: 经 RSA-2048 公钥加密的密码密文 (string, Base64URL 格式)
 *          - db_driver: 数据库驱动类型 (string, 可选, 如 "sqlite" / "postgres")
 *          - db_dsn: 数据库连接串 (string, 可选)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"token": "...", "expires_in": 604800}}
 *          - 400 Bad Request: 参数错误或密码解密校验失败 (code: 1002)
 *          - 403 Forbidden: 系统已完成初始化禁止重复设置 (code: 1004)
 *          - 500 Internal Error: 数据库操作失败 (code: 500)
 *
 *          在单事务中完成管理员创建、默认分类播种、默认账本与导入规则生成。
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void system_setup(csilk_ctx_t* c);

#include "csilk/app/app.h"

/**
 * @brief 注册系统管理相关路由
 *
 * @details 将 /api/system/status 与 /api/system/setup 注册至 Csilk 应用实例。
 *
 * @param[in,out] app Csilk 应用实例指针 (csilk_app_t*)
 */
void register_admin_routes(csilk_app_t* app);
