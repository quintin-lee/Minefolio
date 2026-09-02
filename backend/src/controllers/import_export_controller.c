/**
 * @file import_export_controller.c
 * @brief 财务数据导入与导出控制器实现文件
 *
 * 遵循三层 C 架构规范，本控制器作为 HTTP 控制层入口，包含 services/import_export_service.h。
 * 数据导入导出逻辑分别由：
 * - services/export_service.c: 负责交易明细与日常记账数据的 CSV 序列化及 HTTP 流输出；
 * - services/import_service.c: 负责 CSV 文本解析、规则匹配、资产校验及批量写入；
 * 并在 services/import_export_service.c 中统一对外注册路由。
 */

#include "controllers/import_export_controller.h"
#include "services/import_export_service.h"
