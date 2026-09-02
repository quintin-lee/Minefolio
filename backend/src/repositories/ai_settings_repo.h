#pragma once
#include "csilk/csilk.h"
#include "common/db.h"

/**
 * @file ai_settings_repo.h
 * @brief 全局 AI 设置配置持久化数据访问层接口
 *
 * 负责系统级 AI 模型配置（包括 API 密钥、Provider 端点、模型选择等）
 * 在数据库单行表 `ai_settings` (固定 id=1) 中的读取与保存 (Upsert)。
 */

/**
 * @brief 从数据库加载全局 AI 配置 JSON 字符串
 *
 * 查询 `ai_settings` 表中 id=1 的全局配置数据并提取 `config_json` 字段。
 *
 * @param pool 数据库连接池指针
 * @return char* 成功时返回动态分配的 JSON 格式配置字符串（调用方负责调用 `free()` 释放），若未配置或查询失败返回 NULL
 */
char* ai_settings_load(csilk_db_pool_t* pool);

/**
 * @brief 保存或更新全局 AI 配置 JSON 字符串
 *
 * 使用 UPSERT 语法 (`ON CONFLICT(id) DO UPDATE`) 写入或更新 id=1 的全局配置记录，
 * 同时更新 `updated_at` 时间戳。
 *
 * @param pool 数据库连接池指针
 * @param config_json 待保存的完整配置 JSON 格式字符串
 * @return int 成功返回 0，失败返回 -1
 */
int ai_settings_save(csilk_db_pool_t* pool, const char* config_json);
