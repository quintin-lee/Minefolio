#include "db.h"
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

    free(sql);
    return 0;
}

csilk_db_pool_t* db_get_pool(void) {
    return g_pool;
}
