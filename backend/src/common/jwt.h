#pragma once

/**
 * @file jwt.h
 * @brief JWT 令牌生成与用户信息提取工具接口
 *
 * 提供基于环境变量 MINEFOLIO_JWT_SECRET 的 HS256 JWT Token 签发，
 * 以及从已认证请求上下文中提取 user_id 的快捷函数。
 */

#include "csilk/csilk.h"

/**
 * @brief 为指定用户与版本号生成已签名的 JWT 认证令牌
 *
 * 构建包含 sub (用户ID)、iat (签发时间戳)、exp (过期时间戳，默认7天)、以及 v (Token版本号，用于批量失效控制)
 * 的 JSON 载荷，使用环境变量 MINEFOLIO_JWT_SECRET 通过 HS256 算法生成 JWT 字符串。
 *
 * @param[in,out] c csilk HTTP 上下文指针，不可为 NULL
 * @param[in] user_id 目标用户主键 ID
 * @param[in] token_version 当前用户的 Token 版本号（来自 users.token_version）
 *
 * @return char* 堆分配的 JWT 字符串；若密钥未配置或生成失败则返回 NULL
 *
 * @note 内存所有权：返回的字符串由堆内存分配，调用方在不需要时必须调用 free() 释放。
 * @note 线程安全性：线程安全（依赖环境变量只读访问与 csilk 加密库）。
 */
char* jwt_generate_token(csilk_ctx_t* c, int64_t user_id, int token_version);

/**
 * @brief 从请求上下文中提取已由 jwt_middleware 校验并挂载的用户 ID
 *
 * 从上下文属性 "jwt_payload" 中安全读取 "sub" 声明，且不会销毁或消耗该载荷指针。
 *
 * @param[in,out] c csilk HTTP 上下文指针，不可为 NULL
 *
 * @return int64_t 提取的用户 ID
 * @retval >0 用户主键 ID
 * @retval -1 上下文中无 jwt_payload 载荷或用户未通过鉴权
 *
 * @note 线程安全性：线程安全，仅读取当前请求上下文。
 */
int64_t jwt_get_user_id(csilk_ctx_t* c);
