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
 * @param session_id Current session ID (0 when no session is active).
 * @param name      Tool name (e.g. "get_assets").
 * @param arguments JSON arguments string from the model.
 * @return Heap-allocated JSON result string (caller must free), or NULL on error.
 */
char* ai_tools_execute(csilk_db_pool_t* pool,
                       int64_t          user_id,
                       int64_t          session_id,
                       const char*      name,
                       const char*      arguments);

/**
 * @brief Execute a tool call with pre-parsed arguments (caller owns args).
 * @param pool      Database pool.
 * @param user_id   Current user ID.
 * @param session_id Current session ID (0 when no session is active).
 * @param args      Pre-parsed csilk_json_t arguments object.
 * @param name      Tool name (e.g. "get_assets").
 * @return Heap-allocated JSON result string (caller must free), or NULL on error.
 */
char* ai_tools_execute_parsed(csilk_db_pool_t* pool,
                              int64_t          user_id,
                              int64_t          session_id,
                              csilk_json_t*    args,
                              const char*      name);

/**
 * @brief Same as ai_tools_execute_parsed but threads a cooperative cancel
 *        flag through to the tool context (e.g. MCP dispatch can abort a
 *        pending remote call when the user cancels the conversation).
 * @param cancel_token Opaque flag the runtime sets when cancelled; may be NULL.
 * @return Heap-allocated JSON result string (caller must free), or NULL on error.
 */
char* ai_tools_execute_parsed_cancel(csilk_db_pool_t*     pool,
                                     int64_t              user_id,
                                     int64_t              session_id,
                                     csilk_json_t*        args,
                                     const char*          name,
                                     const volatile bool* cancel_token);

/**
 * @brief Parse an uploaded file and return extracted text content.
 * @param pool       Database pool (may be NULL).
 * @param data       Raw file data.
 * @param data_len   Length of data.
 * @param filename   Original filename (for extension detection).
 * @param max_len    Maximum output length.
 * @return Heap-allocated text string (caller must free), or NULL on error.
 */
char* file_parse_to_string(
    csilk_db_pool_t* pool, const char* data, size_t data_len, const char* filename, size_t max_len);
