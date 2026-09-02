/**
 * @file category_controller.c
 * @brief 分类管理与预设分类播种控制器实现文件
 *
 * 遵循三层 C 架构设计，本文件作为 HTTP 控制层入口，包含 services/category_service.h。
 * 具体的树形分类装配算法、数据库增删改查、默认分类初始化（播种）及父子依赖校验
 * 均在 services/category_service.c 中实现。
 */

#include "controllers/category_controller.h"
#include "services/category_service.h"
