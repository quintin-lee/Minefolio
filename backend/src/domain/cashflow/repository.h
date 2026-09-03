#pragma once

#include <stdint.h>
#include <stddef.h>
#include "domain/cashflow/entity.h"

/**
 * @brief 现金流计划仓储抽象契约接口 (Domain Cashflow Repository Contract)
 * @note 纯 C 契约，严格禁止返回 JSON 节点或直接编写 SQL
 */

int mf_cashflow_repo_list(void* pool, int64_t user_id, mf_cashflow_schedule_t** out_list, size_t* out_count);

void mf_cashflow_repo_free_list(mf_cashflow_schedule_t* list, size_t count);

int mf_cashflow_repo_get(void* pool, int64_t user_id, int64_t id, mf_cashflow_schedule_t* out_schedule);

int mf_cashflow_repo_create(void* pool, const mf_cashflow_schedule_t* schedule, int64_t* out_id);

int mf_cashflow_repo_update(void* pool, const mf_cashflow_schedule_t* schedule);

int mf_cashflow_repo_delete(void* pool, int64_t user_id, int64_t id);

int mf_cashflow_repo_list_active(void* pool, int64_t user_id, mf_cashflow_schedule_t** out_list, size_t* out_count);

int mf_cashflow_repo_get_actual_events(void* pool, int64_t user_id, const char* year_month,
                                      mf_cashflow_event_t** out_events, size_t* out_count);

void mf_cashflow_repo_free_events(mf_cashflow_event_t* events, size_t count);
