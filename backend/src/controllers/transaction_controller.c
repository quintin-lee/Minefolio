/**
 * @file transaction_controller.c
 * @brief 综合交易明细与投资交易流水控制器实现文件
 *
 * 遵循三层 C 架构规范，本控制器包含 services/transaction_service.h。
 * 交易域业务按高内聚低耦合原则拆分为：
 * - services/transaction_query.c: 负责复杂筛选过滤、分页与月度报表数据聚合；
 * - services/transaction_write.c: 负责事务持久化、持仓份额/成本联动（apply_position）、资金账户变动（balance_apply_delta）及父子手续费回滚管理；
 * 并在 services/transaction_service.c 中统一对外注册路由 register_transaction_routes()。
 */

#include "controllers/transaction_controller.h"
#include "services/transaction_service.h"
