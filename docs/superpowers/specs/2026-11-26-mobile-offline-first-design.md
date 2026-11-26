# 移动端离线优先增强设计 (D)

**项目**：Minefolio 移动端功能完善  
**子项目**：D — 离线优先增强（全部可写业务表本地镜像 + 手动冲突确认 + 前后台切换自动推送）  
**日期**：2026-11-26  
**状态**：设计待批准 → 实施计划

---

## 1. 目标

将现有移动端离线同步能力从“部分核心表（当前仅 4 张表注册在 `TABLE_BY_PATH`，实际写入口仅 daily-expenses 走离线代理）”扩展到“全部 6 张可写业务表”，并统一由 `SyncEngine` 驱动，实现：

1. **全部可写业务表本地镜像**：分类（categories）、资产（assets）、账户（accounts）、交易流水（transactions）、日常收支（daily_expenses）、标签（tags）全部可离线增删改。**共 6 张表**，与后端 API 模块一一对应。ledgers、members、skills、tasks、scheduler 等非移动端可写业务表，不在本次离线镜像范围内。
2. **前后台切换自动推送**：用户从后台切回前台，或网络从断开恢复时，自动触发离线队列推送。
3. **冲突用户手动确认**：冲突主来源为 pull 阶段客户端 `updated_at` 比较，另包含 push 阶段服务端返回业务码 `code:1004`（HTTP 200 包体，后端 `respond_conflict`）的场景；两类均标记冲突项，在设置页列表逐条让用户选择“保留本地 / 采用服务端”。

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
- **更关键：移动端写入口大多未走 `offlineApi`。** 当前仅 `views-mobile/ExpenseQuickSheet.vue` 的 daily-expenses 写入经 `offlineApi.post/put`；`AssetsMobile.vue` 等视图只调用 `assetsApi.list()` / `accountsApi.list()` 等读取接口，其余表的 create/update/delete 入口仍直接使用 `http` 的 API 模块，**断网时不会触发离线落库**。因此本子项目除扩展 `TABLE_BY_PATH` 外，必须同步把所有移动端可写表的写入口切换到 `offlineApi`（或让对应 API 模块统一走离线传输层），否则"全部可写表可离线增删改"只是表名注册上的支持，实际写路径不通。该改造也是 A/B 子项目的前置依赖。
- 推送触发已具备基础设施：`sync-network.ts` 的 `registerNetworkListeners` 涵盖 `visibilitychange` 前后台切换、`online` 网络恢复、Capacitor Network/App 状态变更，`main-mobile.ts:44` 已传入 `() => sync.syncNow()`，其中 `sync` 是 bootstrap 阶段 `useSyncStore()` 返回的稳定 Pinia 实例，**不存在闭包捕获过期 store 的问题**，无需改造。真正的问题是 `MobileLayout.vue:61-64,77-84` 又重复监听了 `online`/`offline` 并在挂载时 `syncNow()`，需按 3.3 的集成说明移除，避免重复触发。
- 冲突处理逻辑分散，缺乏统一的“保留本地 / 采用服务端”确认流程。

---

## 3. 架构设计

### 3.1 SyncEngine（新增）

新建单例类 `frontend/src/sync/SyncEngine.ts`，以配置化方式管理全部可写业务表的同步元信息：

```ts
/** 单张可写业务表的同步元信息 */
interface SyncTableDef {
  table: string             // 本地 sqlite 表名（如 'assets'）
  api: SyncApi              // 对应的 API 模块，必须实现 create/update/delete/list
  // 主键列固定为 'id'、全部注册表的 writable 均为 true；不设置这两个字段，避免 YAGNI。
  // 冲突策略固定为 'manual'（用户手动确认），D 子项目已确认，不预留 'auto'。
}

/** API 模块必须实现的同步接口（供 SyncEngine 调用；已确认 6 个现有模块均实现） */
// 注：categoriesApi/tagsApi 的 create/update/delete 不接收 config 参数，运行时忽略多余实参即可。
interface SyncApi {
  create(payload: Record<string, unknown>, config?: any): Promise<any>
  update(id: number, payload: Record<string, unknown>, config?: any): Promise<any>
  delete(id: number, config?: any): Promise<any>
  list(params?: any): Promise<any> // 用于 pullRemote 全量拉取（categories/assets/accounts/transactions/daily_expenses/tags 均已实现）
}

/** ID 重映射回调（供 sync.ts 注册给 SyncEngine，在 pushLocal 流程中调用） */
// 三参数签名与 sync.ts 现有私有函数一致，afterQueueId 避免误改历史队列项。
interface RemapHandlers {
  onAssetRemap(oldId: number, newId: number, afterQueueId: number): void
  onAccountRemap(oldId: number, newId: number, afterQueueId: number): void
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
- **唯一 source of truth 是本地 SQLite 的 `sync_queue` 表**（整个库经 Web Crypto 加密后序列化到一个 localStorage key，见 `db/local.ts:1,73-93,152-170`）。SyncEngine 持有的内存队列、同步中标志、上次同步时间、冲突列表均为该持久化状态的**运行时镜像**：所有 `enqueue`/`pushLocal`/`pullRemote`/`syncNow`/`resolveConflict` 都先读写 DB 再更新内存镜像并 `persist()`。现有 `pushLocal` 之所以逐条重读 DB（`sync.ts:243-247`），正是因为 remap 会改写 DB 中后续队列项的 payload——迁移后**保留这一行为**，杜绝内存与 DB 分叉。
- `useSyncStore`（`frontend/src/stores/sync.ts`）是**委托包装（thin delegation）**：其公开方法只转发给 `SyncEngine.getInstance()`，**不维护独立队列状态**，不直接写队列。store 的 Pinia ref `queue`/`syncing`/`lastSyncAt`/`conflicts` 作为 UI 消费端的缓存快照，通过 `subscribeSyncEngine()` 订阅 `minefolio:sync-state-changed` 对齐：**每次** SyncEngine 变更状态后同步派发该事件（非定时轮询），事件处理函数直接从 DB 重载 `queue`/`syncing`/`lastSyncAt`（即现有 `loadQueue()` + 读取 `sync_meta`），避免靠事件载荷传递整份状态。
- **订阅时机**：`main-mobile.ts` `bootstrap()` 完成 `SyncEngine` 初始化（`registerTable()` + `registerRemapHandlers()`）后，**再**执行 `useSyncStore.subscribeSyncEngine()`，避免首次挂载前漏掉状态变更。
- `offline-http.ts` 改为调用 `SyncEngine.getInstance().enqueue()` 做本地落库与入队（不再调用 `useSyncStore().enqueue()`）。
- **依赖方向**：`SyncEngine.ts` 仅向下依赖 `@/db/local`（run/query/rowsFrom/persist），不 import `sync.ts`；`sync.ts` 向上调用 `SyncEngine.registerRemapHandlers()` 注入回调（依赖注入，非循环依赖）。模块边界清晰，可独立对 SyncEngine 做单元测试（mock db 层即可）。

**启动注册位置**：`registerTable()` 在 `frontend/src/main-mobile.ts` 的 `bootstrap()` 阶段调用一次，在 `initLocalDb()` 之后、`syncStore.init()` 之前完成全部 6 张表的注册；紧接着调用 `registerRemapHandlers({ onAssetRemap, onAccountRemap })`。现有 `remapAssetRefs`/`remapAccountRefs` 是 `sync.ts` 的**私有函数且带第三参数 `afterQueueId`**，注册前需改为导出（或迁入 SyncEngine），接口按 4.1 的三参数签名对齐。注册失败（如缺少对应 API 模块）抛出，被 `bootstrap()` 顶层的 `.catch` 捕获并渲染可见错误屏幕，避免离线写入静默失败。

**单例获取**：`SyncEngine` 采用单例模式，构造函数私有化，通过 `SyncEngine.getInstance()` 获取唯一实例。`offline-http.ts` 的桥接逻辑、UI 组件（通过 `syncStore` 委托）均通过 `getInstance()` 获取引用，保证整个应用生命周期内只有一个执行者与一份持久化队列。

**现有队列迁移路径（不丢数据）**：`offline-http.ts` 当前通过 `useSyncStore().enqueue()` 将离线变更写入本地 SQLite `sync_queue`（库整体持久化在 localStorage），**没有**独立的 localStorage `sync_queue` 键。迁移到 SyncEngine 后，**不清空、不重建现有队列**：`initLocalDb()` 已恢复整个库，SyncEngine 构造/初始化时直接调用 `loadQueue()` 从 `sync_queue` 表装载未同步项即可，不重复解码 localStorage。已入队但尚未推送的离线变更继续由 `SyncEngine.pushLocal()` 推送，不丢失、不重复。

**构造失败模式**：localStorage 不可读或内容损坏时，`initLocalDb()` 已负责备份损坏载荷并以空库启动（`db/local.ts:86-93`）。SyncEngine 在此基础上调用 `loadQueue()` 得到空队列，**不抛出异常**，并通过 `minefolio:sync-state-changed` 事件让 `useSyncStore` 对齐（空队列、无冲突），不阻塞应用启动。后续 `persist()` 因配额失败时，`db/local.ts:163-168` 已派发 `minefolio:localdb-quota-exceeded`，SettingsMobile 监听后展示本地离线库提醒 banner；不丢已成功写入内存库的条目。

### 3.2 本地镜像表扩展

**本次 D 子项目仅对 `sync_queue` 增加 `remote_payload` 列（见 4.2），6 张业务表的 `LOCAL_SCHEMA` 不新增建表语句。** 当前 `frontend/src/db/schema.ts` 的 `LOCAL_SCHEMA` 已覆盖全部 6 张可写业务表（categories、assets、accounts、transactions、daily_expenses、tags）及 `sync_queue`、`sync_meta`，`TABLE_COLUMNS` 也已对齐各字段白名单。阶段二做**契约对齐验证**（见下方 checklist），业务表不新增建表语句，不改动 `schema.ts` 除非验证发现缺字段/类型不一致。

对应后端端点（供 pullRemote 拉取与 pushLocal 推送使用）：

| 表名 | 后端 REST 路径前缀 | 前端 API 模块 | 离线写入路径 |
|------|------------------|--------------|-------------|
| categories | `/api/categories` | categoriesApi | `offline-http.ts` TABLE_BY_PATH **新增** `categories` → `categories`，并同步更新 `tableNameFromUrl()` 正则（现仅匹配 daily-expenses/transactions/assets/accounts） |
| assets | `/api/assets` | assetsApi | `offline-http.ts` TABLE_BY_PATH 已有 `assets` |
| accounts | `/api/accounts` | accountsApi | `offline-http.ts` TABLE_BY_PATH 已有 `accounts` |
| transactions | `/api/transactions` | transactionsApi | `offline-http.ts` TABLE_BY_PATH 已有 `transactions` |
| daily_expenses | `/api/daily-expenses` | dailyExpensesApi | `offline-http.ts` TABLE_BY_PATH 已有 `daily-expenses` |
| tags | `/api/tags` | tagsApi | `offline-http.ts` TABLE_BY_PATH **新增** `tags` → `tags`，并同步更新 `tableNameFromUrl()` 正则 |

**写入口改造（本子项目必做）**：上表"离线写入路径"只描述 `offline-http.ts` 的表名注册条件；要真正支持离线增删改，还必须把各表在移动端的 create/update/delete 调用从直接使用 `http` 的 API 模块切换到 `offlineApi`（参考 `ExpenseQuickSheet.vue` 现有用法）。仅注册表名而不改写入口，断网时仍不会落库入队，A/B 子项目也无法拿到离线数据源。

**排除说明**：`account_permissions` 与 `expense_tags` 虽在 `LOCAL_SCHEMA`/`TABLE_COLUMNS` 中带 `__deleted`/`updated_at` 同步列，但 `account_permissions` 为 RBAC 服务端管控表（不支持移动端离线写入），`expense_tags` 为多对多关联表（随 daily_expenses 一并原子写入，不独立离线），二者均**不列入**可写离线表清单，不在 `TABLE_BY_PATH` 中注册。`expense_tags` 的原子写入由 `dailyExpensesApi.create()` 在服务端一次完成，移动端离线创建 daily_expenses 时，连同其 tags 一并放入 `payload.tags` 字段，push 时由服务端原子处理，无需 `expense_tags` 独立入队。

**阶段二 schema/API 契约对齐验证方法**（对应 Section 7 阶段二；在此阶段对每张可写表，执行以下四项检查并记录结果）：
1. 字段白名单 `TABLE_COLUMNS[table]` 包含后端 API `create/update` 请求体中所有可写字段（通过阅读 `frontend/src/api/<table>.ts` 的 `create`/`update` 参数确认）；
2. 本地建表语句 `LOCAL_SCHEMA` 中对应列的类型与后端 SQLite 迁移 `backend/sql/migrations/sqlite/*.sql` 中同名表列类型一致；
3. `TABLE_BY_PATH` 与 `tableNameFromUrl()` 正则已覆盖该表的 REST 路径前缀（`/api/<table>` 及 `/api/<table>/<id>`），**且移动端写入口已切换到 `offlineApi`**（仅注册表名不算通过）。
4. 确认该表服务端无基于 `updated_at` 的乐观并发版本校验：现有 `respond_conflict` 返回 HTTP 200 + `code:1004`，语义为“资源已存在/操作冲突”，**并非**并发更新冲突，且响应体不含记录数据。因此冲突检测以 pull 阶段客户端 `updated_at` 比较为主、push 阶段 `code:1004` 为辅；不依赖 HTTP 409 状态码，也不预留“服务端胜出自动覆盖”回退分支（策略固定 manual）。
四项全部通过方可进入 **Section 7 阶段三（实施顺序阶段三）**；若某项不通过，由 frontend 负责人在 **Section 7 阶段二（schema/API 对齐）** 修复缺字段/缺注册，不进入下一阶段，避免离线写入路径静默失败。

### 3.3 组件清单

| 组件 | 路径 | 职责 |
|------|------|------|
| SyncEngine | `frontend/src/sync/SyncEngine.ts` | 离线同步引擎核心（单例） |
| SyncStatusBanner | `frontend/src/components/mobile/SyncStatusBanner.vue` | **替换** `MobileLayout.vue` 顶部现有的 inline `net-status-pill`（离线/待同步/冲突提示） |
| ConflictList | `frontend/src/components/mobile/ConflictList.vue` | **替换** `SettingsMobile.vue` 现有的 inline `sync-conflicts-card`（冲突列表 + 重试/放弃按钮） |

**SyncStatusBanner 组件契约（纯 props 模式）**：
- Props：`isOnline: boolean`（沿用 `MobileLayout.vue:59` 的 navigator.onLine 状态）、`pendingCount: number`（**非冲突**待推送数，对应现有 `activePendingCount`）、`conflictCount: number`（冲突条数）、`syncing: boolean`
- 不传 `hasConflict`：需保留现有"同步冲突 N"的**条数**展示（`MobileLayout.vue:20`），不能用布尔值替代。
- Events：`click` → 由父级（`MobileLayout.vue`）绑定 `syncStore.syncNow()`
- 不直接订阅 `minefolio:sync-state-changed`，也不直接读 `useSyncStore()`；父级负责从 store 读取并传入 props，避免数据源重复。

**ConflictList 组件契约**：
- Props：`conflicts: SyncQueueItem[]`（包含新增可选字段 `remote_payload`）
- Events：`retry(id: number)` → 父级调用 `sync.resolveConflictRetry(id)`；`discard(id: number)` → 父级调用 `sync.resolveConflictDiscard(id)`
- 每条行展示 `table_name · operation #record_id`；若为 create 操作且 `record_id` 为本地占位 ID，显示“占位 ID”字样。左右两列分别展示本地 `payload` 与远端 `remote_payload` 的差异字段；若 `remote_payload` 为空（离线 create，服务端无此记录），右列显示“服务端无此记录”。

**UI 替换策略**：两个新组件与现有 inline UI 互斥替换（不并存），替换后 `MobileLayout.vue` 和 `SettingsMobile.vue` 移除对应的 inline 模板与样式，改为引用新组件，行为保持不变。

**MobileLayout.vue 集成说明**：
- 删除 header 顶部现有的 inline `net-status-pill`（离线模式/待同步/同步冲突三种状态的 `<span>` 块，含 `@click="syncStore.syncNow()"`），以及对 `syncStore.activePendingCount` / `syncStore.conflicts.length` 的模板内直接读取。
- 改为引用 `<SyncStatusBanner :is-online="isOnline" :pending-count="syncStore.activePendingCount" :conflict-count="syncStore.conflicts.length" :syncing="syncStore.syncing" @click="syncStore.syncNow()" />`，组件内部按 `isOnline → pendingCount > 0 → conflictCount > 0` 的优先级渲染三种状态，与原 inline UI 完全一致。
- **不复用 `syncStore.pendingCount` 作为组件 prop**：现有 `pendingCount` 含冲突项（`sync.ts:75`），而 `activePendingCount` 不含（`sync.ts:79`）；`SettingsMobile.vue:243` 还用 `sync.pendingCount` 做切换服务端地址的警告。保持两者语义与调用点不变。
- **移除重复的同步监听**：删除 `onMounted` 中 `window.addEventListener('online'/'offline', ...)` 及进入主界面即 `syncStore.syncNow()` 的逻辑（`MobileLayout.vue:61-64,77-84`）；同步触发统一由 `sync-network.ts` 的 `registerNetworkListeners` 负责（`main-mobile.ts:44` 已注册）。`minefolio:ledger-changed` 的处理保留（账本切换是独立触发场景）。

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
网络可用 → 正常返回（在线写不经过 SyncEngine，无队列项）
网络不可用（无 err.response）→ 由 offline-http 调用 SyncEngine.enqueue(tableName, recordId, operation, payload, ledgerId?)
    ↓
本地 sqlite upsert，__deleted=0/1，加密持久化到 localStorage
    ↓
前后台切换（visibilitychange）或网络恢复（online）→ SyncEngine.syncNow()
    ↓
pushLocal：按队列顺序逐条推送到服务端
    ↓
服务端返回新 ID（如资产离线创建）→ **remapAssetRefs(localId, remoteId, afterQueueId)** / **remapAccountRefs(localId, remoteId, afterQueueId)** 被 SyncEngine 调用（通过 registerRemapHandlers 注册的回调），同步更新：
  1. 本地依赖行：`assets.account_id`（若该记录是账户）、`daily_expenses.asset_id`、`transactions.asset_id` 等外键列，将本地占位 ID 替换为服务端真实 ID；
  2. 排在当前记录之后的所有待推送队列项：若其 `payload` 中含同名外键字段且值等于本地占位 ID，替换为服务端真实 ID；
  remap 失败（如本地依赖行不存在）不阻塞推送，仅记录警告，下次推送时该依赖项仍以占位 ID 发送，由服务端校验。
    ↓
标记 synced=1；服务端返回业务码 `code:1004`（HTTP 200 包体）→ markConflict，交给 ConflictList 手动确认（`remote_payload` 此时为 undefined，见 remote_payload 填充契约）
```

**offline-http → SyncEngine 桥接逻辑**（Section 3.1 已说明 `offline-http.ts` 改为调用 `SyncEngine.enqueue()`，下文具体化）：

1. **表→API 路由**：`offline-http.ts` 的 `TABLE_BY_PATH` 新增 `categories`、`tags`，并同步更新 `tableNameFromUrl()` 正则，使 `tableNameFromUrl('/api/categories')` 返回 `'categories'`、`tableNameFromUrl('/api/tags')` 返回 `'tags'`。与 `SyncEngine.registerTable()` 注册的 6 张表表名一一对应（`SyncTableDef.table` 与 `TABLE_BY_PATH` 值相同）。
2. **recordId 决定**：URL 路径若含数字 ID（如 `/assets/123`），`extractId(url)` 取出作为 `recordId`；否则（POST 创建）由 `nextLocalId(table)` 生成本地占位 ID，后续推送成功后通过 `remapAssetRefs`/`remapAccountRefs` 替换为服务端真实 ID。
3. **ledgerId 解析与持久化**：`SyncEngine.enqueue` 按 `ledgerId` 参数 → `payload.ledger_id` → `localStorage['minefolio_active_ledger_id']` 的优先级解析，写入 `sync_queue.ledger_id`（沿用现有 `sync.ts:119-138` 行为）。`pushLocal` 为每条队列项构造 `config = { headers: { 'X-Ledger-Id': String(item.ledger_id) } }`（沿用现有 `sync.ts:257`），**以入队时的账本快照为准**——用户随后切换当前账本不影响已入队数据应归属的账本。
4. **与 SyncEngine 的连接**：`offline-http.ts` 不再直接调用 `useSyncStore().enqueue()`，改为 `SyncEngine.getInstance().enqueue(...)`（SyncEngine 单例，在 `main-mobile.ts` bootstrap 阶段已完成 `registerTable()` + `registerRemapHandlers()`）。
5. **账本隔离不依赖当前活跃账本**：**不引入** `registerAuthHeadersHook` 读取当前 `localStorage` 活跃账本的方案——那会让入队后切换账本的离线变更被推送到错误账本。保持现有按队列项 `ledger_id` 发送 `X-Ledger-Id` 的实现（`sync.ts:257`、`283-291`），经各 API 模块的 `config` 参数透传；无账本列的表（categories、tags）本就不带该头，服务端按默认账本处理。

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
- `pushLocal` 时服务端返回业务码 `code:1004`：后端 `respond_conflict` 仅返回 `code`/`message`，不含记录数据，故 `remote_payload` 恒为 `undefined`，ConflictList 显示“服务端版本不可读”。
- `pullRemote` 时检测到冲突：SyncEngine 将本次 pull 读取的远端该记录最新数据填入 `remote_payload`。
- 离线 `create` 操作（服务端不存在该记录）：`remote_payload` 为 `undefined`，ConflictList 显示“服务端无此记录”，用户只能选择“保留本地”。

#### 冲突手动确认（设置页）

- 展示冲突项：来源表、操作类型、记录 ID；左右两列分别展示本地 `payload` 与远端 `remote_payload` 的差异字段
- “保留本地”：清除 conflict 标记，重新执行 `pushLocal` 推送本地 payload（覆盖服务端）；服务端若再返回 `code:1004`，或下次 pull 时客户端 `updated_at` 比较再次检测到并发变更，再次标记 conflict，交给用户再次确认
- “采用服务端”：从 sync_queue 删除该条，执行 `pullRemote` 将远端数据覆盖本地，标记 synced=1

---

## 4. 接口约定

### 4.1 SyncEngine 公开 API

```ts
/** ID 重映射回调（由 sync.ts 注册，供 SyncEngine 在 pushLocal 流程中调用） */
interface RemapHandlers {
  // afterQueueId 必须与现有 sync.ts 的 remapAssetRefs/remapAccountRefs 签名一致，
  // 只重映射排在当前 create 之后的队列项，避免误改历史队列。
  onAssetRemap(oldId: number, newId: number, afterQueueId: number): void
  onAccountRemap(oldId: number, newId: number, afterQueueId: number): void
}

class SyncEngine {
  // 获取单例实例（构造函数私有化；离线-http 桥接、UI 组件均通过此方式获取）
  static getInstance(): SyncEngine
  // 注册同步表（启动时调用一次，一次性完成全部 6 张表）
  registerTable(def: SyncTableDef): void
  // 注册 ID 重映射回调（启动时由 sync.ts 调用一次，传入 remapAssetRefs/remapAccountRefs；
  // 这两个函数目前是 sync.ts 的私有函数，注册前需改为导出或迁入 SyncEngine）
  registerRemapHandlers(handlers: RemapHandlers): void
  // 将本地写操作放入待推送队列并落库。ledgerId? 为可选账本 ID，用于指定该操作所属账本；
  // 未传入时优先读取 payload.ledger_id，再次未设置则回退到 localStorage 'minefolio_active_ledger_id'。
  // 解析结果写入 sync_queue.ledger_id，pushLocal 据此为每条请求附加 X-Ledger-Id（入队时快照，不受后续账本切换影响）。
  enqueue(tableName: string, recordId: number, operation: 'create' | 'update' | 'delete', payload: Record<string, unknown>, ledgerId?: number): void
  // 若 `tableName` 未通过 `registerTable()` 注册，`enqueue` **抛出描述性错误**（不静默丢弃）。
  // 注册表校验必须发生在本地 sqlite 写入之前，否则会写出未入队的孤立本地行。
  // 推送本地队列到服务端（返回创建/更新的服务端 ID 映射；键为表名，值为该表涉及的本地 recordId 集合）。
  pushLocal(): Promise<Map<string, Set<number>>>
  // 从服务端拉取最新数据覆盖本地。分页/游标由 SyncEngine 内部按表自行处理，调用方无需感知页码；返回 void，完成后内部状态已更新，通过 minefolio:sync-state-changed 通知 useSyncStore。
  pullRemote(): Promise<void>
  // 完整双向同步（Push → Pull）。内部持有 `syncing` 标志：若前一次 `syncNow()` 仍在进行，后续调用（包括 visibilitychange/online/用户点击）立即返回，不重复推送、不并发处理队列，避免队列项被双重处理；`syncing` 在 `syncNow()` 完成（无论成功或失败）后置为 false，允许下次触发。
  syncNow(): Promise<void>
  // 获取冲突项列表（从 DB 的 sync_queue 加载，含 remote_payload）
  getConflicts(): SyncQueueItem[]
  // 手动确认冲突：'retry' 保留本地推送（清除 conflict 标记并重新 syncNow），'discard' 采用服务端（从队列删除并等待 pullRemote 对齐）
  // 与 `useSyncStore` 的映射：action='retry' 对应 `resolveConflictRetry(queueId)`；action='discard' 对应 `resolveConflictDiscard(queueId)`。
  resolveConflict(queueId: number, action: 'retry' | 'discard'): Promise<void>
  // 重置（登出时调用，清空队列与冲突状态）
  reset(): void
}
```

**备注**：`categoriesApi` / `tagsApi` 的 create/update/delete 不接收 `config` 参数（`api/categories.ts:42,51,59`、`api/tags.ts:29,39,48`），与其余 4 个模块不同。运行时多传参数无害，但这两张表无法携带 `X-Ledger-Id` 头；其本地表也无 `ledger_id` 列，符合预期。

### 4.2 冲突项数据结构（扩展 `SyncQueueItem`）

`frontend/src/types/mobile.ts` 中已有 `SyncQueueItem`，包含 `id`、`table_name`、`record_id`、`operation`、`payload`、`local_version`、`synced`、`conflict`、`ledger_id`。为支持冲突时“本地 vs 远端”两版对比，**新增可选字段 `remote_payload?: Record<string, unknown>`**（服务端返回的冲突版本完整数据），无需新增独立类型。

**同步持久化改造（必须）**：`sync_queue` 表（`frontend/src/db/schema.ts`）需新增 `remote_payload TEXT DEFAULT NULL` 列，并在 `loadQueue()` / `markConflict()` / `resolveConflictRetry()` 中读写该列（用 `JSON.stringify` / `JSON.parse`）。现有本地库通过 `CREATE TABLE IF NOT EXISTS` 创建，新增列需幂等的 `ALTER TABLE sync_queue ADD COLUMN remote_payload TEXT`（捕获"duplicate column"错误后忽略），保证老用户升级后冲突远端版本不丢失，App 重启后 ConflictList 仍可展示两版差异。

**冲突项字段含义**：
- `payload`：本地离线写入的完整数据（用户当前想提交的版本）。
- `remote_payload`：pullRemote 时从服务端读取的该记录最新版本（冲突方），由 SyncEngine 在标记 `conflict=1` 时填充，供 ConflictList 展示两版差异；若为离线 create 操作（服务端不存在该记录），则 `remote_payload` 为 `undefined`，ConflictList 显示“服务端无此记录”。
- `local_version`：入队时生成的 ISO 8601 时间戳（`sync.ts:134`），用作队列排序与冲突项标识，**不是递增整数**。本次 D 子项目不实现 per-item break-point resumption：`pushLocal` 采用 continue-on-fail，失败项在下次 `syncNow()` 重新推送；`local_version` 不参与断点续推逻辑，避免 YAGNI。

**关于 `remapAssetRefs` / `remapAccountRefs`**：这是 `frontend/src/stores/sync.ts` 中已有的重映射函数（`sync.ts:168,209`，当前为私有且带第三参数 `afterQueueId`），用于资产/账户离线创建后，把本地占位 ID 替换为服务端返回的真实 ID。实施时需将其**导出为模块级函数**并按 4.1 的三参数签名注册；它们**不是** `SyncEngine` 的公开 API，由 SyncEngine 内部在 `pushLocal` 流程中调用（即 3.4 数据流中的那一步），对外不暴露。

**级联 ID 重映射规则**：离线创建资产 A（占位 ID）→ 离线创建交易 T 引用 A（payload.asset_id=占位 ID）→ 推送 A 成功并获得服务端真实 ID → SyncEngine 内部调用 remapAssetRefs，同步更新本地依赖行（daily_expenses.asset_id、transactions.asset_id/linked_asset_id）以及**排在 A 之后的所有待推送队列项**中 payload 里的占位 ID，确保后续推送 T 时携带的是服务端真实 ID，而非本地占位 ID。账户离线创建同理（remapAccountRefs 更新 assets.account_id 及后续 assets 队列项）。

---

## 5. 错误处理

**错误分类准则**：后端**业务**错误多为 HTTP 200 + 业务码包体（`backend/src/common/response.h:55-57`），不能按 HTTP 409 判断；同时仍存在真实 HTTP 4xx/5xx 状态码。`markConflict` 的触发条件白名单为：`code === 1004`，或 HTTP 状态为 4xx 且不属于 401/408/400/422/429；**其余业务码（`1002` 参数错误、`1003` 资源不存在、`2001` 操作被禁止等）一律不标冲突**。校验/权限类错误通过下方的"校验错误呈现路径"输出到 UI。

| 场景 | 处理方式 |
|------|----------|
| 网络不可用（无 err.response） | 写操作自动落库入队，弹出“已离线保存，联网后自动同步”；读操作直接抛出，页面兜底读本地库 |
| 1001 鉴权失效（后端自定义业务码，HTTP 200 包体内 `code:1001`，表示 JWT 缺失/过期/无效） | `pushLocal` 调用 `onAuthFail()` 跳转登录页；`offline-http.ts` 的在线写路径调用 `useAuthStore().logout()`（`offline-http.ts:151-153`）。两条路径均不标冲突、不污染队列 |
| 业务码 `code:1004`（HTTP 200 包体，后端 `respond_conflict`，如“资源已存在”） | 标记 `conflict=1`（`remote_payload` 为 undefined，因响应不含记录数据），进入 ConflictList，等待用户“保留本地 / 采用服务端”；并发冲突主来源是 pull 时客户端 `updated_at` 比较 |
| 服务端 400/422 等非重试型 4xx，或业务码 `1002`/`1003`/`2001` | 不标冲突；**从 `sync_queue` 删除该队列项**（本地放弃），通过"校验错误呈现路径"向用户展示服务端错误消息（如字段必填、金额非法）。用户修正数据后重新写入生成新队列项，不重试原项（重试也无法成功） |
| 服务端其他 4xx（非 401/408/400/422/429） | 标记 `conflict=1`，保留队列，交给 ConflictList 手动确认 |
| 429 限流 | 不标冲突，保持 `synced=0`。本次不实现 per-item 延迟定时器：`Retry-After` 头若存在仅被读取并写入队列项的元信息（供未来可选的定时重试），`pushLocal` 继续向后推送其他项；失败项在下次 `syncNow()`（由前后台切换/网络恢复/用户点击触发）重新推送，此时若仍 429 则再次保留。不精确等待 `Retry-After` 秒数，避免阻塞整个推送队列。 |
| 5xx / 超时 | 不标冲突，保持 `synced=0`，等待下次 `syncNow()` 重试 |
| localStorage 配额不足 | 抛出 `minefolio:localdb-quota-exceeded` 事件（在 `window` 上派发）；`SettingsMobile.vue` 监听该事件并展示“本地离线库提醒 banner”（浏览器本地存储空间不足，离线变更可能未能持久化）。 |
| ID 重映射失败（create 后服务端未返回 id） | 保留本地占位 ID，下次推送重试，不阻塞后续队列项 |
| `pushLocal` 部分失败（如第 5 条返回 5xx，前 4 条已标 synced=1） | 采用 continue-on-fail 策略：失败项保持 `synced=0`，不标记冲突，继续推送后续项；失败项在下次 `syncNow()` 重试，不回滚已标成功项（各项独立原子，无跨项事务） |
| `syncNow()` 推送成功但 `pullRemote` 失败 | 错误被**有意静默吞掉**（Intentional）：push 已成功，数据已上传；pull 失败仅意味着本地缓存可能与远端短暂不一致，下次 `syncNow()` 会重新执行 pull，最终一致性由下次同步补齐，不丢数据、不重复推送。**不向 UI 暴露“pull 失败”错误信号**，避免用户困扰；若用户需要立即刷新远端数据，可再次点击“立即同步”。（Reviewer 建议：若产品侧认为用户需感知“缓存可能已过期”，可在后续迭代中在 SyncStatusBanner 增加一个轻量“缓存可能已过期”文本标记，本次 D 子项目不优先实现。） |

**校验错误呈现路径**：`pushLocal` 在 catch 中按上述白名单分类，校验/权限类错误收集为 `pushErrors: { queueId: number; table: string; message: string }[]`，随 `minefolio:sync-state-changed` 事件暴露给 store；`SettingsMobile.vue` 的"立即同步"包装函数（`SettingsMobile.vue:196-198`）在 `syncNow()` 完成后读取并逐条 `ElMessage.error(message)`，随后清空。`MobileLayout.vue` 的自动同步不展示这些消息，避免后台切前台时弹窗干扰；用户可在设置页看到对应离线写入失败提示。

---

## 6. 测试计划

### 6.1 前端单元测试（vitest）

- `SyncEngine.getInstance()` 返回同一实例（单例），多次调用不创建新队列
- `SyncEngine.enqueue` / `pushLocal` / `pullRemote` 基本流程；`enqueue` 未注册表时抛错且**未写入本地 sqlite 行**
- 资产离线创建 → 服务端返回新 ID → `remapAssetRefs(oldId, newId, afterQueueId)` 只重映射之后的队列项，不误改历史项
- 冲突项标记后，ConflictList 列表正确展示两版差异；手动确认后队列状态更新
- **`remote_payload` 持久化**：标记冲突 → 模拟重载数据库 → `getConflicts()` 仍返回远端版本
- **错误分类**：`code:1004` / 其他 4xx 标冲突；`1002`/`1003`/`2001`/400/422 删除队列项并进入 `pushErrors`，不标冲突
- **账本隔离**：入队后切换活跃账本，`pushLocal` 仍按队列项 `ledger_id` 发送 `X-Ledger-Id`
- 模拟 `visibilitychange` 前后台切换，自动触发 `syncNow()`（仅 `registerNetworkListeners` 一处监听）
- `TABLE_COLUMNS` 与 `LOCAL_SCHEMA` 字段白名单一致性；6 张可写表写入口均经 `offlineApi`
- **队列迁移**：模拟本地 SQLite `sync_queue` 已有条目（库持久化在 localStorage），`initLocalDb()` + `SyncEngine` 初始化后队列完整恢复；与 `useSyncStore` 的 `queue` 在 bootstrap 阶段一次性对齐，不丢失已入队离线变更
- **单例与 store 委托**：`useSyncStore.enqueue()` / `syncNow()` / `resolveConflictRetry()` / `resolveConflictDiscard()` 均应转发到同一 `SyncEngine.getInstance()` 实例，不维护独立队列状态

### 6.2 前端 E2E（网络拦截）

用 Playwright + 请求拦截模拟断网/恢复/`code:1004` 业务码，覆盖：断网写入 → 切前台自动推送 → 服务端修改触发 pull 冲突 → 手动"保留本地/采用服务端" → 重载后冲突状态与 `remote_payload` 仍在。`test_link.sh` 是 curl + sqlite3 的服务端集成脚本，**无法模拟断网与离线队列**，不作为离线同步功能的验证手段。

### 6.3 后端集成测试（test_link.sh）

脚本位置：`backend/tests/test_link.sh`，已存在，是 Minefolio 核心集成测试套件（244 断言：auth、accounts、CRUD、balance 联动、PnL、CSV、分页、ledger rebuild、backup、AI trace/workflow/skills）。该脚本针对 SQLite 运行，通过 curl 调用全部 API 端点并用 sqlite3 验证数据库状态。本子项目只用它回归服务端 API 契约（含 `respond_conflict` 的 `code:1004` 语义未变），离线队列行为由 6.1/6.2 覆盖。

---

## 7. 实施顺序（分阶段）

1. **阶段一**：`sync_queue` 加 `remote_payload TEXT` 列（幂等 ALTER）；新建 `SyncEngine.ts`，注册 6 张可写表并将队列/推送/拉取/冲突逻辑迁入，保持行为不变；导出 `remapAssetRefs`/`remapAccountRefs` 并按三参数签名注册；`offline-http.ts` 改为调用 SyncEngine。
2. **阶段二**：确认 `schema.ts` 中 6 张可写业务表的本地建表语句与 `TABLE_COLUMNS` 字段白名单已齐全，与后端 API 契约一致（业务表不新增建表语句）；**把各表移动端写入口切换到 `offlineApi`**（含 `categories`/`tags` 的 `TABLE_BY_PATH` 与正则注册），使"全部可写表离线增删改"真正可用；ledgers/skills/tasks/scheduler 等非移动端可写表本就不在 `LOCAL_SCHEMA` 中，仅在文档记录该范围决定，无需实施动作。
3. **阶段三**：`main-mobile.ts:44` 已调用 `registerNetworkListeners(() => sync.syncNow())`，无需重复注册；按 3.3 说明删除 `MobileLayout.vue` 中重复的 `online`/`offline` 监听与挂载时 `syncNow()`；新增 `SyncStatusBanner.vue` 与 `ConflictList.vue` 并接入设置页冲突确认流程。

---

## 8. 与后续子项目的关系

- **A（资产/分类/账户增删改）**：依赖 D 的全部可写表本地镜像，作为资产/账户数据源。
- **B（完整交易流水录入）**：依赖 D 的离线队列与 ID 重映射（资产离线创建后交易引用真实 ID）。
- **C（移动端专用 AI 聊天页）**：可受益于 D 的本地草稿缓存。
- **E（移动端特有能力）**：独立，可并行或最后做。

---

> 以上设计已按脑暴流程逐段呈现并获得用户批准。如无异议，将进入 spec-review 循环，随后由用户审阅并触发 `writing-plans` 进入实施。