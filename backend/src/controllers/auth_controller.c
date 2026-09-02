/**
 * @file auth_controller.c
 * @brief 用户认证、授权与 TOTP 2FA 控制器实现文件
 *
 * 遵循三层 C 架构规范，本控制器作为 HTTP 控制层，包含 services/auth_service.h。
 * 具体的参数绑定、RSA-OAEP 密码解密、Bcrypt 密码哈希、JWT 签发及 TOTP/OAuth 流程
 * 均在 services/auth_service.c 中实现并由 register_auth_routes() 完成路由映射。
 */

#include "controllers/auth_controller.h"
#include "services/auth_service.h"
