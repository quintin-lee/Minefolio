# Minefolio 移动端 App 设计文档

**日期**: 2026-08-13  
**方案**: Vue 3 + Capacitor，离线优先 + 联网同步  
**优先级**: P0 — 快记账为核心场景

---

## 1. 目标与边界

### 1.1 目标

- 手机端快速记录日常收支和交易（核心目标：< 3 秒完成一次记账）
- 完全离线可用，网络恢复后自动同步
- 复用现有 Web 后端 API 和 90%+ 前端代码

### 1.2 不在范围内（本次）

- 推送通知（下次迭代）
- 生物识别登录（下次迭代）
- 多币种实时汇率换算（下次迭代）
- iOS App Store / Android Play Store 上架流程（部署阶段处理）

---

## 2. 整体架构

```
frontend/                              现有 Web 代码（完整复用）
├── src/
│   ├── api/                           ← 不变
│   ├── stores/                        ← auth, category 不变
│   ├── views/                         ← 桌面视图不变
│   │
│   ├── main-mobile.ts                 ← 新增：移动端入口
│   ├── router/mobile.ts               ← 新增：移动端路由
│   │
│   ├── db/local.ts                    ← 新增：sql.js 本地 SQLite 封装
│   ├── store/sync.ts                  ← 新增：离线同步 Pinia store
│   └── utils/offline-http.ts          ← 新增：网络失败时写入 sync queue
│
│   ├── views-mobile/                  ← 新增：移动端专属视图
│   │   ├── MobileLayout.vue           # 底部 Tab 容器
│   │   ├── DashboardMobile.vue        # 首页
│   │   ├── DailyExpensesMobile.vue    # 收支列表
│   │   ├── ExpenseQuickSheet.vue      # 快记账浮窗
│   │   ├── TransactionsMobile.vue     # 交易记录
│   │   ├── AssetsMobile.vue           # 资产列表
│   │   └── ReportsMobile.vue          # 报表
│
├── capacitor.config.ts                ← 新增
├── vite.config.mobile.ts              ← 新增：mobile build target
└── index.html                         ← 已有
```

**同步链路**：

```
用户操作
  ↓
offline-http.ts
  ├── 网络可达 → 直连服务端 API
  └── 网络失败 → 写本地 SQLite + 入 sync_queue
                     ↓
              网络恢复（app 启动 / 页面聚焦 / 定时器）
                     ↓
              sync.ts：pushLocal() → pullRemote() → 冲突解决
```

---

## 3. 移动端导航

### 3.1 底部 Tab（5 个）

| Tab | 图标 | 路由 | 内容 |
|-----|------|------|------|
| 首页 | `DataAnalysis` | `/m/dashboard` | 净资产卡片 + 本月收支概览 + 最近 5 条记录 |
| 记账 | `Plus`（FAB 样式） | `/m/expenses` | 收支列表（卡片式）+ 底部悬浮"+"按钮 |
| 资产 | `Wallet` | `/m/assets` | 资产卡片列表 |
| 报表 | `PieChart` | `/m/reports` | ECharts 图表（复用现有组件） |
| 我的 | `Setting` | `/m/settings` | 分类管理、密码修改、导出数据 |

### 3.2 快记账交互（核心场景）

点击"记账"Tab 的 FAB → 弹出 `ExpenseQuickSheet`（底部半屏 Drawer）：

```
┌─────────────────────────┐
│  [支出] [收入] [交易]   │  ← 顶部类型切换（Radio Button 风格）
├─────────────────────────┤
│  金额                    │
│  ¥  128.00              │  ← 超大字体，直接数字键盘
├─────────────────────────┤
│  分类  🍜 餐饮美食       │  ← 平铺两级：类型 → 名称
│  资产  💵 现金账户       │
│  日期  2026-08-13        │  ← 默认今天，可改
│  备注  （可选）          │
├─────────────────────────┤
│  [  保存  ]             │  ← 主操作按钮，大且醒目
└─────────────────────────┘
```

**优化点**：
- 金额输入优先，打开即用数字键盘（`inputmode="decimal"`）
- 分类和资产使用搜索+选择（复用现有 `el-select` + `filterable`）
- 保存成功后自动关闭，显示 Toast "已记录 ¥128.00"
- 如果离线，保存到本地 SQLite，顶部状态栏显示灰色圆点"离线"

---

## 4. 离线存储设计

### 4.1 本地 Schema（sql.js，与后端 SQLite 完全一致）

```sql
-- 核心业务表（与服务端 migration.sql 保持一致）
CREATE TABLE IF NOT EXISTS daily_expenses (...);
CREATE TABLE IF NOT EXISTS transactions (...);
CREATE TABLE IF NOT EXISTS assets (...);
CREATE TABLE IF NOT EXISTS categories (...);
CREATE TABLE IF NOT EXISTS tags (...);
CREATE TABLE IF NOT EXISTS expense_tags (...);

-- 同步队列（软删除标记：operation='delete' 时不物理删除本地记录）
CREATE TABLE IF NOT EXISTS sync_queue (
  id            INTEGER PRIMARY KEY AUTOINCREMENT,
  table_name    TEXT NOT NULL,
  record_id     INTEGER NOT NULL,
  operation     TEXT NOT NULL CHECK(operation IN ('create','update','delete')),
  payload       TEXT NOT NULL,   -- JSON 完整记录
  local_version TEXT NOT NULL,   -- ISO 8601 时间戳
  synced        INTEGER DEFAULT 0,
  conflict      INTEGER DEFAULT 0
);

-- 同步元数据
CREATE TABLE IF NOT EXISTS sync_meta (
  key   TEXT PRIMARY KEY,
  value TEXT
);
-- key='last_sync_at' 用于增量拉取
```

### 4.2 写操作路径

```typescript
// offline-http.ts 拦截器
async function request(config) {
  try {
    const res = await axios(config)
    return unwrapEnvelope(res.data)
  } catch (err) {
    // 网络失败 → 写入本地 + 入队
    await localDb.exec(config)           // 本地 SQLite 执行相同 SQL
    await syncQueue.enqueue(config)
    return fallbackResponse(err)         // 返回成功状态，UI 不报错
  }
}
```

### 4.3 同步触发时机

| 时机 | 动作 |
|------|------|
| App 启动 | 全量同步（pull + push） |
| 页面从后台切回前台 | 增量同步（pull 有变更的） |
| 网络状态变化（online） | 立即触发 push |
| 每次写操作后 | 静默入队，下次触发时批量推送 |

---

## 5. 同步协议

### 5.1 Push（本地 → 服务端）

对 `sync_queue` 中每条 `synced=0` 记录：

```
CREATE → POST  /api/daily-expenses   （同 payload）
UPDATE → PUT   /api/daily-expenses/:id（同 payload）
DELETE → DELETE /api/daily-expenses/:id
```

服务端返回 `code=0` → `synced=1`  
服务端返回业务错误（如资源不存在）→ 标记 `conflict=1`，记录错误信息

### 5.2 Pull（服务端 → 本地）

```
GET /api/daily-expenses?start_date=2026-08-01&page_size=500
GET /api/transactions?start_date=2026-08-01&page_size=500
...（所有业务表）
```

对每条服务端记录：
- `updated_at > local_record.updated_at` → 服务端胜出，更新本地
- `updated_at <= local_record.updated_at` → 保留本地（本地是更新后的版本）
- 服务端有但本地没有 → INSERT
- 本地有但服务端没有（已删除）→ 检查 sync_queue 是否有 delete 记录：
  - 有且 synced=1 → 物理删除本地记录
  - 有但未 synced → 保留本地（等待 push 完成）
  - 无 → 保留本地（防御性）

### 5.3 冲突处理（个人使用场景）

**策略：服务端胜出，UI 提示**

```
if (server.updated_at > local.updated_at):
  apply server version
  show Toast: "有一条本地修改已同步到服务端"
else:
  keep local
```

不涉及多人协作，不做"用户手动选择"的冲突解决 UI。

### 5.4 软删除标记

DELETE 操作不立即删除本地记录，而是打上软删除标记：

```
operation = 'delete' 时：
  1. 本地记录 updated_at 设为 NULL（标记为待删除）
  2. 写入 sync_queue（operation='delete'）
  3. 列表查询时过滤掉 updated_at=NULL 的记录（本地只读视图）
  4. 同步成功后才真正从本地 SQLite 物理删除
```

**好处**：
- 同步失败时用户可恢复（重新打开 sync_queue 中该记录即可）
- 避免"本地已删、服务端还在"的两边数据不一致
- 与后端 `asset_balance_logs` append-only 设计哲学一致

---

## 6. 移动端视图说明

### 6.1 DashboardMobile.vue

顶部展示 3 张 KPI 卡片（横向滚动）：
- 总资产（青色）
- 总负债（红色）  
- 净资产（绿色，高亮）

中部：本月收支简况（收入 / 支出 / 结余三列）

底部：最近 5 条收支记录（卡片列表，与 DailyExpensesMobile 相同样式）

### 6.2 DailyExpensesMobile.vue

卡片列表（替代桌面版表格）：

```
┌──────────────────────────────┐
│ 08-13  周五                   │
│ 🍜 餐饮美食                   │
│  现金账户   -¥128.00          │
│  午餐 + 咖啡                  │
└──────────────────────────────┘
┌──────────────────────────────┐
│ 08-12  周四                   │
│ 💼 基本工资                   │
│  工资卡   +¥8,500.00         │
└──────────────────────────────┘
```

每卡片右侧：编辑（滑出或点击）、删除（长按确认）

支持上拉加载更多（分页）

### 6.3 ExpenseQuickSheet.vue

即 §3.2 描述的快记账浮窗，用 `el-drawer direction="btt"` 实现。

### 6.4 TransactionsMobile.vue / AssetsMobile.vue / ReportsMobile.vue

- 结构类似 DailyExpensesMobile
- ECharts 图表组件（`MonthlyChart.vue`、`ExpenseCategoryPie.vue`、`NetWorthChart.vue`）直接复用，全宽展示
- 报表页顶部加月份选择器（`el-date-picker type="month"`）

---

## 7. Capacitor 配置

```typescript
// capacitor.config.ts
{
  appId: 'com.minefolio.app',
  appName: 'Minefolio',
  webDir: 'dist-mobile',
  server: {
    androidScheme: 'https',
    iosScheme: 'app'
  },
  plugins: {
    SplashScreen: { launchShowDuration: 500 },
    Keyboard: { resize: 'body' }
  }
}
```

**Required Capacitor Plugins**：
- `@capacitor/network` — 监听网络状态变化
- `@capacitor/app` — 监听 app 生命周期（前台/后台切换）
- `@capacitor/haptics` — 记账成功时触觉反馈
- `sql.js`（Wasm） — 本地 SQLite（非 Capacitor plugin，直接 npm 安装）

---

## 8. 构建流程

```bash
# 现有 Web build（不变）
npm run build          # → frontend/dist/

# 新增 mobile build
npm run build:mobile   # → frontend/dist-mobile/
#   使用 vite.config.mobile.ts（mode: 'mobile'，outputDir: dist-mobile）

# 同步到原生容器
npx cap sync

# 运行
npx cap open android   # 或 ios
```

`vite.config.mobile.ts` 与现有 `vite.config.ts` 的差异：
- `define: { __MOBILE__: true }` — 供代码区分 mobile/desktop
- CSS 增加 mobile-specific 覆盖（480px 以下断点）
- 移除桌面版特有样式（侧边栏等）

---

## 9. 文件改动矩阵

| 文件 | 操作 | 说明 |
|------|------|------|
| `frontend/capacitor.config.ts` | 新增 | Capacitor 容器配置 |
| `frontend/vite.config.mobile.ts` | 新增 | mobile build target |
| `frontend/src/main-mobile.ts` | 新增 | 移动端入口 |
| `frontend/src/router/mobile.ts` | 新增 | 移动端路由 |
| `frontend/src/db/local.ts` | 新增 | sql.js 封装 |
| `frontend/src/store/sync.ts` | 新增 | 同步状态管理 |
| `frontend/src/utils/offline-http.ts` | 新增 | 离线 HTTP 包装 |
| `frontend/src/views-mobile/MobileLayout.vue` | 新增 | 底部 Tab 容器 |
| `frontend/src/views-mobile/DashboardMobile.vue` | 新增 | 首页 |
| `frontend/src/views-mobile/DailyExpensesMobile.vue` | 新增 | 收支列表 |
| `frontend/src/views-mobile/ExpenseQuickSheet.vue` | 新增 | 快记账浮窗 |
| `frontend/src/views-mobile/TransactionsMobile.vue` | 新增 | 交易记录 |
| `frontend/src/views-mobile/AssetsMobile.vue` | 新增 | 资产列表 |
| `frontend/src/views-mobile/ReportsMobile.vue` | 新增 | 报表 |
| `frontend/src/utils/http.ts` | 不改 | 完全复用 |
| `frontend/src/api/*.ts` | 不改 | 完全复用 |
| `frontend/src/views/*.vue` | 不改 | 桌面视图不变 |
| `package.json` | 修改 | 新增 `build:mobile` script + capacitor/sql.js 依赖 |

---

## 10. 验收标准

- [ ] 无网络时打开 App，可正常查看已同步数据
- [ ] 无网络时记账，保存成功（Toast 提示"已离线保存"）
- [ ] 恢复网络后，离线记录自动同步到服务端（5 秒内）
- [ ] 手机端快记账一次操作 < 3 秒（从打开到保存）
- [ ] 无网络时删除记录，列表立即隐藏（软标记），恢复网络后自动同步删除
- [ ] 删除后网络断开，重新联网可恢复被软删的记录（通过 sync_queue 逆向操作）
- [ ] `npm run build:mobile && npm --prefix frontend run build` 均通过（0 TypeScript 错误）
- [ ] 现有 21 项集成测试全部 PASS（后端未改动）
