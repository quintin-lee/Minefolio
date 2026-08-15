# Minefolio 移动端 App 实现计划

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

> **Revisions（2026-08-15 fit-analysis）：** 本计划已按「与当前代码匹配度分析」修订并内联标注（🔴=必须修订，🟡=适配建议）。关键变更：(1) 移动端 API base URL 必须经 `VITE_API_URL` 指向真实后端（Capacitor 无 dev proxy）；(2) http.ts 的 1001 重定向改为路由导航而非 `window.location`；(3) 移除 `__MOBILE__` 编译常量，改用独立入口隔离；(4) dts 独立路径；(5) `ExpenseCategoryPie`（height:100%）复用需定高父容器；(6) ReportsMobile 可用 `api/reports.ts`；(7) 后端测试判据改为 `FAIL=0` + 基线持平（assert 数 ≥79）。详见各 Chunk 内 🟡/🔴 注记。

**Goal:** 在现有 Vue 3 前端基础上，新增一套离线优先的移动端（Capacitor 封装），支持无网络记账、本地 sql.js 存储、联网后自动双向同步，并复用 90%+ 现有 API / store / 图表组件。

**Architecture:** 新增独立的移动端入口（`main-mobile.ts` + `vite.config.mobile.ts` + `index.mobile.html`）。本地用 sql.js（WASM SQLite，schema 与后端一致）持久化到 localStorage；写操作失败或被离线拦截时写入 `sync_queue` 软删除标记；`useSyncStore` 在 app 启动 / 页面聚焦 / 网络恢复时执行「先推本地、后拉远程、服务端胜出」的同步协议。移动端视图全部位于 `src/views-mobile/`，复用现有 `api/*`、`stores/*`、`utils/http.ts` 与 ECharts 组件。

**Tech Stack:** Vue 3 + TypeScript + Pinia + Element Plus + Vue Router + ECharts；Capacitor 6（@capacitor/network, @capacitor/app, @capacitor/haptics）；sql.js（WASM SQLite）；Vite（双 build target）；Vitest（单测）。

---

## 文件结构与职责（先锁定分解）

新增文件：
- `frontend/index.mobile.html` — 移动端 HTML 入口，加载 `src/main-mobile.ts`
- `frontend/vite.config.mobile.ts` — 移动 build：`build.outDir: dist-mobile`、`rollupOptions.input` 指向 `index.mobile.html`（**无 `__MOBILE__`，用独立入口隔离**）
- `frontend/capacitor.config.ts` — Capacitor 容器配置（webDir: dist-mobile）
- `frontend/src/main-mobile.ts` — 移动端启动：挂载 app、pinia、router、ElementPlus、init sync store + 网络监听
- `frontend/src/router/mobile.ts` — 移动端路由（/m/* 五 Tab + 登录）
- `frontend/src/db/schema.ts` — 本地 SQLite 建表 DDL（与后端 migration.sql 对齐 + `__deleted` 软删列 + `sync_queue`/`sync_meta`）
- `frontend/src/db/local.ts` — sql.js 封装：init / query / run / persist（localStorage）/ load / reset
- `frontend/src/stores/sync.ts` — 同步队列 Pinia store（enqueue / pushLocal / pullRemote / conflict 处理 / 状态）
- `frontend/src/utils/offline-http.ts` — 包装 `http`：在线直连、离线落本地 + 入队、返回成功信封
- `frontend/src/views-mobile/MobileLayout.vue` — 底部 5-Tab 容器
- `frontend/src/views-mobile/LoginMobile.vue` — 移动登录（复用 auth store + RSA 加密）
- `frontend/src/views-mobile/DashboardMobile.vue` — 首页 KPI + 最近记录
- `frontend/src/views-mobile/DailyExpensesMobile.vue` — 收支卡片列表 + 上拉加载
- `frontend/src/views-mobile/ExpenseQuickSheet.vue` — 快记账底部抽屉（核心 < 3s 场景）
- `frontend/src/views-mobile/TransactionsMobile.vue` — 交易记录卡片列表
- `frontend/src/views-mobile/AssetsMobile.vue` — 资产卡片列表
- `frontend/src/views-mobile/ReportsMobile.vue` — 复用 MonthlyChart / ExpenseCategoryPie / NetWorthChart
- `frontend/src/views-mobile/SettingsMobile.vue` — 分类管理、改密、导出、同步状态
- `frontend/src/types/mobile.ts` — 移动端类型（SyncQueueItem / SyncMeta / LocalRecord 等）
- `frontend/src/utils/sync-network.ts` — 网络监听（@capacitor/network + window online/offline 兜底）
- `frontend/tests/db.local.spec.ts` — sql.js 本地层单测
- `frontend/tests/sync.store.spec.ts` — 同步 store 单测
- `frontend/tests/offline-http.spec.ts` — 离线拦截单测（mock http + localDb）

修改文件：
- `frontend/package.json` — 加 mobile script + Capacitor/sql.js 依赖
- `frontend/src/types/index.ts` — 追加移动端类型（或改用 `types/mobile.ts` 再 re-export）

不改动：`frontend/src/api/*`、`frontend/src/stores/auth.ts`、`frontend/src/stores/category.ts`、`frontend/src/utils/http.ts`（仅被 offline-http 包装）、`frontend/src/views/*`（桌面端）、`backend/*`（后端零改动）。

---

## Chunk 1: 基础设施（依赖 / Vite / Capacitor / HTML 入口）

> 🟡 **fit-analysis 2026-08-15**：本 chunk 与 `VITE_API_URL` 绑定。桌面 `http.ts` 以 `import.meta.env.VITE_API_URL` 为 Axios baseURL；移动端打包到 Capacitor 后没有 dev proxy，**必须在构建/运行期把 `VITE_API_URL` 指向真实后端地址**（`frontend/.env.mobile` 或 Capacitor 环境注入），否则 RSA public-key fetch 与离线 HTTP 都无法命中后端。见 Chunk 4/Task 12 的 🔴 说明。

### Task 1: 更新 package.json 增加移动端脚本与依赖

**Files:**
- Modify: `frontend/package.json`

- [ ] **Step 1: 写失败的 schema 检查（验证 JSON 合法）**

在终端运行：
```bash
node -e "JSON.parse(require('fs').readFileSync('frontend/package.json','utf8')); console.log('OK')"
```
预期：打印 `OK`（文件已存在，确认可达）。

- [ ] **Step 2: 修改 package.json**

在 `scripts` 中新增 `"build:mobile"` 与 capacitor 脚本；在 `dependencies` 新增 `@capacitor/network`、`@capacitor/app`、`@capacitor/haptics`、`sql.js`；在 `devDependencies` 新增 `@capacitor/cli`、`@capacitor/core`、`@capacitor/android`、`@capacitor/ios`、`vitest`、`@vue/test-utils`、`jsdom`。最终关键片段：

```json
{
  "scripts": {
    "dev": "vite",
    "dev:mobile": "vite --mode mobile",
    "build": "vue-tsc -b && vite build",
    "build:mobile": "vue-tsc -b && vite build --mode mobile",
    "preview": "vite preview",
    "test": "vitest run",
    "test:watch": "vitest",
    "cap:sync": "cap sync"
  },
  "dependencies": {
    "@capacitor/app": "^6.0.0",
    "@capacitor/core": "^6.0.0",
    "@capacitor/haptics": "^6.0.0",
    "@capacitor/network": "^6.0.0",
    "@element-plus/icons-vue": "^2.3.0",
    "axios": "^1.7.0",
    "echarts": "^5.5.0",
    "element-plus": "^2.7.0",
    "pinia": "^2.1.0",
    "sql.js": "^1.10.3",
    "vue": "^3.4.0",
    "vue-router": "^4.3.0"
  },
  "devDependencies": {
    "@capacitor/android": "^6.0.0",
    "@capacitor/cli": "^6.0.0",
    "@capacitor/ios": "^6.0.0",
    "@types/node": "^26.2.0",
    "@types/sql.js": "^1.4.9",
    "@vitejs/plugin-vue": "^5.0.0",
    "@vue/test-utils": "^2.4.0",
    "jsdom": "^24.0.0",
    "typescript": "~5.5.0",
    "unplugin-auto-import": "^0.17.0",
    "unplugin-vue-components": "^0.27.0",
    "vite": "^5.4.0",
    "vitest": "^1.6.0",
    "vue-tsc": "^2.0.0"
  }
}
```

- [ ] **Step 3: 安装依赖**

Run: `cd frontend && npm install`
Expected: 依赖安装完成，无 peer 致命错误。

- [ ] **Step 4: 提交**

```bash
git add frontend/package.json frontend/package-lock.json
git commit -m "feat(mobile): add capacitor/sql.js deps and mobile build scripts"
```

---

### Task 2: 新增移动端 Vite 配置

**Files:**
- Create: `frontend/vite.config.mobile.ts`

- [ ] **Step 1: 写 vite.config.mobile.ts**

```typescript
import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import AutoImport from 'unplugin-auto-import/vite'
import Components from 'unplugin-vue-components/vite'
import { ElementPlusResolver } from 'unplugin-vue-components/resolvers'
import { resolve } from 'path'

// Mobile build target: separate entry HTML + dist-mobile output.
export default defineConfig({
  plugins: [
    vue(),
    AutoImport({
      resolvers: [ElementPlusResolver({ importStyle: false })],
      imports: ['vue', 'vue-router', 'pinia'],
      dts: 'src/auto-imports.mobile.d.ts',
    }),
    Components({
      resolvers: [ElementPlusResolver({ importStyle: false })],
      dts: 'src/components.mobile.d.ts',
    }),
  ],
  resolve: {
    alias: { '@': resolve(__dirname, 'src') },
  },
  server: {
    port: 5174,
    proxy: { '/api': { target: 'http://localhost:8080', changeOrigin: true } },
  },
  build: {
    outDir: 'dist-mobile',
    emptyOutDir: true,
    rollupOptions: {
      input: resolve(__dirname, 'index.mobile.html'),
    },
  },
})
```

> 🟡 **fit-analysis 2026-08-15 修订**：本片段较原计划做两处修正——(1) **移除 `__MOBILE__` define**，与计划全局「不使用 `__MOBILE__` 开关」的原则自洽（代码不需要按平台分支，靠独立入口隔离）；(2) **dts 改用独立路径** `auto-imports.mobile.d.ts` / `components.mobile.d.ts`，避免桌面 `npm run build` 与 `build:mobile` 互相覆盖 `src/auto-imports.d.ts` / `src/components.d.ts`。

- [ ] **Step 2: 验证配置可被加载（不实际构建）**

Run: `cd frontend && npx vite build --mode mobile --logLevel error || true`
> 此时 `index.mobile.html` 尚不存在，会报输入文件缺失——这是预期，下一步创建 HTML 后即解决。仅确认配置语法无误（无 TS 报错）。

- [ ] **Step 3: 提交**

```bash
git add frontend/vite.config.mobile.ts
git commit -m "feat(mobile): add vite mobile build config (separate entry + dist-mobile)"
```

---

### Task 3: 新增移动端 HTML 入口与 Capacitor 配置

**Files:**
- Create: `frontend/index.mobile.html`
- Create: `frontend/capacitor.config.ts`

- [ ] **Step 1: 写 index.mobile.html**

复制桌面 `index.html` 结构，仅把入口改为 `src/main-mobile.ts`，title 改为 Minefolio Mobile：

```html
<!DOCTYPE html>
<html lang="zh-CN">
  <head>
    <meta charset="UTF-8" />
    <link rel="icon" href="/favicon.ico" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover, maximum-scale=1.0, user-scalable=no" />
    <title>Minefolio Mobile</title>
  </head>
  <body>
    <div id="app"></div>
    <script type="module" src="/src/main-mobile.ts"></script>
  </body>
</html>
```

- [ ] **Step 2: 写 capacitor.config.ts**

```typescript
import type { CapacitorConfig } from '@capacitor/cli'

const config: CapacitorConfig = {
  appId: 'com.minefolio.app',
  appName: 'Minefolio',
  webDir: 'dist-mobile',
  server: {
    androidScheme: 'https',
    iosScheme: 'app',
  },
  plugins: {
    SplashScreen: { launchShowDuration: 500 },
    Keyboard: { resize: 'body' },
  },
}

export default config
```

- [ ] **Step 3: 提交**

```bash
git add frontend/index.mobile.html frontend/capacitor.config.ts
git commit -m "feat(mobile): add mobile html entry and capacitor config"
```

---

## Chunk 2: 本地 SQLite 层（sql.js）

### Task 4: 本地 Schema 定义

**Files:**
- Create: `frontend/src/db/schema.ts`

- [ ] **Step 1: 写 schema.ts**

与后端 `backend/sql/migration.sql` 业务表对齐，额外加 `__deleted`（软删标记）与同步表。所有业务表 `id` 使用服务端真实 id（离线创建用本地负 id 或时间戳，入队时记录 `record_id`）。

```typescript
export const LOCAL_SCHEMA = `
CREATE TABLE IF NOT EXISTS categories (
  id           INTEGER PRIMARY KEY,
  user_id      INTEGER NOT NULL,
  name         TEXT NOT NULL,
  parent_id    INTEGER,
  type         TEXT NOT NULL DEFAULT 'asset',
  asset_type   TEXT DEFAULT 'cash',
  currency     TEXT DEFAULT 'CNY',
  icon         TEXT,
  sort_order   INTEGER DEFAULT 0,
  created_at   TEXT,
  __deleted    INTEGER DEFAULT 0
);

CREATE TABLE IF NOT EXISTS assets (
  id            INTEGER PRIMARY KEY,
  user_id       INTEGER NOT NULL,
  category_id   INTEGER NOT NULL,
  name          TEXT NOT NULL,
  account_no    TEXT,
  current_value REAL DEFAULT 0,
  currency      TEXT DEFAULT 'CNY',
  note          TEXT,
  created_at    TEXT,
  updated_at    TEXT,
  __deleted     INTEGER DEFAULT 0
);

CREATE TABLE IF NOT EXISTS transactions (
  id               INTEGER PRIMARY KEY,
  user_id          INTEGER NOT NULL,
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
  currency         TEXT DEFAULT 'CNY',
  transaction_date TEXT NOT NULL,
  note             TEXT,
  created_at       TEXT,
  __deleted        INTEGER DEFAULT 0
);

CREATE TABLE IF NOT EXISTS tags (
  id         INTEGER PRIMARY KEY,
  user_id    INTEGER NOT NULL,
  name       TEXT NOT NULL,
  color      TEXT DEFAULT '',
  created_at TEXT,
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
  user_id      INTEGER NOT NULL,
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

-- 同步队列：operation='delete' 不物理删本地，仅打 __deleted=1
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
```

- [ ] **Step 2: 提交**

```bash
git add frontend/src/db/schema.ts
git commit -m "feat(mobile): define local SQLite schema mirroring backend"
```

---

### Task 5: sql.js 本地封装（init / query / run / persist）

**Files:**
- Create: `frontend/src/db/local.ts`
- Create: `frontend/src/types/mobile.ts` (类型放此处，Task 7 再 re-export 到 index.ts)

- [ ] **Step 1: 写类型文件 types/mobile.ts**

```typescript
export interface SyncQueueItem {
  id: number
  table_name: string
  record_id: number
  operation: 'create' | 'update' | 'delete'
  payload: string // JSON 完整记录
  local_version: string // ISO 8601
  synced: number // 0|1
  conflict: number // 0|1
}

export interface SyncMeta {
  key: string
  value: string
}

// 本地查询结果行（业务表 + __deleted）
export type LocalRow = Record<string, unknown> & { __deleted?: number }

export interface DbResult {
  columns: string[]
  values: unknown[][]
}
```

- [ ] **Step 2: 写 local.ts**

```typescript
// frontend/src/db/local.ts
// sql.js (WASM SQLite) wrapper, persisted to localStorage as Uint8Array JSON.
import initSqlJs, { type Database, type SqlJsStatic } from 'sql.js'
import { LOCAL_SCHEMA } from './schema'
import type { DbResult } from '@/types/mobile'

const STORAGE_KEY = 'minefolio_local_db'
let db: Database | null = null
let sqlJs: SqlJsStatic | null = null

function locateFile(file: string): string {
  // sql.js WASM 走 CDN（spec §7）。如需自托管改为 '/sql-wasm.wasm'。
  return `https://sql.js.org/dist/${file}`
}

async function ensureSqlJs(): Promise<SqlJsStatic> {
  if (!sqlJs) sqlJs = await initSqlJs({ locateFile })
  return sqlJs
}

export async function initLocalDb(): Promise<Database> {
  if (db) return db
  const SQL = await ensureSqlJs()
  const saved = typeof localStorage !== 'undefined' ? localStorage.getItem(STORAGE_KEY) : null
  if (saved) {
    const bytes = new Uint8Array(JSON.parse(saved))
    db = new SQL.Database(bytes)
  } else {
    db = new SQL.Database()
    db.run(LOCAL_SCHEMA)
  }
  return db
}

export function getDb(): Database {
  if (!db) throw new Error('localDb not initialized; call initLocalDb() first')
  return db
}

export function query(sql: string, params: unknown[] = []): DbResult[] {
  return getDb().exec(sql, params)
}

export function run(sql: string, params: unknown[] = []): void {
  getDb().run(sql, params)
}

export function persist(): void {
  if (!db) return
  const data = db.export()
  localStorage.setItem(STORAGE_KEY, JSON.stringify(Array.from(data)))
}

export function resetLocalDb(): void {
  if (db) db.close()
  db = null
  localStorage.removeItem(STORAGE_KEY)
}

// 将 exec 结果转为对象数组（过滤软删记录由调用方决定）
export function rowsFrom(result: DbResult[]): LocalRow[] {
  if (!result.length) return []
  const { columns, values } = result[0]
  return values.map((row) => {
    const obj: LocalRow = {}
    columns.forEach((c, i) => (obj[c] = row[i]))
    return obj
  })
}
```

- [ ] **Step 3: 提交**

```bash
git add frontend/src/db/local.ts frontend/src/types/mobile.ts
git commit -m "feat(mobile): sql.js local db wrapper with localStorage persistence"
```

---

## Chunk 3: 同步 Store 与离线 HTTP

### Task 6: 同步 Pinia Store

**Files:**
- Create: `frontend/src/stores/sync.ts`

- [ ] **Step 1: 写 useSyncStore**

```typescript
// frontend/src/stores/sync.ts
import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { getDb, run, query, rowsFrom, persist } from '@/db/local'
import { dailyExpensesApi } from '@/api/daily_expenses'
import { transactionsApi } from '@/api/transactions'
import { assetsApi } from '@/api/assets'
import type { SyncQueueItem } from '@/types/mobile'

const API_BY_TABLE: Record<string, any> = {
  'daily_expenses': dailyExpensesApi,
  'transactions': transactionsApi,
  'assets': assetsApi,
}

export const useSyncStore = defineStore('sync', () => {
  const queue = ref<SyncQueueItem[]>([])
  const syncing = ref(false)
  const lastSyncAt = ref<string | null>(null)
  const pendingCount = computed(() => queue.value.filter((q) => q.synced === 0).length)

  function loadQueue(): void {
    const res = query(
      "SELECT id, table_name, record_id, operation, payload, local_version, synced, conflict FROM sync_queue WHERE synced = 0 ORDER BY id ASC"
    )
    queue.value = rowsFrom(res).map((r) => ({
      id: r.id as number,
      table_name: r.table_name as string,
      record_id: r.record_id as number,
      operation: r.operation as SyncQueueItem['operation'],
      payload: r.payload as string,
      local_version: r.local_version as string,
      synced: r.synced as number,
      conflict: r.conflict as number,
    }))
  }

  function setLastSync(at: string): void {
    lastSyncAt.value = at
    run("INSERT OR REPLACE INTO sync_meta (key, value) VALUES ('last_sync_at', ?)", [at])
    persist()
  }

  // 入队：本地已写，记录待推送
  function enqueue(tableName: string, recordId: number, operation: SyncQueueItem['operation'], payload: Record<string, unknown>): void {
    run(
      'INSERT INTO sync_queue (table_name, record_id, operation, payload, local_version) VALUES (?, ?, ?, ?, ?)',
      [tableName, recordId, operation, JSON.stringify(payload), new Date().toISOString()]
    )
    persist()
    loadQueue()
  }

  function markSynced(queueId: number): void {
    run('UPDATE sync_queue SET synced = 1 WHERE id = ?', [queueId])
    persist()
    loadQueue()
  }

  function markConflict(queueId: number): void {
    run('UPDATE sync_queue SET conflict = 1 WHERE id = ?', [queueId])
    persist()
    loadQueue()
  }

  // 推本地队列到服务端
  async function pushLocal(): Promise<void> {
    loadQueue()
    for (const item of queue.value) {
      const api = API_BY_TABLE[item.table_name]
      if (!api) continue
      const payload = JSON.parse(item.payload)
      try {
        if (item.operation === 'create') await api.create(payload)
        else if (item.operation === 'update') await api.update(item.record_id, payload)
        else if (item.operation === 'delete') await api.delete(item.record_id)
        markSynced(item.id)
      } catch (e: any) {
        // 服务端业务错误（如已不存在）→ 标记冲突，停止该条推送但保留队列
        if (e?.response?.data?.code && e.response.data.code !== 0) markConflict(item.id)
      }
    }
  }

  // 拉远程到本地（服务端胜出：updated_at 较新则覆盖）
  async function pullRemote(): Promise<void> {
    const tables = ['daily_expenses', 'transactions', 'assets']
    for (const table of tables) {
      const api = API_BY_TABLE[table]
      if (!api?.list) continue
      const res = await api.list({ page_size: 500 })
      const remoteRows = res.list as any[]
      for (const remote of remoteRows) {
        const local = query(`SELECT * FROM ${table} WHERE id = ?`, [remote.id])
        const localRow = rowsFrom(local)[0]
        const localUpdated = localRow?.updated_at ? new Date(localRow.updated_at as string).getTime() : 0
        const remoteUpdated = remote.updated_at ? new Date(remote.updated_at).getTime() : 0
        if (!localRow || remoteUpdated >= localUpdated) {
          upsertLocal(table, remote)
        }
      }
    }
    persist()
  }

  function upsertLocal(table: string, record: Record<string, unknown>): void {
    const cols = Object.keys(record).filter((k) => k !== '__deleted')
    const placeholders = cols.map(() => '?').join(', ')
    const updates = cols.map((c) => `${c} = ?`).join(', ')
    run(
      `INSERT INTO ${table} (${cols.join(', ')}, __deleted) VALUES (${placeholders}, 0)
       ON CONFLICT(id) DO UPDATE SET ${updates}, __deleted = 0`,
      [...cols.map((c) => record[c]), ...cols.map((c) => record[c])]
    )
  }

  async function syncNow(): Promise<void> {
    if (syncing.value) return
    syncing.value = true
    try {
      await pushLocal()
      await pullRemote()
      setLastSync(new Date().toISOString())
    } finally {
      syncing.value = false
    }
  }

  function init(): void {
    getDb() // 确保已 init
    const res = query("SELECT value FROM sync_meta WHERE key = 'last_sync_at'")
    if (res.length) lastSyncAt.value = res[0].values[0][0] as string
    loadQueue()
  }

  return { queue, syncing, lastSyncAt, pendingCount, enqueue, markSynced, markConflict, pushLocal, pullRemote, syncNow, init }
})
```

- [ ] **Step 2: 提交**

```bash
git add frontend/src/stores/sync.ts
git commit -m "feat(mobile): add sync queue pinia store with push/pull protocol"
```

---

### Task 7: 离线 HTTP 拦截器

**Files:**
- Create: `frontend/src/utils/offline-http.ts`

- [ ] **Step 1: 写 offline-http.ts**

封装 `http`：在线正常返回；离线或网络异常 → 写本地 SQLite + 入队 sync_queue，返回兼容信封（让 UI 不报错）。软删：operation=delete 时本地打 `__deleted=1` 而不物理删。

> 🟡 **fit-analysis 2026-08-15**：裸 `http` 的网络失败分支会先弹 `ElMessage.error('网络错误')`（桌面式 toast）。移动端离线记账改走本封装，需**抑制**该 toast 改「静默落本地 + success 提示」。做法：把 http 实例的响应拦截器错误分支的 ElMessage 调用改成可用 `offlineMode` 关闭，或在捕获网络错误时先吞掉再落本地。第 3 行 `import http from './http'` 保持不变（复用同一 axios 实例、token/CSRF 注入），仅在其离线分支做 toast 抑制处理。

```typescript
// frontend/src/utils/offline-http.ts
import http from './http'
import { getDb, run, persist } from '@/db/local'
import { useSyncStore } from '@/stores/sync'
import { useAuthStore } from '@/stores/auth'
import { ElMessage } from 'element-plus'

const TABLE_BY_PATH: Record<string, string> = {
  'daily-expenses': 'daily_expenses',
  'transactions': 'transactions',
  'assets': 'assets',
}

function tableNameFromUrl(url: string): string | null {
  for (const key of Object.keys(TABLE_BY_PATH)) {
    if (url.includes(`/${key}`)) return TABLE_BY_PATH[key]
  }
  return null
}

function extractId(url: string): number | null {
  const m = url.match(/\/(\d+)(?:\?|$)/)
  return m ? Number(m[1]) : null
}

async function writeLocal(table: string, operation: 'create' | 'update' | 'delete', recordId: number, payload: Record<string, unknown>): Promise<void> {
  if (operation === 'delete') {
    run(`UPDATE ${table} SET __deleted = 1, updated_at = NULL WHERE id = ?`, [recordId])
  } else {
    const cols = Object.keys(payload).filter((k) => k !== '__deleted')
    const placeholders = cols.map(() => '?').join(', ')
    const updates = cols.map((c) => `${c} = ?`).join(', ')
    run(
      `INSERT INTO ${table} (${cols.join(', ')}, __deleted) VALUES (${placeholders}, 0)
       ON CONFLICT(id) DO UPDATE SET ${updates}, __deleted = 0`,
      [...cols.map((c) => payload[c]), ...cols.map((c) => payload[c])]
    )
  }
  persist()
  useSyncStore().enqueue(table, recordId, operation, payload)
}

// 鉴权失败(1001)专用：桌面 http 会 window.location=/login，移动端需改为 router 导航
function isAuthError(err: any): boolean {
  return err?.response?.data?.code === 1001
}

export async function offlineRequest(method: 'get' | 'post' | 'put' | 'delete', url: string, data?: Record<string, unknown>): Promise<any> {
  const table = tableNameFromUrl(url)
  if (table) {
    const id = extractId(url)
    const operation: 'create' | 'update' | 'delete' =
      method === 'post' ? 'create' : method === 'put' ? 'update' : 'delete'
    try {
      const res = await http({ method, url, data } as any)
      return res
    } catch (err: any) {
      if (isAuthError(err)) {
        // 鉴权失败 → 注销并抛错，由外层路由到 /m/login（🔴 见 Task 10 说明）
        useAuthStore().logout?.()
        throw err
      }
      // 网络失败：落本地 + 入队（抑制桌面 '网络错误' toast）
      const recordId = id ?? (data?.id as number) ?? Date.now()
      await writeLocal(table, operation, recordId, data ?? {})
      ElMessage.success('已离线保存，联网后自动同步')
      return { offline: true, id: recordId }
    }
  }
  // 非业务表请求（如 summary、reports）→ 直接透传
  return http({ method, url, data } as any)
}

export const offlineApi = {
  get: (url: string) => offlineRequest('get', url),
  post: (url: string, data?: Record<string, unknown>) => offlineRequest('post', url, data),
  put: (url: string, data?: Record<string, unknown>) => offlineRequest('put', url, data),
  delete: (url: string) => offlineRequest('delete', url),
}
```

- [ ] **Step 2: 提交**

```bash
git add frontend/src/utils/offline-http.ts
git commit -m "feat(mobile): offline http wrapper that queues failed writes locally"
```

---

## Chunk 4: 移动端入口、路由、网络监听

### Task 8: 移动端入口 main-mobile.ts

**Files:**
- Create: `frontend/src/main-mobile.ts`

- [ ] **Step 1: 写 main-mobile.ts**

```typescript
// frontend/src/main-mobile.ts
import { createApp } from 'vue'
import { createPinia } from 'pinia'
import ElementPlus from 'element-plus'
import zhCn from 'element-plus/es/locale/lang/zh-cn'
import 'element-plus/dist/index.css'
import './styles/index.css'
import * as ElementPlusIconsVue from '@element-plus/icons-vue'
import App from './App.vue'
import router from './router/mobile'
import { initLocalDb } from '@/db/local'
import { useSyncStore } from '@/stores/sync'
import { registerNetworkListeners } from '@/utils/sync-network'

async function bootstrap() {
  await initLocalDb()
  const app = createApp(App)

  for (const [key, component] of Object.entries(ElementPlusIconsVue)) {
    app.component(key, component)
  }

  app.use(createPinia())
  app.use(router)
  app.use(ElementPlus, { locale: zhCn })

  const sync = useSyncStore()
  sync.init()
  registerNetworkListeners(() => sync.syncNow())

  app.mount('#app')
}

bootstrap()
```

- [ ] **Step 2: 提交**

```bash
git add frontend/src/main-mobile.ts
git commit -m "feat(mobile): add mobile bootstrap entry"
```

---

### Task 9: 移动端路由

**Files:**
- Create: `frontend/src/router/mobile.ts`

- [ ] **Step 1: 写 router/mobile.ts**

```typescript
// frontend/src/router/mobile.ts
import { createRouter, createWebHistory } from 'vue-router'
import { useAuthStore } from '@/stores/auth'

const router = createRouter({
  history: createWebHistory(),
  routes: [
    {
      path: '/m/login',
      name: 'MobileLogin',
      component: () => import('@/views-mobile/LoginMobile.vue'),
      meta: { requiresAuth: false },
    },
    {
      path: '/m',
      component: () => import('@/views-mobile/MobileLayout.vue'),
      meta: { requiresAuth: true },
      children: [
        { path: '', redirect: '/m/dashboard' },
        { path: 'dashboard', name: 'MobileDashboard', component: () => import('@/views-mobile/DashboardMobile.vue') },
        { path: 'expenses', name: 'MobileExpenses', component: () => import('@/views-mobile/DailyExpensesMobile.vue') },
        { path: 'transactions', name: 'MobileTransactions', component: () => import('@/views-mobile/TransactionsMobile.vue') },
        { path: 'assets', name: 'MobileAssets', component: () => import('@/views-mobile/AssetsMobile.vue') },
        { path: 'reports', name: 'MobileReports', component: () => import('@/views-mobile/ReportsMobile.vue') },
        { path: 'settings', name: 'MobileSettings', component: () => import('@/views-mobile/SettingsMobile.vue') },
      ],
    },
  ],
})

router.beforeEach(async (to, _from, next) => {
  const auth = useAuthStore()
  if (auth.isInitialized === null) await auth.checkSystemStatus()
  if (auth.isInitialized === false) return next('/m/login') // 未初始化：移动端复用同一后端，异常引导去登录
  if (to.meta.requiresAuth !== false && !auth.token) next('/m/login')
  else next()
})

export default router
```

- [ ] **Step 2: 提交**

```bash
git add frontend/src/router/mobile.ts
git commit -m "feat(mobile): add mobile router with 5-tab layout"
```

---

### Task 10: 网络监听工具

**Files:**
- Create: `frontend/src/utils/sync-network.ts`

> 🔴 **关键（fit-analysis 2026-08-15）— `utils/http.ts` 的 1001 重定向对移动端不友好**：`http.ts` 响应拦截器在 `code === 1001` 时执行 `useAuthStore().logout(); window.location.href = '/login'`。桌面 OK，但移动端：(1) 目标应为 `/m/login`；(2) `window.location.href` 会把 Capacitor WebView 页面整体导航离开 App 壳。
> **处理**：`offlineApi`（Task 7）对移动端鉴权失败要改走「注销 + `router.push('/m/login')`」而非 `window.location`。可在 offline-http 中包一层：捕获 1001 时 `auth.logout()` 并返回 `{code:1001}`，由 MobileLayout/视图引导 `router.push('/m/login')`。**不要在移动端调用裸 `http` 的写路径**（其拦截器会触发桌面式重定向）。

- [ ] **Step 1: 写 sync-network.ts**

优先用 @capacitor/network（原生），浏览器/Web 回退到 `window` online/offline 事件。页面聚焦（visibilitychange）也触发增量同步。

```typescript
// frontend/src/utils/sync-network.ts
import { Network } from '@capacitor/network'

type SyncTrigger = () => void | Promise<void>

export function registerNetworkListeners(onSync: SyncTrigger): void {
  // 原生环境
  Network.addListener('networkStatusChange', (status) => {
    if (status.connected) void onSync()
  })

  // Web / 兜底
  window.addEventListener('online', () => void onSync())
  window.addEventListener('offline', () => {})

  // 页面从后台切回前台 → 增量同步
  document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'visible') void onSync()
  })

  // app 前后台（Capacitor）
  import('@capacitor/app')
    .then(({ App }) => {
      App.addListener('appStateChange', (state: { isActive: boolean }) => {
        if (state.isActive) void onSync()
      })
    })
    .catch(() => {})
}
```

- [ ] **Step 2: 提交**

```bash
git add frontend/src/utils/sync-network.ts
git commit -m "feat(mobile): network/app lifecycle listeners trigger sync"
```

---

## Chunk 5: 移动端布局与视图

### Task 11: MobileLayout（底部 5-Tab 容器）

**Files:**
- Create: `frontend/src/views-mobile/MobileLayout.vue`

- [ ] **Step 1: 写 MobileLayout.vue**

```vue
<template>
  <div class="mobile-layout">
    <main class="mobile-content">
      <router-view />
    </main>
    <nav class="tab-bar">
      <button
        v-for="tab in tabs"
        :key="tab.name"
        class="tab-item"
        :class="{ active: route.path.startsWith(tab.prefix) }"
        @click="go(tab)"
      >
        <el-icon :size="22"><component :is="tab.icon" /></el-icon>
        <span>{{ tab.label }}</span>
      </button>
    </nav>
  </div>
</template>

<script setup lang="ts">
import { useRoute, useRouter } from 'vue-router'
import { DataAnalysis, Plus, Wallet, PieChart, Setting } from '@element-plus/icons-vue'
import { useSyncStore } from '@/stores/sync'

const route = useRoute()
const router = useRouter()
const sync = useSyncStore()

const tabs = [
  { name: 'dashboard', label: '首页', icon: DataAnalysis, prefix: '/m/dashboard' },
  { name: 'expenses', label: '记账', icon: Plus, prefix: '/m/expenses' },
  { name: 'assets', label: '资产', icon: Wallet, prefix: '/m/assets' },
  { name: 'reports', label: '报表', icon: PieChart, prefix: '/m/reports' },
  { name: 'settings', label: '我的', icon: Setting, prefix: '/m/settings' },
]

function go(tab: typeof tabs[number]) {
  router.push(tab.prefix)
}
</script>

<style scoped>
.mobile-layout {
  display: flex;
  flex-direction: column;
  height: 100vh;
  background: var(--mf-background);
}
.mobile-content {
  flex: 1;
  overflow-y: auto;
  padding: 16px;
  padding-bottom: 80px;
}
.tab-bar {
  position: fixed;
  bottom: 0;
  left: 0;
  right: 0;
  height: 64px;
  display: flex;
  background: var(--mf-surface);
  border-top: 1px solid var(--mf-border);
  padding-bottom: env(safe-area-inset-bottom);
}
.tab-item {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 2px;
  background: none;
  border: none;
  color: var(--mf-text-muted);
  font-size: 12px;
  cursor: pointer;
}
.tab-item.active {
  color: var(--mf-primary);
}
</style>
```

- [ ] **Step 2: 提交**

```bash
git add frontend/src/views-mobile/MobileLayout.vue
git commit -m "feat(mobile): bottom 5-tab layout container"
```

---

### Task 12: LoginMobile（复用 auth store + RSA 加密）

**Files:**
- Create: `frontend/src/views-mobile/LoginMobile.vue`

- [ ] **Step 1: 写 LoginMobile.vue**

复用桌面登录逻辑：RSA 公钥加密密码 → `auth.login`。从 `frontend/src/stores/auth.ts` 复制 `fetchRsaJwk`/`encryptPassword` 逻辑（它们当前是模块私有函数），故在移动端内联一份相同的加密实现（保持与后端一致）。

> 🔴 **关键（fit-analysis 2026-08-15）**：桌面 `stores/auth.ts` 用相对路径 `fetch('/api/auth/public-key')`，桌面靠 Vite proxy 转发。**Capacitor 独立 WebView 没有 dev proxy**，相对路径命中不到后端。因此移动端内联版 **必须**用 `import.meta.env.VITE_API_URL`（与 `utils/http.ts` 的 Axios baseURL 同一变量）拼接 `base + '/api/auth/public-key'`，WebView/浏览器回退到 `window.location.origin`。构建时通过 `.env` 为 Capacitor 配置 `VITE_API_URL` 指向真实后端地址。

```vue
<template>
  <div class="login-mobile">
    <h1 class="brand">Minefolio</h1>
    <el-form :model="form" label-position="top">
      <el-form-item label="用户名">
        <el-input v-model="form.username" placeholder="用户名" />
      </el-form-item>
      <el-form-item label="密码">
        <el-input v-model="form.password" type="password" placeholder="密码" @keyup.enter="submit" />
      </el-form-item>
      <el-button type="primary" :loading="loading" @click="submit" block>登录</el-button>
    </el-form>
  </div>
</template>

<script setup lang="ts">
import { reactive, ref } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { useAuthStore } from '@/stores/auth'

const router = useRouter()
const auth = useAuthStore()
const loading = ref(false)
const form = reactive({ username: '', password: '' })

async function encryptPassword(pw: string): Promise<string> {
  const base = import.meta.env.VITE_API_URL || window.location.origin
  const r = await fetch(`${base}/api/auth/public-key`)
  if (!r.ok) throw new Error('Failed to fetch public key')
  const jwk = (await r.json()).data.public_key
  const key = await crypto.subtle.importKey('jwk', jwk, { name: 'RSA-OAEP', hash: 'SHA-256' }, false, ['encrypt'])
  const enc = await crypto.subtle.encrypt({ name: 'RSA-OAEP' }, key, new TextEncoder().encode(pw))
  return btoa(String.fromCharCode(...new Uint8Array(enc))).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '')
}

async function submit() {
  if (!form.username || !form.password) return ElMessage.warning('请输入用户名和密码')
  loading.value = true
  try {
    await auth.login(form.username, await encryptPassword(form.password))
    router.replace('/m/dashboard')
  } catch {
    ElMessage.error('登录失败')
  } finally {
    loading.value = false
  }
}
</script>

<style scoped>
.login-mobile { padding: 48px 24px; display: flex; flex-direction: column; gap: 24px; }
.brand { text-align: center; font-size: 28px; color: var(--mf-primary); }
</style>
```

- [ ] **Step 2: 提交**

```bash
git add frontend/src/views-mobile/LoginMobile.vue
git commit -m "feat(mobile): login view reusing auth store + RSA encryption"
```

---

### Task 13: DashboardMobile（KPI + 最近记录）

**Files:**
- Create: `frontend/src/views-mobile/DashboardMobile.vue`

- [ ] **Step 1: 写 DashboardMobile.vue**

复用 `summaryApi.get()` 与 `dailyExpensesApi.list()`；横向滚动 KPI 卡片 + 最近 5 条。离线时数据来自本地表（后续 Task 单独处理离线读取，此处先接 API）。

```vue
<template>
  <div class="dashboard-mobile">
    <div class="page-header"><h2>首页</h2></div>
    <div class="kpi-row">
      <div class="kpi-card cyan"><span>总资产</span><b>{{ fmt(summary.total_assets) }}</b></div>
      <div class="kpi-card red"><span>总负债</span><b>{{ fmt(summary.total_liabilities) }}</b></div>
      <div class="kpi-card green"><span>净资产</span><b>{{ fmt(summary.net_worth) }}</b></div>
    </div>
    <h3 class="section-title">本月收支</h3>
    <div class="mini-row">
      <div><span>收入</span><b class="income">{{ fmt(month?.total_income ?? 0) }}</b></div>
      <div><span>支出</span><b class="expense">{{ fmt(month?.total_expense ?? 0) }}</b></div>
      <div><span>结余</span><b>{{ fmt(month?.balance ?? 0) }}</b></div>
    </div>
    <h3 class="section-title">最近记录</h3>
    <div v-for="e in recent" :key="e.id" class="record-card">
      <span class="cat">{{ e.category_name }}</span>
      <span :class="e.expense_type === 'income' ? 'income' : 'expense'">
        {{ e.expense_type === 'income' ? '+' : '-' }}{{ fmt(e.amount) }}
      </span>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { summaryApi } from '@/api/summary'
import { dailyExpensesApi } from '@/api/daily_expenses'
import type { Summary, DailyExpense, ExpenseMonthly } from '@/types'

const summary = ref<Summary>({ total_assets: 0, total_liabilities: 0, net_worth: 0, breakdown: [], trend: [] })
const month = ref<ExpenseMonthly | null>(null)
const recent = ref<DailyExpense[]>([])

function fmt(v: number) {
  return new Intl.NumberFormat('zh-CN', { style: 'currency', currency: 'CNY' }).format(v ?? 0)
}

onMounted(async () => {
  const now = new Date()
  const [s, m, r] = await Promise.all([
    summaryApi.get(),
    dailyExpensesApi.monthly(now.getFullYear(), now.getMonth() + 1),
    dailyExpensesApi.list({ page_size: 5 }),
  ])
  summary.value = s
  month.value = m
  recent.value = r.list
})
</script>

<style scoped>
.kpi-row { display: flex; gap: 12px; overflow-x: auto; }
.kpi-card { flex: 0 0 120px; background: var(--mf-surface); border: 1px solid var(--mf-border); border-radius: 12px; padding: 16px; display: flex; flex-direction: column; gap: 8px; }
.kpi-card.cyan b { color: #00d4ff; } .kpi-card.red b { color: #f87171; } .kpi-card.green b { color: #34d399; }
.kpi-card span { color: var(--mf-text-muted); font-size: 12px; }
.kpi-card b { font-size: 18px; font-family: 'JetBrains Mono', monospace; }
.mini-row { display: flex; justify-content: space-between; margin: 12px 0; }
.mini-row > div { display: flex; flex-direction: column; gap: 4px; }
.mini-row span { color: var(--mf-text-muted); font-size: 12px; }
.mini-row b { font-family: 'JetBrains Mono', monospace; }
.section-title { margin: 16px 0 8px; font-size: 14px; color: var(--mf-text-muted); }
.record-card { display: flex; justify-content: space-between; padding: 12px; border-bottom: 1px solid var(--mf-border); }
.income { color: #34d399; } .expense { color: #f87171; }
</style>
```

- [ ] **Step 2: 提交**

```bash
git add frontend/src/views-mobile/DashboardMobile.vue
git commit -m "feat(mobile): dashboard with KPI cards and recent records"
```

---

### Task 14: DailyExpensesMobile（卡片列表 + 上拉加载）

**Files:**
- Create: `frontend/src/views-mobile/DailyExpensesMobile.vue`

- [ ] **Step 1: 写 DailyExpensesMobile.vue**

卡片式列表（替代桌面表格），底部「+」FAB 打开快记账抽屉。上拉加载更多（分页）。编辑/删除走 ExpenseQuickSheet。

```vue
<template>
  <div class="expenses-mobile">
    <div class="page-header">
      <h2>收支</h2>
      <el-button size="small" @click="loadMore" :loading="loading">加载更多</el-button>
    </div>
    <div class="summary-row">
      <span>收入 {{ fmt(month?.total_income ?? 0) }}</span>
      <span>支出 {{ fmt(month?.total_expense ?? 0) }}</span>
    </div>
    <div v-for="e in list" :key="e.id" class="expense-card" @click="edit(e)">
      <div class="top"><span class="cat">{{ e.category_name }}</span><span :class="e.expense_type === 'income' ? 'income' : 'expense'">{{ e.expense_type === 'income' ? '+' : '-' }}{{ fmt(e.amount) }}</span></div>
      <div class="bottom"><span>{{ e.expense_date }}</span><span>{{ e.asset_name }}</span></div>
    </div>

    <el-button class="fab" type="primary" circle :icon="Plus" @click="create" />
    <ExpenseQuickSheet v-model="sheetOpen" :record="editing" @saved="onSaved" />
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { Plus } from '@element-plus/icons-vue'
import { dailyExpensesApi } from '@/api/daily_expenses'
import type { DailyExpense, ExpenseMonthly } from '@/types'
import ExpenseQuickSheet from './ExpenseQuickSheet.vue'

const list = ref<DailyExpense[]>([])
const month = ref<ExpenseMonthly | null>(null)
const page = ref(1)
const loading = ref(false)
const sheetOpen = ref(false)
const editing = ref<DailyExpense | null>(null)

function fmt(v: number) {
  return new Intl.NumberFormat('zh-CN', { style: 'currency', currency: 'CNY' }).format(v ?? 0)
}

async function loadData(reset = false) {
  if (reset) page.value = 1
  loading.value = true
  const now = new Date()
  const [res, m] = await Promise.all([
    dailyExpensesApi.list({ page: page.value, page_size: 20 }),
    dailyExpensesApi.monthly(now.getFullYear(), now.getMonth() + 1),
  ])
  list.value = reset ? res.list : [...list.value, ...res.list]
  month.value = m
  loading.value = false
}

function loadMore() { page.value++; loadData() }
function create() { editing.value = null; sheetOpen.value = true }
function edit(e: DailyExpense) { editing.value = e; sheetOpen.value = true }
function onSaved() { sheetOpen.value = false; loadData(true) }

onMounted(() => loadData(true))
</script>

<style scoped>
.expenses-mobile { padding-bottom: 80px; }
.summary-row { display: flex; gap: 16px; margin: 12px 0; color: var(--mf-text-muted); font-size: 13px; }
.expense-card { background: var(--mf-surface); border: 1px solid var(--mf-border); border-radius: 12px; padding: 14px; margin-bottom: 10px; cursor: pointer; }
.expense-card .top { display: flex; justify-content: space-between; font-size: 16px; }
.expense-card .bottom { display: flex; justify-content: space-between; color: var(--mf-text-muted); font-size: 12px; margin-top: 6px; }
.income { color: #34d399; } .expense { color: #f87171; }
.fab { position: fixed; right: 20px; bottom: 80px; width: 56px; height: 56px; box-shadow: var(--mf-shadow-glow); }
</style>
```

- [ ] **Step 2: 提交**

```bash
git add frontend/src/views-mobile/DailyExpensesMobile.vue
git commit -m "feat(mobile): daily expenses card list with FAB"
```

---

### Task 15: ExpenseQuickSheet（核心 < 3s 快记账抽屉）

**Files:**
- Create: `frontend/src/views-mobile/ExpenseQuickSheet.vue`

- [ ] **Step 1: 写 ExpenseQuickSheet.vue**

`el-drawer direction="btt"` 底部半屏；金额优先（自动唤起数字键盘 `inputmode="decimal"`）；分类/资产用 `el-select filterable`；保存走 `offlineApi` 实现离线落本地。

```vue
<template>
  <el-drawer v-model="visible" direction="btt" size="75%" :with-header="false" class="quick-sheet">
    <div class="sheet-body">
      <div class="type-switch">
        <el-radio-group v-model="form.expense_type">
          <el-radio-button value="expense">支出</el-radio-button>
          <el-radio-button value="income">收入</el-radio-button>
        </el-radio-group>
      </div>

      <div class="amount-input">
        <span class="currency">¥</span>
        <input
          ref="amountRef"
          v-model="form.amount"
          type="text"
          inputmode="decimal"
          placeholder="0.00"
          class="amount-field"
        />
      </div>

      <el-form label-position="top">
        <el-form-item label="分类">
          <el-select v-model="form.category_id" filterable placeholder="选择分类" style="width:100%">
            <el-option v-for="c in categories" :key="c.id" :label="c.name" :value="Number(c.id)" />
          </el-select>
        </el-form-item>
        <el-form-item label="资产">
          <el-select v-model="form.asset_id" filterable placeholder="选择资产" style="width:100%">
            <el-option v-for="a in assets" :key="a.id" :label="a.name" :value="Number(a.id)" />
          </el-select>
        </el-form-item>
        <el-form-item label="日期">
          <el-date-picker v-model="form.expense_date" type="date" value-format="YYYY-MM-DD" style="width:100%" />
        </el-form-item>
        <el-form-item label="备注">
          <el-input v-model="form.note" placeholder="可选" />
        </el-form-item>
      </el-form>

      <el-button type="primary" size="large" :loading="saving" @click="save" block>保存</el-button>
    </div>
  </el-drawer>
</template>

<script setup lang="ts">
import { ref, reactive, computed, onMounted, nextTick } from 'vue'
import { ElMessage } from 'element-plus'
import { useCategoryStore } from '@/stores/category'
import { assetsApi } from '@/api/assets'
import { offlineApi } from '@/utils/offline-http'
import { dailyExpensesApi } from '@/api/daily_expenses'
import type { DailyExpense, Category, Asset } from '@/types'

const props = defineProps<{ record?: DailyExpense | null }>()
const emit = defineEmits<{ (e: 'saved'): void; (e: 'update:modelValue', v: boolean): void }>()

const visible = computed({
  get: () => (props as any).modelValue ?? false,
  set: (v: boolean) => emit('update:modelValue', v),
})
const amountRef = ref<HTMLInputElement | null>(null)
const saving = ref(false)
const categories = ref<Category[]>([])
const assets = ref<Asset[]>([])
const categoryStore = useCategoryStore()

const form = reactive({
  expense_type: 'expense' as 'income' | 'expense',
  category_id: null as number | null,
  asset_id: null as number | null,
  amount: '' as string | number,
  expense_date: new Date().toISOString().slice(0, 10),
  note: '',
})

onMounted(async () => {
  await categoryStore.loadCategories()
  categories.value = categoryStore.incomeExpenseCategories
  const res = await assetsApi.list({ page_size: 500 })
  assets.value = res.list
  if (props.record) {
    form.expense_type = props.record.expense_type
    form.category_id = Number(props.record.category_id)
    form.asset_id = Number(props.record.asset_id)
    form.amount = props.record.amount
    form.expense_date = props.record.expense_date
    form.note = props.record.note ?? ''
  }
  await nextTick()
  amountRef.value?.focus()
})

async function save() {
  if (!form.amount || Number(form.amount) <= 0) return ElMessage.warning('请输入金额')
  if (!form.category_id || !form.asset_id) return ElMessage.warning('请选择分类和资产')
  saving.value = true
  const payload = {
    expense_type: form.expense_type,
    category_id: form.category_id,
    asset_id: form.asset_id,
    amount: Number(form.amount),
    currency: 'CNY',
    expense_date: form.expense_date,
    note: form.note,
  }
  try {
    if (props.record) await offlineApi.put(`/daily-expenses/${props.record.id}`, payload)
    else await offlineApi.post('/daily-expenses', payload)
    ElMessage.success(`已记录 ¥${form.amount}`)
    visible.value = false
    emit('saved')
  } finally {
    saving.value = false
  }
}
</script>

<style scoped>
.sheet-body { padding: 16px; }
.type-switch { display: flex; justify-content: center; margin-bottom: 16px; }
.amount-input { display: flex; align-items: center; gap: 8px; margin-bottom: 20px; }
.currency { font-size: 24px; color: var(--mf-text-muted); }
.amount-field { flex: 1; background: transparent; border: none; border-bottom: 2px solid var(--mf-border); color: var(--mf-text-main); font-size: 36px; font-family: 'JetBrains Mono', monospace; outline: none; text-align: right; }
.amount-field:focus { border-color: var(--mf-primary); }
</style>
```

- [ ] **Step 2: 提交**

```bash
git add frontend/src/views-mobile/ExpenseQuickSheet.vue
git commit -m "feat(mobile): quick expense bottom-sheet (core <3s flow)"
```

---

### Task 16: TransactionsMobile / AssetsMobile / ReportsMobile / SettingsMobile

**Files:**
- Create: `frontend/src/views-mobile/TransactionsMobile.vue`
- Create: `frontend/src/views-mobile/AssetsMobile.vue`
- Create: `frontend/src/views-mobile/ReportsMobile.vue`
- Create: `frontend/src/views-mobile/SettingsMobile.vue`

- [ ] **Step 1: 写 TransactionsMobile.vue**

结构同 DailyExpensesMobile，复用 `transactionsApi`，卡片展示交易类型/金额/资产。删除走 `offlineApi.delete`。

```vue
<template>
  <div class="tx-mobile">
    <div class="page-header"><h2>交易</h2></div>
    <div v-for="t in list" :key="t.id" class="tx-card">
      <div class="top"><span>{{ t.category_name || t.transaction_type }}</span><span :class="isIncome(t) ? 'income' : 'expense'">{{ isIncome(t) ? '+' : '-' }}{{ fmt(t.amount) }}</span></div>
      <div class="bottom"><span>{{ t.transaction_date }}</span><span>{{ t.asset_name }}</span></div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { transactionsApi } from '@/api/transactions'
import type { Transaction } from '@/types'

const list = ref<Transaction[]>([])
function fmt(v: number) { return new Intl.NumberFormat('zh-CN', { style: 'currency', currency: 'CNY' }).format(v ?? 0) }
function isIncome(t: Transaction) { return ['deposit', 'transfer_in', 'income', 'interest', 'sell'].includes(t.transaction_type) }

onMounted(async () => {
  const res = await transactionsApi.list({ page_size: 50 })
  list.value = res.list
})
</script>

<style scoped>
.tx-card { background: var(--mf-surface); border: 1px solid var(--mf-border); border-radius: 12px; padding: 14px; margin-bottom: 10px; }
.tx-card .top { display: flex; justify-content: space-between; font-size: 16px; }
.tx-card .bottom { display: flex; justify-content: space-between; color: var(--mf-text-muted); font-size: 12px; margin-top: 6px; }
.income { color: #34d399; } .expense { color: #f87171; }
</style>
```

- [ ] **Step 2: 写 AssetsMobile.vue**

```vue
<template>
  <div class="assets-mobile">
    <div class="page-header"><h2>资产</h2></div>
    <div v-for="a in list" :key="a.id" class="asset-card">
      <span class="name">{{ a.name }}</span>
      <span class="value">{{ fmt(a.current_value) }}</span>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { assetsApi } from '@/api/assets'
import type { Asset } from '@/types'

const list = ref<Asset[]>([])
function fmt(v: number) { return new Intl.NumberFormat('zh-CN', { style: 'currency', currency: 'CNY' }).format(v ?? 0) }
onMounted(async () => {
  const res = await assetsApi.list({ page_size: 500 })
  list.value = res.list
})
</script>

<style scoped>
.asset-card { display: flex; justify-content: space-between; background: var(--mf-surface); border: 1px solid var(--mf-border); border-radius: 12px; padding: 14px; margin-bottom: 10px; }
.asset-card .value { font-family: 'JetBrains Mono', monospace; }
</style>
```

- [ ] **Step 3: 写 ReportsMobile.vue（复用现有 ECharts 组件）**

> 🟡 **fit-analysis 2026-08-15 两处适配**：
> (1) `ExpenseCategoryPie.vue` 的图表根 div 当前为 `height:100%`（其余图表固定 px）。复用**必须**给它一个有确定高度的父容器（如下 `.chart-block` 内 `.pie-wrap{height:260px}`），否则 `clientHeight=0` 会让 ECharts `ensureChart` 直接跳过、图不渲染。
> (2) 本 Tab 仅用 `dailyExpensesApi.monthly` 够用，但若想要更丰富报表（资产趋势/分布），`api/reports.ts` 已提供 `reportsApi.assetTrend/assetBreakdown/expenseTrend/expenseCategory` 等，无需新增后端。

```vue
<template>
  <div class="reports-mobile">
    <div class="page-header"><h2>报表</h2></div>
    <el-date-picker v-model="month" type="month" value-format="YYYY-MM" @change="load" />
    <div class="chart-block"><h4>分类占比</h4><div class="pie-wrap"><ExpenseCategoryPie :data="monthly?.by_category ?? []" /></div></div>
    <div class="chart-block"><h4>月度收支</h4><MonthlyChart :data="monthly" /></div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { dailyExpensesApi } from '@/api/daily_expenses'
import ExpenseCategoryPie from '@/components/ExpenseCategoryPie.vue'
import MonthlyChart from '@/components/MonthlyChart.vue'
import type { ExpenseMonthly } from '@/types'

const month = ref(new Date().toISOString().slice(0, 7))
const monthly = ref<ExpenseMonthly | null>(null)

async function load() {
  const [y, m] = month.value.split('-').map(Number)
  monthly.value = await dailyExpensesApi.monthly(y, m)
}
onMounted(load)
</script>

<style scoped>
.chart-block { background: var(--mf-surface); border: 1px solid var(--mf-border); border-radius: 12px; padding: 16px; margin: 12px 0; }
.chart-block h4 { margin: 0 0 12px; color: var(--mf-text-muted); }
.pie-wrap { height: 260px; } /* 🟡 ExpenseCategoryPie 图表是 height:100%，父容器必须有确定高度 */
</style>
```

- [ ] **Step 4: 写 SettingsMobile.vue（分类管理 / 改密 / 导出 / 同步状态）**

```vue
<template>
  <div class="settings-mobile">
    <div class="page-header"><h2>我的</h2></div>
    <div class="sync-status">
      <span>待同步：{{ pending }}</span>
      <span>上次同步：{{ lastSync || '从未' }}</span>
      <el-button size="small" :loading="syncing" @click="syncNow">立即同步</el-button>
    </div>
    <el-button @click="exportCsv" block>导出 CSV</el-button>
    <el-button @click="goCategories" block>分类管理</el-button>
    <el-button @click="logout" block>退出登录</el-button>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useRouter } from 'vue-router'
import { useSyncStore } from '@/stores/sync'
import { useAuthStore } from '@/stores/auth'
import { dailyExpensesApi } from '@/api/daily_expenses'
import http from '@/utils/http'

const router = useRouter()
const sync = useSyncStore()
const auth = useAuthStore()
const pending = computed(() => sync.pendingCount)
const lastSync = computed(() => sync.lastSyncAt)
const syncing = computed(() => sync.syncing)

function syncNow() { sync.syncNow() }
function goCategories() { router.push('/m/settings') }
async function exportCsv() {
  const blob = (await http.get('/export/daily-expenses', { responseType: 'blob' })) as unknown as Blob
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = 'daily_expenses.csv'
  a.click()
  URL.revokeObjectURL(url)
}
function logout() { auth.logout(); router.replace('/m/login') }
</script>

<style scoped>
.sync-status { display: flex; flex-direction: column; gap: 8px; background: var(--mf-surface); border: 1px solid var(--mf-border); border-radius: 12px; padding: 16px; margin-bottom: 12px; }
.settings-mobile > * { margin-bottom: 12px; }
</style>
```

- [ ] **Step 5: 提交**

```bash
git add frontend/src/views-mobile/TransactionsMobile.vue frontend/src/views-mobile/AssetsMobile.vue frontend/src/views-mobile/ReportsMobile.vue frontend/src/views-mobile/SettingsMobile.vue
git commit -m "feat(mobile): transactions/assets/reports/settings mobile views"
```

---

## Chunk 6: 测试与验证

### Task 17: 本地 DB 层单测（Vitest）

**Files:**
- Create: `frontend/tests/db.local.spec.ts`
- Modify: `frontend/vite.config.mobile.ts` 追加 test 配置（或新建 `vitest.config.ts`）

- [ ] **Step 1: 新增 vitest 配置**

在 `frontend/vite.config.mobile.ts` 末尾 `export default` 内追加 `test` 字段（Vitest 复用 Vite 配置）：

```typescript
import { defineConfig } from 'vitest/config'
// 注意：若使用 vitest/config 的 defineConfig 可自带 test 类型
test: {
  environment: 'jsdom',
  globals: true,
  include: ['tests/**/*.spec.ts'],
},
```
> 实际提交时把文件顶部 `defineConfig` 改为从 `vitest/config` 导入，以拿到 `test` 类型。

- [ ] **Step 2: 写 db.local.spec.ts**

sql.js 在 jsdom 下可用（WASM）。测试建表、插入、软删、查询过滤。

```typescript
import { describe, it, expect, beforeEach } from 'vitest'
import { initLocalDb, getDb, run, query, rowsFrom, resetLocalDb, persist } from '@/db/local'

beforeEach(() => {
  resetLocalDb()
})

describe('local db', () => {
  it('creates tables and inserts a daily_expense', async () => {
    await initLocalDb()
    run('INSERT INTO daily_expenses (id, user_id, category_id, asset_id, expense_type, amount, currency, expense_date) VALUES (?,?,?,?,?,?,?,?)',
      [1, 1, 10, 20, 'expense', 12.5, 'CNY', '2026-08-13'])
    const res = query('SELECT * FROM daily_expenses WHERE id = 1')
    const rows = rowsFrom(res)
    expect(rows.length).toBe(1)
    expect(rows[0].amount).toBe(12.5)
    expect(rows[0].__deleted).toBe(0)
  })

  it('soft-deletes without physical removal', async () => {
    await initLocalDb()
    run('INSERT INTO daily_expenses (id, user_id, category_id, asset_id, expense_type, amount, currency, expense_date) VALUES (?,?,?,?,?,?,?,?)',
      [2, 1, 10, 20, 'expense', 5, 'CNY', '2026-08-13'])
    run('UPDATE daily_expenses SET __deleted = 1, updated_at = NULL WHERE id = 2')
    const res = query('SELECT * FROM daily_expenses WHERE id = 2')
    expect(rowsFrom(res)[0].__deleted).toBe(1)
  })

  it('persists and reloads from localStorage', async () => {
    await initLocalDb()
    run('INSERT INTO sync_meta (key, value) VALUES (?,?)', ['last_sync_at', '2026-08-13T00:00:00Z'])
    persist()
    resetLocalDb()
    await initLocalDb()
    const res = query("SELECT value FROM sync_meta WHERE key = 'last_sync_at'")
    expect(rowsFrom(res)[0].value).toBe('2026-08-13T00:00:00Z')
  })
})
```

- [ ] **Step 3: 运行测试**

Run: `cd frontend && npx vitest run tests/db.local.spec.ts`
Expected: 3 passed

- [ ] **Step 4: 提交**

```bash
git add frontend/tests/db.local.spec.ts frontend/vite.config.mobile.ts
git commit -m "test(mobile): local db layer unit tests"
```

---

### Task 18: 同步 Store 单测

**Files:**
- Create: `frontend/tests/sync.store.spec.ts`

- [ ] **Step 1: 写 sync.store.spec.ts**

用 `mock` 替换 `api/*` 的 `create`，验证入队 / push 成功标记 / 冲突处理。

```typescript
import { describe, it, expect, beforeEach, vi } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import { initLocalDb, run, resetLocalDb } from '@/db/local'
import { useSyncStore } from '@/stores/sync'

vi.mock('@/api/daily_expenses', () => ({
  dailyExpensesApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn() },
}))
vi.mock('@/api/transactions', () => ({ transactionsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn() } }))
vi.mock('@/api/assets', () => ({ assetsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn() } }))

import { dailyExpensesApi } from '@/api/daily_expenses'

beforeEach(async () => {
  setActivePinia(createPinia())
  resetLocalDb()
  await initLocalDb()
})

describe('sync store', () => {
  it('enqueues a create op and pushes it on syncNow', async () => {
    const store = useSyncStore()
    store.init()
    store.enqueue('daily_expenses', 99, 'create', { amount: 10, expense_type: 'expense' })
    expect(store.pendingCount).toBe(1)
    await store.syncNow()
    expect(dailyExpensesApi.create).toHaveBeenCalled()
    expect(store.pendingCount).toBe(0)
  })

  it('marks conflict when server rejects', async () => {
    ;(dailyExpensesApi.create as any).mockRejectedValue({ response: { data: { code: 1003 } } })
    const store = useSyncStore()
    store.init()
    store.enqueue('daily_expenses', 100, 'create', { amount: 1 })
    await store.syncNow()
    expect(store.pendingCount).toBe(1) // 仍待处理但已标记
  })
})
```

- [ ] **Step 2: 运行测试**

Run: `cd frontend && npx vitest run tests/sync.store.spec.ts`
Expected: 2 passed

- [ ] **Step 3: 提交**

```bash
git add frontend/tests/sync.store.spec.ts
git commit -m "test(mobile): sync store queue/push/conflict tests"
```

---

### Task 19: 离线 HTTP 单测

**Files:**
- Create: `frontend/tests/offline-http.spec.ts`

- [ ] **Step 1: 写 offline-http.spec.ts**

mock `http` 抛网络错误 → 验证落本地 + 入队，返回离线信封。

```typescript
import { describe, it, expect, beforeEach, vi } from 'vitest'
import { initLocalDb, query, rowsFrom, resetLocalDb } from '@/db/local'
import { useSyncStore } from '@/stores/sync'
import { offlineApi } from '@/utils/offline-http'

vi.mock('@/utils/http', () => ({ default: vi.fn() }))
import http from '@/utils/http'

beforeEach(async () => {
  resetLocalDb()
  await initLocalDb()
})

describe('offline-http', () => {
  it('writes to local db and queues when http fails', async () => {
    ;(http as any).mockRejectedValue(new Error('Network Error'))
    const res = await offlineApi.post('/daily-expenses', { amount: 50, expense_type: 'expense', category_id: 1, asset_id: 1, currency: 'CNY', expense_date: '2026-08-13' })
    expect(res.offline).toBe(true)
    const rows = rowsFrom(query('SELECT * FROM daily_expenses WHERE amount = 50'))
    expect(rows.length).toBe(1)
    expect(rows[0].__deleted).toBe(0)
    const q = rowsFrom(query('SELECT * FROM sync_queue WHERE synced = 0'))
    expect(q.length).toBe(1)
  })

  it('soft-deletes locally on delete failure', async () => {
    ;(http as any).mockRejectedValue(new Error('Network Error'))
    await offlineApi.delete('/daily-expenses/7')
    const rows = rowsFrom(query('SELECT * FROM daily_expenses WHERE id = 7'))
    expect(rows[0].__deleted).toBe(1)
  })
})
```

- [ ] **Step 2: 运行测试**

Run: `cd frontend && npx vitest run tests/offline-http.spec.ts`
Expected: 2 passed

- [ ] **Step 3: 提交**

```bash
git add frontend/tests/offline-http.spec.ts
git commit -m "test(mobile): offline-http fallback to local queue"
```

---

### Task 20: 构建与后端集成验证

**Files:** 无新增；验证步骤

- [ ] **Step 1: 移动端构建零错误**

Run: `cd frontend && npm run build:mobile`
Expected: 输出到 `dist-mobile/`，`vue-tsc` 0 错误，无 TS 报错

- [ ] **Step 2: 桌面端构建仍通过**

Run: `cd frontend && npm run build`
Expected: 0 错误（确认未破坏桌面端）

- [ ] **Step 3: 运行全部移动端单测**

Run: `cd frontend && npx vitest run`
Expected: 7 passed（3+2+2）

- [ ] **Step 4: 后端集成测试不受影响（后端零改动）**

> ✅ 核对：套件输出形如 `结果: PASS=NN FAIL=0`。当前（2026-08-15）实际断言数约 **79+**（AGENTS.md 记录），非计划初稿估算的 21。判据以 `FAIL=0` 为主、PASS 数不低于改动前基线即可。

Run: `cd backend && cmake --build build --parallel && ./tests/test_link.sh`
Expected: `结果: PASS=.. FAIL=0`（FAIL 必须为 0；PASS 数与基线持平）

- [ ] **Step 5: 提交（收尾）**

```bash
git add -A
git commit -m "feat(mobile): complete offline-first mobile app with sync + tests"
```

---

## 验收对照（spec §10）

- [x] 无网络打开 App 可查看已同步数据（本地表读取；可视化验证）
- [x] 无网络记账 → Toast「已离线保存」
- [x] 恢复网络后离线记录 5 秒内自动同步（networkStatusChange 触发 syncNow）
- [x] 快记账 < 3 秒（ExpenseQuickSheet 金额优先 + 数字键盘）
- [x] 无网络删除 → 列表软隐藏（__deleted=1 过滤）
- [x] 删除后断网可恢复（sync_queue 逆向）
- [x] `build:mobile` 与 `build` 均 0 TS 错误
- [x] 后端集成测试 FAIL=0（后端未改动；断言数 ≥79，以基线持平为准）

