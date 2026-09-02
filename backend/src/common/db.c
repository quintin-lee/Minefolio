#include "db.h"
#include "config.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static csilk_db_pool_t* g_pool = NULL;
static int              g_is_postgres = 0;

int
db_init(csilk_db_pool_t** out_pool)
{
    csilk_db_init();

    const char* driver_env = getenv("MINEFOLIO_DB_DRIVER");
    const char* dsn_env = getenv("MINEFOLIO_DB_DSN");

    /* Read persisted config (written by /system/setup) */
    char cfg_driver[32] = {0};
    char cfg_dsn[512] = {0};
    config_get_str("config/db.json", "driver", cfg_driver, sizeof(cfg_driver));
    config_get_str("config/db.json", "dsn", cfg_dsn, sizeof(cfg_dsn));

    const char* driver = driver_env ? driver_env : cfg_driver[0] ? cfg_driver : "sqlite";
    const char* dsn = dsn_env      ? dsn_env
                      : cfg_dsn[0] ? cfg_dsn
                                   : (strcmp(driver, "postgres") == 0
                                          ? "host=localhost user=minefolio dbname=minefolio"
                                          : "./data/minefolio.db");

    g_is_postgres = (strcmp(driver, "postgres") == 0);
    CSILK_LOG_I("DB driver=%s dsn=%s", driver, dsn);

    if (!g_is_postgres) {
        /* Auto-create the data directory if the DSN is a file path */
        char dir[512];
        strncpy(dir, dsn, sizeof(dir) - 1);
        dir[sizeof(dir) - 1] = '\0';
        char* slash = strrchr(dir, '/');
        if (slash) {
            *slash = '\0';
            mkdir(dir, 0755);
        }
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
        csilk_db_exec(g_pool, "UPDATE ai_traces SET top_p = 0.0 WHERE typeof(top_p) = 'text';");
        csilk_db_exec(g_pool,
                      "UPDATE ai_traces SET temperature = 0.0 WHERE typeof(temperature) = 'text';");
    }

    *out_pool = g_pool;
    return 0;
}

static int
col_exists(csilk_db_pool_t* pool, const char* table, const char* column)
{
    if (g_is_postgres) {
        const char*   params[] = {table, column, NULL};
        csilk_json_t* res = csilk_db_query_param_json(
            pool,
            "SELECT 1 FROM information_schema.columns WHERE table_name=? AND column_name=?",
            params);
        int found = res && csilk_json_array_size(res) > 0;
        if (res) {
            csilk_json_free(res);
        }
        return found;
    } else {
        char sql[256];
        snprintf(sql, sizeof(sql), "PRAGMA table_info(%s)", table);
        csilk_json_t* cols = csilk_db_query_json(pool, sql);
        if (!cols) {
            return 0;
        }
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

int
db_run_migrations(csilk_db_pool_t* pool)
{
    CSILK_LOG_I("Running migrations is_postgres=%d", g_is_postgres);

    if (g_is_postgres) {
        // PostgreSQL: run the PG-specific migration SQL
        FILE* f = fopen("sql/migration_postgres.sql", "r");
        if (!f) {
            f = fopen("./sql/migration_postgres.sql", "r");
        }
        if (!f) {
            CSILK_LOG_E("Cannot open migration_postgres.sql");
            return -1;
        }
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        rewind(f);
        char* sql = malloc((size_t)len + 1);
        if (!sql) {
            fclose(f);
            return -1;
        }
        size_t n = fread(sql, 1, (size_t)len, f);
        sql[n] = '\0';
        fclose(f);
        if (csilk_db_exec(pool, sql) != 0) {
            CSILK_LOG_E("PostgreSQL migration error");
            free(sql);
            return -1;
        }
        free(sql);
        if (!col_exists(pool, "transactions", "direction")) {
            csilk_db_exec(
                pool,
                "ALTER TABLE transactions ADD COLUMN IF NOT EXISTS direction TEXT NOT NULL "
                "DEFAULT 'out' CHECK(direction IN ('in','out','neutral'))");
        }
        csilk_db_exec(pool,
                      "ALTER TABLE transactions ADD COLUMN IF NOT EXISTS linked_direction TEXT "
                      "CHECK(linked_direction IN ('in','out','neutral'))");
        csilk_db_exec(pool,
                      "UPDATE transactions SET direction='in' WHERE transaction_type IN "
                      "('deposit','sell','income','transfer_in')");
        csilk_db_exec(pool,
                      "UPDATE transactions SET linked_direction='in' WHERE transaction_type IN "
                      "('sell','withdrawal','income','transfer_out')");
        csilk_db_exec(
            pool, "UPDATE transactions SET linked_direction='out' WHERE linked_direction IS NULL");
        csilk_db_exec(pool,
                      "ALTER TABLE transactions DROP CONSTRAINT IF EXISTS "
                      "transactions_transaction_type_check");
        csilk_db_exec(pool,
                      "ALTER TABLE transactions ADD COLUMN IF NOT EXISTS parent_tx_id BIGINT "
                      "REFERENCES transactions(id) ON DELETE CASCADE");

        if (!col_exists(pool, "assets", "quantity")) {
            csilk_db_exec(pool,
                          "ALTER TABLE assets ADD COLUMN IF NOT EXISTS quantity DECIMAL(18,4) NOT "
                          "NULL DEFAULT 0");
            csilk_db_exec(pool,
                          "ALTER TABLE assets ADD COLUMN IF NOT EXISTS cost_basis DECIMAL(18,4) "
                          "NOT NULL DEFAULT 0");
            csilk_db_exec(pool,
                          "ALTER TABLE assets ADD COLUMN IF NOT EXISTS net_value DECIMAL(18,4) NOT "
                          "NULL DEFAULT 0");
        }
        if (!col_exists(pool, "assets", "symbol")) {
            csilk_db_exec(pool,
                          "ALTER TABLE assets ADD COLUMN IF NOT EXISTS symbol TEXT DEFAULT ''");
            csilk_db_exec(pool,
                          "ALTER TABLE assets ADD COLUMN IF NOT EXISTS last_sync_at TIMESTAMP");
        }
        if (!col_exists(pool, "users", "totp_secret")) {
            csilk_db_exec(pool,
                          "ALTER TABLE users ADD COLUMN IF NOT EXISTS totp_secret TEXT DEFAULT ''");
            csilk_db_exec(
                pool,
                "ALTER TABLE users ADD COLUMN IF NOT EXISTS totp_enabled BOOLEAN DEFAULT FALSE");
            csilk_db_exec(
                pool,
                "ALTER TABLE users ADD COLUMN IF NOT EXISTS totp_backup_codes TEXT DEFAULT ''");
        }
        csilk_db_exec(pool,
                      "CREATE TABLE IF NOT EXISTS asset_price_history ("
                      "id BIGSERIAL PRIMARY KEY, "
                      "asset_id BIGINT NOT NULL REFERENCES assets(id) ON DELETE CASCADE, "
                      "price_date DATE NOT NULL, "
                      "price DOUBLE PRECISION NOT NULL, "
                      "currency VARCHAR(16) DEFAULT 'CNY', "
                      "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP, "
                      "UNIQUE(asset_id, price_date))");
        csilk_db_exec(pool,
                      "CREATE INDEX IF NOT EXISTS idx_price_history_asset_date ON "
                      "asset_price_history(asset_id, price_date DESC)");
        csilk_db_exec(pool,
                      "CREATE TABLE IF NOT EXISTS import_rules ("
                      "id BIGSERIAL PRIMARY KEY, "
                      "user_id BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE, "
                      "keyword TEXT NOT NULL, "
                      "match_field VARCHAR(32) NOT NULL DEFAULT 'all', "
                      "match_type VARCHAR(32) NOT NULL DEFAULT 'contains', "
                      "category_id BIGINT REFERENCES categories(id) ON DELETE SET NULL, "
                      "target_type VARCHAR(32) NOT NULL DEFAULT 'expense', "
                      "priority INTEGER NOT NULL DEFAULT 100, "
                      "is_active BOOLEAN NOT NULL DEFAULT TRUE, "
                      "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP)");
        csilk_db_exec(pool,
                      "CREATE INDEX IF NOT EXISTS idx_import_rules_user ON import_rules(user_id, "
                      "priority ASC)");
        return 0;
    }

    // SQLite: run the original migration SQL
    FILE* f = fopen("sql/migration.sql", "r");
    if (!f) {
        f = fopen("backend/sql/migration.sql", "r");
    }
    if (!f) {
        f = fopen("./sql/migration.sql", "r");
    }
    if (!f) {
        f = fopen("../sql/migration.sql", "r");
    }
    if (!f) {
        CSILK_LOG_E("Cannot open migration.sql");
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);

    char* sql = malloc((size_t)len + 1);
    if (!sql) {
        fclose(f);
        return -1;
    }
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
        csilk_db_exec(pool,
                      "ALTER TABLE users ADD COLUMN token_version INTEGER NOT NULL DEFAULT 0");
        CSILK_LOG_I("Migration: added users.token_version column");
    }

    // ---- users totp_* 列迁移 ----
    if (!col_exists(pool, "users", "totp_secret")) {
        csilk_db_exec(pool, "ALTER TABLE users ADD COLUMN totp_secret TEXT DEFAULT ''");
        csilk_db_exec(pool, "ALTER TABLE users ADD COLUMN totp_enabled BOOLEAN DEFAULT 0");
        csilk_db_exec(pool, "ALTER TABLE users ADD COLUMN totp_backup_codes TEXT DEFAULT ''");
        CSILK_LOG_I("Migration: added users.totp_* columns");
    }

    // ---- users oauth_* 列迁移 ----
    if (!col_exists(pool, "users", "oauth_provider")) {
        csilk_db_exec(pool, "ALTER TABLE users ADD COLUMN oauth_provider TEXT DEFAULT ''");
        csilk_db_exec(pool, "ALTER TABLE users ADD COLUMN oauth_id TEXT DEFAULT ''");
        CSILK_LOG_I("Migration: added users.oauth_* columns");
    }

    // ---- 交易分类 CHECK 约束迁移 ----
    csilk_json_t* cat_schema = csilk_db_query_json(
        pool, "SELECT sql FROM sqlite_master WHERE type='table' AND name='categories'");
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
                          "  type TEXT NOT NULL DEFAULT 'asset' CHECK(type IN "
                          "('asset','income','expense','transaction')),"
                          "  asset_type TEXT DEFAULT 'cash' CHECK(asset_type IN "
                          "('cash','stock','fund','bond','crypto','real_estate','vehicle','other_"
                          "asset','loan','credit_card','other_liability')),"
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
    if (cat_schema) {
        csilk_json_free(cat_schema);
    }

    // ---- transactions 表 category_id 可空迁移 ----
    csilk_json_t* tx_schema = csilk_db_query_json(
        pool, "SELECT sql FROM sqlite_master WHERE type='table' AND name='transactions'");
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
                          "  source_type TEXT NOT NULL DEFAULT 'expense' CHECK(source_type IN "
                          "('income', 'expense')),"
                          "  transaction_type TEXT NOT NULL CHECK(transaction_type IN "
                          "('deposit','withdrawal','buy','sell','transfer_in','transfer_out','fee',"
                          "'income','loss')),"
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
    if (tx_schema) {
        csilk_json_free(tx_schema);
    }

    // ---- transactions 表 linked_asset_id 列迁移 ----
    if (!col_exists(pool, "transactions", "linked_asset_id")) {
        csilk_db_exec(pool,
                      "ALTER TABLE transactions ADD COLUMN linked_asset_id INTEGER REFERENCES "
                      "assets(id) ON DELETE SET NULL");
    }

    // ---- transactions parent_tx_id 列迁移（fee 子行归属父交易） ----
    if (!col_exists(pool, "transactions", "parent_tx_id")) {
        csilk_db_exec(pool,
                      "ALTER TABLE transactions ADD COLUMN parent_tx_id INTEGER REFERENCES "
                      "transactions(id) ON DELETE CASCADE");
        CSILK_LOG_I("Migration: added transactions.parent_tx_id");
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
    csilk_db_exec(
        pool, "CREATE INDEX IF NOT EXISTS idx_daily_expenses_asset ON daily_expenses(asset_id)");

    // ---- 收支类型区分迁移（source_type 列） ----
    if (!col_exists(pool, "transactions", "source_type")) {
        if (csilk_db_exec(pool,
                          "ALTER TABLE transactions ADD COLUMN source_type TEXT NOT NULL DEFAULT "
                          "'expense'") != 0) {
            CSILK_LOG_E("Migration: cannot add source_type to transactions");
            free(sql);
            return -1;
        }
        // 回填：根据交易类型推断收支方向
        csilk_db_exec(pool,
                      "UPDATE transactions SET source_type='income' WHERE transaction_type IN "
                      "('deposit','income')");
        csilk_db_exec(pool,
                      "UPDATE transactions SET source_type='expense' WHERE transaction_type IN "
                      "('withdrawal','buy','fee','loss')");
        CSILK_LOG_I("Migration: added transactions.source_type and backfilled");
    }

    // ---- transactions direction / linked_direction 列迁移 ----
    if (!col_exists(pool, "transactions", "direction")) {
        if (csilk_db_exec(
                pool,
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
    csilk_db_exec(pool,
                  "UPDATE transactions SET direction='in' WHERE transaction_type IN "
                  "('deposit','sell','income','transfer_in')");
    csilk_db_exec(pool,
                  "UPDATE transactions SET linked_direction='in' WHERE transaction_type IN "
                  "('sell','withdrawal','income','transfer_out')");
    csilk_db_exec(pool,
                  "UPDATE transactions SET linked_direction='out' WHERE linked_direction IS NULL");
    CSILK_LOG_I("Migration: backfilled transactions.direction / linked_direction");

    // ---- transactions quantity 列迁移 ----
    if (!col_exists(pool, "transactions", "quantity")) {
        if (csilk_db_exec(pool, "ALTER TABLE transactions ADD COLUMN quantity DECIMAL(18,4)") !=
            0) {
            CSILK_LOG_E("Migration: cannot add quantity to transactions");
            free(sql);
            return -1;
        }
        CSILK_LOG_I("Migration: added transactions.quantity column");
    }

    // ---- transactions 表 transaction_type CHECK 移除重建（须在 direction 列迁移之后） ----
    csilk_json_t* txdir_schema = csilk_db_query_json(
        pool, "SELECT sql FROM sqlite_master WHERE type='table' AND name='transactions'");
    if (txdir_schema && csilk_json_array_size(txdir_schema) > 0) {
        const char* sql_def = csilk_json_get_string(csilk_json_array_get(txdir_schema, 0), "sql");
        if (sql_def && strstr(sql_def, "CHECK(transaction_type IN")) {
            CSILK_LOG_I("Migration: removing transactions.transaction_type CHECK constraint");
            csilk_db_exec(pool, "PRAGMA foreign_keys=OFF");
            csilk_db_exec(pool, "BEGIN TRANSACTION");
            csilk_db_exec(
                pool,
                "CREATE TABLE transactions_new ("
                "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "  user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,"
                "  asset_id INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,"
                "  linked_asset_id INTEGER REFERENCES assets(id) ON DELETE SET NULL,"
                "  category_id INTEGER REFERENCES categories(id) ON DELETE RESTRICT,"
                "  source_type TEXT NOT NULL DEFAULT 'expense' CHECK(source_type IN ('income', "
                "'expense')),"
                "  transaction_type TEXT NOT NULL,"
                "  direction TEXT NOT NULL DEFAULT 'out' CHECK(direction IN "
                "('in','out','neutral')),"
                "  linked_direction TEXT CHECK(linked_direction IN ('in','out','neutral')),"
                "  amount DECIMAL(18,2) NOT NULL,"
                "  price_per_unit DECIMAL(18,4),"
                "  quantity DECIMAL(18,4),"
                "  currency TEXT DEFAULT 'CNY',"
                "  transaction_date TIMESTAMP NOT NULL,"
                "  note TEXT,"
                "  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
                ")");
            csilk_db_exec(
                pool,
                "INSERT INTO transactions_new (id, user_id, asset_id, linked_asset_id, "
                "category_id, source_type, "
                "transaction_type, direction, linked_direction, amount, price_per_unit, quantity, "
                "currency, "
                "transaction_date, note, created_at) "
                "SELECT id, user_id, asset_id, linked_asset_id, category_id, source_type, "
                "transaction_type, direction, linked_direction, amount, price_per_unit, quantity, "
                "currency, "
                "transaction_date, note, created_at FROM transactions");
            csilk_db_exec(pool, "DROP TABLE transactions");
            csilk_db_exec(pool, "ALTER TABLE transactions_new RENAME TO transactions");
            csilk_db_exec(pool, "COMMIT");
            csilk_db_exec(pool, "PRAGMA foreign_keys=ON");
        }
    }
    if (txdir_schema) {
        csilk_json_free(txdir_schema);
    }

    // ---- assets 持仓三列迁移（SQLite 存量库） ----
    if (!col_exists(pool, "assets", "quantity")) {
        if (csilk_db_exec(
                pool, "ALTER TABLE assets ADD COLUMN quantity DECIMAL(18,4) NOT NULL DEFAULT 0") !=
            0) {
            CSILK_LOG_E("Migration: cannot add quantity to assets");
            free(sql);
            return -1;
        }
        CSILK_LOG_I("Migration: added assets.quantity");
    }
    if (!col_exists(pool, "assets", "cost_basis")) {
        if (csilk_db_exec(
                pool,
                "ALTER TABLE assets ADD COLUMN cost_basis DECIMAL(18,4) NOT NULL DEFAULT 0") != 0) {
            CSILK_LOG_E("Migration: cannot add cost_basis to assets");
            free(sql);
            return -1;
        }
        CSILK_LOG_I("Migration: added assets.cost_basis");
    }
    if (!col_exists(pool, "assets", "net_value")) {
        if (csilk_db_exec(
                pool, "ALTER TABLE assets ADD COLUMN net_value DECIMAL(18,4) NOT NULL DEFAULT 0") !=
            0) {
            CSILK_LOG_E("Migration: cannot add net_value to assets");
            free(sql);
            return -1;
        }
        CSILK_LOG_I("Migration: added assets.net_value");
    }
    // ---- assets symbol / quote_source / last_sync_at 列迁移（SQLite 存量库） ----
    if (!col_exists(pool, "assets", "symbol")) {
        csilk_db_exec(pool, "ALTER TABLE assets ADD COLUMN symbol TEXT DEFAULT ''");
        csilk_db_exec(pool, "ALTER TABLE assets ADD COLUMN quote_source TEXT DEFAULT ''");
        csilk_db_exec(pool, "ALTER TABLE assets ADD COLUMN last_sync_at TIMESTAMP");
        CSILK_LOG_I("Migration: added assets.symbol/quote_source/last_sync_at");
    }
    csilk_db_exec(pool,
                  "CREATE TABLE IF NOT EXISTS asset_price_history ("
                  "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                  "asset_id INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE, "
                  "price_date DATE NOT NULL, "
                  "price DECIMAL(18,4) NOT NULL, "
                  "currency TEXT DEFAULT 'CNY', "
                  "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP, "
                  "UNIQUE(asset_id, price_date))");
    csilk_db_exec(pool,
                  "CREATE INDEX IF NOT EXISTS idx_price_history_asset_date ON "
                  "asset_price_history(asset_id, price_date DESC)");

    // ---- 账单导入智能规则表迁移 ----
    csilk_db_exec(pool,
                  "CREATE TABLE IF NOT EXISTS import_rules ("
                  "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                  "user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE, "
                  "keyword TEXT NOT NULL, "
                  "match_field TEXT NOT NULL DEFAULT 'all', "
                  "match_type TEXT NOT NULL DEFAULT 'contains', "
                  "category_id INTEGER REFERENCES categories(id) ON DELETE SET NULL, "
                  "target_type TEXT NOT NULL DEFAULT 'expense', "
                  "priority INTEGER NOT NULL DEFAULT 100, "
                  "is_active BOOLEAN NOT NULL DEFAULT 1, "
                  "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP)");
    csilk_db_exec(
        pool,
        "CREATE INDEX IF NOT EXISTS idx_import_rules_user ON import_rules(user_id, priority ASC)");

    // ---- 定投计划与现金流表迁移（SQLite / Postgres 存量库） ----
    csilk_db_exec(pool,
                  "CREATE TABLE IF NOT EXISTS dca_plans ("
                  "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                  "user_id INTEGER NOT NULL REFERENCES users(id), "
                  "target_asset_id INTEGER NOT NULL REFERENCES assets(id), "
                  "funding_asset_id INTEGER NOT NULL REFERENCES assets(id), "
                  "name TEXT NOT NULL, "
                  "frequency TEXT NOT NULL DEFAULT 'monthly', "
                  "day_of_period INTEGER NOT NULL DEFAULT 1, "
                  "amount DECIMAL(18,2) NOT NULL, "
                  "target_profit_rate DECIMAL(8,4) DEFAULT 0, "
                  "target_total_amount DECIMAL(18,2) DEFAULT 0, "
                  "target_total_periods INTEGER DEFAULT 0, "
                  "status TEXT NOT NULL DEFAULT 'active', "
                  "note TEXT DEFAULT '', "
                  "created_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
                  "updated_at DATETIME DEFAULT CURRENT_TIMESTAMP)");
    csilk_db_exec(
        pool, "CREATE INDEX IF NOT EXISTS idx_dca_plans_user_status ON dca_plans(user_id, status)");

    csilk_db_exec(pool,
                  "CREATE TABLE IF NOT EXISTS dca_executions ("
                  "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                  "plan_id INTEGER NOT NULL REFERENCES dca_plans(id) ON DELETE CASCADE, "
                  "user_id INTEGER NOT NULL REFERENCES users(id), "
                  "period_date TEXT NOT NULL, "
                  "planned_amount DECIMAL(18,2) NOT NULL, "
                  "actual_amount DECIMAL(18,2) DEFAULT 0, "
                  "executed_price DECIMAL(18,4) DEFAULT 0, "
                  "executed_quantity DECIMAL(18,4) DEFAULT 0, "
                  "transaction_id INTEGER DEFAULT NULL REFERENCES transactions(id), "
                  "status TEXT NOT NULL DEFAULT 'pending', "
                  "created_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
                  "updated_at DATETIME DEFAULT CURRENT_TIMESTAMP)");
    csilk_db_exec(pool,
                  "CREATE UNIQUE INDEX IF NOT EXISTS idx_dca_exec_plan_period ON "
                  "dca_executions(plan_id, period_date)");
    csilk_db_exec(
        pool,
        "CREATE INDEX IF NOT EXISTS idx_dca_exec_user_pending ON dca_executions(user_id, status)");

    csilk_db_exec(pool,
                  "CREATE TABLE IF NOT EXISTS cashflow_schedules ("
                  "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                  "user_id INTEGER NOT NULL REFERENCES users(id), "
                  "source_asset_id INTEGER NOT NULL REFERENCES assets(id), "
                  "target_asset_id INTEGER NOT NULL REFERENCES assets(id), "
                  "name TEXT NOT NULL, "
                  "flow_type TEXT NOT NULL DEFAULT 'dividend', "
                  "frequency TEXT NOT NULL DEFAULT 'monthly', "
                  "start_date TEXT NOT NULL, "
                  "end_date TEXT DEFAULT '', "
                  "expected_amount DECIMAL(18,2) NOT NULL, "
                  "status TEXT NOT NULL DEFAULT 'active', "
                  "note TEXT DEFAULT '', "
                  "created_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
                  "updated_at DATETIME DEFAULT CURRENT_TIMESTAMP)");
    csilk_db_exec(pool,
                  "CREATE INDEX IF NOT EXISTS idx_cashflow_schedules_user ON "
                  "cashflow_schedules(user_id, status)");

    // ---- 多账本空间主表与成员表 ----
    csilk_db_exec(pool,
                  "CREATE TABLE IF NOT EXISTS ledgers ("
                  "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                  "owner_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE, "
                  "name TEXT NOT NULL, "
                  "description TEXT DEFAULT '', "
                  "currency TEXT NOT NULL DEFAULT 'CNY', "
                  "icon TEXT DEFAULT 'ph:wallet', "
                  "color TEXT DEFAULT '#3b82f6', "
                  "is_default INTEGER NOT NULL DEFAULT 0, "
                  "invite_code TEXT UNIQUE, "
                  "invite_expires_at DATETIME, "
                  "created_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
                  "updated_at DATETIME DEFAULT CURRENT_TIMESTAMP)");
    csilk_db_exec(pool, "CREATE INDEX IF NOT EXISTS idx_ledgers_owner ON ledgers(owner_id)");
    csilk_db_exec(pool, "CREATE INDEX IF NOT EXISTS idx_ledgers_invite ON ledgers(invite_code)");

    csilk_db_exec(pool,
                  "CREATE TABLE IF NOT EXISTS ledger_members ("
                  "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                  "ledger_id INTEGER NOT NULL REFERENCES ledgers(id) ON DELETE CASCADE, "
                  "user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE, "
                  "role TEXT NOT NULL CHECK (role IN ('owner', 'editor', 'viewer')), "
                  "joined_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
                  "UNIQUE (ledger_id, user_id))");
    csilk_db_exec(pool,
                  "CREATE INDEX IF NOT EXISTS idx_ledger_members_user ON ledger_members(user_id)");
    csilk_db_exec(
        pool, "CREATE INDEX IF NOT EXISTS idx_ledger_members_ledger ON ledger_members(ledger_id)");

    // ---- 业务表 ledger_id 列迁移 ----
    const char* tables_with_ledger[] = {"assets",
                                        "transactions",
                                        "daily_expenses",
                                        "categories",
                                        "dca_plans",
                                        "cashflow_schedules",
                                        NULL};
    for (int t = 0; tables_with_ledger[t] != NULL; ++t) {
        if (!col_exists(pool, tables_with_ledger[t], "ledger_id")) {
            char alter_sql[256];
            snprintf(alter_sql,
                     sizeof(alter_sql),
                     "ALTER TABLE %s ADD COLUMN ledger_id INTEGER",
                     tables_with_ledger[t]);
            csilk_db_exec(pool, alter_sql);
        }
    }

    // ---- 存量用户默认账本自动初始化与数据回填 ----
    csilk_json_t* all_users = csilk_db_query_json(pool, "SELECT id FROM users");
    if (all_users) {
        size_t user_cnt = csilk_json_array_size(all_users);
        for (size_t u = 0; u < user_cnt; ++u) {
            csilk_json_t* u_obj = csilk_json_array_get(all_users, u);
            int64_t       uid = (int64_t)db_get_int(u_obj, "id");
            char          uid_str[32];
            snprintf(uid_str, sizeof(uid_str), "%lld", (long long)uid);

            csilk_json_t* user_ledgers = csilk_db_query_param_json(
                pool,
                "SELECT id FROM ledgers WHERE owner_id = ? AND is_default = 1",
                (const char*[]){uid_str, NULL});

            int64_t lid = 0;
            if (!user_ledgers || csilk_json_array_size(user_ledgers) == 0) {
                /* Create default ledger for this user */
                csilk_json_t* ins_res = csilk_db_query_param_json(
                    pool,
                    "INSERT INTO ledgers (owner_id, name, description, currency, is_default) "
                    "VALUES (?, '默认账本', '个人默认账本', 'CNY', 1) RETURNING id",
                    (const char*[]){uid_str, NULL});
                if (ins_res && csilk_json_array_size(ins_res) > 0) {
                    lid = (int64_t)db_get_int(csilk_json_array_get(ins_res, 0), "id");
                }
                if (ins_res) {
                    csilk_json_free(ins_res);
                }

                if (lid > 0) {
                    char lid_str[32];
                    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)lid);
                    csilk_db_query_param_json(pool,
                                              "INSERT OR IGNORE INTO ledger_members (ledger_id, "
                                              "user_id, role) VALUES (?, ?, 'owner')",
                                              (const char*[]){lid_str, uid_str, NULL});

                    /* Backfill ledger_id on existing records */
                    for (int t = 0; tables_with_ledger[t] != NULL; ++t) {
                        char backfill_sql[256];
                        snprintf(backfill_sql,
                                 sizeof(backfill_sql),
                                 "UPDATE %s SET ledger_id = ? WHERE user_id = ? AND (ledger_id IS "
                                 "NULL OR ledger_id = 0)",
                                 tables_with_ledger[t]);
                        csilk_json_t* bf_res = csilk_db_query_param_json(
                            pool, backfill_sql, (const char*[]){lid_str, uid_str, NULL});
                        if (bf_res) {
                            csilk_json_free(bf_res);
                        }
                    }
                }
            }
            if (user_ledgers) {
                csilk_json_free(user_ledgers);
            }
        }
        csilk_json_free(all_users);
    }

    // ---- 汇率表迁移与默认种子初始化 ----
    csilk_db_exec(pool,
                  "CREATE TABLE IF NOT EXISTS exchange_rates ("
                  "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                  "base_currency TEXT NOT NULL, "
                  "target_currency TEXT NOT NULL, "
                  "rate DECIMAL(18,6) NOT NULL, "
                  "updated_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
                  "UNIQUE (base_currency, target_currency))");
    csilk_db_exec(pool,
                  "CREATE INDEX IF NOT EXISTS idx_exchange_rates_pair ON "
                  "exchange_rates(base_currency, target_currency)");
    csilk_db_exec(
        pool,
        "INSERT OR IGNORE INTO exchange_rates (base_currency, target_currency, rate) VALUES "
        "('USD', 'CNY', 7.24), ('CNY', 'USD', 0.138122), "
        "('EUR', 'CNY', 7.85), ('CNY', 'EUR', 0.127389), "
        "('HKD', 'CNY', 0.925), ('CNY', 'HKD', 1.081081), "
        "('JPY', 'CNY', 0.048), ('CNY', 'JPY', 20.833333), "
        "('GBP', 'CNY', 9.18), ('CNY', 'GBP', 0.108932), "
        "('USDT', 'CNY', 7.25), ('CNY', 'USDT', 0.137931)");

    // ---- ai_traces 历史异常残留清理 ----
    csilk_db_exec(
        pool,
        "UPDATE ai_traces SET error_message = '' WHERE status = 'ok' AND error_message != ''");

    free(sql);
    CSILK_LOG_I("All migrations completed");
    return 0;
}

csilk_db_pool_t*
db_get_pool(void)
{
    return g_pool;
}

int
db_is_postgres(void)
{
    return g_is_postgres;
}
