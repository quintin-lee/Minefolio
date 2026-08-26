#include "db.h"
#include "config.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static csilk_db_pool_t* g_pool = NULL;
static int g_is_postgres = 0;

int db_init(csilk_db_pool_t** out_pool) {
    csilk_db_init();

    const char* driver_env = getenv("MINEFOLIO_DB_DRIVER");
    const char* dsn_env    = getenv("MINEFOLIO_DB_DSN");

    /* Read persisted config (written by /system/setup) */
    char cfg_driver[32] = {0};
    char cfg_dsn[512]   = {0};
    config_get_str("config/db.json", "driver", cfg_driver, sizeof(cfg_driver));
    config_get_str("config/db.json", "dsn",    cfg_dsn,    sizeof(cfg_dsn));

    const char* driver = driver_env ? driver_env
           : cfg_driver[0]            ? cfg_driver
                                      : "sqlite";
    const char* dsn    = dsn_env    ? dsn_env
           : cfg_dsn[0]             ? cfg_dsn
                                      : (strcmp(driver, "postgres") == 0
                                         ? "host=localhost user=minefolio dbname=minefolio"
                                         : "./data/minefolio.db");

    g_is_postgres = (strcmp(driver, "postgres") == 0);
    CSILK_LOG_I("DB driver=%s dsn=%s", driver, dsn);

    if (!g_is_postgres) {
        /* Auto-create the data directory if the DSN is a file path */
        char dir[512];
        strncpy(dir, dsn, sizeof(dir) - 1); dir[sizeof(dir) - 1] = '\0';
        char* slash = strrchr(dir, '/');
        if (slash) { *slash = '\0'; mkdir(dir, 0755); }
    }
    g_pool = csilk_db_pool_new(driver, dsn);
    if (!g_pool) {
        CSILK_LOG_E("Failed to create database pool driver=%s dsn=%s", driver, dsn);
        return -1;
    }

    if (!g_is_postgres) {
        csilk_db_exec(g_pool, "PRAGMA journal_mode=WAL;");
        csilk_db_exec(g_pool, "PRAGMA busy_timeout=5000;");
        csilk_db_exec(g_pool, "PRAGMA synchronous=NORMAL;");
    }

    *out_pool = g_pool;
    return 0;
}

static int col_exists(csilk_db_pool_t* pool, const char* table, const char* column) {
    if (g_is_postgres) {
        const char* params[] = { table, column, NULL };
        csilk_json_t* res = csilk_db_query_param_json(pool,
            "SELECT 1 FROM information_schema.columns WHERE table_name=? AND column_name=?",
            params);
        int found = res && csilk_json_array_size(res) > 0;
        if (res) csilk_json_free(res);
        return found;
    } else {
        char sql[256];
        snprintf(sql, sizeof(sql), "PRAGMA table_info(%s)", table);
        csilk_json_t* cols = csilk_db_query_json(pool, sql);
        if (!cols) return 0;
        size_t n = csilk_json_array_size(cols);
        for (size_t i = 0; i < n; i++) {
            const char* cname = csilk_json_get_string(csilk_json_array_get(cols, i), "name");
            if (cname && strcmp(cname, column) == 0) {
                csilk_json_free(cols);
                return 1;
            }
        }
        csilk_json_free(cols);
        return 0;
    }
}

int db_run_migrations(csilk_db_pool_t* pool) {
    CSILK_LOG_I("Running migrations is_postgres=%d", g_is_postgres);

    if (g_is_postgres) {
        // PostgreSQL: run the PG-specific migration SQL
        FILE* f = fopen("sql/migration_postgres.sql", "r");
        if (!f) f = fopen("./sql/migration_postgres.sql", "r");
        if (!f) {
            CSILK_LOG_E("Cannot open migration_postgres.sql");
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
        if (csilk_db_exec(pool, sql) != 0) {
            CSILK_LOG_E("PostgreSQL migration error");
            free(sql);
            return -1;
        }
        free(sql);
        // ---- 方向列 + 移除 transaction_type CHECK（PG 存量库） ----
        csilk_db_exec(pool, "ALTER TABLE transactions ADD COLUMN IF NOT EXISTS direction TEXT NOT NULL DEFAULT 'out' CHECK(direction IN ('in','out','neutral'))");
        csilk_db_exec(pool, "ALTER TABLE transactions ADD COLUMN IF NOT EXISTS linked_direction TEXT CHECK(linked_direction IN ('in','out','neutral'))");
        csilk_db_exec(pool, "UPDATE transactions SET direction='in' WHERE transaction_type IN ('deposit','sell','income','transfer_in')");
        csilk_db_exec(pool, "UPDATE transactions SET linked_direction='in' WHERE transaction_type IN ('sell','withdrawal','income','transfer_out')");
        csilk_db_exec(pool, "UPDATE transactions SET linked_direction='out' WHERE linked_direction IS NULL");
        csilk_db_exec(pool, "ALTER TABLE transactions DROP CONSTRAINT IF EXISTS transactions_transaction_type_check");
        // ---- assets 持仓三列迁移（PG 存量库） ----
        if (!col_exists(pool, "assets", "quantity")) {
            csilk_db_exec(pool, "ALTER TABLE assets ADD COLUMN IF NOT EXISTS quantity DECIMAL(18,4) NOT NULL DEFAULT 0");
            csilk_db_exec(pool, "ALTER TABLE assets ADD COLUMN IF NOT EXISTS cost_basis DECIMAL(18,4) NOT NULL DEFAULT 0");
            csilk_db_exec(pool, "ALTER TABLE assets ADD COLUMN IF NOT EXISTS net_value DECIMAL(18,4) NOT NULL DEFAULT 0");
        }
        if (!col_exists(pool, "transactions", "fee")) {
            csilk_db_exec(pool, "ALTER TABLE transactions ADD COLUMN IF NOT EXISTS fee DECIMAL(18,2) DEFAULT 0");
        }
        return 0;
    }

    // SQLite: run the original migration SQL
    FILE* f = fopen("sql/migration.sql", "r");
    if (!f) f = fopen("backend/sql/migration.sql", "r");
    if (!f) f = fopen("./sql/migration.sql", "r");
    if (!f) f = fopen("../sql/migration.sql", "r");
    if (!f) {
        CSILK_LOG_E("Cannot open migration.sql");
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
        CSILK_LOG_E("Migration SQL error");
        free(sql);
        return -1;
    }
    CSILK_LOG_I("Initial migration SQL applied");

    // Try adding 'type' column for pre-existing databases (ignore failure if column already exists)
    csilk_db_exec(pool, "ALTER TABLE categories ADD COLUMN type TEXT NOT NULL DEFAULT 'asset'");

    // ---- users token_version 列迁移（仅新建表时由 migration.sql 处理，存量库补列）----
    if (!col_exists(pool, "users", "token_version")) {
        csilk_db_exec(pool, "ALTER TABLE users ADD COLUMN token_version INTEGER NOT NULL DEFAULT 0");
        CSILK_LOG_I("Migration: added users.token_version column");
    }

    // ---- 交易分类 CHECK 约束迁移 ----
    csilk_json_t* cat_schema = csilk_db_query_json(pool, "SELECT sql FROM sqlite_master WHERE type='table' AND name='categories'");
    if (cat_schema && csilk_json_array_size(cat_schema) > 0) {
        const char* sql_def = csilk_json_get_string(csilk_json_array_get(cat_schema, 0), "sql");
        if (sql_def && !strstr(sql_def, "'transaction'")) {
            CSILK_LOG_I("Migration: rewriting categories table to add 'transaction' type");
            csilk_db_exec(pool, "PRAGMA foreign_keys=OFF");
            csilk_db_exec(pool, "BEGIN TRANSACTION");
            csilk_db_exec(pool,
                "CREATE TABLE categories_new ("
                "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "  user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,"
                "  name TEXT NOT NULL,"
                "  parent_id INTEGER REFERENCES categories(id) ON DELETE SET NULL,"
                "  type TEXT NOT NULL DEFAULT 'asset' CHECK(type IN ('asset','income','expense','transaction')),"
                "  asset_type TEXT DEFAULT 'cash' CHECK(asset_type IN ('cash','stock','fund','bond','crypto','real_estate','vehicle','other_asset','loan','credit_card','other_liability')),"
                "  currency TEXT DEFAULT 'CNY',"
                "  icon TEXT,"
                "  sort_order INTEGER DEFAULT 0,"
                "  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
                "  UNIQUE(user_id, name, parent_id)"
                ")");
            csilk_db_exec(pool, "INSERT INTO categories_new SELECT * FROM categories");
            csilk_db_exec(pool, "DROP TABLE categories");
            csilk_db_exec(pool, "ALTER TABLE categories_new RENAME TO categories");
            csilk_db_exec(pool, "COMMIT");
            csilk_db_exec(pool, "PRAGMA foreign_keys=ON");
        }
    }
    if (cat_schema) csilk_json_free(cat_schema);

    // ---- transactions 表 category_id 可空迁移 ----
    csilk_json_t* tx_schema = csilk_db_query_json(pool, "SELECT sql FROM sqlite_master WHERE type='table' AND name='transactions'");
    if (tx_schema && csilk_json_array_size(tx_schema) > 0) {
        const char* sql_def = csilk_json_get_string(csilk_json_array_get(tx_schema, 0), "sql");
        if (sql_def && strstr(sql_def, "category_id      INTEGER NOT NULL")) {
            CSILK_LOG_I("Migration: making transactions.category_id nullable");
            csilk_db_exec(pool, "PRAGMA foreign_keys=OFF");
            csilk_db_exec(pool, "BEGIN TRANSACTION");
            csilk_db_exec(pool,
                "CREATE TABLE transactions_new ("
                "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "  user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,"
                "  asset_id INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,"
                "  category_id INTEGER REFERENCES categories(id) ON DELETE RESTRICT,"
                "  source_type TEXT NOT NULL DEFAULT 'expense' CHECK(source_type IN ('income', 'expense')),"
                "  transaction_type TEXT NOT NULL CHECK(transaction_type IN ('deposit','withdrawal','buy','sell','transfer_in','transfer_out','fee','income','loss')),"
                "  amount DECIMAL(18,2) NOT NULL,"
                "  price_per_unit DECIMAL(18,4),"
                "  quantity DECIMAL(18,4),"
                "  currency TEXT DEFAULT 'CNY',"
                "  transaction_date TIMESTAMP NOT NULL,"
                "  note TEXT,"
                "  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
                ")");
            csilk_db_exec(pool, "INSERT INTO transactions_new SELECT * FROM transactions");
            csilk_db_exec(pool, "DROP TABLE transactions");
            csilk_db_exec(pool, "ALTER TABLE transactions_new RENAME TO transactions");
            csilk_db_exec(pool, "COMMIT");
            csilk_db_exec(pool, "PRAGMA foreign_keys=ON");
        }
    }
    if (tx_schema) csilk_json_free(tx_schema);

    // ---- transactions 表 linked_asset_id 列迁移 ----
    if (!col_exists(pool, "transactions", "linked_asset_id")) {
        csilk_db_exec(pool, "ALTER TABLE transactions ADD COLUMN linked_asset_id INTEGER REFERENCES assets(id) ON DELETE SET NULL");
        CSILK_LOG_I("Migration: added transactions.linked_asset_id");
    }

    // ---- 收支-资产联动迁移（列存在性门控，一次性） ----
    if (!col_exists(pool, "daily_expenses", "asset_id")) {
        CSILK_LOG_I("Migration: adding daily_expenses.asset_id (clearing dependent data)");
        csilk_db_exec(pool, "DELETE FROM expense_tags");
        csilk_db_exec(pool, "DELETE FROM daily_expenses");
        csilk_db_exec(pool, "DELETE FROM transactions");
        if (csilk_db_exec(pool,
                "ALTER TABLE daily_expenses ADD COLUMN asset_id INTEGER NOT NULL "
                "REFERENCES assets(id) ON DELETE CASCADE") != 0) {
            CSILK_LOG_E("Migration: cannot add asset_id to daily_expenses");
            free(sql);
            return -1;
        }
    }

    // 无条件幂等建索引（全新库首启 / 存量库 ALTER 后 / 失败自愈均覆盖）
    csilk_db_exec(pool, "CREATE INDEX IF NOT EXISTS idx_daily_expenses_asset ON daily_expenses(asset_id)");

    // ---- 收支类型区分迁移（source_type 列） ----
    if (!col_exists(pool, "transactions", "source_type")) {
        if (csilk_db_exec(pool,
                "ALTER TABLE transactions ADD COLUMN source_type TEXT NOT NULL DEFAULT 'expense'") != 0) {
            CSILK_LOG_E("Migration: cannot add source_type to transactions");
            free(sql);
            return -1;
        }
        // 回填：根据交易类型推断收支方向
        csilk_db_exec(pool, "UPDATE transactions SET source_type='income' WHERE transaction_type IN ('deposit','income')");
        csilk_db_exec(pool, "UPDATE transactions SET source_type='expense' WHERE transaction_type IN ('withdrawal','buy','fee','loss')");
        CSILK_LOG_I("Migration: added transactions.source_type and backfilled");
    }

    // ---- transactions direction / linked_direction 列迁移 ----
    if (!col_exists(pool, "transactions", "direction")) {
        if (csilk_db_exec(pool,
                "ALTER TABLE transactions ADD COLUMN direction TEXT NOT NULL DEFAULT 'out' "
                "CHECK(direction IN ('in','out','neutral'))") != 0) {
            CSILK_LOG_E("Migration: cannot add direction to transactions");
            free(sql);
            return -1;
        }
    }
    if (!col_exists(pool, "transactions", "linked_direction")) {
        if (csilk_db_exec(pool,
                "ALTER TABLE transactions ADD COLUMN linked_direction TEXT "
                "CHECK(linked_direction IN ('in','out','neutral'))") != 0) {
            CSILK_LOG_E("Migration: cannot add linked_direction to transactions");
            free(sql);
            return -1;
        }
    }
    // 回填：direction / linked_direction 按存量类型推断
    csilk_db_exec(pool, "UPDATE transactions SET direction='in' WHERE transaction_type IN ('deposit','sell','income','transfer_in')");
    csilk_db_exec(pool, "UPDATE transactions SET linked_direction='in' WHERE transaction_type IN ('sell','withdrawal','income','transfer_out')");
    csilk_db_exec(pool, "UPDATE transactions SET linked_direction='out' WHERE linked_direction IS NULL");
    CSILK_LOG_I("Migration: backfilled transactions.direction / linked_direction");

    // ---- transactions 表 transaction_type CHECK 移除重建（须在 direction 列迁移之后） ----
    csilk_json_t* txdir_schema = csilk_db_query_json(pool, "SELECT sql FROM sqlite_master WHERE type='table' AND name='transactions'");
    if (txdir_schema && csilk_json_array_size(txdir_schema) > 0) {
        const char* sql_def = csilk_json_get_string(csilk_json_array_get(txdir_schema, 0), "sql");
        if (sql_def && strstr(sql_def, "CHECK(transaction_type IN")) {
            CSILK_LOG_I("Migration: removing transactions.transaction_type CHECK constraint");
            csilk_db_exec(pool, "PRAGMA foreign_keys=OFF");
            csilk_db_exec(pool, "BEGIN TRANSACTION");
            csilk_db_exec(pool,
                "CREATE TABLE transactions_new ("
                "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "  user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,"
                "  asset_id INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,"
                "  linked_asset_id INTEGER REFERENCES assets(id) ON DELETE SET NULL,"
                "  category_id INTEGER REFERENCES categories(id) ON DELETE RESTRICT,"
                "  source_type TEXT NOT NULL DEFAULT 'expense' CHECK(source_type IN ('income', 'expense')),"
                "  transaction_type TEXT NOT NULL,"
                "  direction TEXT NOT NULL DEFAULT 'out' CHECK(direction IN ('in','out','neutral')),"
                "  linked_direction TEXT CHECK(linked_direction IN ('in','out','neutral')),"
                "  amount DECIMAL(18,2) NOT NULL,"
                "  price_per_unit DECIMAL(18,4),"
                "  quantity DECIMAL(18,4),"
                "  currency TEXT DEFAULT 'CNY',"
                "  transaction_date TIMESTAMP NOT NULL,"
                "  note TEXT,"
                "  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
                ")");
            csilk_db_exec(pool,
                "INSERT INTO transactions_new (id, user_id, asset_id, linked_asset_id, category_id, source_type, "
                "transaction_type, direction, linked_direction, amount, price_per_unit, quantity, currency, "
                "transaction_date, note, created_at) "
                "SELECT id, user_id, asset_id, linked_asset_id, category_id, source_type, "
                "transaction_type, direction, linked_direction, amount, price_per_unit, quantity, currency, "
                "transaction_date, note, created_at FROM transactions");
            csilk_db_exec(pool, "DROP TABLE transactions");
            csilk_db_exec(pool, "ALTER TABLE transactions_new RENAME TO transactions");
            csilk_db_exec(pool, "COMMIT");
            csilk_db_exec(pool, "PRAGMA foreign_keys=ON");
        }
    }
    if (txdir_schema) csilk_json_free(txdir_schema);

    // ---- assets 持仓三列迁移（SQLite 存量库） ----
    if (!col_exists(pool, "assets", "quantity")) {
        if (csilk_db_exec(pool,
                "ALTER TABLE assets ADD COLUMN quantity DECIMAL(18,4) NOT NULL DEFAULT 0") != 0) {
            CSILK_LOG_E("Migration: cannot add quantity to assets");
            free(sql);
            return -1;
        }
        CSILK_LOG_I("Migration: added assets.quantity");
    }
    if (!col_exists(pool, "assets", "cost_basis")) {
        if (csilk_db_exec(pool,
                "ALTER TABLE assets ADD COLUMN cost_basis DECIMAL(18,4) NOT NULL DEFAULT 0") != 0) {
            CSILK_LOG_E("Migration: cannot add cost_basis to assets");
            free(sql);
            return -1;
        }
        CSILK_LOG_I("Migration: added assets.cost_basis");
    }
    if (!col_exists(pool, "assets", "net_value")) {
        if (csilk_db_exec(pool,
                "ALTER TABLE assets ADD COLUMN net_value DECIMAL(18,4) NOT NULL DEFAULT 0") != 0) {
            CSILK_LOG_E("Migration: cannot add net_value to assets");
            free(sql);
            return -1;
        }
        CSILK_LOG_I("Migration: added assets.net_value");
    }
    // ---- transactions fee 列迁移（SQLite 存量库） ----
    if (!col_exists(pool, "transactions", "fee")) {
        if (csilk_db_exec(pool,
                "ALTER TABLE transactions ADD COLUMN fee DECIMAL(18,2) DEFAULT 0") != 0) {
            CSILK_LOG_E("Migration: cannot add fee to transactions");
            free(sql);
            return -1;
        }
        CSILK_LOG_I("Migration: added transactions.fee");
    }

    // ---- ai_traces 历史异常残留清理 ----
    csilk_db_exec(pool, "UPDATE ai_traces SET error_message = '' WHERE status = 'ok' AND error_message != ''");

    free(sql);
    CSILK_LOG_I("All migrations completed");
    return 0;
}

csilk_db_pool_t* db_get_pool(void) {
    return g_pool;
}

int db_is_postgres(void) {
    return g_is_postgres;
}
