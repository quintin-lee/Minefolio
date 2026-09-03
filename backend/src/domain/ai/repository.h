#pragma once

#include <stdint.h>
#include <stddef.h>
#include "domain/ai/entity.h"

/**
 * @brief AI 领域仓储抽象契约接口 (Domain AI Repository Contract)
 * @note 纯 C 契约，严格禁止依赖 JSON 或直接编写 SQL
 */

int mf_ai_session_repo_list(void* pool, int64_t user_id, int64_t page, int64_t page_size,
                            int64_t* total, mf_ai_session_t* out_list, size_t max_out);

int64_t mf_ai_session_repo_create(void* pool, int64_t user_id, const char* title,
                                  const char* model, const char* provider);

int mf_ai_session_repo_get(void* pool, int64_t user_id, int64_t id, mf_ai_session_t* out_session);

int mf_ai_session_repo_update(void* pool, int64_t user_id, int64_t id,
                              const char* title, const char* model);

int mf_ai_session_repo_delete(void* pool, int64_t user_id, int64_t id);

int mf_ai_trace_repo_stats(void* pool, int64_t user_id, mf_ai_trace_summary_t* out_stats);
