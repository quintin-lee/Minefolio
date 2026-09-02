/**
 * @file admin_controller.c
 * @brief 系统管理与初始引导控制器实现文件
 *
 * 遵循三层 C 架构规范，控制器层负责 HTTP 路由入口声明与服务层包含。
 * 具体的参数提取、RSA 解密、事务处理与业务逻辑由 services/admin_service.c 实现。
 */

#include "controllers/admin_controller.h"
#include "services/admin_service.h"
