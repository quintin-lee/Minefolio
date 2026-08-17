# Minefolio 项目全景分析报告

> 生成日期：2026-08-17  
> 分析范围：全栈（后端 C + 前端 Vue 3）

---

## 一、项目概览

Minefolio 是一个**个人综合资产管理系统**，支持多币种、多账户、投资持仓追踪、日常收支管理，以及完整的报表分析功能。项目当前状态：**核心功能完备，测试通过，UI 持续迭代中**。

### 1.1 技术栈总览

| 层级 | 技术选型 | 版本/框架 |
|------|----------|-----------|
| 后端 | C23 + csilk | HTTP 框架 + 依赖注入 |
| 数据库 | SQLite（主）/ PostgreSQL（备选） | SQL 迁移驱动 |
| 认证 | JWT (HS256) + RSA-OAEP 加密传输 | bcrypt 存储（cost=12） |
| 前端 | Vue 3 + TypeScript | Vite 5, Pinia, Element Plus |
| 图表 | ECharts 5 | 多种定制组件 |
| 移动端 | Capacitor 6 | Android APK 构建 |
| 部署 | Docker Compose + nginx | 多阶段构建 |

### 1.2 代码规模

```
后端 C 代码：     约 5,448 行（12 个 .c 文件）
前端 Vue 代码：   约 9,401 行（12 个视图 + 15 个组件）
移动端 Vue 代码： 约 1,859 行（9 个视图）
数据库迁移：      132 行 SQL，9 张表
设计文档：        14 份 spec + 13 份 plan
集成测试：        103 PASS（test_link.sh）
Git 提交历史：    261 次提交
```

### 1.3 核心业务领域

```
┌─────────────────────────────────────────────────────┐
│                   Minefolio 业务域                    │
├──────────────┬──────────────┬────────────────────────┤
│   账户体系    │   分类体系    │      交易引擎          │
│  - 用户注册   │  - 四类分类   │  - 日常收支            │
│  - JWT 认证  │  - 树形结构  │  - 投资买卖（stock/     │
│  - RSA 加密  │  - 资产类型   │    fund/bond/crypto） │
│              │  - 负债方向   │  - 转账               │
│              │    反转       │  - CSV 导入导出        │
├──────────────┼──────────────┼────────────────────────┤
│   资产持仓    │   报表分析    │      系统集成          │
│  - 多币种     │  - 月度收支   │  - Docker 部署        │
│  - 净值追踪   │  - 支出分类   │  - 移动端适配         │
│  - 盈亏计算   │  - 资产分布   │  - 审计日志           │
│  - 成本基准   │  - 趋势图表   │  - 标签系统           │
└──────────────┴──────────────┴────────────────────────┘
```

---

## 二、架构分析

### 2.1 后端架构

```
main.c (路由注册 + 中间件)
  │
  ├── JWT 中间件（排除公开端点）
  ├── CORS 中间件
  └── CSRF 中间件（可选）
       │
       ├── auth.c        → /api/auth/*
       ├── categories.c   → /api/categories/*
       ├── assets.c       → /api/assets/*
       ├── transactions.c → /api/transactions/*
       ├── daily_expenses.c → /api/daily-expenses/*
       ├── tags.c         → /api/tags/*
       ├── transfers.c    → /api/transfers
       ├── reports.c      → /api/reports/*
       ├── import_export.c → /api/import/* /api/export/*
       └── common/
           ├── db.h/.c    → 连接池 + 迁移
           ├── balance.h/.c → 余额计算核心
           ├── tx_types.h/.c → 交易类型注册表
           ├── jwt.h/.c   → JWT 工具
           └── response.h → 统一响应宏
```

**关键设计亮点：**
- **Transaction Type Registry**：`tx_types.c` 定义所有交易类型与余额方向的映射，避免硬编码
- **Balance Direction Flip**：`balance_apply_delta()` 自动反转负债类资产方向，确保净值计算正确
- **Position Tracking**：`apply_position()` 处理投资类资产的买卖、手续费、成本基准计算
- **审计日志**：`asset_balance_logs` 表记录所有余额变更，支持追溯

### 2.2 前端架构

```
App.vue
  │
  ├── Router (vue-router)
  │   ├── /login, /setup（公开路由）
  │   └── /（需要认证）
  │       ├── Dashboard    → 资产总览 + 年度收支趋势
  │       ├── Assets       → 资产列表 + CRUD
  │       ├── Holdings     → 投资持仓 + PnL
  │       ├── Transactions → 交易记录 + 筛选
  │       ├── DailyExpenses → 日常收支 + 标签
  │       ├── Categories   → 分类管理（树形）
  │       ├── Reports      → 多维报表
  │       ├── AuditLogs    → 操作审计
  │       └── Settings     → 系统设置
  │
  ├── Stores (Pinia)
  │   ├── auth.ts       → 用户状态 + JWT
  │   ├── category.ts   → 分类缓存 + 树构建
  │   └── sync.ts       → 网络同步（离线优先）
  │
  ├── API Layer
  │   └── 每个 domain 一个 api/*.ts 文件
  │
  └── Components
      ├── ECharts 包装组件（8 个）
      ├── 通用组件（TagPicker, AssetCard 等）
      └── Mobile 专属视图（9 个）
```

### 2.3 数据库 Schema（9 张表）

| 表名 | 用途 | 关键字段 |
|------|------|----------|
| `users` | 用户认证 | username, password (bcrypt) |
| `categories` | 四类型分类树 | type, asset_type, parent_id |
| `assets` | 资产持仓 | current_value, quantity, cost_basis |
| `transactions` | 投资交易记录 | type, amount, fee, direction |
| `daily_expenses` | 日常收支 | expense_type, amount, date |
| `transfers` | 资产间转账 | from_asset_id, to_asset_id |
| `tags` / `expense_tags` | 标签系统 | 多对多关联 |
| `asset_balance_logs` | 余额审计日志 | delta, balance_after, source |
| `category_seed_state` | 分类种子状态 | 懒加载标记 |

---

## 三、代码质量分析

### 3.1 优点

**1. 数据库安全（大部分合规）**
- 153 处数据库查询，146 处使用 `csilk_db_query_param_json()` 参数化查询
- 响应统一使用 `respond_ok/respond_error` 宏，符合规范
- 事务处理规范：`BEGIN/COMMIT/ROLLBACK` 在 6 个文件中正确实现

**2. 前端 API 层封装良好**
- 所有 API 调用集中在 `api/*.ts`，组件不直接调用 fetch
- `http.ts` 统一处理 JWT token、CSRF header、错误解包
- TypeScript 接口完整，类型安全

**3. 测试覆盖**
- 103 个集成测试全部通过
- 覆盖认证、分类、资产、交易、日常收支、报表全链路

**4. 设计文档完善**
- 14 份 spec + 13 份 plan，形成完整的设计追溯链
- 每次功能迭代都有对应文档

**5. 多数据库支持**
- 同时支持 SQLite 和 PostgreSQL（通过环境变量切换）

### 3.2 需要关注的问题

#### 问题 1：JSON 数值解析不一致（中等风险）

**位置：** `categories.c:372-373`, `daily_expenses.c:328-331`

**现状：** 部分请求体仍直接使用 `csilk_json_get_number()`，违反了 `db.h` 中 `db_get_num()` 的设计意图。

```c
// categories.c:372 - 不安全
int64_t parent_id = (int64_t)csilk_json_get_number(body, "parent_id");
int sort_order = (int)csilk_json_get_number(body, "sort_order");

// daily_expenses.c:328 - 不安全  
int64_t category_id = (int64_t)csilk_json_get_number(body, "category_id");
double amount = csilk_json_get_number(body, "amount");
```

**风险：** 当客户端发送字符串数字（如 `"parent_id": "123"`）时，`csilk_json_get_number()` 返回 0.0，导致静默数据错误。

**建议：** 统一到 `db_get_num()` / `db_get_int()`，或在请求解析层增加类型检查。

---

#### 问题 2：`snprintf` SQL 拼接（低风险，需确认合规）

**位置：** 39 处 `snprintf` SQL 拼接

**现状：** 大量使用 `snprintf` 构建 SQL 语句，虽然参数来自内部格式化（非用户输入），但增加了维护复杂度。

```c
// assets.c:36 - 合法但可优化
snprintf(sql, sizeof(sql),
    "SELECT id, name, category_id, account_no, current_value, currency, note,"
    " quantity, cost_basis, net_value FROM assets"
    " WHERE user_id = %lld ORDER BY sort_order",
    (long long)user_id);
```

**建议：** 对于参数化查询部分，改用 `csilk_db_query_param_json()`；对于纯字面量拼接，保持现状即可。

---

#### 问题 3：`reports.c` 代码膨胀（低风险）

**现状：** `reports.c` 796 行，是第二大后端文件，包含多种报表逻辑。

**建议：** 考虑按报表类型拆分（如 `reports_expense.c`, `reports_asset.c`），或提取报表查询为公共辅助函数。

---

#### 问题 4：移动端视图不完整（中风险）

**现状：** 已有 9 个移动端视图，但缺少：
- `HoldingsMobile.vue`
- `AssetsMobile.vue`（仅 27 行，几乎为空）
- `TransactionsMobile.vue`（仅 31 行）

**建议：** 优先补全核心交易和持仓的移动端体验。

---

#### 问题 5：前端 `console.log` 残留（低风险）

**发现：** 视图中有 5 处 `console.` 调用，可能是调试残留。

```
frontend/src/views/Layout.vue: 2 处
frontend/src/views/Dashboard.vue: 1 处
frontend/src/views/Transactions.vue: 2 处
```

**建议：** 清理生产环境的 console 输出。

---

#### 问题 6：CSRF 默认关闭（安全风险）

**现状：** CSRF 保护通过 `MINEFOLIO_ENABLE_CSRF` 环境变量启用，默认关闭。

**建议：** 生产环境必须启用；开发环境保持关闭以避免跨域调试问题。

---

## 四、模块复杂度分析

### 4.1 后端文件复杂度

| 文件 | 行数 | 主要职责 | 复杂度 |
|------|------|----------|--------|
| `reports.c` | 796 | 7 种报表查询 | ⭐⭐⭐⭐ |
| `import_export.c` | 629 | CSV 导入导出 | ⭐⭐⭐ |
| `transactions.c` | 611 | 交易 CRUD + 投资逻辑 | ⭐⭐⭐⭐ |
| `daily_expenses.c` | 544 | 收支 CRUD + 月度汇总 | ⭐⭐⭐ |
| `categories.c` | 501 | 分类树 CRUD | ⭐⭐⭐ |
| `auth.c` | 403 | 认证 + RSA 加密 | ⭐⭐ |
| `assets.c` | 366 | 资产 CRUD | ⭐⭐ |
| `main.c` | 279 | 路由注册 + 中间件 | ⭐⭐ |

### 4.2 前端视图复杂度

| 视图 | 行数 | 主要功能 | 复杂度 |
|------|------|----------|--------|
| `Transactions.vue` | 981 | 交易列表 + 筛选 + 导入导出 | ⭐⭐⭐⭐ |
| `Categories.vue` | 826 | 分类树编辑 + 批量操作 | ⭐⭐⭐⭐ |
| `DailyExpenses.vue` | 694 | 收支记录 + 标签 + 月度视图 | ⭐⭐⭐ |
| `Reports.vue` | 597 | 多图表联动报表 | ⭐⭐⭐ |
| `Assets.vue` | 504 | 资产 CRUD + 净值更新 | ⭐⭐ |
| `Login.vue` | 493 | 登录 + RSA 加密 | ⭐⭐ |
| `Holdings.vue` | 297 | 投资持仓 + PnL 展示 | ⭐⭐ |
| `Dashboard.vue` | 308 | 首页总览 + sparkline | ⭐⭐ |

---

## 五、关键业务流程分析

### 5.1 日常收支 → 资产余额联动

```
用户创建收支记录
    ↓
POST /api/daily-expenses
    ↓
daily_expenses_create()
    ├── INSERT daily_expenses
    ├── balance_apply_delta()
    │   ├── 读取资产类型
    │   ├── 根据 asset_type 确定方向
    │   │   ├── 普通资产: delta = +amount（收入）或 -amount（支出）
    │   │   └── 负债类: delta = -amount（收入）或 +amount（支出）
    │   ├── UPDATE assets.current_value
    │   └── INSERT asset_balance_logs
    └── RETURN 200
```

### 5.2 投资交易 → 持仓 + 余额联动

```
用户创建买卖交易
    ↓
POST /api/transactions
    ↓
transactions_create()
    ├── BEGIN TRANSACTION
    ├── apply_position()
    │   ├── 更新 assets.quantity / cost_basis / net_value
    │   └── 返回 position_delta
    ├── balance_apply_delta()
    │   ├── 扣减资金账户余额
    │   └── 更新资金资产 current_value
    ├── （fee > 0）INSERT fee 行（raw SQL）
    ├── （buy/sell）INSERT transaction
    ├── COMMIT
    └── RETURN 200
```

### 5.3 报表计算逻辑

```
GET /api/reports/asset/breakdown
    ↓
reports.c → report_asset_breakdown()
    ├── SELECT 投资类资产 SUM(current_value)
    ├── SELECT 负债类资产 SUM(current_value)
    ├── 计算净资产 = 资产 - 负债
    └── 按分类聚合返回
```

---

## 六、项目健康度评分

| 维度 | 评分 | 说明 |
|------|------|------|
| **代码规范** | ⭐⭐⭐⭐ | 响应宏、事务处理、参数化查询整体合规 |
| **测试覆盖** | ⭐⭐⭐⭐ | 103 集成测试覆盖核心链路 |
| **文档完整性** | ⭐⭐⭐⭐⭐ | 14 spec + 13 plan，追溯链完整 |
| **类型安全** | ⭐⭐⭐⭐⭐ | 完整 TypeScript 接口定义 |
| **安全性** | ⭐⭐⭐ | CSRF 默认关闭，需注意生产配置 |
| **可维护性** | ⭐⭐⭐⭐ | 文件拆分合理，reports.c 略大 |
| **移动端支持** | ⭐⭐⭐ | 核心页面已适配，持仓页缺失 |

**综合评分：⭐⭐⭐⭐（4/5）— 成熟可用的个人财务管理应用**

---

## 七、建议改进优先级

### P0（立即处理）
1. **修复 JSON 数值解析不一致** — 将 `csilk_json_get_number()` 替换为 `db_get_num()` / `db_get_int()`
2. **清理 console.log** — 5 处调试输出移除

### P1（近期处理）
3. **补齐移动端持仓页** — `HoldingsMobile.vue`
4. **启用生产 CSRF** — 更新 Docker 默认配置

### P2（规划处理）
5. **拆分 reports.c** — 按报表类型分解为大文件
6. **增加单元测试** — 当前只有集成测试，核心逻辑（balance.c）缺乏单测覆盖
7. **性能监控** — 添加慢查询日志、API 响应时间统计

---

## 八、依赖关系图

```
                    ┌──────────────┐
                    │   frontend   │
                    │  Vue 3 + TS  │
                    └──────┬───────┘
                           │ HTTP
                    ┌──────▼───────┐
                    │   nginx      │
                    │  :80 / :443  │
                    └──────┬───────┘
                           │ proxy
                    ┌──────▼───────┐
                    │  backend C   │
                    │  csilk + :8080│
                    └──────┬───────┘
                           │
              ┌────────────┼────────────┐
              ▼            ▼            ▼
        ┌──────────┐  ┌──────────┐  ┌──────────┐
        │ SQLite   │  │ Postgres │  │  RSA Key │
        │  (default)│  │ (option) │  │  (env)   │
        └──────────┘  └──────────┘  └──────────┘
```

---

## 九、待解决的问题清单

| 编号 | 问题 | 优先级 | 涉及文件 |
|------|------|--------|----------|
| BUG-001 | `csilk_json_get_number` 在请求体中不安全 | P0 | categories.c, daily_expenses.c |
| BUG-002 | 生产环境 console.log 残留 | P0 | Layout.vue, Dashboard.vue, Transactions.vue |
| FEAT-001 | 移动端持仓页缺失 | P1 | 需新建 HoldingsMobile.vue |
| SEC-001 | CSRF 默认关闭 | P1 | main.c, Dockerfile |
| PERF-001 | reports.c 单文件过大 | P2 | reports.c |
| TEST-001 | 缺少后端单元测试 | P2 | balance.c, tx_types.c |

---

## 十、结论

Minefolio 是一个**架构清晰、文档完整、测试覆盖良好**的个人资产管理系统。核心业务逻辑（余额联动、投资交易、报表计算）实现正确，技术债务较少。

**主要优势：**
- 设计文档体系完善，每次变更有据可查
- 数据库安全实践到位（参数化查询、事务处理）
- 前后端类型安全，代码组织清晰

**主要改进空间：**
- 修复少数 JSON 解析不一致问题
- 补齐移动端核心页面
- 增加后端单元测试覆盖

**总体评价：项目处于良好可维护状态，适合继续迭代开发。**
