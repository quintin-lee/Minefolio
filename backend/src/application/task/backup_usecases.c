#include "application/task/backup_usecases.h"
#include "application/task/async_task_manager.h"
#include "infrastructure/database/db_backup.h"
#include "common/db.h"
#include <stdio.h>

int
backup_usecase_get_status(void* pool, const char* backup_dir, csilk_json_t** out_status)
{
    if (!pool || !backup_dir || !out_status) {
        return -1;
    }
    return db_backup_get_status((csilk_db_pool_t*)pool, backup_dir, out_status);
}

static int
backup_create_task_fn(void*   pool,
                      int64_t task_id,
                      int64_t user_id,
                      int64_t ledger_id,
                      void*   arg,
                      char**  out_result_json,
                      char*   err_buf,
                      size_t  err_cap)
{
    (void)user_id;
    (void)ledger_id;
    (void)arg;
    mf_async_task_update_progress(pool, task_id, 20, "正在执行 SQLite 在线热备份");

    char filename[128] = {0};
    if (db_backup_create((csilk_db_pool_t*)pool, "data/backups", filename, sizeof(filename)) != 0) {
        snprintf(err_buf, err_cap, "执行数据库在线热备份失败");
        return -1;
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "filename", filename);
    if (out_result_json) {
        *out_result_json = csilk_json_serialize(resp, NULL);
    }
    csilk_json_free(resp);
    return 0;
}

int64_t
backup_usecase_create_submit(void* pool, int64_t user_id, bool* out_deduplicated)
{
    if (!pool || user_id <= 0) {
        return -1;
    }
    bool    dedup = false;
    int64_t task_id = mf_async_task_submit((csilk_db_pool_t*)pool,
                                           user_id,
                                           0,
                                           "system_backup_create",
                                           "system:backup:create",
                                           "正在排队创建数据库在线热备份",
                                           backup_create_task_fn,
                                           NULL,
                                           NULL,
                                           &dedup);
    if (out_deduplicated) {
        *out_deduplicated = dedup;
    }
    return task_id;
}

bool
backup_usecase_is_valid_filename(const char* filename)
{
    return db_backup_is_valid_filename(filename);
}

int
backup_usecase_delete(const char* backup_dir, const char* filename)
{
    return db_backup_delete(backup_dir, filename);
}
