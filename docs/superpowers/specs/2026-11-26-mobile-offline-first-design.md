# 移动端离线优先增强设计 (D)

**项目**：Minefolio 移动端功能完善  
**子项目**：D — 离线优先增强（全部可写业务表本地镜像 + 手动冲突确认 + 前后台切换自动推送）  
**日期**：2026-11-26  
**状态**：设计待批准 → 实施计划

---

## 1. 目标

将现有移动端离线同步能力从“仅 6 张核心表”扩展到“全部可写业务表”，并统一由 `SyncEngine` 驱动，实现：

1. **全部可写业务表本地镜像**：分类（categories）、资产（assets）、账户（accounts）、交易流水（transactions）、日常收支（daily_expenses）、标签（tags）全部可离线增删改。**共 6 张表**，与后端 API 模块一一对应。ledgers、members、skills、tasks、scheduler 等非移动端可写业务表，不在本次离线镜像范围内。
2. **前后台切换自动推送**：用户从后台切回前台，或网络从断开恢复时，自动触发离线队列推送。
3. **冲突用户手动确认**：推送时若服务端返回 409/业务冲突码，标记冲突项，在设置页列表逐条让用户选择“保留本地 / 采用服务端”。

---

## 2. 现状回顾

现有代码已实现以下基础：

| 文件 | 职责 |
|------|------|
| `frontend/src/db/schema.ts` | 本地 sqlite 6 张可写业务表建表语句 + `TABLE_COLUMNS` 字段白名单 |
| `frontend/src/db/local.ts` | sql.js WASM 封装、加密持久化、查询/写入 API |
| `frontend/src/stores/sync.ts` | `useSyncStore`：队列加载、入队、推送、拉取、冲突标记、ID 重映射 |
| `frontend/src/utils/offline-http.ts` | `offlineApi`：网络不可用时写操作自动落库入队 |
| `frontend/src/views-mobile/SettingsMobile.vue` | 已有 `syncConflicts` 卡片、“重试/放弃”按钮 |
| `frontend/src/views-mobile/MobileLayout.vue` | 顶部离线/待同步状态提示，进入主界面自动 `syncNow()` |

**不足**：
- `offline-http.ts` `TABLE_BY_PATH` 当前仅覆盖 4 张表（daily-expenses、transactions、assets、accounts），正则 `tableNameFromUrl()` 也硬编码了这 4 张；**categories、tags 尚未注册**，离线写入这两张表会走非业务表透传路径，无法落库入队。
- 推送触发依赖 `online` 事件与进入主界面，已有 `sync-network.ts` 的 `registerNetworkListeners`（涵盖 `visibilitychange` 前后台切换、`online` 网络恢复、Capacitor Network/App 状态变更），`main-mobile.ts:44` 已调用；**待补充的是**：`registerNetworkListeners` 的回调目前是 `() => sync.syncNow()`，需确认其内部对 `sync-network.ts` 的引用与 `useSyncStore` 实例一致，避免闭包捕获到过期 store 实例。
- 冲突处理逻辑分散，缺乏统一的“保留本地 / 采用服务端”确认流程。

---

## 3. 架构设计

### 3.1 SyncEngine（新增）

新建单例类 `frontend/src/sync/SyncEngine.ts`，以配置化方式管理全部可写业务表的同步元信息：

```ts
/** 单张可写业务表的同步元信息 */
interface SyncTableDef {
  table: string             // 本地 sqlite 表名（如 'assets'）
  api: SyncApi              // 对应的 API 模块，必须实现 create/update/delete
  idColumn: string          // 主键列，默认 'id'
  writable: boolean         // 是否支持离线增删改
  // 冲突策略固定为 'manual'（用户手动确认），D 子项目已确认，不预留 'auto'，避免 YAGNI。
}

/** API 模块必须实现的同步接口（供 SyncEngine 调用；已确认 6 个现有模块均实现） */
interface SyncApi {
  create(payload: Record<string, unknown>, config?: any): Promise<any>
  update(id: number, payload: Record<string, unknown>, config?: any): Promise<any>
  delete(id: number, config?: any): Promise<any>
  list(params?: any): Promise<any> // 用于 pullRemote 全量拉取（categories/assets/accounts/transactions/daily_expenses/tags 均已实现）
}

/** ID 重映射回调（供 sync.ts 注册给 SyncEngine，在 pushLocal 流程中调用） */
interface RemapHandlers {
  onAssetRemap(localId: number, remoteId: number): void   // 更新本地依赖行与后续队列项中的 asset_id
  onAccountRemap(localId: number, remoteId: number): void  // 更新本地依赖行与后续队列项中的 account_id
}

/** SyncEngine 的完整公开 API */
interface SyncEngineApi {
  registerTable(def: SyncTableDef): void
  registerRemapHandlers(handlers: RemapHandlers): void   // 由 sync.ts 在启动时调用，注册 remapAssetRefs/remapAccountRefs
  enqueue(tableName, recordId, operation, payload, ledgerId?): void
  pushLocal(): Promise<Map<string, Set<number>>>
  pullRemote(): Promise<void>
  syncNow(): Promise<void>
  getConflicts(): SyncQueueItem[]
  resolveConflict(queueId, action: 'retry' | 'discard'): Promise<void>
  reset(): void
}
```

**可写业务表全量清单**（与后端 API 模块一一对应，`frontend/src/api/*.ts`）：

| 表名 | API 模块 | 说明 |
|------|----------|------|
| categories | categoriesApi | 分类（支出/资产/负债） |
| assets | assetsApi | 资产（含账户关联） |
| accounts | accountsApi | 账户容器 |
| transactions | transactionsApi | 交易流水 |
| daily_expenses | dailyExpensesApi | 日常收支 |
| tags | tagsApi | 标签 |

**职责**：
- 注册全部可写业务表（现有 6 张，按上表注册）
- 统一处理本地写入入队、推送队列、拉取远端、冲突标记与 ID 重映射
- 暴露 `enqueue()`、`pushLocal()`、`pullRemote()`、`syncNow()`、`getConflicts()`、`resolveConflict()`

**与 `useSyncStore` 的状态契约（SyncEngine 为唯一执行者，store 为委托包装）**：
- `SyncEngine` 是**唯一执行者与 source of truth**：内部持有队列、同步中标志、上次同步时间、冲突列表等运行时状态；所有入队（`enqueue`）、推送（`pushLocal`）、拉取（`pullRemote`）、双向同步（`syncNow`）、冲突确认（`resolveConflict`）均由 `SyncEngine` 执行并直接操作内部状态。
- `useSyncStore`（`frontend/src/stores/sync.ts`）是**委托包装（thin delegation）**：其公开方法 `enqueue`/`syncNow`/`resolveConflictRetry`/`resolveConflictDiscard` 等内部只负责转发调用给 `SyncEngine.getInstance()` 的对应方法，**不维护独立队列状态**，不直接写队列。store 的 Pinia ref `queue`/`syncing`/`lastSyncAt`/`conflicts` 作为 UI 消费端的**缓存快照**，通过订阅 `minefolio:sync-state-changed`（在 `window` 上派发）定期与 SyncEngine 内部状态对齐。
- **订阅时机**：`main-mobile.ts` `bootstrap()` 完成 `SyncEngine` 初始化（`registerTable()` + `registerRemapHandlers()`）后，**再**执行 `useSyncStore.subscribeSyncEngine()`，避免首次挂载前漏掉状态变更。
- `offline-http.ts` 改为调用 `SyncEngine.getInstance().enqueue()` 做本地落库与入队（不再调用 `useSyncStore().enqueue()`）。
- **依赖方向**：`SyncEngine.ts` 仅向下依赖 `@/db/local`（run/query/rowsFrom/persist），不 import `sync.ts`；`sync.ts` 向上调用 `SyncEngine.registerRemapHandlers()` 注入回调（依赖注入，非循环依赖）。模块边界清晰，可独立对 SyncEngine 做单元测试（mock db 层即可）。

**启动注册位置**：`registerTable()` 在 `frontend/src/main-mobile.ts` 的 `bootstrap()` 阶段调用一次，在 `initLocalDb()` 之后、`syncStore.init()` 之前完成全部 6 张表的注册；紧接着调用 `registerRemapHandlers({ onAssetRemap, onAccountRemap })`，将 `sync.ts` 中已有的私有函数 `remapAssetRefs`/`remapAccountRefs` 注册给 SyncEngine。注册失败（如缺少对应 API 模块）抛出，被 `bootstrap()` 顶层的 `.catch` 捕获并渲染可见错误屏幕，避免离线写入静默失败。

**单例获取**：`SyncEngine` 采用单例模式，构造函数私有化，通过 `SyncEngine.getInstance()` 获取唯一实例。`offline-http.ts` 的桥接逻辑、UI 组件（通过 `syncStore` 委托）均通过 `getInstance()` 获取引用，保证整个应用生命周期内只有一个队列与同步状态源头。

**现有队列迁移路径（不丢数据）**：`offline-http.ts` 当前已通过 `useSyncStore().enqueue()` 将离线变更写入本地 `sync_queue` 与 `localStorage`。迁移到 SyncEngine 后，**不清空、不重建现有队列**；`SyncEngine` 构造时从 `localStorage` 读取现有 `sync_queue` 条目并恢复到内存队列，与 `useSyncStore` 的现有 `queue` 状态在 bootstrap 阶段一次性对齐（以 `SyncEngine` 内部状态为准，`useSyncStore` 随后通过订阅事件对齐）。已入队但尚未推送的离线变更在迁移后继续由 `SyncEngine.pushLocal()` 推送，不丢失、不重复。

**构造失败模式**：`SyncEngine` 构造时从 `localStorage` 读取现有 `sync_queue` 条目恢复队列。若 `localStorage` 不可读、内容损坏或已超配额，构造**不抛出异常**，而是启动一个空队列，并通过 `minefolio:sync-state-changed` 事件告知 `useSyncStore`（`conflicts` 为空、`queue` 为空）；后续离线写入若因存储超额失败，按 Section 5 的 `minefolio:localdb-quota-exceeded` 事件处理（SettingsMobile 展示本地离线库提醒 banner），不丢已成功写入的条目、不阻塞应用启动。

### 3.2 本地镜像表扩展

**本次 D 子项目不新增本地建表语句。** 当前 `frontend/src/db/schema.ts` 的 `LOCAL_SCHEMA` 已覆盖全部 6 张可写业务表（categories、assets、accounts、transactions、daily_expenses、tags）及 `sync_queue`、`sync_meta`，`TABLE_COLUMNS` 也已对齐各字段白名单。阶段二仅做**契约对齐验证**（见下方 checklist），不新增建表语句，不改动 `schema.ts` 除非验证发现缺字段/类型不一致。

对应后端端点（供 pullRemote 拉取与 pushLocal 推送使用）：

| 表名 | 后端 REST 路径前缀 | 前端 API 模块 | 离线写入路径 |
|------|------------------|--------------|-------------|
| categories | `/api/categories` | categoriesApi | `offline-http.ts` TABLE_BY_PATH **新增** `categories` → `categories`，并同步更新 `tableNameFromUrl()` 正则（现仅匹配 daily-expenses/transactions/assets/accounts） |
| assets | `/api/assets` | assetsApi | `offline-http.ts` TABLE_BY_PATH 已有 `assets` |
| accounts | `/api/accounts` | accountsApi | `offline-http.ts` TABLE_BY_PATH 已有 `accounts` |
| transactions | `/api/transactions` | transactionsApi | `offline-http.ts` TABLE_BY_PATH 已有 `transactions` |
| daily_expenses | `/api/daily-expenses` | dailyExpensesApi | `offline-http.ts` TABLE_BY_PATH 已有 `daily-expenses` |
| tags | `/api/tags` | tagsApi | `offline-http.ts` TABLE_BY_PATH **新增** `tags` → `tags`，并同步更新 `tableNameFromUrl()` 正则 |

**排除说明**：`account_permissions` 与 `expense_tags` 虽在 `LOCAL_SCHEMA`/`TABLE_COLUMNS` 中带 `__deleted`/`updated_at` 同步列，但 `account_permissions` 为 RBAC 服务端管控表（不支持移动端离线写入），`expense_tags` 为多对多关联表（随 daily_expenses 一并原子写入，不独立离线），二者均**不列入**可写离线表清单，不在 `TABLE_BY_PATH` 中注册。`expense_tags` 的原子写入由 `dailyExpensesApi.create()` 在服务端一次完成，移动端离线创建 daily_expenses 时，连同其 tags 一并放入 `payload.tags` 字段，push 时由服务端原子处理，无需 `expense_tags` 独立入队。

**阶段二 schema/API 契约对齐验证方法**（对应 Section 7 阶段二；在此阶段对每张可写表，执行以下四项检查并记录结果）：
1. 字段白名单 `TABLE_COLUMNS[table]` 包含后端 API `create/update` 请求体中所有可写字段（通过阅读 `frontend/src/api/<table>.ts` 的 `create`/`update` 参数确认）；
2. 本地建表语句 `LOCAL_SCHEMA` 中对应列的类型与后端 SQLite 迁移 `backend/sql/migrations/sqlite/*.sql` 中同名表列类型一致；
3. `TABLE_BY_PATH` 与 `tableNameFromUrl()` 正则已覆盖该表的 REST 路径前缀（`/api/<table>` 及 `/api/<table>/<id>`）。
4. 后端已确认对该表返回 409 业务冲突（当服务端与客户端对同一记录有并发更新时），以支持 ConflictList 手动确认流程；如后端未实现 409，阶段三可先按“服务端胜出（Pull 覆盖本地）”简化处理，待后端补 409 后再切回手动确认。
四项全部通过方可进入 **Section 7 阶段三（实施顺序阶段三）**；若某项不通过，由 frontend 负责人在 **Section 7 阶段二（schema/API 对齐）** 修复缺字段/缺注册，不进入下一阶段，避免离线写入路径静默失败。

### 3.3 组件清单

| 组件 | 路径 | 职责 |
|------|------|------|
| SyncEngine | `frontend/src/sync/SyncEngine.ts` | 离线同步引擎核心（单例） |
| SyncStatusBanner | `frontend/src/components/mobile/SyncStatusBanner.vue` | **替换** `MobileLayout.vue` 顶部现有的 inline `net-status-pill`（离线/待同步/冲突提示） |
| ConflictList | `frontend/src/components/mobile/ConflictList.vue` | **替换** `SettingsMobile.vue` 现有的 inline `sync-conflicts-card`（冲突列表 + 重试/放弃按钮） |

**SyncStatusBanner 组件契约（纯 props 模式）**：
- Props：`pendingCount: number`，`syncing: boolean`，`hasConflict: boolean`
- Events：`click` → 由父级（`MobileLayout.vue`）绑定 `syncStore.syncNow()`
- 不直接订阅 `minefolio:sync-state-changed`，也不直接读 `useSyncStore()`；父级负责从 store 读取并传入 props，避免数据源重复。

**ConflictList 组件契约**：
- Props：`conflicts: SyncQueueItem[]`（包含新增可选字段 `remote_payload`）
- Events：`retry(id: number)` → 父级调用 `sync.resolveConflictRetry(id)`；`discard(id: number)` → 父级调用 `sync.resolveConflictDiscard(id)`
- 每条行展示 `table_name · operation #record_id`；若为 create 操作且 `record_id` 为本地占位 ID，显示“占位 ID”字样。左右两列分别展示本地 `payload` 与远端 `remote_payload` 的差异字段；若 `remote_payload` 为空（离线 create，服务端无此记录），右列显示“服务端无此记录”。

**UI 替换策略**：两个新组件与现有 inline UI 互斥替换（不并存），替换后 `MobileLayout.vue` 和 `SettingsMobile.vue` 移除对应的 inline 模板与样式，改为引用新组件，行为保持不变。

**MobileLayout.vue 集成说明**：
- 删除 header 顶部现有的 inline `net-status-pill`（离线模式/待同步/同步冲突三种状态的 `<span>` 块，含 `@click="syncStore.syncNow()"`），以及对 `syncStore.activePendingCount` / `syncStore.conflicts.length` 的模板内直接读取。
- 改为引用 `<SyncStatusBanner :pending-count="syncStore.pendingCount" :syncing="syncStore.syncing" :has-conflict="syncStore.hasConflict" @click="syncStore.syncNow()" />`。
- `syncStore` 新增 `pendingCount`（待推送条数，现有 `activePendingCount` 重命名或新增计算属性）、`syncing`（同步中标志，现有 `syncing`）、`hasConflict`（冲突条数是否 > 0，新增计算属性）。

**SettingsMobile.vue 集成说明**：
- 删除现有的 inline `sync-conflicts-card`（冲突列表 + 重试/放弃按钮）及其模板、样式。
- 改为引用 `<ConflictList :conflicts="syncStore.conflicts" @retry="resolveConflictRetry" @discard="resolveConflictDiscard" />`，事件处理函数复用现有 `resolveConflictRetry(queueId)` / `resolveConflictDiscard(queueId)`。
- `resolvePasswordFailure`、`retrySyncFailure` 等现有方法保持不变，作为 `ConflictList` 事件的包装层。

### 3.4 数据流

#### 离线写入（Push）

```
用户在移动端编辑/新增/删除可写业务记录
    ↓
offline-http.offlineRequest(method, url, data) 尝试走正常 API
    ↓
网络可用 → 正常返回，SyncEngine 标记 synced=1
网络不可用（无 err.response）→ 由 offline-http 调用 SyncEngine.enqueue(tableName, recordId, operation, payload, ledgerId?)
    ↓
本地 sqlite upsert，__deleted=0/1，加密持久化到 localStorage
    ↓
前后台切换（visibilitychange）或网络恢复（online）→ SyncEngine.syncNow()
    ↓
pushLocal：按队列顺序逐条推送到服务端
    ↓
服务端返回新 ID（如资产离线创建）→ **remapAssetRefs(localId, remoteId)** / **remapAccountRefs(localId, remoteId)** 被 SyncEngine 调用（通过 registerRemapHandlers 注册的回调），同步更新：
  1. 本地依赖行：`assets.account_id`（若该记录是账户）、`daily_expenses.asset_id`、`transactions.asset_id` 等外键列，将本地占位 ID 替换为服务端真实 ID；
  2. 排在当前记录之后的所有待推送队列项：若其 `payload` 中含同名外键字段且值等于本地占位 ID，替换为服务端真实 ID；
  remap 失败（如本地依赖行不存在）不阻塞推送，仅记录警告，下次推送时该依赖项仍以占位 ID 发送，由服务端校验。
    ↓
标记 synced=1；服务端返回 409/业务冲突码 → markConflict（填充 remote_payload），交给 ConflictList 手动确认
```

**offline-http → SyncEngine 桥接逻辑**（Section 3.1 已说明 `offline-http.ts` 改为调用 `SyncEngine.enqueue()`，下文具体化）：

1. **表→API 路由**：`offline-http.ts` 的 `TABLE_BY_PATH` 新增 `categories`、`tags`，并同步更新 `tableNameFromUrl()` 正则，使 `tableNameFromUrl('/api/categories')` 返回 `'categories'`、`tableNameFromUrl('/api/tags')` 返回 `'tags'`。与 `SyncEngine.registerTable()` 注册的 6 张表表名一一对应（`SyncTableDef.table` 与 `TABLE_BY_PATH` 值相同）。
2. **recordId 决定**：URL 路径若含数字 ID（如 `/assets/123`），`extractId(url)` 取出作为 `recordId`；否则（POST 创建）由 `nextLocalId(table)` 生成本地占位 ID，后续推送成功后通过 `remapAssetRefs`/`remapAccountRefs` 替换为服务端真实 ID。
3. **ledgerId 解析**：`offline-http` 从请求体 `data.ledger_id` 优先读取；若缺失，读取 `localStorage.getItem('minefolio_active_ledger_id')`；均不存在则不传 `ledgerId`，由 `SyncEngine.enqueue` 内部回退到同 localStorage 键。`pushLocal` 时通过 `registerAuthHeadersHook` 注册的钩子将 `X-Ledger-Id` 作为请求头随队列项一并发送，供服务端做账本维度权限隔离（避免在 `offline-http.ts` 中硬编码头名）。
4. **与 SyncEngine 的连接**：`offline-http.ts` 不再直接调用 `useSyncStore().enqueue()`，改为 `SyncEngine.getInstance().enqueue(...)`（SyncEngine 单例，在 `main-mobile.ts` bootstrap 阶段已完成 `registerTable()` + `registerRemapHandlers()`）。
5. **auth 头钩子集成**：`main-mobile.ts` bootstrap 阶段在 `registerTable()` + `registerRemapHandlers()` 之后，由 `offline-http.ts` 调用 `SyncEngine.getInstance().registerAuthHeadersHook(() => ({ 'X-Ledger-Id': String(activeLedgerId || '') }))`，`activeLedgerId` 优先读取 `localStorage.getItem('minefolio_active_ledger_id')`，缺失时返回空字符串。**`pushLocal` 向每条队列项的 HTTP 请求附加 hook 返回的头**，供服务端做账本维度权限隔离。若 hook 未注册或返回空头，`pushLocal` **不阻断推送、不静默失败**，仅不发送 `X-Ledger-Id`，由服务端按默认账本处理；如需精确权限隔离，应确保 hook 在 bootstrap 阶段已注册。

#### 拉取远端（Pull）

```
syncNow() 完成 push 后执行 pullRemote
    ↓
按表分页拉取远端最新数据（accounts、categories、tags、assets、transactions、daily_expenses…）
    ↓
逐条比较 updated_at（pull 表内循环，非逐条 HTTP，分页由 SyncEngine 内部处理）：
  - 远端更新且本地无待推送项（synced=1 或无队列项）→ upsertLocal 覆盖本地，标记 synced=1
  - 远端更新且本地有待推送项（synced=0）→ 标记本地队列项 conflict=1，填充 remote_payload 为远端最新版本，保留本地 payload 与远端 remote_payload 两个版本，交给 ConflictList 手动确认；本地行不被覆盖
  - 远端已删除且本地无待推送项 → 物理删除本地行
  - 远端已删除且本地有待推送项 → 标记 conflict=1，remote_payload 为 null（表示远端记录已不存在），等待用户确认
    ↓
更新 last_sync_at 到 sync_meta
```

**remote_payload 填充契约**：
- `pushLocal` 时服务端返回 409：SyncEngine 在 `markConflict(queueId)` 时，将 409 响应体中的该记录完整数据填入 `remote_payload`；若响应体不含该记录数据，则 `remote_payload` 为 `undefined`，ConflictList 显示“服务端版本不可读”。
- `pullRemote` 时检测到冲突：SyncEngine 将本次 pull 读取的远端该记录最新数据填入 `remote_payload`。
- 离线 `create` 操作（服务端不存在该记录）：`remote_payload` 为 `undefined`，ConflictList 显示“服务端无此记录”，用户只能选择“保留本地”。

#### 冲突手动确认（设置页）

- 展示冲突项：来源表、操作类型、记录 ID；左右两列分别展示本地 `payload` 与远端 `remote_payload` 的差异字段
- “保留本地”：清除 conflict 标记，重新执行 `pushLocal` 推送本地 payload（覆盖服务端）；服务端若再返回 409，再次标记 conflict，交给用户再次确认
- “采用服务端”：从 sync_queue 删除该条，执行 `pullRemote` 将远端数据覆盖本地，标记 synced=1

---

## 4. 接口约定

### 4.1 SyncEngine 公开 API

```ts
/** ID 重映射回调（由 sync.ts 注册，供 SyncEngine 在 pushLocal 流程中调用） */
interface RemapHandlers {
  onAssetRemap(localId: number, remoteId: number): void   // 更新本地依赖行与后续队列项中的 asset_id
  onAccountRemap(localId: number, remoteId: number): void  // 更新本地依赖行与后续队列项中的 account_id
}

class SyncEngine {
  // 获取单例实例（构造函数私有化；离线-http 桥接、UI 组件均通过此方式获取）
  static getInstance(): SyncEngine
  // 注册同步表（启动时调用一次，一次性完成全部 6 张表）
  registerTable(def: SyncTableDef): void
  // 注册 ID 重映射回调（启动时由 sync.ts 调用一次，传入 remapAssetRefs/remapAccountRefs）
  registerRemapHandlers(handlers: RemapHandlers): void
  // 注册自定义请求头钩子（由 api 模块调用，替代硬编码 X-Ledger-Id）；返回的头会随每条队列项推送时附加到 HTTP 请求
  registerAuthHeadersHook(hook: () => Record<string, string>): void
  // 将本地写操作放入待推送队列并落库。ledgerId? 为可选账本 ID，用于指定该操作所属账本；
  // 未传入时优先读取 payload.ledger_id，再次未设置则回退到 localStorage 'minefolio_active_ledger_id'。
  // 内部用于 pushLocal 时通过 registerAuthHeadersHook 返回的头携带 X-Ledger-Id，供服务端做账本维度的权限隔离。
  enqueue(tableName: string, recordId: number, operation: 'create' | 'update' | 'delete', payload: Record<string, unknown>, ledgerId?: number): void
  // 若 `tableName` 未通过 `registerTable()` 注册，`enqueue` **抛出描述性错误**（不静默丢弃），由 `offline-http.ts` 的调用方捕获并向用户展示“离线写入未注册表”的提示，避免离线变更无声丢失。
  // 推送本地队列到服务端（返回创建/更新的服务端 ID 映射；键为表名，值为该表涉及的本地 recordId 集合）。
  // dryRun=true 时仅预检不发送 HTTP，返回哪些项会因服务端 409 而冲突，供 UI 预览。
  pushLocal(dryRun?: boolean): Promise<Map<string, Set<number>>>
  // 获取下一批待推送队列项（供 UI 预览，不发送 HTTP，不改变 synced 状态）
  getNextBatch(): SyncQueueItem[]
  // 从服务端拉取最新数据覆盖本地。分页/游标由 SyncEngine 内部按表自行处理，调用方无需感知页码；返回 void，完成后内部状态已更新，通过 minefolio:sync-state-changed 通知 useSyncStore。
  pullRemote(): Promise<void>
  // 完整双向同步（Push → Pull）。内部持有 `syncing` 标志：若前一次 `syncNow()` 仍在进行，后续调用（包括 visibilitychange/online/用户点击）立即返回，不重复推送、不并发处理队列，避免队列项被双重处理；`syncing` 在 `syncNow()` 完成（无论成功或失败）后置为 false，允许下次触发。
  syncNow(): Promise<void>
  // 获取冲突项列表（同步返回，直接读内存状态）
  getConflicts(): SyncQueueItem[]
  // 手动确认冲突：'retry' 保留本地推送（清除 conflict 标记并重新 syncNow），'discard' 采用服务端（从队列删除并等待 pullRemote 对齐）
  // 与 `useSyncStore` 的映射：action='retry' 对应 `resolveConflictRetry(queueId)`；action='discard' 对应 `resolveConflictDiscard(queueId)`。
  resolveConflict(queueId: number, action: 'retry' | 'discard'): Promise<void>
  // 重置（登出时调用，清空队列与冲突状态）
  reset(): void
}
```

### 4.2 冲突项数据结构（扩展 `SyncQueueItem`）

`frontend/src/types/mobile.ts` 中已有 `SyncQueueItem`，包含 `id`、`table_name`、`record_id`、`operation`、`payload`、`local_version`、`synced`、`conflict`、`ledger_id`。为支持冲突时“本地 vs 远端”两版对比，**新增可选字段 `remote_payload?: Record<string, unknown>`**（服务端返回的冲突版本完整数据），无需新增独立类型。

**冲突项字段含义**：
- `payload`：本地离线写入的完整数据（用户当前想提交的版本）。
- `remote_payload`：pullRemote 时从服务端读取的该记录最新版本（冲突方），由 SyncEngine 在标记 `conflict=1` 时填充，供 ConflictList 展示两版差异；若为离线 create 操作（服务端不存在该记录），则 `remote_payload` 为 `undefined`，ConflictList 显示“服务端无此记录”。
- `local_version`：本地队列版本号，按入队递增，**仅用于队列排队顺序与冲突项标识**。本次 D 子项目不实现 per-item break-point resumption：`pushLocal` 采用 continue-on-fail，失败项在下次 `syncNow()` 重新推送；`local_version` 不参与断点续推逻辑，避免 YAGNI。

**关于 `remapAssetRefs` / `remapAccountRefs`**：这是 `frontend/src/stores/sync.ts` 中已有的**私有重映射函数**，用于资产/账户离线创建后，把本地占位 ID 替换为服务端返回的真实 ID。它们**不是** `SyncEngine` 的公开 API，由 SyncEngine 内部在 `pushLocal` 流程中调用（即 3.4 数据流中的那一步），对外不暴露。

**级联 ID 重映射规则**：离线创建资产 A（占位 ID）→ 离线创建交易 T 引用 A（payload.asset_id=占位 ID）→ 推送 A 成功并获得服务端真实 ID → SyncEngine 内部调用 remapAssetRefs，同步更新本地依赖行（daily_expenses.asset_id、transactions.asset_id/linked_asset_id）以及**排在 A 之后的所有待推送队列项**中 payload 里的占位 ID，确保后续推送 T 时携带的是服务端真实 ID，而非本地占位 ID。账户离线创建同理（remapAccountRefs 更新 assets.account_id 及后续 assets 队列项）。

---

## 5. 错误处理

| 场景 | 处理方式 |
|------|----------|
| 网络不可用（无 err.response） | 写操作自动落库入队，弹出“已离线保存，联网后自动同步”；读操作直接抛出，页面兜底读本地库 |
| 1001 鉴权失效（后端自定义业务码，HTTP 200 包体内 `code:1001`，表示 JWT 缺失/过期/无效） | 调用 `onAuthFail()` 跳转登录页，不标冲突、不污染队列 |
| 服务端 409 业务冲突 | 标记 `conflict=1`，填充 `remote_payload`，进入 ConflictList，等待用户“保留本地 / 采用服务端” |
| 服务端 400/422 等非重试型 4xx | 不标冲突，直接向用户展示服务端返回的验证错误消息（如字段必填、金额非法），**不入队重试**，重试也无法成功 |
| 服务端其他 4xx（非 401/408/409/400/422/429） | 标记 `conflict=1`，保留队列，交给 ConflictList 手动确认 |
| 429 限流 | 不标冲突，保持 `synced=0`。本次不实现 per-item 延迟定时器：`Retry-After` 头若存在仅被读取并写入队列项的元信息（供未来可选的定时重试），`pushLocal` 继续向后推送其他项；失败项在下次 `syncNow()`（由前后台切换/网络恢复/用户点击触发）重新推送，此时若仍 429 则再次保留。不精确等待 `Retry-After` 秒数，避免阻塞整个推送队列。 |
| 5xx / 超时 | 不标冲突，保持 `synced=0`，等待下次 `syncNow()` 重试 |
| localStorage 配额不足 | 抛出 `minefolio:localdb-quota-exceeded` 事件（在 `window` 上派发）；`SettingsMobile.vue` 监听该事件并展示“本地离线库提醒 banner”（浏览器本地存储空间不足，离线变更可能未能持久化）。 |
| ID 重映射失败（create 后服务端未返回 id） | 保留本地占位 ID，下次推送重试，不阻塞后续队列项 |
| `pushLocal` 部分失败（如第 5 条返回 5xx，前 4 条已标 synced=1） | 采用 continue-on-fail 策略：失败项保持 `synced=0`，不标记冲突，继续推送后续项；失败项在下次 `syncNow()` 重试，不回滚已标成功项（各项独立原子，无跨项事务） |
| `syncNow()` 推送成功但 `pullRemote` 失败 | 错误被**有意静默吞掉**（Intentional）：push 已成功，数据已上传；pull 失败仅意味着本地缓存可能与远端短暂不一致，下次 `syncNow()` 会重新执行 pull，最终一致性由下次同步补齐，不丢数据、不重复推送。**不向 UI 暴露“pull 失败”错误信号**，避免用户困扰；若用户需要立即刷新远端数据，可再次点击“立即同步”。（Reviewer 建议：若产品侧认为用户需感知“缓存可能已过期”，可在后续迭代中在 SyncStatusBanner 增加一个轻量“缓存可能已过期”文本标记，本次 D 子项目不优先实现。） |

---

## 6. 测试计划

### 6.1 前端单元测试（vitest）

- `SyncEngine.getInstance()` 返回同一实例（单例），多次调用不创建新队列
- `SyncEngine.enqueue` / `pushLocal` / `pullRemote` 基本流程
- 资产离线创建 → 服务端返回新 ID → `remapAssetRefs` 重映射本地依赖行与后续队列 payload
- 冲突项标记后，ConflictList 列表正确展示；手动确认后队列状态更新
- 模拟 `visibilitychange` 前后台切换，自动触发 `syncNow()`
- `TABLE_COLUMNS` 与 `LOCAL_SCHEMA` 字段白名单一致性
- **队列迁移**：模拟 `localStorage` 中已有 `sync_queue` 条目，`SyncEngine` 构造后应恢复到内存队列；与 `useSyncStore` 的 `queue` 在 bootstrap 阶段一次性对齐，不丢失已入队离线变更
- **单例与 store 委托**：`useSyncStore.enqueue()` / `syncNow()` / `resolveConflictRetry()` / `resolveConflictDiscard()` 均应转发到同一 `SyncEngine.getInstance()` 实例，不维护独立队列状态

### 6.2 后端集成测试（test_link.sh）

脚本位置：`backend/tests/test_link.sh`，已存在，是 Minefolio 核心集成测试套件（244 断言：auth、accounts、CRUD、balance 联动、PnL、CSV、分页、ledger rebuild、backup、AI trace/workflow/skills）。该脚本针对 SQLite 运行，通过 curl 调用全部 API 端点并用 sqlite3 验证数据库状态。

离线同步相关集成测试用例：
- 离线创建资产/分类/账户后联网同步，验证服务端与本地 ID 一致
- 冲突场景：同一资产离线修改 + 服务端修改，推送返回 409，验证冲突被标记并可手动确认
- 断网期间多次写入，联网后按顺序推送，不丢数据、不重复
- 登出后 `reset()` 清空队列与本地库，下次登录重建

---

## 7. 实施顺序（分阶段）

1. **阶段一**：新建 `SyncEngine.ts`，将现有 6 张可写业务表迁移进引擎（注册 `SyncTableDef`），保持行为不变；`offline-http.ts` 改为调用 SyncEngine。
2. **阶段二**：确认 `schema.ts` 中 6 张可写业务表的本地建表语句与 `TABLE_COLUMNS` 字段白名单已齐全，与后端 API 契约一致（不新增建表语句）；将剩余非移动端可写业务表（ledgers、skills、tasks、scheduler 等）从离线镜像范围中排除，文档化该决定。
3. **阶段三**：复用现有 `frontend/src/utils/sync-network.ts` 的 `registerNetworkListeners(syncStore.syncNow)`，在 `main-mobile.ts` 启动阶段注册（涵盖 `visibilitychange` 前后台切换、`online` 网络恢复、Capacitor Network/App 状态变更），**不在 `MobileLayout.vue` 重复监听**；新增 `ConflictList.vue` 并接入设置页冲突确认流程。

---

## 8. 与后续子项目的关系

- **A（资产/分类/账户增删改）**：依赖 D 的全部可写表本地镜像，作为资产/账户数据源。
- **B（完整交易流水录入）**：依赖 D 的离线队列与 ID 重映射（资产离线创建后交易引用真实 ID）。
- **C（移动端专用 AI 聊天页）**：可受益于 D 的本地草稿缓存。
- **E（移动端特有能力）**：独立，可并行或最后做。

---

> 以上设计已按脑暴流程逐段呈现并获得用户批准。如无异议，将进入 spec-review 循环，随后由用户审阅并触发 `writing-plans` 进入实施。