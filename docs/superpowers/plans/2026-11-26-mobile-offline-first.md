# 移动端离线优先增强实现计划 (Mobile Offline-First Enhancement Implementation Plan)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将移动端离线同步从“部分表注册、写入口分散”升级为以 `SyncEngine` 单例为唯一执行者的全量可写表离线镜像，支持 pull 阶段客户端并发冲突检测 + 手动确认、前后台切换自动推送，并保证账本隔离、ID 重映射与队列迁移零回归。

**Architecture:**
1. **唯一状态源**：本地 SQLite `sync_queue` 表（库整体加密持久化到单个 localStorage key）。`SyncEngine` 持有内存镜像，所有操作读写 DB 再 `persist()`；`useSyncStore` 退化为委托包装 + 事件驱动的缓存快照。
2. **冲突模型**：后端业务错误为 HTTP 200 + `code`（`respond_conflict` 为 `code:1004`，语义“资源已存在/操作冲突”，非并发版本冲突）。并发冲突主来源是 `pullRemote` 时客户端 `updated_at` 比较；`code:1004` 为辅助来源，响应无记录数据，`remote_payload` 仅 pull 阶段可填充。策略固定 manual。
3. **传输层**：`offline-http.ts` 统一注册 6 张表（`TABLE_BY_PATH` + `tableNameFromUrl()` 正则），写入口经 `offlineApi` 落库入队；`X-Ledger-Id` 按队列项 `ledger_id` 快照发送，不读取当前活跃账本。
4. **UI**：`SyncStatusBanner.vue` 与 `ConflictList.vue` 替换现有 inline UI；`main-mobile.ts` 已注册的 `registerNetworkListeners` 为唯一自动同步触发源，`MobileLayout.vue` 的重复监听移除。

**Tech Stack:** Vue 3, Pinia, TypeScript, sql.js (WASM SQLite), Vitest, Playwright.

**Spec:** `docs/superpowers/specs/2026-11-26-mobile-offline-first-design.md`（已按 spec-review 修订）。

## Global Constraints

- 保持现有 6 张业务表 `LOCAL_SCHEMA` / `TABLE_COLUMNS` 不变，仅对 `sync_queue` 增加一列并幂等迁移。
- 不引入 HTTP 409 状态码判断；冲突白名单仅 `code:1004` 与特定 4xx，其余业务码不标冲突。
- `remapAssetRefs` / `remapAccountRefs` 必须保持三参数签名（含 `afterQueueId`），只重映射当前 create 之后的队列项。
- 队列迁移不清空、不重建；`initLocalDb()` 恢复的现有 `sync_queue` 条目必须继续可推送。
- `pushLocal` 采用 continue-on-fail：单项失败不阻塞后续项，不回滚已成功项。
- `syncNow()` 由 `syncing` 标志防重入；前一次仍在进行时后续触发立即返回。
- 后端代码不在本计划范围内（无版本校验改造），冲突检测完全在客户端实现。

## Review Focus

1. 断网写入 → 切前台 → 自动推送 → pull 冲突 → 手动“保留本地/采用服务端”全链路。
2. `remote_payload` 重载后仍在；冲突两版差异可展示。
3. 入队后切换账本，队列项仍推送到原账本（`X-Ledger-Id` 为入队快照）。
4. 资产离线创建 → 服务端 ID → 依赖行与后续队列 payload 的 ID 重映射正确，历史队列不被误改。
5. 老用户升级（已有 `sync_queue` 数据 + 新列不存在）时迁移幂等、无数据丢失。

---

## Task 1: schema 与类型层改造（remote_payload 持久化）

**Files:**
- Modify: `frontend/src/db/schema.ts:134-144`（`sync_queue` 建表语句）
- Modify: `frontend/src/types/mobile.ts:2-12`（`SyncQueueItem`）
- Modify: `frontend/src/db/local.ts`（幂等 ALTER，紧随 `LOCAL_SCHEMA` 初始化之后）

**Details:**
1. `sync_queue` 建表语句新增 `remote_payload TEXT DEFAULT NULL`。
2. `SyncQueueItem` 新增 `remote_payload?: Record<string, unknown>`（读 DB 时 `JSON.parse`，写 DB 时 `JSON.stringify`；`null`/空串解析为 `undefined`）。
3. 在 `db/local.ts` 的 `initLocalDb()` 中，`LOCAL_SCHEMA` 执行后追加幂等迁移：
   ```ts
   try {
     db.run('ALTER TABLE sync_queue ADD COLUMN remote_payload TEXT')
   } catch (e) {
     // 老库已存在该列时 SQLite 报 duplicate column，忽略
     if (!/duplicate column/i.test(String((e as Error)?.message))) throw e
   }
   ```
   保证 `CREATE TABLE IF NOT EXISTS` 创建的新库与老库升级都具备该列。

- [ ] **Step 1: 修改 `schema.ts` 建表语句与 `types/mobile.ts` 类型**
- [ ] **Step 2: 在 `db/local.ts` 增加幂等 ALTER 迁移并验证异常分支只吞 duplicate column**
- [ ] **Step 3: 补充 `tests/db.local.spec.ts` 用例：老库（无新列）初始化后列存在且数据保留**

---

## Task 2: SyncEngine 核心实现

**Files:**
- Create: `frontend/src/sync/SyncEngine.ts`
- Modify: `frontend/src/stores/sync.ts`（导出 `remapAssetRefs`/`remapAccountRefs`，迁出逻辑后保留委托）

**Details:**
1. 单例类：私有构造 + `static getInstance()`；内部维护 `queue`/`syncing`/`lastSyncAt`/`pushErrors` 等镜像，初始化时 `loadQueue()`。
2. `registerTable(def: SyncTableDef)`：注册 6 张表（categories、assets、accounts、transactions、daily_expenses、tags），表定义仅含 `table` 与 `api`，不设 `writable`/`idColumn`，冲突策略固定 manual。
3. `registerRemapHandlers({ onAssetRemap, onAccountRemap })`：三参数签名 `(oldId, newId, afterQueueId)`，由 `sync.ts` 导出的现有函数注入。
4. `enqueue(table, recordId, operation, payload, ledgerId?)`：
   - **先校验表已注册**，未注册抛描述性错误，禁止写出未入队的孤立本地行；
   - 账本优先级 `ledgerId → payload.ledger_id → localStorage['minefolio_active_ledger_id']`，写入 `sync_queue.ledger_id`；
   - 本地 sqlite upsert（`TABLE_COLUMNS` 白名单）与 `persist()` 沿用 `writeLocal` 现有逻辑。
5. `pushLocal()`：按队列顺序逐条处理，每条**重读 DB 最新行**（避免 remap 改写后使用过期 payload）：
   - `config = item.ledger_id > 0 ? { headers: { 'X-Ledger-Id': String(item.ledger_id) } } : undefined`；
   - create 成功且服务端返回新 ID → 更新本地表 `id` 与后续队列 `record_id`，调用 remap handler；
   - 错误分类：鉴权 `code:1001` → `onAuthFail` 并抛出；冲突 `code:1004` 或 4xx（排除 401/408/400/422/429）→ `markConflict`；校验/权限类（`1002`/`1003`/`2001`/400/422）→ 删除队列项并收集到 `pushErrors`；429/5xx/超时 → 保持 `synced=0` 不标冲突，继续后续项；
   - 通过 `minefolio:sync-state-changed` 事件通知状态变更（含 `pushErrors`）。
6. `pullRemote()`：分页拉取 6 张表，逐条 `updated_at` 比较；远端更新且本地有 `synced=0` 队列项 → `markConflict` 并**填充 `remote_payload`**（本地行不覆盖）；远端删除且本地有队列项 → conflict + `remote_payload = undefined`（语义“服务端无此记录”）；无冲突项则 upsert 覆盖 / 物理删除。
7. `syncNow()`：`syncing` 防重入 → `pushLocal()` → `pullRemote(pushedInCycle)` → 写 `last_sync_at`；pull 失败有意静默吞掉；`finally` 复位 `syncing` 并派发事件。
8. `getConflicts()` / `resolveConflict(queueId, 'retry'|'discard')` / `reset()`：从 DB 读写，所有变更派发事件。
9. `sync.ts` 保留 `parseSyncTimestamp`、`setSyncOnAuthFail`、`useSyncStore` 导出；`markConflict` 等内部函数迁入引擎，store 方法改为委托。

- [ ] **Step 1: 新建 `SyncEngine.ts` 骨架、单例与注册 API**
- [ ] **Step 2: 实现 `enqueue`（校验顺序、账本快照、本地 upsert）**
- [ ] **Step 3: 实现 `pushLocal`（重读 DB、remap 三参数、错误码白名单、pushErrors）**
- [ ] **Step 4: 实现 `pullRemote`（分页、`updated_at` 比较、`remote_payload` 填充、删除清理）**
- [ ] **Step 5: 实现 `syncNow` / `getConflicts` / `resolveConflict` / `reset` 与事件派发**
- [ ] **Step 6: `sync.ts` 导出 remap 函数并执行委托改造，`npm run typecheck` 通过**

---

## Task 3: 传输层与 bootstrap 接线

**Files:**
- Modify: `frontend/src/utils/offline-http.ts:16-21,30-34,79,89,101`
- Modify: `frontend/src/main-mobile.ts:25-49`
- Modify: `frontend/src/stores/sync.ts`（新增 `subscribeSyncEngine()`）

**Details:**
1. `TABLE_BY_PATH` 新增 `'categories': 'categories'`、`'tags': 'tags'`；`tableNameFromUrl()` 正则扩展为 `(daily-expenses|transactions|assets|accounts|categories|tags)`，保持排除 `/import`、`/monthly` 等子动作。
2. `writeLocal()` 中三处 `useSyncStore().enqueue(...)` 改为 `SyncEngine.getInstance().enqueue(...)`；删去对 `useSyncStore` 的直接依赖（保留 auth/ElMessage）。
3. `useSyncStore.subscribeSyncEngine()`：监听 `minefolio:sync-state-changed`，从 DB 重载 `queue`/`syncing`/`lastSyncAt`/`pushErrors`；组件通过 store 读取。
4. `main-mobile.ts` bootstrap 顺序：`initLocalDb()` → `SyncEngine.getInstance()` 初始化 + `registerTable()` ×6 → `registerRemapHandlers({ onAssetRemap, onAccountRemap })` → `sync.init()` → `useSyncStore().subscribeSyncEngine()` → `registerNetworkListeners(() => sync.syncNow())`（**已存在，保持**）→ mount。注册异常由顶层 `.catch` 渲染错误屏。
5. 现状澄清：移动端除 daily-expenses（`ExpenseQuickSheet.vue`）外暂无其他表的写入 UI；本任务交付的是**传输层就绪 + 约束固化**。A/B 子项目新增写入口时必须统一调用 `offlineApi`，本任务通过单测与静态检查（grep 不得出现直接 `http` 的写调用）固化该约束。

- [ ] **Step 1: 扩展 `TABLE_BY_PATH` / 正则并覆盖 6 张表**
- [ ] **Step 2: `offline-http.ts` 改调 `SyncEngine.enqueue`**
- [ ] **Step 3: 实现 `subscribeSyncEngine()` 并接入 bootstrap 正确顺序**
- [ ] **Step 4: 运行 `npm test -- tests/offline-http.spec.ts tests/sync.store.spec.ts` 验证委托与迁移**

---

## Task 4: UI 组件替换与重复监听移除

**Files:**
- Create: `frontend/src/components/mobile/SyncStatusBanner.vue`
- Create: `frontend/src/components/mobile/ConflictList.vue`
- Modify: `frontend/src/views-mobile/MobileLayout.vue:6-21,61-84,226-243`
- Modify: `frontend/src/views-mobile/SettingsMobile.vue:108-130,196-205,432-`
- Modify: `frontend/src/stores/sync.ts`（`hasConflict` 不必要，保留现有 `conflicts`/`activePendingCount`/`pendingCount` 语义）

**Details:**
1. `SyncStatusBanner.vue`：纯 props（`isOnline`、`pendingCount`、`conflictCount`、`syncing`），按 `isOnline → pendingCount>0 → conflictCount>0` 渲染离线/待同步 N/同步冲突 N 三态，`@click` 透传父级。
2. `ConflictList.vue`：props `conflicts: SyncQueueItem[]`；每行 `table_name · operation #record_id`（create + 本地占位 ID 显示“占位 ID”）；两列展示 `payload` 与 `remote_payload` 差异；`remote_payload` 为空时分别显示“服务端无此记录”/“服务端版本不可读”；emit `retry(id)`/`discard(id)`。
3. `MobileLayout.vue`：删除 inline `net-status-pill` 与样式，改用组件；删除 `onMounted` 中 `online`/`offline` 监听和挂载时 `syncStore.syncNow()`；保留 `minefolio:ledger-changed` 处理。
4. `SettingsMobile.vue`：删除 inline `sync-conflicts-card` 与样式，改用 `ConflictList`；保留 `resolveConflictRetry`/`resolveConflictDiscard` 包装；在“立即同步”包装函数中读取 `pushErrors` 并逐条 `ElMessage.error` 后清空；新增 `minefolio:localdb-quota-exceeded` 监听展示 banner。
5. `sync.pendingCount`（含冲突）语义与 `SettingsMobile.vue:243` 的切换服务端警告保持不变，组件只使用 `activePendingCount`。

- [ ] **Step 1: 新建 `SyncStatusBanner.vue` 与 `ConflictList.vue`**
- [ ] **Step 2: `MobileLayout.vue` 替换 inline UI 并移除重复监听**
- [ ] **Step 3: `SettingsMobile.vue` 替换 inline UI、接入 pushErrors 与配额 banner**
- [ ] **Step 4: 运行 `npm test -- tests/frontend-mobile-layout.spec.ts` 与 `npm run build:mobile`**

---

## Task 5: 测试与端到端验证

**Files:**
- Create: `frontend/tests/sync.engine.spec.ts`
- Modify: `frontend/tests/offline-http.spec.ts`、`frontend/tests/sync.store.spec.ts`、`frontend/tests/db.local.spec.ts`
- Create: `frontend/e2e/mobile-offline-sync.spec.ts`

**Details:**
1. 单元测试（Vitest，沿用现有 mock API 模式）：
   - 单例、`enqueue` 未注册表抛错且不写本地行；
   - remap 三参数只影响之后队列项；
   - `remote_payload` 标记冲突 → 模拟重载 → 仍在；
   - 错误分类：`1004`/其他 4xx 标冲突，`1002`/`1003`/`2001`/400/422 删队列项进 `pushErrors`；
   - 账本隔离：入队后切换活跃账本，`pushLocal` 仍发送原 `ledger_id`；
   - `visibilitychange` 仅触发一次 `syncNow()`；
   - 迁移：老库已有 `sync_queue` 条目 → 初始化后完整恢复。
2. E2E（Playwright + 请求拦截）：断网写入 daily-expenses → 切前台自动推送 → 服务端修改触发 pull 冲突 → 手动“保留本地/采用服务端” → 重载后冲突状态与 `remote_payload` 仍在。
3. 回归命令：
   - `cd frontend && npm test`
   - `cd frontend && npm run typecheck`
   - `cd frontend && npm run build:mobile`
   - `cd backend && ./tests/test_link.sh`（仅回归服务端 API 契约，不作为离线队列验证）

- [ ] **Step 1: 编写 `sync.engine.spec.ts` 覆盖 Task 2/3 全部场景**
- [ ] **Step 2: 更新既有 spec 以适配委托架构与新增列**
- [ ] **Step 3: 编写并运行 Playwright E2E**
- [ ] **Step 4: 全量测试与移动端构建通过**

---

## Verification Checklist

- [ ] 6 张表 `TABLE_BY_PATH` / 正则 / `registerTable()` 三处一致
- [ ] `remote_payload` 列幂等迁移，老库数据保留
- [ ] 冲突仅由 `code:1004` 与 pull 时 `updated_at` 比较触发，其余业务码不标冲突
- [ ] `X-Ledger-Id` 为入队时账本快照，切换账本不影响待推送项
- [ ] remap 三参数，历史队列项不被误改
- [ ] `MobileLayout.vue` 无重复同步监听；`SettingsMobile.vue` 展示冲突两版差异与配额 banner
- [ ] `npm test` / `npm run typecheck` / `npm run build:mobile` 全绿
