/**
 * @file transfer_controller.c
 * @brief 账户间资金划转控制器实现文件
 *
 * 遵循三层 C 架构规范，控制器层负责 HTTP 路由入口声明与服务层包含。
 * 具体的参数校验、跨账户资产归属校验、双向交易流水生成及原子余额对冲逻辑
 * 均在 services/transfer_service.c 中实现。
 */

#include "controllers/transfer_controller.h"
#include "services/transfer_service.h"
