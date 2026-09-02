/**
 * @file asset_controller.c
 * @brief 资产管理与持仓流水控制器实现文件
 *
 * 遵循三层 C 架构设计，控制器层作为 HTTP 路由与服务层桥梁，包含 services/asset_service.h。
 * 具体的参数解析、数据库仓储访问、持仓市值计算（如 net_value 变动重算）以及
 * 余额变动日志（asset_balance_logs）流水的写入由 services/asset_service.c 完成。
 */

#include "controllers/asset_controller.h"
#include "services/asset_service.h"
