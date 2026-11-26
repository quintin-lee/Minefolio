// Local SQLite schema (sql.js WASM) mirroring the backend business tables.
// Each business table adds mobile-only sync columns:
//   __deleted  INTEGER 0|1  — soft-delete marker (sync deletes never hard-delete locally)
//   updated_at TEXT        — local write timestamp used as {local_version} for sync ordering
// Plus sync bookkeeping tables: sync_queue, sync_meta.
//
// SCOPE: only tables the mobile client can read AND write offline are mirrored here.
// Server-side scheduling/push state (im_push_outbox, im_bot_instances,
// notification_channels, job_run_logs) is intentionally excluded: it is real-time
// service state that must never be served stale, so those views always require the
// online API and fall back to a loading/empty state rather than local SQLite.
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
  ledger_id     INTEGER DEFAULT 0,
  category_id   INTEGER NOT NULL,
  name          TEXT NOT NULL,
  account_no    TEXT,
  account_id    INTEGER,
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

CREATE TABLE IF NOT EXISTS accounts (
  id            INTEGER PRIMARY KEY,
  user_id       INTEGER DEFAULT 0,
  ledger_id     INTEGER DEFAULT 0,
  name          TEXT NOT NULL,
  institution   TEXT NOT NULL DEFAULT 'other',
  account_no    TEXT,
  currency      TEXT DEFAULT 'CNY',
  icon          TEXT,
  note          TEXT,
  sort_order    INTEGER DEFAULT 0,
  archived      INTEGER DEFAULT 0,
  created_at    TEXT,
  updated_at    TEXT,
  __deleted     INTEGER DEFAULT 0
);

CREATE TABLE IF NOT EXISTS account_permissions (
  id               INTEGER PRIMARY KEY,
  ledger_id        INTEGER NOT NULL,
  account_id       INTEGER NOT NULL,
  user_id          INTEGER NOT NULL,
  permission_level TEXT NOT NULL DEFAULT 'inherit',
  mask_balance     INTEGER NOT NULL DEFAULT 0,
  mask_account_no  INTEGER NOT NULL DEFAULT 0,
  created_at       TEXT,
  updated_at       TEXT,
  __deleted        INTEGER DEFAULT 0
);

CREATE TABLE IF NOT EXISTS transactions (
  id               INTEGER PRIMARY KEY,
  user_id          INTEGER DEFAULT 0,
  ledger_id        INTEGER DEFAULT 0,
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
  ledger_id    INTEGER DEFAULT 0,
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
  ledger_id     INTEGER DEFAULT 0,
  operation     TEXT NOT NULL CHECK(operation IN ('create','update','delete')),
  payload       TEXT NOT NULL,
  local_version TEXT NOT NULL,
  synced        INTEGER DEFAULT 0,
  conflict      INTEGER DEFAULT 0,
  remote_payload TEXT DEFAULT NULL
);

CREATE TABLE IF NOT EXISTS sync_meta (
  key   TEXT PRIMARY KEY,
  value TEXT
);
-- key='last_sync_at' 用于增量拉取

-- 移动端聊天草稿：仅本地存储，不进入 SyncEngine 同步队列
CREATE TABLE IF NOT EXISTS chat_drafts (
  scope_key  TEXT PRIMARY KEY,
  content    TEXT NOT NULL,
  updated_at TEXT NOT NULL
);

-- 移动端视图读缓存：仅本地存储，不进入 SyncEngine 同步队列
CREATE TABLE IF NOT EXISTS view_cache (
  key        TEXT PRIMARY KEY,
  payload    TEXT NOT NULL,
  updated_at TEXT NOT NULL
);
`

/** 各本地同步表允许持久化到 SQLite 的字段白名单 (不含移动端内部控制列 __deleted) */
export const TABLE_COLUMNS: Record<string, string[]> = {
  categories: ['id', 'user_id', 'name', 'parent_id', 'type', 'asset_type', 'currency', 'icon', 'sort_order', 'created_at', 'updated_at'],
  assets: ['id', 'user_id', 'ledger_id', 'category_id', 'name', 'account_no', 'account_id', 'current_value', 'quantity', 'cost_basis', 'net_value', 'currency', 'note', 'created_at', 'updated_at'],
  accounts: ['id', 'user_id', 'ledger_id', 'name', 'institution', 'account_no', 'currency', 'icon', 'note', 'sort_order', 'archived', 'created_at', 'updated_at'],
  account_permissions: ['id', 'ledger_id', 'account_id', 'user_id', 'permission_level', 'mask_balance', 'mask_account_no', 'created_at', 'updated_at'],
  transactions: ['id', 'user_id', 'ledger_id', 'asset_id', 'linked_asset_id', 'category_id', 'source_type', 'transaction_type', 'direction', 'linked_direction', 'amount', 'price_per_unit', 'quantity', 'fee', 'currency', 'transaction_date', 'note', 'created_at', 'updated_at'],
  tags: ['id', 'user_id', 'name', 'color', 'created_at', 'updated_at'],
  expense_tags: ['expense_id', 'tag_id'],
  daily_expenses: ['id', 'user_id', 'ledger_id', 'category_id', 'asset_id', 'expense_type', 'amount', 'currency', 'expense_date', 'note', 'created_at', 'updated_at'],
}
