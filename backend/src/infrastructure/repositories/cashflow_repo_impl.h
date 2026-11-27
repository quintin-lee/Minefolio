#pragma once

#include "domain/cashflow/repository.h"
#include "csilk/csilk.h"

/* 基础设施层：基于 SQL/SQLite 实现领域仓储契约及特定只读查询 */
csilk_json_t* cashflow_repo_list_detailed(void* pool, int64_t user_id, int64_t ledger_id);
csilk_json_t* cashflow_repo_list_detailed_paged(
    void* pool, int64_t user_id, int64_t ledger_id, int64_t page, int64_t page_size, int64_t* out_total);
csilk_json_t*
cashflow_repo_get_detailed(void* pool, int64_t user_id, int64_t ledger_id, int64_t id);
