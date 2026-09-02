/**
 * @file tag_controller.c
 * @brief 标签管理与输入补全控制器实现文件
 *
 * 遵循三层 C 架构规范，控制器层负责 HTTP 路由入口声明与服务层包含。
 * 具体的标签数据仓储读写、引用频次统计（use_count）以及模糊联想建议逻辑
 * 均在 services/tag_service.c 中实现。
 */

#include "controllers/tag_controller.h"
#include "services/tag_service.h"
