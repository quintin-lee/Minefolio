// Local SQLite schema (sql.js WASM) mirroring the backend business tables.
// Each business table adds mobile-only sync columns:
//   __deleted  INTEGER 0|1  — soft-delete marker (sync deletes never hard-delete locally)
//   updated_at TEXT        — local write timestamp used as {local_version} for sync ordering
// Plus sync bookkeeping tables: sync_queue, sync_meta.
export const LOCAL_SCHEMA = `
CREATE TABLE IF NOT EXISTS categories (
  id         INTEGER PRIMARY KEY,
  user_id    INTEGER DEFAULT 0,
  name       TEXT NOT NULL,
  parent_id  INTEGER,
  type       TEXT NOT NULL DEFAULT 'asset',
  asset_type TEXT DEFAULT 'cash',
  currency   TEXT DEFAULT 'CNY',
  icon       TEXT,
  sort_order INTEGER DEFAULT 0,
  created_at TEXT,
  updated_at TEXT,
  __deleted  INTEGER DEFAULT 0
);

CREATE TABLE IF NOT EXISTS assets (
  id            INTEGER PRIMARY KEY,
  user_id       INTEGER DEFAULT 0,
  category_id   INTEGER NOT NULL,
  name          TEXT NOT NULL,
  account_no    TEXT,
  current_value REAL DEFAULT 0,
  quantity      REAL DEFAULT 0,
  cost_basis    REAL DEFAULT 0,
  net_value     REAL DEFAULT 0,
  currency      TEXT DEFAULT 'CNY',
  note          TEXT,
  created_at    TEXT,
  updated_at    TEXT,
  __deleted     INTEGER DEFAULT 0
);

CREATE TABLE IF NOT EXISTS transactions (
  id               INTEGER PRIMARY KEY,
  user_id          INTEGER DEFAULT 0,
  asset_id         INTEGER NOT NULL,
  linked_asset_id  INTEGER,
  category_id      INTEGER,
  source_type      TEXT NOT NULL DEFAULT 'expense',
  transaction_type TEXT NOT NULL,
  direction        TEXT NOT NULL DEFAULT 'out',
  linked_direction TEXT,
  amount           REAL NOT NULL,
  price_per_unit   REAL,
  quantity         REAL,
  fee              REAL DEFAULT 0,
  currency         TEXT DEFAULT 'CNY',
  transaction_date TEXT NOT NULL,
  note             TEXT,
  created_at       TEXT,
  updated_at       TEXT,
  __deleted        INTEGER DEFAULT 0
);

CREATE TABLE IF NOT EXISTS tags (
  id         INTEGER PRIMARY KEY,
  user_id    INTEGER DEFAULT 0,
  name       TEXT NOT NULL,
  color      TEXT DEFAULT '',
  created_at TEXT,
  updated_at TEXT,
  __deleted  INTEGER DEFAULT 0
);

CREATE TABLE IF NOT EXISTS expense_tags (
  expense_id INTEGER NOT NULL,
  tag_id     INTEGER NOT NULL,
  __deleted  INTEGER DEFAULT 0,
  PRIMARY KEY (expense_id, tag_id)
);

CREATE TABLE IF NOT EXISTS daily_expenses (
  id           INTEGER PRIMARY KEY,
  user_id      INTEGER DEFAULT 0,
  category_id  INTEGER NOT NULL,
  asset_id     INTEGER NOT NULL,
  expense_type TEXT NOT NULL DEFAULT 'expense',
  amount       REAL NOT NULL,
  currency     TEXT DEFAULT 'CNY',
  expense_date TEXT NOT NULL,
  note         TEXT,
  created_at   TEXT,
  updated_at   TEXT,
  __deleted    INTEGER DEFAULT 0
);

-- Sync queue: operation='delete' never physically deletes locally, only sets __deleted=1
CREATE TABLE IF NOT EXISTS sync_queue (
  id            INTEGER PRIMARY KEY AUTOINCREMENT,
  table_name    TEXT NOT NULL,
  record_id     INTEGER NOT NULL,
  operation     TEXT NOT NULL CHECK(operation IN ('create','update','delete')),
  payload       TEXT NOT NULL,
  local_version TEXT NOT NULL,
  synced        INTEGER DEFAULT 0,
  conflict      INTEGER DEFAULT 0
);

CREATE TABLE IF NOT EXISTS sync_meta (
  key   TEXT PRIMARY KEY,
  value TEXT
);
-- key='last_sync_at' 用于增量拉取
`
