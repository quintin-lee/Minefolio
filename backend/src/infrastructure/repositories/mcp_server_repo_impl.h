#pragma once

/**
 * @file mcp_server_repo_impl.h
 * @brief 基础设施层 MCP 服务器仓储实现头文件
 *
 * 实现 domain/mcp/repository.h 声明的所有纯 C 仓储契约。
 * 基于 SQL 数据库 (SQLite/PostgreSQL) 执行持久化操作。
 */

#include "domain/mcp/repository.h"
