#pragma once

/**
 * @file tag_repo_impl.h
 * @brief 基础设施层标签仓储实现头文件
 *
 * 实现 domain/tag/repository.h 声明的所有纯 C 仓储契约。
 * 基于 SQL 数据库 (SQLite/PostgreSQL) 执行持久化操作。
 */

#include "domain/tag/repository.h"
