/**
 * @file report_controller.c
 * @brief 统计报表、持仓分析与仪表盘数据控制器实现文件
 *
 * 遵循三层 C 架构规范，本文件作为 HTTP 控制层入口，包含 services/report_service.h。
 * 复杂的报表统计业务拆分为：
 * - services/report_expense_service.c: 负责月度/年度收支、分类及标签聚合；
 * - services/report_asset_service.c: 负责资产趋势、资产分布及资产负债总览；
 * - services/report_holdings_service.c: 负责投资持仓成本、实时市值、浮动盈亏及外汇 PnL 计算；
 * 并在 services/report_service.c 中统一对外注册 HTTP 路由。
 */

#include "controllers/report_controller.h"
#include "services/report_service.h"
