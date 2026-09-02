/**
 * @file daily_expense_controller.c
 * @brief 日常收支记账控制器实现文件
 *
 * 遵循三层 C 架构设计，控制器层包含 services/daily_expense_service.h。
 * 复杂的日常记账业务按读写分离模式拆分为：
 * - services/daily_expense_query.c: 处理多条件检索与分页查询
 * - services/daily_expense_write.c: 处理事务写入、标签多对多映射及账户余额联动
 * 并在 services/daily_expense_service.c 中统一暴露 register_daily_expense_routes()。
 */

#include "controllers/daily_expense_controller.h"
#include "services/daily_expense_service.h"
