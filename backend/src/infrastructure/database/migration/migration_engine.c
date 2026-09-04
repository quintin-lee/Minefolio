#include "infrastructure/database/migration/migration_engine.h"
#include "infrastructure/database/migration/checksum.h"
#include "infrastructure/database/migration/lock.h"
#include "infrastructure/database/statement.h"
#include "infrastructure/database/transaction.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define MF_MIGRATION_BASELINE_VERSION 7

struct mf_migration_engine_s {
    mf_db_t*       db;
    mf_db_engine_t engine;
    char           base_dir[256];
    char           dialect_dir[256];
};

static const char* s_create_schema_migrations_sql =
    "CREATE TABLE IF NOT EXISTS schema_migrations ("
    "    version INTEGER PRIMARY KEY,"
    "    name TEXT NOT NULL,"
    "    checksum TEXT NOT NULL,"
    "    applied_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
    "    execution_time_ms INTEGER NOT NULL DEFAULT 0,"
    "    execution_time INTEGER NOT NULL DEFAULT 0"
    ");";

static const char*
resolve_migrations_base_dir(const char* hint)
{
    static const char* candidates[] = {"sql/migrations",
                                       "backend/sql/migrations",
                                       "../sql/migrations",
                                       "../../sql/migrations",
                                       "../../backend/sql/migrations",
                                       NULL};

    if (hint && hint[0]) {
        if (access(hint, R_OK | X_OK) == 0) {
            return hint;
        }
    }

    for (int i = 0; candidates[i] != NULL; i++) {
        if (access(candidates[i], R_OK | X_OK) == 0) {
            return candidates[i];
        }
    }

    return hint ? hint : "sql/migrations";
}

int
mf_migration_engine_new(mf_db_t*                db,
                        const char*             migrations_base_dir,
                        mf_migration_engine_t** out_engine)
{
    if (!db || !out_engine) {
        return -1;
    }

    mf_migration_engine_t* engine = calloc(1, sizeof(*engine));
    if (!engine) {
        return -1;
    }

    engine->db = db;
    engine->engine = mf_db_get_engine(db);

    const char* resolved_base = resolve_migrations_base_dir(migrations_base_dir);
    strncpy(engine->base_dir, resolved_base, sizeof(engine->base_dir) - 1);

    /* Trim any trailing slash */
    size_t base_len = strlen(engine->base_dir);
    while (base_len > 1 && engine->base_dir[base_len - 1] == '/') {
        engine->base_dir[base_len - 1] = '\0';
        base_len--;
    }

    if (engine->engine == MF_DB_ENGINE_POSTGRES) {
        snprintf(engine->dialect_dir, sizeof(engine->dialect_dir), "%s/postgres", engine->base_dir);
    } else {
        snprintf(engine->dialect_dir, sizeof(engine->dialect_dir), "%s/sqlite", engine->base_dir);
    }

    *out_engine = engine;
    return 0;
}

void
mf_migration_engine_free(mf_migration_engine_t* engine)
{
    if (engine) {
        free(engine);
    }
}

static bool
parse_migration_filename(const char* filename, int* out_version, char* out_name, size_t name_size)
{
    if (!filename || (filename[0] != 'V' && filename[0] != 'v')) {
        return false;
    }
    size_t len = strlen(filename);
    if (len < 8 || strcmp(filename + len - 4, ".sql") != 0) {
        return false;
    }

    const char* sep = strstr(filename, "__");
    if (!sep || sep == filename + 1) {
        return false;
    }

    for (const char* p = filename + 1; p < sep; p++) {
        if (!isdigit((unsigned char)*p)) {
            return false;
        }
    }

    int version = atoi(filename + 1);
    if (version <= 0) {
        return false;
    }

    const char* desc_start = sep + 2;
    size_t      desc_len = (filename + len - 4) - desc_start;
    if (desc_len == 0 || desc_len >= name_size) {
        return false;
    }

    if (out_version) {
        *out_version = version;
    }
    if (out_name) {
        strncpy(out_name, desc_start, desc_len);
        out_name[desc_len] = '\0';
    }
    return true;
}

static int
compare_migration_items(const void* a, const void* b)
{
    const mf_migration_item_t* item_a = (const mf_migration_item_t*)a;
    const mf_migration_item_t* item_b = (const mf_migration_item_t*)b;
    if (item_a->version < item_b->version) {
        return -1;
    }
    if (item_a->version > item_b->version) {
        return 1;
    }
    return 0;
}

int
mf_migration_discover(mf_migration_engine_t* engine,
                      mf_migration_item_t**  out_items,
                      int*                   out_count)
{
    if (!engine || !out_items || !out_count) {
        return -1;
    }
    *out_items = NULL;
    *out_count = 0;

    DIR* dir = opendir(engine->dialect_dir);
    if (!dir) {
        return -1;
    }

    size_t               capacity = 16;
    size_t               count = 0;
    mf_migration_item_t* items = calloc(capacity, sizeof(mf_migration_item_t));
    if (!items) {
        closedir(dir);
        return -1;
    }

    struct dirent* ent = NULL;
    while ((ent = readdir(dir)) != NULL) {
        int  version = 0;
        char name[128] = {0};
        if (!parse_migration_filename(ent->d_name, &version, name, sizeof(name))) {
            continue;
        }

        if (count >= capacity) {
            capacity *= 2;
            mf_migration_item_t* new_items = realloc(items, capacity * sizeof(mf_migration_item_t));
            if (!new_items) {
                free(items);
                closedir(dir);
                return -1;
            }
            items = new_items;
        }

        mf_migration_item_t* cur = &items[count];
        cur->version = version;
        snprintf(cur->name, sizeof(cur->name), "%s", name);
        snprintf(cur->filepath, sizeof(cur->filepath), "%s/%s", engine->dialect_dir, ent->d_name);
        cur->is_applied = false;
        cur->execution_time_ms = 0;

        if (mf_migration_checksum_file(cur->filepath, cur->checksum) != 0) {
            free(items);
            closedir(dir);
            return -1;
        }

        count++;
    }
    closedir(dir);

    if (count > 1) {
        qsort(items, count, sizeof(mf_migration_item_t), compare_migration_items);

        for (size_t i = 0; i < count - 1; i++) {
            if (items[i].version == items[i + 1].version) {
                free(items);
                return -1;
            }
        }
    }

    *out_items = items;
    *out_count = (int)count;
    return 0;
}

static bool
table_exists(mf_db_t* db, mf_db_engine_t engine, const char* table_name)
{
    if (!db || !table_name) {
        return false;
    }
    mf_stmt_t*  stmt = NULL;
    const char* sql = (engine == MF_DB_ENGINE_POSTGRES)
                          ? "SELECT 1 FROM information_schema.tables WHERE table_name = ?;"
                          : "SELECT 1 FROM sqlite_master WHERE type='table' AND name = ?;";

    if (mf_stmt_prepare(db, sql, &stmt) != 0 || !stmt) {
        return false;
    }
    mf_stmt_bind_text(stmt, 1, table_name);
    mf_result_t* res = NULL;
    bool         exists = false;
    if (mf_stmt_query(stmt, &res) == 0 && res) {
        if (mf_result_next(res)) {
            exists = true;
        }
        mf_result_free(res);
    }
    mf_stmt_close(stmt);
    return exists;
}

static int
ensure_schema_migrations_and_baseline(mf_migration_engine_t* engine)
{
    bool has_migrations_table = table_exists(engine->db, engine->engine, "schema_migrations");
    if (has_migrations_table) {
        return 0;
    }

    bool has_users_table = table_exists(engine->db, engine->engine, "users");

    if (mf_db_execute(engine->db, s_create_schema_migrations_sql) != 0) {
        return -1;
    }

    if (!has_users_table) {
        return 0;
    }

    mf_migration_item_t* items = NULL;
    int                  count = 0;
    if (mf_migration_discover(engine, &items, &count) != 0) {
        return -1;
    }

    for (int i = 0; i < count; i++) {
        if (items[i].version <= MF_MIGRATION_BASELINE_VERSION) {
            mf_stmt_t* stmt = NULL;
            int        rc = mf_stmt_prepare(
                engine->db,
                "INSERT INTO schema_migrations (version, name, checksum, applied_at, "
                "execution_time_ms, execution_time) "
                "VALUES (?, ?, ?, CURRENT_TIMESTAMP, 0, 0);",
                &stmt);
            if (rc != 0 || !stmt) {
                free(items);
                return -1;
            }
            mf_stmt_bind_int64(stmt, 1, items[i].version);
            mf_stmt_bind_text(stmt, 2, items[i].name);
            mf_stmt_bind_text(stmt, 3, items[i].checksum);
            rc = mf_stmt_execute(stmt, NULL);
            mf_stmt_close(stmt);
            if (rc != 0) {
                free(items);
                return -1;
            }
        }
    }

    free(items);
    return 0;
}

int
mf_migration_validate(mf_migration_engine_t* engine)
{
    if (!engine) {
        return -1;
    }

    if (!table_exists(engine->db, engine->engine, "schema_migrations")) {
        return 0;
    }

    mf_migration_item_t* disk_items = NULL;
    int                  disk_count = 0;
    if (mf_migration_discover(engine, &disk_items, &disk_count) != 0) {
        return -1;
    }

    mf_stmt_t* stmt = NULL;
    int        rc = mf_stmt_prepare(
        engine->db,
        "SELECT version, name, checksum FROM schema_migrations ORDER BY version ASC;",
        &stmt);
    if (rc != 0 || !stmt) {
        free(disk_items);
        return -1;
    }

    mf_result_t* res = NULL;
    if (mf_stmt_query(stmt, &res) != 0 || !res) {
        mf_stmt_close(stmt);
        free(disk_items);
        return -1;
    }

    int  max_applied_version = 0;
    bool valid = true;

    while (mf_result_next(res)) {
        int64_t     db_version = mf_result_get_int64(res, "version");
        const char* db_checksum = mf_result_get_text(res, "checksum");
        if (db_version > max_applied_version) {
            max_applied_version = (int)db_version;
        }

        bool found = false;
        for (int i = 0; i < disk_count; i++) {
            if (disk_items[i].version == (int)db_version) {
                found = true;
                if (!db_checksum || strcmp(disk_items[i].checksum, db_checksum) != 0) {
                    valid = false;
                }
                break;
            }
        }
        if (!found) {
            valid = false;
        }
        if (!valid) {
            break;
        }
    }

    mf_result_free(res);
    mf_stmt_close(stmt);

    if (valid) {
        for (int i = 0; i < disk_count; i++) {
            if (disk_items[i].version < max_applied_version) {
                mf_stmt_t* chk_stmt = NULL;
                if (mf_stmt_prepare(engine->db,
                                    "SELECT 1 FROM schema_migrations WHERE version = ?;",
                                    &chk_stmt) == 0 &&
                    chk_stmt) {
                    mf_stmt_bind_int64(chk_stmt, 1, disk_items[i].version);
                    mf_result_t* chk_res = NULL;
                    bool         was_applied = false;
                    if (mf_stmt_query(chk_stmt, &chk_res) == 0 && chk_res) {
                        if (mf_result_next(chk_res)) {
                            was_applied = true;
                        }
                        mf_result_free(chk_res);
                    }
                    mf_stmt_close(chk_stmt);
                    if (!was_applied) {
                        valid = false;
                        break;
                    }
                }
            }
        }
    }

    free(disk_items);
    return valid ? 0 : -1;
}

int
mf_migration_status(mf_migration_engine_t* engine, mf_migration_item_t** out_items, int* out_count)
{
    if (!engine || !out_items || !out_count) {
        return -1;
    }

    mf_migration_item_t* items = NULL;
    int                  count = 0;
    if (mf_migration_discover(engine, &items, &count) != 0) {
        return -1;
    }

    if (table_exists(engine->db, engine->engine, "schema_migrations")) {
        mf_stmt_t* stmt = NULL;
        if (mf_stmt_prepare(engine->db,
                            "SELECT version, execution_time_ms FROM schema_migrations;",
                            &stmt) == 0 &&
            stmt) {
            mf_result_t* res = NULL;
            if (mf_stmt_query(stmt, &res) == 0 && res) {
                while (mf_result_next(res)) {
                    int64_t v = mf_result_get_int64(res, "version");
                    int64_t t = mf_result_get_int64(res, "execution_time_ms");
                    for (int i = 0; i < count; i++) {
                        if (items[i].version == (int)v) {
                            items[i].is_applied = true;
                            items[i].execution_time_ms = (int)t;
                            break;
                        }
                    }
                }
                mf_result_free(res);
            }
            mf_stmt_close(stmt);
        }
    }

    *out_items = items;
    *out_count = count;
    return 0;
}

static char*
read_file_to_string(const char* filepath)
{
    FILE* f = fopen(filepath, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);

    char* buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t read_bytes = fread(buf, 1, (size_t)size, f);
    buf[read_bytes] = '\0';
    fclose(f);
    return buf;
}

int
mf_migration_apply(mf_migration_engine_t* engine, int* out_applied_count)
{
    if (!engine) {
        return -1;
    }

    if (mf_migration_lock_acquire(engine->db, "minefolio_engine", 10) != 0) {
        return -1;
    }

    if (ensure_schema_migrations_and_baseline(engine) != 0) {
        mf_migration_lock_release(engine->db, "minefolio_engine");
        return -1;
    }

    if (mf_migration_validate(engine) != 0) {
        mf_migration_lock_release(engine->db, "minefolio_engine");
        return -1;
    }

    mf_migration_item_t* items = NULL;
    int                  count = 0;
    if (mf_migration_discover(engine, &items, &count) != 0) {
        mf_migration_lock_release(engine->db, "minefolio_engine");
        return -1;
    }

    int applied_count = 0;
    for (int i = 0; i < count; i++) {
        mf_stmt_t* stmt = NULL;
        bool       already_applied = false;
        if (mf_stmt_prepare(
                engine->db, "SELECT 1 FROM schema_migrations WHERE version = ?;", &stmt) == 0 &&
            stmt) {
            mf_stmt_bind_int64(stmt, 1, items[i].version);
            mf_result_t* res = NULL;
            if (mf_stmt_query(stmt, &res) == 0 && res) {
                if (mf_result_next(res)) {
                    already_applied = true;
                }
                mf_result_free(res);
            }
            mf_stmt_close(stmt);
        }

        if (already_applied) {
            continue;
        }

        char* sql_content = read_file_to_string(items[i].filepath);
        if (!sql_content) {
            free(items);
            mf_migration_lock_release(engine->db, "minefolio_engine");
            return -1;
        }

        mf_tx_t* tx = NULL;
        if (mf_tx_begin(engine->db, &tx) != 0) {
            free(sql_content);
            free(items);
            mf_migration_lock_release(engine->db, "minefolio_engine");
            return -1;
        }

        struct timespec t_start, t_end;
        clock_gettime(CLOCK_MONOTONIC, &t_start);

        if (mf_tx_execute(tx, sql_content) != 0) {
            mf_tx_rollback(tx);
            free(sql_content);
            free(items);
            mf_migration_lock_release(engine->db, "minefolio_engine");
            return -1;
        }
        free(sql_content);

        clock_gettime(CLOCK_MONOTONIC, &t_end);
        int elapsed_ms = (int)((t_end.tv_sec - t_start.tv_sec) * 1000 +
                               (t_end.tv_nsec - t_start.tv_nsec) / 1000000);
        if (elapsed_ms < 0) {
            elapsed_ms = 0;
        }

        mf_stmt_t* ins_stmt = NULL;
        int        ins_rc =
            mf_tx_prepare(tx,
                          "INSERT INTO schema_migrations (version, name, checksum, applied_at, "
                          "execution_time_ms, execution_time) "
                          "VALUES (?, ?, ?, CURRENT_TIMESTAMP, ?, ?);",
                          &ins_stmt);
        if (ins_rc == 0 && ins_stmt) {
            mf_stmt_bind_int64(ins_stmt, 1, items[i].version);
            mf_stmt_bind_text(ins_stmt, 2, items[i].name);
            mf_stmt_bind_text(ins_stmt, 3, items[i].checksum);
            mf_stmt_bind_int64(ins_stmt, 4, elapsed_ms);
            mf_stmt_bind_int64(ins_stmt, 5, elapsed_ms);
            ins_rc = mf_stmt_execute(ins_stmt, NULL);
            mf_stmt_close(ins_stmt);
        }

        if (ins_rc != 0) {
            mf_tx_rollback(tx);
            free(items);
            mf_migration_lock_release(engine->db, "minefolio_engine");
            return -1;
        }

        if (mf_tx_commit(tx) != 0) {
            free(items);
            mf_migration_lock_release(engine->db, "minefolio_engine");
            return -1;
        }

        applied_count++;
    }

    free(items);
    mf_migration_lock_release(engine->db, "minefolio_engine");

    if (out_applied_count) {
        *out_applied_count = applied_count;
    }
    return 0;
}
