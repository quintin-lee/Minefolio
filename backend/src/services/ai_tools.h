#pragma once
#include "csilk/csilk.h"
#include "common/db.h"

/**
 * @brief Get the array of tool definitions for AI function calling.
 * @param count [out] Number of tools returned.
 * @return Pointer to static array of csilk_ai_tool_t.
 */
const csilk_ai_tool_t* ai_tools_get_definitions(size_t* count);

/**
 * @brief Execute a tool call from the AI model.
 * @param pool      Database pool.
 * @param user_id   Current user ID.
 * @param name      Tool name (e.g. "get_assets").
 * @param arguments JSON arguments string from the model.
 * @return Heap-allocated JSON result string (caller must free), or NULL on error.
 */
char* ai_tools_execute(csilk_db_pool_t* pool, int64_t user_id,
                       const char* name, const char* arguments);
