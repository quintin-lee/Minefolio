#include "db.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static csilk_db_pool_t* g_pool = NULL;

int db_init(csilk_db_pool_t** out_pool) {
    csilk_db_init();

    const char* dsn = getenv("MINEFOLIO_DB_DSN");
    if (!dsn) dsn = "./data/minefolio.db";

    g_pool = csilk_db_pool_new("sqlite", dsn);
    if (!g_pool) {
        fprintf(stderr, "Failed to create database pool\n");
        return -1;
    }

    *out_pool = g_pool;
    return 0;
}

int db_run_migrations(csilk_db_pool_t* pool) {
    FILE* f = fopen("sql/migration.sql", "r");
    if (!f) {
        f = fopen("./sql/migration.sql", "r");
    }
    if (!f) {
        fprintf(stderr, "Cannot open migration.sql\n");
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);

    char* sql = malloc((size_t)len + 1);
    if (!sql) { fclose(f); return -1; }
    size_t n = fread(sql, 1, (size_t)len, f);
    sql[n] = '\0';
    fclose(f);

    // Execute the full SQL - SQLite handles multiple statements
    if (csilk_db_exec(pool, sql) != 0) {
        fprintf(stderr, "Migration error\n");
        free(sql);
        return -1;
    }

    // Try adding 'type' column for pre-existing databases (ignore failure if column already exists)
    csilk_db_exec(pool, "ALTER TABLE categories ADD COLUMN type TEXT NOT NULL DEFAULT 'asset'");

    // ---- 收支-资产联动迁移（列存在性门控，一次性） ----
    // 检测 daily_expenses 是否已有 asset_id 列
    int has_asset_id = 0;
    csilk_json_t* cols = csilk_db_query_json(pool, "PRAGMA table_info(daily_expenses)");
    if (cols) {
        size_t n = csilk_json_array_size(cols);
        for (size_t i = 0; i < n; i++) {
            const csilk_json_t* col = csilk_json_array_get(cols, i);
            const char* cname = csilk_json_get_string(col, "name");
            if (cname && strcmp(cname, "asset_id") == 0) { has_asset_id = 1; break; }
        }
        csilk_json_free(cols);
    }

    if (!has_asset_id) {
        // 存量库一次性迁移：清空旧数据（用户已确认）+ 加列（门控闭合点）
        csilk_db_exec(pool, "DELETE FROM expense_tags");
        csilk_db_exec(pool, "DELETE FROM daily_expenses");
        csilk_db_exec(pool, "DELETE FROM transactions");
        if (csilk_db_exec(pool,
                "ALTER TABLE daily_expenses ADD COLUMN asset_id INTEGER NOT NULL "
                "REFERENCES assets(id) ON DELETE CASCADE") != 0) {
            fprintf(stderr, "Migration error: cannot add asset_id to daily_expenses\n");
            free(sql);
            return -1;
        }
    }

    // 无条件幂等建索引（全新库首启 / 存量库 ALTER 后 / 失败自愈均覆盖）
    csilk_db_exec(pool, "CREATE INDEX IF NOT EXISTS idx_daily_expenses_asset ON daily_expenses(asset_id)");

    free(sql);
    return 0;
}

csilk_db_pool_t* db_get_pool(void) {
    return g_pool;
}
