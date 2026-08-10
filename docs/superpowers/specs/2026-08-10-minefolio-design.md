# Minefolio — 个人资产管理系统设计文档

## 1. 概述

Minefolio 是一个前后端分离的个人综合资产管理系统，帮助用户追踪和管理金融资产（股票、基金、债券、加密货币等）、实物资产（房产、车辆等）以及负债（贷款、信用卡等）。

**技术栈：**
- 前端：Vue 3 + Vite + TypeScript + Element Plus
- 后端：csilk C HTTP 框架 + SQLite
- 认证：JWT (HS256)，secret 从环境变量读取
- 部署：nginx 反向代理（开发期 Vite proxy）

---

## 2. 系统架构

```
┌─────────────────────────────────────────────────────┐
│                    浏览器 (Vue)                     │
│  Login │ Dashboard │ Assets │ Categories │ History  │
└─────────────────────────────────────────────────────┘
                           │ HTTP/JSON
                           ▼
┌─────────────────────────────────────────────────────┐
│               Nginx (Production)                     │
│  /api/* → proxy_pass http://127.0.0.1:8080         │
│  /*         → serve static/                          │
└─────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────┐
│               csilk Server (Port 8080)               │
│  JWT Auth │ CSRF │ CORS │ Logger │ Recovery         │
│  ┌───────────────────────────────────────────────┐  │
│  │  /api/auth/*     → Login / Refresh            │  │
│  │  /api/categories → CRUD + Tree                │  │
│  │  /api/assets/*   → CRUD + Summary             │  │
│  │  /api/transactions → CRUD + Filter            │  │
│  │  /api/summary    → Net worth + breakdown      │  │
│  └───────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────┐
│                  SQLite (minefolio.db)               │
└─────────────────────────────────────────────────────┘
```

**开发环境：** Vite 开发服务器 (`:5173`) + csilk (`:8080`)，Vite `proxy` 配置将 `/api` 转发至 8080。

---

## 3. 数据库设计 (SQLite)

### 3.1 用户表

```sql
CREATE TABLE users (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    username   TEXT UNIQUE NOT NULL,
    password   TEXT NOT NULL,          -- bcrypt hash
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
-- 注：JWT secret 从环境变量读取，不存储在 DB 中。
```

### 3.2 资产分类表（支持嵌套）

```sql
CREATE TABLE categories (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id     INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    name        TEXT NOT NULL,
    parent_id   INTEGER REFERENCES categories(id) ON DELETE SET NULL,
    asset_type  TEXT NOT NULL CHECK(asset_type IN
        ('cash','stock','fund','bond','crypto',
         'real_estate','vehicle','other_asset',
         'loan','credit_card','other_liability')),
    currency    TEXT DEFAULT 'CNY',
    icon        TEXT,
    sort_order  INTEGER DEFAULT 0,
    created_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(user_id, name, parent_id)
);
```

**说明：**
- `parent_id` 为 NULL 表示一级分类，非 NULL 表示子类。
- `asset_type` 预置常用类型，用户可通过 `name` 自定义细分（如"A股"、"美股"）。
- 删除父分类时，子类 `parent_id` 置 NULL（变为独立一级分类）。

### 3.3 资产账户表

```sql
CREATE TABLE assets (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id       INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    category_id   INTEGER NOT NULL REFERENCES categories(id) ON DELETE RESTRICT,
    name          TEXT NOT NULL,
    account_no    TEXT,               -- 账户编号/卡号/基金代码等
    current_value DECIMAL(18,2) DEFAULT 0,
    currency      TEXT DEFAULT 'CNY',
    note          TEXT,
    created_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

### 3.4 交易记录表

```sql
CREATE TABLE transactions (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id          INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    asset_id         INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    category_id      INTEGER NOT NULL REFERENCES categories(id) ON DELETE RESTRICT,
    transaction_type TEXT NOT NULL CHECK(transaction_type IN
        ('deposit','withdrawal','buy','sell',
         'transfer_in','transfer_out','fee',
         'income','loss')),
    amount           DECIMAL(18,2) NOT NULL,
    price_per_unit   DECIMAL(18,4),  -- 单价（买/卖时使用）
    quantity         DECIMAL(18,4),  -- 数量（买/卖时使用）
    currency         TEXT DEFAULT 'CNY',
    transaction_date TIMESTAMP NOT NULL,
    note             TEXT,
    created_at       TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

### 3.5 转账关联表（资产间转账）

```sql
CREATE TABLE transfers (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id       INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    from_asset_id INTEGER NOT NULL REFERENCES assets(id) ON DELETE RESTRICT,
    to_asset_id   INTEGER NOT NULL REFERENCES assets(id) ON DELETE RESTRICT,
    amount        DECIMAL(18,2) NOT NULL,
    currency      TEXT DEFAULT 'CNY',
    transfer_date TIMESTAMP NOT NULL,
    note          TEXT,
    created_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

### 3.6 标签表（多对多关联）

```sql
CREATE TABLE tags (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id     INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    name        TEXT NOT NULL,
    color       TEXT DEFAULT '',     -- HEX color code for UI display
    created_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(user_id, name)
);

CREATE TABLE expense_tags (
    expense_id  INTEGER NOT NULL REFERENCES daily_expenses(id) ON DELETE CASCADE,
    tag_id      INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE,
    PRIMARY KEY (expense_id, tag_id)
);

CREATE INDEX idx_tags_user ON tags(user_id);
CREATE INDEX idx_expense_tags_tag ON expense_tags(tag_id);
```

### 3.7 日常收支表（收入/支出记账）

```sql
CREATE TABLE daily_expenses (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id       INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    category_id   INTEGER NOT NULL REFERENCES categories(id) ON DELETE RESTRICT,
    expense_type  TEXT NOT NULL CHECK(expense_type IN ('expense','income')),
    amount        DECIMAL(18,2) NOT NULL,
    currency      TEXT DEFAULT 'CNY',
    expense_date  DATE NOT NULL,
    note          TEXT,
    created_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- 索引：按日期和类型查询
CREATE INDEX idx_daily_expenses_date ON daily_expenses(expense_date);
CREATE INDEX idx_daily_expenses_type ON daily_expenses(expense_type);
CREATE INDEX idx_daily_expenses_cat ON daily_expenses(category_id);
```

**说明：**
- `expense_type = 'expense'` 表示支出（日常消费）
- `expense_type = 'income'` 表示收入（工资、奖金等）
- 与 `categories` 表关联，用户可为收支项目复用同一分类体系
- 标签通过 `tags` + `expense_tags` 多对多表管理，支持颜色标签和按标签筛选
- 支持按月/按分类/按标签汇总，用于月度收支报表

---

## 4. API 设计

### 4.1 统一响应格式

**成功响应：**
```json
{ "code": 0, "message": "ok", "data": <payload> }
```

**错误响应：**
```json
{ "code": <errno>, "message": "<human-readable error>" }
```

**常见 code 枚举：**
| code | 含义 |
|------|------|
| 0 | 成功 |
| 1001 | 未授权（JWT 无效/过期） |
| 1002 | 参数错误 |
| 1003 | 资源不存在 |
| 1004 | 资源已存在（如重复用户名） |
| 2001 | 分类有子分类，无法删除 |

### 4.2 认证模块

| 方法 | 路径 | 说明 | 认证 |
|------|------|------|------|
| POST | `/api/auth/register` | 注册（仅首次用户，后续返回已存在） | 否 |
| POST | `/api/auth/login` | 登录，返回 `{ token, expires_in }` | 否 |
| GET  | `/api/auth/me` | 获取当前用户信息 | 是 |

**JWT Payload：** `{ sub: "<user_id>", iat: <ts>, exp: <ts> }`

**登录/注册响应：**
```json
{ "code": 0, "data": { "token": "<jwt>", "expires_in": 604800 } }
```

### 4.3 分类模块

| 方法 | 路径 | 说明 | 认证 |
|------|------|------|------|
| GET    | `/api/categories` | 返回树形分类列表 | 是 |
| POST   | `/api/categories` | 创建分类（支持 parent_id 创建子类） | 是 |
| PUT    | `/api/categories/:id` | 更新分类名称/icon 等 | 是 |
| DELETE | `/api/categories/:id` | 删除分类（有子类时返回 2001） | 是 |

**GET /api/categories 响应（递归树形）：**
```json
{
  "code": 0,
  "data": [
    {
      "id": 1, "name": "金融资产", "asset_type": "stock",
      "parent_id": null, "children": [
        { "id": 4, "name": "A股", "parent_id": 1, "children": [] },
        { "id": 5, "name": "基金", "parent_id": 1, "children": [] }
      ]
    }
  ]
}
```

### 4.4 资产模块

| 方法 | 路径 | 说明 | 认证 |
|------|------|------|------|
| GET    | `/api/assets` | 列表（支持 `category_id`、`currency` 过滤） | 是 |
| POST   | `/api/assets` | 创建资产 | 是 |
| PUT    | `/api/assets/:id` | 更新资产（名称、当前值、备注） | 是 |
| DELETE | `/api/assets/:id` | 删除资产（有关联交易时仍允许删除，交易记录保留） | 是 |
| GET    | `/api/assets/:id` | 资产详情 + 历史交易 | 是 |

### 4.5 交易模块

| 方法 | 路径 | 说明 | 认证 |
|------|------|------|------|
| GET    | `/api/transactions` | 列表（支持 `asset_id`、`category_id`、`type`、`start_date`、`end_date` 过滤） | 是 |
| POST   | `/api/transactions` | 创建交易 | 是 |
| PUT    | `/api/transactions/:id` | 更新交易 | 是 |
| DELETE | `/api/transactions/:id` | 删除交易 | 是 |

**transaction_type 枚举及含义：**
| 值 | 含义 |
|----|------|
| `deposit` | 存入（现金类资产增加） |
| `withdrawal` | 取出（现金类资产减少） |
| `buy` | 买入（股票/基金等，需填 quantity + price_per_unit） |
| `sell` | 卖出（股票/基金等，需填 quantity + price_per_unit） |
| `transfer_in` | 转入（从其他资产转入） |
| `transfer_out` | 转出（转出到其他资产） |
| `fee` | 手续费 |
| `income` | 收益（股息、利息等） |
| `loss` | 亏损 |

### 4.6 汇总模块

| 方法 | 路径 | 说明 | 认证 |
|------|------|------|------|
| GET | `/api/summary` | 总资产净值 + 分类占比 + 近30天趋势 + 本月收支概览 | 是 |

### 4.7 日常收支模块

| 方法 | 路径 | 说明 | 认证 |
|------|------|------|------|
| GET    | `/api/daily-expenses` | 列表（支持 `expense_type`、`category_id`、`tag_ids`、`start_date`、`end_date` 过滤） | 是 |
| POST   | `/api/daily-expenses` | 创建收支记录 | 是 |
| PUT    | `/api/daily-expenses/:id` | 更新收支记录 | 是 |
| DELETE | `/api/daily-expenses/:id` | 删除收支记录 | 是 |
| GET    | `/api/daily-expenses/monthly` | 月度汇总（按年月，返回收支 totals + 分类 breakdown） | 是 |

### 4.8 标签管理模块

| 方法 | 路径 | 说明 | 认证 |
|------|------|------|------|
| GET    | `/api/tags` | 列出当前用户所有标签 | 是 |
| POST   | `/api/tags` | 创建标签（`{ name, color? }`） | 是 |
| PUT    | `/api/tags/:id` | 更新标签（重命名/改颜色） | 是 |
| DELETE | `/api/tags/:id` | 删除标签（不级联删除关联记录） | 是 |
| GET    | `/api/tags/suggestions` | 标签建议（根据输入前缀模糊匹配，用于输入框自动补全） | 是 |

### 4.9 报表模块

| 方法 | 路径 | 说明 | 认证 |
|------|------|------|------|
| GET | `/api/reports/expense/monthly` | 月度收支报表（可选年月，返回收支明细 + 分类/标签 breakdown） | 是 |
| GET | `/api/reports/expense/trend` | 收支趋势（近 N 月柱状图数据） | 是 |
| GET | `/api/reports/expense/category` | 支出分类占比（饼图数据，支持年月过滤） | 是 |
| GET | `/api/reports/expense/tag` | 标签支出分析（按标签聚合，支持年月过滤） | 是 |
| GET | `/api/reports/asset/trend` | 净资产趋势（近 30/90/365 天，支持自定义范围） | 是 |
| GET | `/api/reports/asset/breakdown` | 资产分布（当前各分类占比） | 是 |
| GET | `/api/reports/transaction/performance` | 交易表现（买/卖记录 + 收益率） | 是 |
| GET | `/api/reports/asset/summary` | 资产总览（当前值 + 变动统计） | 是 |

**月度收支报表响应示例：**
```json
{
  "code": 0,
  "data": {
    "year": 2026, "month": 8,
    "total_income": 15000.00,
    "total_expense": 8200.00,
    "balance": 6800.00,
    "by_category": [
      { "name": "餐饮", "type": "expense", "amount": 3200.00, "pct": 39.0 },
      { "name": "工资", "type": "income",  "amount": 15000.00, "pct": 100.0 }
    ],
    "by_tag": [
      { "tag_name": "聚餐", "amount": 1200.00, "count": 5 },
      { "tag_name": "通勤", "amount": 800.00,  "count": 22 }
    ],
    "daily_breakdown": [
      { "date": "2026-08-01", "income": 15000.00, "expense": 3200.00 },
      { "date": "2026-08-02", "income": 0,         "expense": 450.00 }
    ]
  }
}
```

**收支趋势响应示例：**
```json
{
  "code": 0,
  "data": {
    "labels": ["2026-03","2026-04","2026-05","2026-06","2026-07","2026-08"],
    "income":  [14500, 15200, 14800, 15000, 15500, 15000],
    "expense": [7800,  8200,  7500,  8100,  8400,  8200]
  }
}
```

**资产趋势响应示例：**
```json
{
  "code": 0,
  "data": {
    "period": "30d",
    "labels": ["07-12","07-13","07-14", ... "08-10"],
    "net_worth": [880000, 882000, 879000, ... 900000],
    "assets":     [1250000, 1252000, 1248000, ... 1250000],
    "liabilities":[350000,  350000,  350000,  ... 350000]
  }
}
```

**交易表现响应示例：**
```json
{
  "code": 0,
  "data": {
    "total_trades": 24,
    "total_gain": 12500.00,
    "total_loss": 3200.00,
    "net_gain": 9300.00,
    "trades": [
      {
        "id": 1, "asset_name": "贵州茅台", "type": "buy",
        "date": "2026-07-15", "quantity": 100, "price": 1750.00, "amount": 175000.00
      },
      {
        "id": 2, "asset_name": "贵州茅台", "type": "sell",
        "date": "2026-08-05", "quantity": 50, "price": 1820.00, "amount": 91000.00,
        "profit": 3500.00
      }
    ]
  }
}
```
```json
{
  "code": 0,
  "data": {
    "year": 2026, "month": 8,
    "total_income": 15000.00,
    "total_expense": 8200.00,
    "balance": 6800.00,
    "by_category": [
      { "category_name": "餐饮", "expense_type": "expense", "amount": 3200.00 },
      { "category_name": "交通", "expense_type": "expense", "amount": 800.00 },
      { "category_name": "工资", "expense_type": "income", "amount": 15000.00 }
    ]
  }
}
```

**响应示例：**
```json
{
  "code": 0,
  "data": {
    "total_assets": 1250000.00,
    "total_liabilities": 350000.00,
    "net_worth": 900000.00,
    "breakdown": [
      { "category_name": "A股", "value": 450000.00, "pct": 36.0 },
      { "category_name": "基金", "value": 200000.00, "pct": 16.0 }
    ],
    "trend": [
      { "date": "2026-08-01", "net_worth": 880000.00 },
      { "date": "2026-08-10", "net_worth": 900000.00 }
    ]
  }
}
```

---

## 5. 后端 (csilk) 模块结构

```
backend/
├── CMakeLists.txt
├── src/
│   ├── main.c              # 入口：初始化 DB、加载配置、路由注册、启动
│   ├── auth.c              # 登录/注册/JWT 处理
│   ├── categories.c        # 分类 CRUD（含递归树形查询）
│   ├── assets.c            # 资产 CRUD
│   ├── transactions.c      # 交易 CRUD
│   ├── transfers.c         # 转账处理（写 transactions 两笔 + 更新 assets）
│   ├── daily_expenses.c    # 日常收支 CRUD + 月度汇总
│   ├── tags.c              # 标签 CRUD + 自动补全
│   ├── summary.c           # 汇总统计（SQL 聚合查询）
│   └── reports.c           # 各类报表：收支/资产/交易
│   └── common/
│       ├── db.h / db.c     # DB 连接池管理（单例）
│       ├── jwt.h / jwt.c   # JWT 生成/验证工具函数
│       └── response.h      # 统一响应格式宏
└── config/
    └── minefolio.yaml      # YAML 配置（端口、日志级别、静态文件路径）
```

### 5.1 csilk 中间件使用顺序

```c
// main.c
csilk_db_init();
csilk_server_t* server = csilk_server_new(router);

// Server 级别中间件（最外层→最内层）
csilk_server_use(server, csilk_recovery_handler);
csilk_server_use(server, csilk_logger_handler);
csilk_server_use(server, csilk_request_id_middleware);
csilk_server_use(server, csilk_cors_middleware, &cors_config);

// 公开路由（无需认证）
csilk_router_get(router, "/healthz", csilk_health_check_handler);
csilk_router_post(router, "/api/auth/register", auth_register);
csilk_router_post(router, "/api/auth/login", auth_login);

// API 路由组（需要 JWT 认证）
csilk_group_t* api = csilk_router_group(router, "/api");
csilk_group_use(api, csilk_csrf_middleware);
csilk_group_use(api, csilk_jwt_middleware, getenv("MINEFOLIO_JWT_SECRET"));

csilk_router_get(api, "/auth/me", auth_me);
csilk_router_get(api, "/categories", categories_list);
csilk_router_post(api, "/categories", categories_create);
csilk_router_put(api, "/categories/:id", categories_update);
csilk_router_delete(api, "/categories/:id", categories_delete);
csilk_router_get(api, "/assets", assets_list);
csilk_router_post(api, "/assets", assets_create);
csilk_router_put(api, "/assets/:id", assets_update);
csilk_router_delete(api, "/assets/:id", assets_delete);
csilk_router_get(api, "/transactions", transactions_list);
csilk_router_post(api, "/transactions", transactions_create);
csilk_router_put(api, "/transactions/:id", transactions_update);
csilk_router_delete(api, "/transactions/:id", transactions_delete);
csilk_router_get(api, "/daily-expenses", daily_expenses_list);
csilk_router_post(api, "/daily-expenses", daily_expenses_create);
csilk_router_put(api, "/daily-expenses/:id", daily_expenses_update);
csilk_router_delete(api, "/daily-expenses/:id", daily_expenses_delete);
csilk_router_get(api, "/daily-expenses/monthly", daily_expenses_monthly);
csilk_router_get(api, "/tags", tags_list);
csilk_router_post(api, "/tags", tags_create);
csilk_router_put(api, "/tags/:id", tags_update);
csilk_router_delete(api, "/tags/:id", tags_delete);
csilk_router_get(api, "/tags/suggestions", tags_suggestions);
csilk_router_get(api, "/summary", summary_get);

// 报表路由
csilk_router_get(api, "/reports/expense/monthly", report_expense_monthly);
csilk_router_get(api, "/reports/expense/trend",   report_expense_trend);
csilk_router_get(api, "/reports/expense/category", report_expense_category);
csilk_router_get(api, "/reports/expense/tag",      report_expense_tag);
csilk_router_get(api, "/reports/asset/trend",      report_asset_trend);
csilk_router_get(api, "/reports/asset/breakdown",  report_asset_breakdown);
csilk_router_get(api, "/reports/transaction/performance", report_transaction_performance);
csilk_router_get(api, "/reports/asset/summary",    report_asset_summary);

// 静态文件服务（前端构建产物）
csilk_router_get(router, "/*", csilk_static, "/opt/minefolio/frontend/dist");

csilk_server_run(server, config.port);
```

### 5.2 统一响应宏（response.h）

```c
#define RESP_OK(c, data) do { \
    csilk_json_t *_r = csilk_json_object(); \
    csilk_json_add_number(_r, "code", 0); \
    csilk_json_add_string(_r, "message", "ok"); \
    csilk_json_add_item(_r, "data", (data)); \
    csilk_json((c), CSILK_STATUS_OK, _r); \
} while(0)

#define RESP_ERR(c, code, msg) do { \
    csilk_json_t *_r = csilk_json_object(); \
    csilk_json_add_number(_r, "code", (code)); \
    csilk_json_add_string(_r, "message", (msg)); \
    csilk_json((c), CSILK_STATUS_OK, _r); \
} while(0)
```

### 5.3 关键处理逻辑说明

**分类树形查询（categories.c）：**
- 查询该用户所有分类后，在内存中递归组装树形结构。
- 使用 `parent_id` 建立父子关系，避免 N+1 查询。

**转账处理（transfers.c）：**
- 开启 SQLite 事务，保证原子性。
- 从 `from_asset_id` 写一条 `transfer_out` 交易，向 `to_asset_id` 写一条 `transfer_in` 交易。
- 更新两个资产的 `current_value`。
- 提交或回滚。

**汇总统计（summary.c）：**
- 资产总计：`SELECT SUM(current_value) FROM assets WHERE user_id=?`（按 asset_type 过滤资产类和负债类）
- 负债总计：从 categories 找出负债类，聚合对应 assets。
- 趋势：按日期聚合每日 net_worth（用 transactions 累计）。

**报表（reports.c）：**
- 月度收支：聚合 daily_expenses，按 category/tag 分组，计算每日明细。
- 收支趋势：按月份聚合 income/expense，返回近 N 月数据。
- 支出分类占比：按 category 聚合支出，计算百分比。
- 标签支出分析：JOIN expense_tags + tags，按标签聚合。
- 资产趋势：每日 SELECT SUM(current_value) FROM assets，滑动窗口计算净值。
- 资产分布：当前各分类资产值及占比（资产类 vs 负债类分开）。
- 交易表现：聚合 buy/sell 交易，计算盈亏（sell_amount - buy_cost）。
- 资产总览：当前值 + 30日变动统计 + 分类明细。

---

## 6. 前端 (Vue 3) 模块结构

### 6.0 语言规范

**所有前端 UI 文本统一使用简体中文**，包括但不限于：

- 页面标题、导航菜单项
- 表单标签、占位提示（placeholder）
- 按钮文字（确认、取消、删除、保存等）
- 表格列名、筛选条件标签
- 错误提示、成功提示（Message/Notification）
- 对话框标题与正文

**技术实现：**
- 使用 `naive-ui` 或 Element Plus 的 `zh-cn` locale 注册
- 自定义业务文本统一收口到 `src/locales/zh-CN.ts`，由 Pinia store 提供
- 不引入 i18n 插件（YAGNI），单一语言直接硬编码中文文本
- 货币格式化使用 `Intl.NumberFormat('zh-CN', { style: 'currency', currency: 'CNY' })`

---

```
frontend/
├── package.json
├── vite.config.ts
├── tsconfig.json
├── env.development          # VITE_API_URL=http://localhost:8080/api
├── env.production
├── src/
│   ├── main.ts
│   ├── App.vue
│   ├── locales/
│   │   └── zh-CN.ts          # 全部中文 UI 文本
│   ├── router/
│   │   └── index.ts          # 路由 + 登录守卫
│   ├── stores/
│   │   ├── auth.ts           # Pinia: token、用户信息
│   │   └── category.ts       # Pinia: 分类树缓存
│   ├── api/
│   │   ├── auth.ts
│   │   ├── categories.ts
│   │   ├── assets.ts
│   │   ├── transactions.ts
│   │   ├── daily_expenses.ts
│   │   ├── tags.ts
│   │   ├── summary.ts
│   │   └── reports.ts
│   ├── views/
│   │   ├── Login.vue
│   │   ├── Dashboard.vue     # 总览 + 趋势图（ECharts）
│   │   ├── Assets.vue        # 资产列表 + 新增/编辑对话框
│   │   ├── Transactions.vue  # 交易记录表格 + 筛选
│   │   ├── DailyExpenses.vue # 日常收支记账 + 月度报表
│   │   ├── Categories.vue    # 分类树管理
│   │   ├── Transfer.vue      # 资产间转账
│   │   └── Reports.vue       # 报表中心（全部报表入口 + 概览）
│   ├── components/
│   │   ├── AssetCard.vue
│   │   ├── TransactionTable.vue
│   │   ├── DailyExpenseForm.vue    # 收支表单（含标签选择器）
│   │   ├── TagPicker.vue           # 标签选择/新建组件
│   │   ├── MonthlyChart.vue
│   │   ├── CategoryTree.vue
│   │   └── NetWorthChart.vue
│   ├── utils/
│   │   ├── http.ts           # axios 封装（拦截器注入 token + CSRF token）
│   │   └── format.ts         # 货币格式化、日期格式化
│   └── types/
│       └── index.ts
└── public/
```

### 6.1 路由设计

```ts
const routes = [
  { path: '/login', component: () => import('@/views/Login.vue') },
  {
    path: '/',
    component: () => import('@/views/Layout.vue'),
    meta: { requiresAuth: true },
    children: [
      { path: '', redirect: '/dashboard' },
      { path: 'dashboard', component: () => import('@/views/Dashboard.vue') },
      { path: 'assets',       component: () => import('@/views/Assets.vue') },
      { path: 'transactions', component: () => import('@/views/Transactions.vue') },
      { path: 'daily-expenses', component: () => import('@/views/DailyExpenses.vue') },
      { path: 'categories',   component: () => import('@/views/Categories.vue') },
      { path: 'transfer',     component: () => import('@/views/Transfer.vue') },
      { path: 'reports',      component: () => import('@/views/Reports.vue') },
    ]
  }
]
```

### 6.2 类型定义

```ts
// src/types/index.ts
export interface Category {
  id: number;
  name: string;
  parent_id: number | null;
  asset_type: string;
  currency: string;
  icon?: string;
  sort_order: number;
  children?: Category[];
}

export interface Asset {
  id: number;
  user_id: number;
  category_id: number;
  name: string;
  account_no?: string;
  current_value: number;
  currency: string;
  note?: string;
  created_at: string;
  updated_at: string;
}

export type TransactionType =
  | 'deposit' | 'withdrawal' | 'buy' | 'sell'
  | 'transfer_in' | 'transfer_out' | 'fee'
  | 'income' | 'loss';

export type ExpenseType = 'income' | 'expense';

export interface Tag {
  id: number;
  user_id: number;
  name: string;
  color: string;             // HEX color, e.g. "#FF6B6B"
  created_at: string;
}

export interface DailyExpense {
  id: number;
  user_id: number;
  category_id: number;
  expense_type: ExpenseType;
  amount: number;
  currency: string;
  expense_date: string;       // YYYY-MM-DD
  note?: string;
  tags?: Tag[];
  category_name?: string;     // join 展示用
  created_at: string;
  updated_at: string;
}

export interface MonthlyExpenseSummary {
  year: number;
  month: number;
  total_income: number;
  total_expense: number;
  balance: number;
  by_category: {
    category_name: string;
    expense_type: ExpenseType;
    amount: number;
  }[];
}

export interface Transaction {
  id: number;
  asset_id: number;
  category_id: number;
  transaction_type: TransactionType;
  amount: number;
  price_per_unit?: number;
  quantity?: number;
  currency: string;
  transaction_date: string;
  note?: string;
  asset_name?: string;     // 前端 join 展示用
  category_name?: string;  // 前端 join 展示用
}

export interface Summary {
  total_assets: number;
  total_liabilities: number;
  net_worth: number;
  breakdown: { category_name: string; value: number; pct: number }[];
  trend: { date: string; net_worth: number }[];
}

export interface ExpenseMonthly {
  year: number; month: number;
  total_income: number; total_expense: number; balance: number;
  by_category: { name: string; type: ExpenseType; amount: number; pct: number }[];
  by_tag: { tag_name: string; amount: number; count: number }[];
  daily_breakdown: { date: string; income: number; expense: number }[];
}

export interface ExpenseTrend {
  labels: string[];          // ["2026-03","2026-04",...]
  income:  number[];
  expense: number[];
}

export interface ExpenseCategoryBreakdown {
  period: string;            // "2026-08" or "2026"
  items: {
    name: string;
    expense_type: ExpenseType;
    amount: number;
    pct: number;
  }[];
}

export interface ExpenseTagBreakdown {
  period: string;
  items: { tag_name: string; amount: number; count: number; pct: number }[];
}

export interface AssetTrend {
  period: "30d" | "90d" | "365d" | "custom";
  labels: string[];
  net_worth: number[];
  assets: number[];
  liabilities: number[];
}

export interface AssetBreakdown {
  assets:     { name: string; value: number; pct: number }[];
  liabilities:{ name: string; value: number; pct: number }[];
  total_assets: number;
  total_liabilities: number;
  net_worth: number;
}

export interface TransactionPerformance {
  total_trades: number;
  total_gain: number;
  total_loss: number;
  net_gain: number;
  trades: {
    id: number;
    asset_name: string;
    type: 'buy' | 'sell';
    date: string;
    quantity: number;
    price: number;
    amount: number;
    profit?: number;
  }[];
}

export interface AssetSummary {
  current_value: number;
  change_30d: number;
  change_30d_pct: number;
  by_category: { name: string; value: number; pct: number; change: number }[];
}

export interface ApiResponse<T> {
  code: number;
  message: string;
  data: T;
}
```

### 6.3 Axios 拦截器（http.ts）

```ts
import axios from 'axios';
import { useAuthStore } from '@/stores/auth';

const http = axios.create({ baseURL: import.meta.env.VITE_API_URL });

// 请求拦截：注入 token
http.interceptors.request.use(config => {
  const auth = useAuthStore();
  if (auth.token) {
    config.headers.Authorization = `Bearer ${auth.token}`;
  }
  return config;
});

// 响应拦截：统一错误处理
http.interceptors.response.use(
  res => res.data,
  err => {
    if (err.response?.data?.code === 1001) {
      useAuthStore().logout();
      window.location.href = '/login';
    }
    return Promise.reject(err);
  }
);

export default http;
```

---

## 7. 部署配置

### 7.1 Nginx 配置（production）

```nginx
server {
    listen 80;
    server_name _;

    # 前端静态文件
    location / {
        root /opt/minefolio/frontend/dist;
        try_files $uri $uri/ /index.html;
    }

    # API 反向代理
    location /api/ {
        proxy_pass http://127.0.0.1:8080;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }

    # 健康检查
    location /healthz {
        proxy_pass http://127.0.0.1:8080;
    }
}
```

### 7.2 csilk 配置 (minefolio.yaml)

```yaml
server:
  host: "0.0.0.0"
  port: 8080
  workers: 2

database:
  driver: sqlite
  dsn: "/opt/minefolio/data/minefolio.db"

static:
  root_dir: "/opt/minefolio/frontend/dist"
  index_file: "index.html"

logging:
  level: info
  file: "/opt/minefolio/logs/server.log"
```

### 7.3 环境变量

```bash
export MINEFOLIO_JWT_SECRET="your-random-secret-at-least-32-chars"
```

---

## 8. 安全设计

- JWT secret 从环境变量读取，不写入代码或配置文件。
- 密码使用 bcrypt 哈希（通过 csilk `csilk_bcrypt_hash` 或 SQLite extension）。
- CSRF 中间件对所有状态修改接口（POST/PUT/DELETE）生效。
- CORS 生产环境限定前端域名；开发环境允许 `*`。
- SQL 注入防护：所有数据库查询使用参数化占位符（`?`），禁止字符串拼接 SQL。
- WAF 中间件：防御 XSS / SQL 注入等常见攻击。
- SQLite 文件权限：`chmod 600`，仅 owner 可读写。

---

## 9. 开发计划（MVP 范围）

| 阶段 | 内容 | 优先级 |
|------|------|--------|
| 1 | DB 初始化（SQL migration）+ 分类 CRUD API | P0 |
| 2 | 认证模块（注册/登录/JWT） | P0 |
| 3 | 资产 CRUD API | P0 |
| 4 | 交易记录 API | P0 |
| 5 | 日常收支 CRUD API + 月度汇总 API + 标签 CRUD API | P0 |
| 6 | 汇总统计 API + 全部报表 API（8个端点） | P0 |
| 7 | 前端基础框架（Vite + TS + Element Plus + 路由 + 守卫） | P0 |
| 8 | 前端登录页 + Pinia auth store | P0 |
| 9 | 前端分类管理页（树形 CRUD） | P0 |
| 10 | 前端标签管理页（CRUD + 颜色选择） | P0 |
| 11 | 前端资产列表 + 新增/编辑 | P0 |
| 12 | 前端交易记录页 + 筛选 | P1 |
| 13 | 前端日常收支记账页（表单含标签选择 + 月度图表） | P0 |
| 14 | 前端 Dashboard（汇总 + 趋势图 + 月度收支概览） | P1 |
| 15 | 前端报表中心页（全部报表入口 + 月度报表 + 收支趋势 + 资产分布 + 交易表现） | P0 |
| 16 | 前端转账功能 | P2 |
| 17 | 生产构建脚本 + nginx 部署模板 | P2 |

---

## 10. YAGNI 排除项（不在 MVP 范围）

- 多用户系统（固定单用户）
- 第三方金融数据源自动同步
- 移动端 App
- 多数据库适配（PostgreSQL/MySQL）— 架构已预留接口，但不实现
- 实时 WebSocket 推送
- 导出 Excel/CSV
