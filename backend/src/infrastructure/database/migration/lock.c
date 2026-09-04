#include "infrastructure/database/migration/lock.h"
#include "infrastructure/database/statement.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
mf_migration_lock_init(mf_db_t* db)
{
    if (!db) {
        return -1;
    }

    const char* ddl = "CREATE TABLE IF NOT EXISTS schema_migration_lock ("
                      "id INTEGER PRIMARY KEY, "
                      "is_locked INTEGER NOT NULL DEFAULT 0, "
                      "locked_at TIMESTAMP, "
                      "locked_by TEXT"
                      ");";
    if (mf_db_execute(db, ddl) != 0) {
        return -1;
    }

    const char* init_row =
        "INSERT INTO schema_migration_lock (id, is_locked, locked_by) VALUES (1, 0, '') "
        "ON CONFLICT(id) DO NOTHING;";
    return mf_db_execute(db, init_row);
}

int
mf_migration_lock_acquire(mf_db_t* db, const char* locked_by, int timeout_seconds)
{
    if (!db) {
        return -1;
    }
    if (mf_migration_lock_init(db) != 0) {
        return -1;
    }

    const char* who = locked_by ? locked_by : "minefolio_instance";
    int         max_attempts = timeout_seconds > 0 ? (timeout_seconds * 10) : 1;

    for (int attempt = 0; attempt < max_attempts; attempt++) {
        mf_stmt_t* stmt = NULL;
        int rc = mf_stmt_prepare(db,
                                 "UPDATE schema_migration_lock "
                                 "SET is_locked = 1, locked_at = CURRENT_TIMESTAMP, locked_by = ? "
                                 "WHERE id = 1 AND is_locked = 0;",
                                 &stmt);
        if (rc == 0 && stmt) {
            mf_stmt_bind_text(stmt, 1, who);
            int64_t aff = 0;
            rc = mf_stmt_execute(stmt, &aff);
            mf_stmt_close(stmt);

            if (rc == 0 && aff == 1) {
                return 0; /* 成功获取锁 */
            }
        }

        if (attempt + 1 < max_attempts) {
            usleep(100000); /* 100ms 等待重试 */
        }
    }

    return -1; /* 超时或未能获取 */
}

int
mf_migration_lock_release(mf_db_t* db, const char* locked_by)
{
    if (!db) {
        return -1;
    }

    mf_stmt_t* stmt = NULL;
    int        rc = mf_stmt_prepare(
        db,
        "UPDATE schema_migration_lock SET is_locked = 0, locked_at = NULL, locked_by = '' WHERE "
        "id = 1;",
        &stmt);
    if (rc != 0 || !stmt) {
        return -1;
    }

    int64_t aff = 0;
    rc = mf_stmt_execute(stmt, &aff);
    mf_stmt_close(stmt);
    return rc;
}
