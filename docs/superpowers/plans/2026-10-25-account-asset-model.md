# Account-Centric Asset Model Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Introduce a ledger-scoped `机构 → 账户 → 资产` hierarchy so every asset belongs to an account, and converge asset categories into 15 flat type categories.

**Architecture:** Two new tables (`institutions`, `accounts`) plus `assets.account_id`. Categories keep only asset *type* semantics; institution/account identity lives in the new tables. A deterministic, zero-loss migration (V017 schema + V018 backfill) remaps existing flat assets into inferred accounts, preserving `legacy_category_id` for rollback. New domain/application/repository/controller layers follow the existing DDD conventions; the Vue frontend gains an Accounts page and an account picker on the Assets page.

**Tech Stack:** C23 + csilk v0.5.2 + SQLite/PostgreSQL; CMake (Unix Makefiles); Vue 3 + TypeScript + Element Plus; vitest + Playwright; bash integration tests.

**Authoritative spec:** `docs/superpowers/specs/2026-10-25-account-asset-model-design.md`

---

## File Structure

| Path | Responsibility |
|---|---|
| `backend/sql/migrations/sqlite/V017__accounts_schema.sql` | SQLite DDL for institutions/accounts + asset columns |
| `backend/sql/migrations/postgres/V017__accounts_schema.sql` | PostgreSQL DDL |
| `backend/sql/migrations/sqlite/V018__accounts_backfill.sql` | SQLite data backfill + category convergence + archiving |
| `backend/sql/migrations/postgres/V018__accounts_backfill.sql` | PostgreSQL data backfill |
| `backend/sql/migrations/rollback/V018_accounts_backfill_rollback.sql` | Manual rollback |
| `backend/src/domain/institution/{entity.h,rules.h,rules.c}` | Institution entity + validation |
| `backend/src/domain/account/{entity.h,rules.h,rules.c}` | Account entity + validation |
| `backend/src/domain/asset/entity.h` | Add `account_id` to `mf_asset_t` |
| `backend/src/domain/asset/account.h` | **Delete** (dead code) |
| `backend/src/application/institution/{commands.h,usecases.h,usecases.c}` | Institution use cases |
| `backend/src/application/account/{commands.h,usecases.h,usecases.c}` | Account use cases incl. `ensure_system_rows`, merge |
| `backend/src/application/asset/{commands.h,usecases.c}` | Require `account_id`; stop writing `account_no` |
| `backend/src/application/category/usecases.c` | Seed 15 flat asset categories + system rows |
| `backend/src/infrastructure/repositories/institution_repo_impl.{h,c}` | Institution SQL |
| `backend/src/infrastructure/repositories/account_repo_impl.{h,c}` | Account SQL + totals |
| `backend/src/infrastructure/repositories/asset_repo_impl.{h,c}` | Account joins, filters, `account_id` writes |
| `backend/src/infrastructure/repositories/category_repo_impl.c` | Hide archived categories from lists |
| `backend/src/interfaces/http/controllers/institution_controller.c` | `/api/institutions` |
| `backend/src/interfaces/http/controllers/account_controller.c` | `/api/accounts`, merge |
| `backend/src/interfaces/http/controllers/asset_controller.c` | account_id in create/update/list |
| `backend/src/main.c` | Register new routes |
| `backend/src/services/ai/tools/asset_tool.c` | Required `account_id` in tool schema |
| `backend/src/application/import_export/usecases.c` | CSV import fallback → 未分类, export columns |
| `frontend/src/types/index.ts` | `Institution`, `Account`, `AccountTotals`; extend `Asset`/`AssetInput` |
| `frontend/src/api/{institutions,accounts}.ts` | REST clients |
| `frontend/src/stores/account.ts` | Account/institution cache with `invalidate()`/`reset()` |
| `frontend/src/components/{AccountCard,AccountSelect,AccountDialog,InstitutionDialog,MergeAccountDialog,AccountDetailDrawer}.vue` | UI units |
| `frontend/src/views/Accounts.vue` | Account management page |
| `frontend/src/views/Assets.vue` | Account column, filters, grouping, dialog picker |
| `frontend/src/views-mobile/AssetsMobile.vue` | Account grouping + picker (online) |
| `frontend/src/router/index.ts`, `views/Layout.vue`, `locales/{zh-CN,en-US}.ts` | Route/menu/i18n |
| `backend/tests/unit/test_domain_{institution,account}.c` | Domain rule tests |
| `backend/tests/test_accounts.sh` | Integration suite |
| `backend/tests/test_accounts_migration.sh` | Migration verification against a V016 sample DB |
| `frontend/src/stores/__tests__/account.spec.ts` | Store tests |

---

## Milestone M1 — Migrations

### Task 1: SQLite V017 schema

**Files:**
- Create: `backend/sql/migrations/sqlite/V017__accounts_schema.sql`

- [ ] **Step 1: Write the migration file**

```sql
-- V017__accounts_schema.sql
CREATE TABLE IF NOT EXISTS institutions (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id     INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    ledger_id   INTEGER NOT NULL REFERENCES ledgers(id) ON DELETE CASCADE,
    name        TEXT    NOT NULL,
    type        TEXT    NOT NULL CHECK (type IN ('bank','broker','payment','fund_platform','cash','other')),
    code        TEXT,
    icon        TEXT,
    sort_order  INTEGER NOT NULL DEFAULT 0,
    note        TEXT,
    is_system   INTEGER NOT NULL DEFAULT 0,
    created_at  TEXT    NOT NULL DEFAULT (datetime('now')),
    updated_at  TEXT    NOT NULL DEFAULT (datetime('now')),
    UNIQUE (user_id, ledger_id, name),
    UNIQUE (user_id, ledger_id, code)
);
CREATE INDEX IF NOT EXISTS idx_institutions_user_ledger ON institutions(user_id, ledger_id);

CREATE TABLE IF NOT EXISTS accounts (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id        INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    ledger_id      INTEGER NOT NULL REFERENCES ledgers(id) ON DELETE CASCADE,
    institution_id INTEGER NOT NULL REFERENCES institutions(id) ON DELETE RESTRICT,
    name           TEXT    NOT NULL,
    type           TEXT    NOT NULL CHECK (type IN ('debit_card','credit_card','securities','fund','payment','deposit','loan','cash','virtual')),
    code           TEXT,
    account_no     TEXT,
    sort_order     INTEGER NOT NULL DEFAULT 0,
    note           TEXT,
    is_system      INTEGER NOT NULL DEFAULT 0,
    created_at     TEXT    NOT NULL DEFAULT (datetime('now')),
    updated_at     TEXT    NOT NULL DEFAULT (datetime('now')),
    UNIQUE (user_id, ledger_id, institution_id, name),
    UNIQUE (user_id, ledger_id, code)
);
CREATE INDEX IF NOT EXISTS idx_accounts_user_ledger ON accounts(user_id, ledger_id);
CREATE INDEX IF NOT EXISTS idx_accounts_institution ON accounts(institution_id);

ALTER TABLE assets ADD COLUMN account_id INTEGER REFERENCES accounts(id) ON DELETE RESTRICT;
ALTER TABLE assets ADD COLUMN legacy_category_id INTEGER;
ALTER TABLE categories ADD COLUMN is_archived INTEGER NOT NULL DEFAULT 0;

CREATE INDEX IF NOT EXISTS idx_assets_account ON assets(account_id);
```

- [ ] **Step 2: Verify it applies to a fresh SQLite database**

Run:
```bash
cd backend
rm -f /tmp/mf_v017.db
MINEFOLIO_DB_DRIVER=sqlite MINEFOLIO_DB_DSN=/tmp/mf_v017.db MINEFOLIO_JWT_SECRET=test \
  ./build/minefolio & sleep 3
sqlite3 /tmp/mf_v017.db ".schema institutions" ".schema accounts" \
  "SELECT name FROM pragma_table_info('assets') WHERE name IN ('account_id','legacy_category_id');"
kill %1 2>/dev/null || true
```
Expected: the two `CREATE TABLE` statements print; the `pragma_table_info` query prints `account_id` and `legacy_category_id`.

- [ ] **Step 3: Commit**

```bash
git add backend/sql/migrations/sqlite/V017__accounts_schema.sql
git commit -m "feat(db): ✨ add institutions and accounts schema"
```

### Task 2: PostgreSQL V017 schema

**Files:**
- Create: `backend/sql/migrations/postgres/V017__accounts_schema.sql`

- [ ] **Step 1: Write the PostgreSQL translation**

Use the SQLite file with these exact substitutions:

| SQLite | PostgreSQL |
|---|---|
| `INTEGER PRIMARY KEY AUTOINCREMENT` | `BIGSERIAL PRIMARY KEY` |
| `created_at TEXT NOT NULL DEFAULT (datetime('now'))` | `created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP` |
| `updated_at TEXT NOT NULL DEFAULT (datetime('now'))` | `updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP` |
| `asset_id INTEGER` | `asset_id BIGINT` |
| `INTEGER NOT NULL DEFAULT 0` (is_system/is_archived/sort_order) | `INTEGER NOT NULL DEFAULT 0` (unchanged — C reads ints) |

`CREATE TABLE IF NOT EXISTS`, `ALTER TABLE ... ADD COLUMN`, `CHECK`, `UNIQUE`, and `CREATE INDEX IF NOT EXISTS` are valid PostgreSQL. Full file:

```sql
-- V017__accounts_schema.sql (PostgreSQL)
CREATE TABLE IF NOT EXISTS institutions (
    id          BIGSERIAL PRIMARY KEY,
    user_id     BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    ledger_id   BIGINT NOT NULL REFERENCES ledgers(id) ON DELETE CASCADE,
    name        TEXT   NOT NULL,
    type        TEXT   NOT NULL CHECK (type IN ('bank','broker','payment','fund_platform','cash','other')),
    code        TEXT,
    icon        TEXT,
    sort_order  INTEGER NOT NULL DEFAULT 0,
    note        TEXT,
    is_system   INTEGER NOT NULL DEFAULT 0,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (user_id, ledger_id, name),
    UNIQUE (user_id, ledger_id, code)
);
CREATE INDEX IF NOT EXISTS idx_institutions_user_ledger ON institutions(user_id, ledger_id);

CREATE TABLE IF NOT EXISTS accounts (
    id             BIGSERIAL PRIMARY KEY,
    user_id        BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    ledger_id      BIGINT NOT NULL REFERENCES ledgers(id) ON DELETE CASCADE,
    institution_id BIGINT NOT NULL REFERENCES institutions(id) ON DELETE RESTRICT,
    name           TEXT   NOT NULL,
    type           TEXT   NOT NULL CHECK (type IN ('debit_card','credit_card','securities','fund','payment','deposit','loan','cash','virtual')),
    code           TEXT,
    account_no     TEXT,
    sort_order     INTEGER NOT NULL DEFAULT 0,
    note           TEXT,
    is_system      INTEGER NOT NULL DEFAULT 0,
    created_at     TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at     TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (user_id, ledger_id, institution_id, name),
    UNIQUE (user_id, ledger_id, code)
);
CREATE INDEX IF NOT EXISTS idx_accounts_user_ledger ON accounts(user_id, ledger_id);
CREATE INDEX IF NOT EXISTS idx_accounts_institution ON accounts(institution_id);

ALTER TABLE assets ADD COLUMN account_id BIGINT REFERENCES accounts(id) ON DELETE RESTRICT;
ALTER TABLE assets ADD COLUMN legacy_category_id BIGINT;
ALTER TABLE categories ADD COLUMN is_archived INTEGER NOT NULL DEFAULT 0;

CREATE INDEX IF NOT EXISTS idx_assets_account ON assets(account_id);
```

- [ ] **Step 2: Verify syntax against a disposable PostgreSQL database (skip if no local PG)**

```bash
psql "$MINEFOLIO_PG_TEST_DSN" -v ON_ERROR_STOP=1 \
  -f backend/sql/migrations/postgres/V017__accounts_schema.sql
```
Expected: no error; `\d institutions` shows both unique constraints.

- [ ] **Step 3: Commit**

```bash
git add backend/sql/migrations/postgres/V017__accounts_schema.sql
git commit -m "feat(db): ✨ add institutions and accounts schema for postgres"
```

### Task 3: SQLite V018 backfill

**Files:**
- Create: `backend/sql/migrations/sqlite/V018__accounts_backfill.sql`

The file is pure SQL and idempotent where it inserts (categories/system rows), so a re-run cannot duplicate. Backfill targets only assets that still have `account_id IS NULL`.

- [ ] **Step 1: Write the migration file**

```sql
-- V018__accounts_backfill.sql
-- 1. 15 flat asset type categories per (user, ledger)
WITH type_seed(name, asset_type, icon, sort_order) AS (
    VALUES ('现金','cash','💵',1), ('活期存款','cash','🏦',2), ('定期存款','cash','🔒',3),
           ('银行理财','fund','📈',4), ('货币基金','fund','🐷',5), ('基金','fund','📊',6),
           ('股票','stock','📉',7), ('债券','bond','💎',8), ('加密货币','crypto','⛓',9),
           ('房产','real_estate','🏠',10), ('车辆','vehicle','🚗',11),
           ('应收款','other_asset','🪪',12), ('信用卡','credit_card','💳',13),
           ('贷款','loan','💸',14), ('其他负债','other_liability','📉',15)
),
scopes AS (
    SELECT DISTINCT user_id, ledger_id FROM categories WHERE ledger_id IS NOT NULL
    UNION
    SELECT DISTINCT user_id, COALESCE(ledger_id, (SELECT id FROM ledgers l WHERE l.owner_id = a.user_id AND l.is_default = 1))
    FROM assets a WHERE user_id IS NOT NULL
)
INSERT INTO categories (user_id, ledger_id, name, parent_id, type, asset_type, currency, icon, sort_order, created_at)
SELECT s.user_id, s.ledger_id, t.name, NULL, 'asset', t.asset_type, 'CNY', t.icon, t.sort_order, CURRENT_TIMESTAMP
FROM scopes s CROSS JOIN type_seed t
WHERE s.ledger_id IS NOT NULL
  AND NOT EXISTS (
      SELECT 1 FROM categories c
      WHERE c.user_id = s.user_id AND c.ledger_id = s.ledger_id
        AND c.type = 'asset' AND c.parent_id IS NULL AND c.name = t.name
  );

-- 2. System institutions (cash / own_assets / unclassified)
WITH sys(name, type, code, icon, sort_order) AS (
    VALUES ('现金','cash','cash','💵',1),
           ('自有资产','other','own_assets','📦',2),
           ('未分类','other','unclassified','❓',3)
),
scopes AS (
    SELECT DISTINCT user_id, ledger_id FROM categories WHERE ledger_id IS NOT NULL
    UNION
    SELECT DISTINCT user_id, COALESCE(ledger_id, (SELECT id FROM ledgers l WHERE l.owner_id = a.user_id AND l.is_default = 1))
    FROM assets a WHERE user_id IS NOT NULL
)
INSERT INTO institutions (user_id, ledger_id, name, type, code, icon, sort_order, is_system, created_at, updated_at)
SELECT s.user_id, s.ledger_id, y.name, y.type, y.code, y.icon, y.sort_order, 1, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP
FROM scopes s CROSS JOIN sys y
WHERE s.ledger_id IS NOT NULL
  AND NOT EXISTS (
      SELECT 1 FROM institutions i
      WHERE i.user_id = s.user_id AND i.ledger_id = s.ledger_id AND i.code = y.code
  );

-- 3. System accounts pointing at the system institutions
WITH sys(institution_code, name, type, code, sort_order) AS (
    VALUES ('cash','现金账户','cash','cash',1),
           ('own_assets','实物资产','virtual','physical',2),
           ('unclassified','未分类账户','virtual','unclassified',3)
),
scopes AS (
    SELECT DISTINCT user_id, ledger_id FROM categories WHERE ledger_id IS NOT NULL
    UNION
    SELECT DISTINCT user_id, COALESCE(ledger_id, (SELECT id FROM ledgers l WHERE l.owner_id = a.user_id AND l.is_default = 1))
    FROM assets a WHERE user_id IS NOT NULL
)
INSERT INTO accounts (user_id, ledger_id, institution_id, name, type, code, sort_order, is_system, created_at, updated_at)
SELECT s.user_id, s.ledger_id, i.id, y.name, y.type, y.code, y.sort_order, 1, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP
FROM scopes s
CROSS JOIN sys y
JOIN institutions i
  ON i.user_id = s.user_id AND i.ledger_id = s.ledger_id AND i.code = y.institution_code
WHERE s.ledger_id IS NOT NULL
  AND NOT EXISTS (
      SELECT 1 FROM accounts a
      WHERE a.user_id = s.user_id AND a.ledger_id = s.ledger_id AND a.code = y.code
  );

-- 4. Per-asset institution inference (keyword table, longest/specific first)
CREATE TEMP TABLE _asset_inst AS
SELECT a.id AS asset_id,
       a.user_id,
       COALESCE(a.ledger_id, (SELECT l.id FROM ledgers l WHERE l.owner_id = a.user_id AND l.is_default = 1)) AS ledger_id,
       COALESCE(c.name, '') AS legacy_cat_name,
       COALESCE(c.asset_type, 'cash') AS legacy_asset_type,
       a.name AS asset_name,
       a.account_no AS account_no,
       CASE
         WHEN a.name LIKE '%支付宝%' OR COALESCE(c.name,'') LIKE '%支付宝%'
           OR a.name LIKE '%余额宝%' OR a.name LIKE '%余利宝%' OR a.name LIKE '%花呗%' THEN '支付宝'
         WHEN a.name LIKE '%微信%' OR a.name LIKE '%零钱通%' OR COALESCE(c.name,'') LIKE '%微信%' THEN '微信支付'
         WHEN a.name LIKE '%京东%' OR COALESCE(c.name,'') LIKE '%京东%' THEN '京东金融'
         WHEN a.name LIKE '%招商证券%' THEN '招商证券'
         WHEN a.name LIKE '%中信证券%' THEN '中信证券'
         WHEN a.name LIKE '%华泰%' THEN '华泰证券'
         WHEN a.name LIKE '%东方财富%' THEN '东方财富'
         WHEN a.name LIKE '%国泰君安%' THEN '国泰君安'
         WHEN a.name LIKE '%富途%' THEN '富途证券'
         WHEN a.name LIKE '%老虎%' THEN '老虎证券'
         WHEN a.name LIKE '%天天基金%' OR a.name LIKE '%蚂蚁财富%' THEN '天天基金'
         WHEN a.name LIKE '%工商%' THEN '中国工商银行'
         WHEN a.name LIKE '%建设%' THEN '中国建设银行'
         WHEN a.name LIKE '%农业%' THEN '中国农业银行'
         WHEN a.name LIKE '%中国银行%' THEN '中国银行'
         WHEN a.name LIKE '%交通银行%' THEN '交通银行'
         WHEN a.name LIKE '%邮储%' OR a.name LIKE '%邮政%' THEN '中国邮政储蓄银行'
         WHEN a.name LIKE '%招商%' THEN '招商银行'
         WHEN a.name LIKE '%中信%' THEN '中信银行'
         WHEN a.name LIKE '%浦发%' THEN '浦发银行'
         WHEN a.name LIKE '%民生%' THEN '民生银行'
         WHEN a.name LIKE '%兴业%' THEN '兴业银行'
         WHEN a.name LIKE '%光大%' THEN '光大银行'
         WHEN a.name LIKE '%平安%' THEN '平安银行'
         WHEN a.name LIKE '%华夏%' THEN '华夏银行'
         WHEN a.name LIKE '%广发%' THEN '广发银行'
         WHEN COALESCE(c.asset_type,'') IN ('real_estate','vehicle','other_asset') THEN '自有资产'
         ELSE '未分类'
       END AS inst_name,
       CASE
         WHEN a.name LIKE '%支付宝%' OR COALESCE(c.name,'') LIKE '%支付宝%'
           OR a.name LIKE '%余额宝%' OR a.name LIKE '%余利宝%' OR a.name LIKE '%花呗%'
           OR a.name LIKE '%微信%' OR a.name LIKE '%零钱通%' OR COALESCE(c.name,'') LIKE '%微信%'
           OR a.name LIKE '%京东%' OR COALESCE(c.name,'') LIKE '%京东%' THEN 'payment'
         WHEN a.name LIKE '%招商证券%' OR a.name LIKE '%中信证券%' OR a.name LIKE '%华泰%'
           OR a.name LIKE '%东方财富%' OR a.name LIKE '%国泰君安%' OR a.name LIKE '%富途%'
           OR a.name LIKE '%老虎%' THEN 'broker'
         WHEN a.name LIKE '%天天基金%' OR a.name LIKE '%蚂蚁财富%' THEN 'fund_platform'
         WHEN a.name LIKE '%银行%' OR a.name LIKE '%工商%' OR a.name LIKE '%建设%' OR a.name LIKE '%农业%'
           OR a.name LIKE '%中国银行%' OR a.name LIKE '%交通银行%' OR a.name LIKE '%邮储%'
           OR a.name LIKE '%邮政%' OR a.name LIKE '%招商%' OR a.name LIKE '%中信%'
           OR a.name LIKE '%浦发%' OR a.name LIKE '%民生%' OR a.name LIKE '%兴业%'
           OR a.name LIKE '%光大%' OR a.name LIKE '%平安%' OR a.name LIKE '%华夏%'
           OR a.name LIKE '%广发%' THEN 'bank'
         WHEN COALESCE(c.asset_type,'') IN ('real_estate','vehicle','other_asset') THEN 'other'
         ELSE 'other'
       END AS inst_type
FROM assets a
LEFT JOIN categories c ON c.id = a.category_id;

-- 5. Materialise inferred institutions
INSERT INTO institutions (user_id, ledger_id, name, type, code, icon, sort_order, is_system, created_at, updated_at)
SELECT DISTINCT m.user_id, m.ledger_id, m.inst_name, m.inst_type, NULL, NULL, 100, 0, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP
FROM _asset_inst m
WHERE m.ledger_id IS NOT NULL
  AND NOT EXISTS (
      SELECT 1 FROM institutions i
      WHERE i.user_id = m.user_id AND i.ledger_id = m.ledger_id AND i.name = m.inst_name
  );

-- 6. Group assets by (user, ledger, institution, legacy category)
CREATE TEMP TABLE _grp AS
SELECT m.user_id, m.ledger_id, i.id AS institution_id, m.legacy_cat_name,
       COUNT(*) AS cnt,
       MAX(m.asset_name) AS sample_name,
       MAX(m.legacy_asset_type) AS asset_type,
       MAX(CASE WHEN m.account_no IS NOT NULL AND m.account_no <> '' THEN m.account_no END) AS any_no,
       COUNT(DISTINCT CASE WHEN m.account_no IS NOT NULL AND m.account_no <> '' THEN m.account_no END) AS no_cnt
FROM _asset_inst m
JOIN institutions i ON i.user_id = m.user_id AND i.ledger_id = m.ledger_id AND i.name = m.inst_name
WHERE m.ledger_id IS NOT NULL
GROUP BY m.user_id, m.ledger_id, i.id, m.legacy_cat_name;

-- 7. Materialise accounts
CREATE TEMP TABLE _acct AS
SELECT g.user_id, g.ledger_id, g.institution_id,
       CASE WHEN g.cnt = 1 THEN g.sample_name ELSE g.legacy_cat_name END AS acct_name,
       CASE
         WHEN g.legacy_cat_name LIKE '%支付宝%' OR g.legacy_cat_name LIKE '%微信%'
           OR g.legacy_cat_name LIKE '%京东%' THEN 'payment'
         WHEN g.legacy_cat_name LIKE '%现金%' THEN 'cash'
         WHEN g.legacy_cat_name LIKE '%信用卡%' OR g.asset_type = 'credit_card' THEN 'credit_card'
         WHEN g.legacy_cat_name LIKE '%贷款%' OR g.asset_type = 'loan' THEN 'loan'
         WHEN g.legacy_cat_name LIKE '%股票%' OR g.legacy_cat_name LIKE '%港美股%'
           OR g.asset_type = 'stock' THEN 'securities'
         WHEN g.legacy_cat_name LIKE '%基金%' OR g.legacy_cat_name LIKE '%理财%'
           OR g.asset_type = 'fund' THEN 'fund'
         WHEN g.legacy_cat_name LIKE '%银行%' OR g.legacy_cat_name LIKE '%存款%' THEN 'deposit'
         ELSE 'virtual'
       END AS acct_type,
       CASE WHEN g.no_cnt = 1 THEN g.any_no ELSE NULL END AS acct_no
FROM _grp g;

INSERT INTO accounts (user_id, ledger_id, institution_id, name, type, account_no, sort_order, is_system, created_at, updated_at)
SELECT a.user_id, a.ledger_id, a.institution_id, a.acct_name, a.acct_type, a.acct_no, 100, 0, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP
FROM _acct a
WHERE NOT EXISTS (
    SELECT 1 FROM accounts x
    WHERE x.user_id = a.user_id AND x.ledger_id = a.ledger_id
      AND x.institution_id = a.institution_id AND x.name = a.acct_name
);

-- 8. Asset → new category mapping
CREATE TEMP TABLE _asset_newcat AS
SELECT m.asset_id,
       CASE
         WHEN m.asset_name LIKE '%定期%' OR m.legacy_cat_name LIKE '%定期%' THEN '定期存款'
         WHEN m.asset_name LIKE '%活期%' OR m.legacy_cat_name LIKE '%活期%'
           OR m.legacy_cat_name LIKE '%银行存款%' THEN '活期存款'
         WHEN m.asset_name LIKE '%理财%' OR m.legacy_cat_name LIKE '%理财%' THEN '银行理财'
         WHEN m.asset_name LIKE '%余额宝%' OR m.asset_name LIKE '%余利宝%'
           OR m.asset_name LIKE '%零钱通%' OR m.asset_name LIKE '%货币基金%' THEN '货币基金'
         WHEN m.legacy_asset_type = 'cash' THEN '现金'
         WHEN m.legacy_asset_type = 'stock' THEN '股票'
         WHEN m.legacy_asset_type = 'fund' THEN '基金'
         WHEN m.legacy_asset_type = 'bond' THEN '债券'
         WHEN m.legacy_asset_type = 'crypto' THEN '加密货币'
         WHEN m.legacy_asset_type = 'real_estate' THEN '房产'
         WHEN m.legacy_asset_type = 'vehicle' THEN '车辆'
         WHEN m.legacy_asset_type = 'credit_card' THEN '信用卡'
         WHEN m.legacy_asset_type = 'loan' THEN '贷款'
         WHEN m.legacy_asset_type = 'other_liability' THEN '其他负债'
         ELSE '应收款'
       END AS new_cat_name
FROM _asset_inst m;

-- 9. Asset → account mapping
CREATE TEMP TABLE _asset_map AS
SELECT m.asset_id, m.user_id, m.ledger_id, i.id AS institution_id,
       x.id AS account_id, n.new_cat_name
FROM _asset_inst m
JOIN institutions i ON i.user_id = m.user_id AND i.ledger_id = m.ledger_id AND i.name = m.inst_name
JOIN _acct x ON x.user_id = m.user_id AND x.ledger_id = m.ledger_id
            AND x.institution_id = i.id AND x.acct_name = CASE WHEN x.acct_name = x.acct_name THEN x.acct_name END
JOIN _acct a2 ON a2.user_id = m.user_id AND a2.ledger_id = m.ledger_id
             AND a2.institution_id = i.id AND a2.acct_name = CASE WHEN (SELECT cnt FROM _grp g
                  WHERE g.user_id=m.user_id AND g.ledger_id=m.ledger_id
                    AND g.institution_id=i.id AND g.legacy_cat_name=m.legacy_cat_name) = 1
                  THEN (SELECT sample_name FROM _grp g
                        WHERE g.user_id=m.user_id AND g.ledger_id=m.ledger_id
                          AND g.institution_id=i.id AND g.legacy_cat_name=m.legacy_cat_name)
                  ELSE m.legacy_cat_name END
JOIN _asset_newcat n ON n.asset_id = m.asset_id
WHERE m.ledger_id IS NOT NULL;
```

> **Note to implementer:** the `_asset_map` join above is over-specified and will not parse cleanly. Replace step 9 with the straightforward two-step form below before committing — it is the canonical version:

```sql
-- 9a. Attach the resolved account name to every asset
CREATE TEMP TABLE _asset_acct AS
SELECT m.asset_id, m.user_id, m.ledger_id, i.id AS institution_id,
       COALESCE(x.id, 0) AS account_id
FROM _asset_inst m
JOIN institutions i ON i.user_id = m.user_id AND i.ledger_id = m.ledger_id AND i.name = m.inst_name
LEFT JOIN _acct x
  ON x.user_id = m.user_id AND x.ledger_id = m.ledger_id
 AND x.institution_id = i.id
 AND x.acct_name = (
       SELECT CASE WHEN g.cnt = 1 THEN g.sample_name ELSE g.legacy_cat_name END
       FROM _grp g
       WHERE g.user_id = m.user_id AND g.ledger_id = m.ledger_id
         AND g.institution_id = i.id AND g.legacy_cat_name = m.legacy_cat_name
     );

-- 9b. Final mapping table
CREATE TEMP TABLE _asset_map AS
SELECT a.asset_id, a.account_id, n.new_cat_name
FROM _asset_acct a
JOIN _asset_newcat n ON n.asset_id = a.asset_id
WHERE a.account_id > 0;

-- 10. Rewrite assets (zero-loss: keep legacy_category_id, keep account_no column untouched)
UPDATE assets
SET legacy_category_id = category_id,
    category_id = (
        SELECT c.id FROM categories c
        JOIN _asset_map m ON m.asset_id = assets.id
        WHERE c.user_id = assets.user_id AND c.ledger_id = assets.ledger_id
          AND c.type = 'asset' AND c.parent_id IS NULL AND c.name = m.new_cat_name
    ),
    account_id = (SELECT m.account_id FROM _asset_map m WHERE m.asset_id = assets.id)
WHERE id IN (SELECT asset_id FROM _asset_map);

-- 11. Archive every legacy asset category (the 15 converged roots stay active)
UPDATE categories
SET is_archived = 1
WHERE type = 'asset'
  AND NOT (
      parent_id IS NULL
      AND name IN ('现金','活期存款','定期存款','银行理财','货币基金','基金','股票','债券',
                   '加密货币','房产','车辆','应收款','信用卡','贷款','其他负债')
  );

-- 12. Cleanup
DROP TABLE IF EXISTS _asset_map;
DROP TABLE IF EXISTS _asset_acct;
DROP TABLE IF EXISTS _asset_newcat;
DROP TABLE IF EXISTS _acct;
DROP TABLE IF EXISTS _grp;
DROP TABLE IF EXISTS _asset_inst;
```

- [ ] **Step 2: Run it against a copy of a real database and check invariants**

```bash
cp ./data/minefolio.db /tmp/mf_v018.db
MINEFOLIO_DB_DRIVER=sqlite MINEFOLIO_DB_DSN=/tmp/mf_v018.db MINEFOLIO_JWT_SECRET=test \
  ./build/minefolio & sleep 3
sqlite3 /tmp/mf_v018.db \
  "SELECT 'assets', COUNT(*) FROM assets;" \
  "SELECT 'unassigned', COUNT(*) FROM assets WHERE account_id IS NULL;" \
  "SELECT 'active_asset_cats', COUNT(*) FROM categories WHERE type='asset' AND is_archived=0;"
kill %1 2>/dev/null || true
```
Expected: `unassigned` = 0; `active_asset_cats` is a multiple of 15 (one set per user/ledger).

- [ ] **Step 3: Commit**

```bash
git add backend/sql/migrations/sqlite/V018__accounts_backfill.sql
git commit -m "feat(db): ✨ backfill accounts and converge asset categories"
```

### Task 4: PostgreSQL V018 backfill

**Files:**
- Create: `backend/sql/migrations/postgres/V018__accounts_backfill.sql`

- [ ] **Step 1: Port the SQLite file**

Apply these substitutions to every statement:

| SQLite | PostgreSQL |
|---|---|
| `CURRENT_TIMESTAMP` | `CURRENT_TIMESTAMP` (unchanged) |
| `CREATE TEMP TABLE` | `CREATE TEMP TABLE` (unchanged) |
| `UPDATE ... SET col = (SELECT ...)` | unchanged (correlated subquery is valid) |
| `INSERT ... WHERE NOT EXISTS` | unchanged |
| `COALESCE(a.ledger_id, (SELECT ...))` | unchanged |

No `INSERT OR IGNORE` is used, so no `ON CONFLICT` rewrite is needed. PostgreSQL treats `NULL` values as distinct in `UNIQUE` constraints, matching the SQLite behaviour relied on by `UNIQUE(user_id, ledger_id, code)`.

- [ ] **Step 2: Verify on a disposable PostgreSQL database**

```bash
psql "$MINEFOLIO_PG_TEST_DSN" -v ON_ERROR_STOP=1 \
  -f backend/sql/migrations/postgres/V017__accounts_schema.sql \
  -f backend/sql/migrations/postgres/V018__accounts_backfill.sql
psql "$MINEFOLIO_PG_TEST_DSN" -tAc "SELECT COUNT(*) FROM assets WHERE account_id IS NULL;"
```
Expected: `0`.

- [ ] **Step 3: Enforce NOT NULL after backfill**

Append to the PostgreSQL V018 file:
```sql
ALTER TABLE assets ALTER COLUMN account_id SET NOT NULL;
```
SQLite keeps the column nullable (documented constraint: SQLite cannot add NOT NULL without rebuilding the table).

- [ ] **Step 4: Commit**

```bash
git add backend/sql/migrations/postgres/V018__accounts_backfill.sql
git commit -m "feat(db): ✨ backfill accounts and converge asset categories for postgres"
```

### Task 5: Rollback script + migration verification script

**Files:**
- Create: `backend/sql/migrations/rollback/V018_accounts_backfill_rollback.sql`
- Create: `backend/tests/test_accounts_migration.sh`

- [ ] **Step 1: Write the manual rollback**

```sql
-- V018_accounts_backfill_rollback.sql (manual, SQLite)
-- Restores category assignment and removes the account dimension.
UPDATE assets
SET category_id = legacy_category_id
WHERE legacy_category_id IS NOT NULL;

UPDATE assets
SET legacy_category_id = NULL,
    account_id = NULL;

UPDATE categories
SET is_archived = 0
WHERE type = 'asset';

DELETE FROM categories
WHERE type = 'asset' AND parent_id IS NULL
  AND name IN ('现金','活期存款','定期存款','银行理财','货币基金','基金','股票','债券',
               '加密货币','房产','车辆','应收款','信用卡','贷款','其他负债')
  AND NOT EXISTS (SELECT 1 FROM assets a WHERE a.category_id = categories.id);

DELETE FROM accounts;
DELETE FROM institutions;
```
For PostgreSQL, run the same statements then:
```sql
ALTER TABLE assets DROP COLUMN account_id;
ALTER TABLE assets DROP COLUMN legacy_category_id;
ALTER TABLE categories DROP COLUMN is_archived;
DROP TABLE accounts;
DROP TABLE institutions;
```

- [ ] **Step 2: Write the migration verification script**

```bash
#!/usr/bin/env bash
# Usage: ./tests/test_accounts_migration.sh /path/to/v016-sample.db
set -euo pipefail
SRC="${1:?usage: $0 <v016-sample.db>}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"; kill "${PID:-0}" 2>/dev/null || true' EXIT

cp "$SRC" "$WORK/mf.db"
cd "$(dirname "$0")/.."

before_assets=$(sqlite3 "$WORK/mf.db" "SELECT COUNT(*) FROM assets;")
before_tx=$(sqlite3 "$WORK/mf.db" "SELECT COUNT(*) FROM transactions;")

MINEFOLIO_DB_DRIVER=sqlite MINEFOLIO_DB_DSN="$WORK/mf.db" MINEFOLIO_JWT_SECRET=test \
  ./build/minefolio >"$WORK/server.log" 2>&1 &
PID=$!
for _ in $(seq 1 30); do
  sqlite3 "$WORK/mf.db" "SELECT 1 FROM schema_migrations WHERE version='V018' LIMIT 1;" \
    >/dev/null 2>&1 && break
  sleep 0.5
done

after_assets=$(sqlite3 "$WORK/mf.db" "SELECT COUNT(*) FROM assets;")
after_tx=$(sqlite3 "$WORK/mf.db" "SELECT COUNT(*) FROM transactions;")
unassigned=$(sqlite3 "$WORK/mf.db" "SELECT COUNT(*) FROM assets WHERE account_id IS NULL;")
bad_cat=$(sqlite3 "$WORK/mf.db" \
  "SELECT COUNT(*) FROM assets a LEFT JOIN categories c ON c.id=a.category_id
   WHERE c.id IS NULL OR c.type<>'asset';")
sys_dup=$(sqlite3 "$WORK/mf.db" \
  "SELECT COUNT(*) FROM (SELECT user_id, ledger_id, code, COUNT(*) n FROM institutions
    WHERE code IS NOT NULL GROUP BY 1,2,3 HAVING n>1);")

[ "$before_assets" = "$after_assets" ] || { echo "asset count changed"; exit 1; }
[ "$before_tx" = "$after_tx" ] || { echo "transaction count changed"; exit 1; }
[ "$unassigned" = "0" ] || { echo "assets without account: $unassigned"; exit 1; }
[ "$bad_cat" = "0" ] || { echo "assets with invalid category: $bad_cat"; exit 1; }
[ "$sys_dup" = "0" ] || { echo "duplicate system institutions"; exit 1; }

echo "migration OK: assets=$after_assets unassigned=0"
```

- [ ] **Step 3: Make it executable and run it against a sample V016 database**

```bash
chmod +x backend/tests/test_accounts_migration.sh
./backend/tests/test_accounts_migration.sh /path/to/v016-sample.db
```
Expected: `migration OK: assets=<n> unassigned=0`.

- [ ] **Step 4: Commit**

```bash
git add backend/sql/migrations/rollback/V018_accounts_backfill_rollback.sql \
        backend/tests/test_accounts_migration.sh
git commit -m "test(db): ✅ add account migration verification and rollback"
```

---
