# 账户维度资产管理设计

- 日期：2026-10-25
- 状态：已确认，待实施
- 范围：backend（C23 / csilk）、frontend（Vue 3）、数据库迁移 V017–V018
- 关联文档：
  - `docs/superpowers/specs/2026-10-17-transfer-explicit-transactions-design.md`
  - `docs/superpowers/specs/2026-10-17-postgres-migration-test-mock-design.md`

## 1. 背景与问题

当前 `assets` 表是**平铺**结构：每一行既可能是「招商银行活期」这类账户，也可能是「茅台股票」这类持仓。账户身份只靠 `assets.account_no` 这个自由文本字段表达，系统里没有真正的账户实体。

由此产生三个问题：

1. **无法按账户查看资产。** 一个支付宝账户下的余额、余额宝、基金、股票是四条互不关联的资产行，报表只能按 `asset_type` 聚合，无法回答「我在支付宝里一共放了多少」。
2. **账户信息重复且不一致。** 同一个银行账户的多条资产需要重复填写 `account_no`，改名要逐条修改。
3. **分类树承担了两种职责。** `categories` 既表达资产类型（现金/基金/股票），又混入了机构与账户名（支付宝、银行存款、余额宝/零钱通、港美股账户），职责不清。

本设计引入 **机构 → 账户 → 资产** 两级归属维度，并把分类收敛为纯类型。

> `domain/asset/account.h` 中的 `mf_account_t` 是只定义、从未引用的死代码（含 `balance`/`is_liability` 字段），本设计将其删除并在 `domain/account/` 重新定义。

## 2. 目标与非目标

### 目标

- 新增 `institutions` 与 `accounts` 两张表，资产必须归属到一个账户。
- 账户可容纳任意类型的资产：一个支付宝账户下可以有现金余额、货币基金、基金、股票。
- 资产列表、账户详情、报表支持按机构/账户维度查看与筛选。
- 存量数据零丢失、可回滚地迁移到新模型。
- 分类体系收敛为 15 个扁平的资产类型，机构与账户身份完全由新表承担。

### 非目标（本期不做）

- 账户之间直接转账（转账仍是资产对资产）。
- 账户对账单导入、余额校准、账户级对账。
- 自定义机构类型 / 账户类型表（使用固定枚举 + 自由名称）。
- 移动端离线机构/账户表（离线模式不展示账户维度）。
- 账户级权限（权限仍按账本角色）。
- Dashboard 账户维度图表。

## 3. 核心决策

| 编号 | 决策 | 说明 |
|---|---|---|
| D1 | **账户是纯容器** | 账户不持有任何金额，没有 `balance` 字段。支付宝余额、余额宝、基金、股票都是账户下的资产行。净资产仍由资产行汇总，不存在双重记账。 |
| D2 | **两级结构：机构 → 账户 → 资产** | 例：`支付宝`（机构）→ `余额账户` / `基金账户`（账户）→ `余额`、`余额宝`、`华夏基金`（资产）。 |
| D3 | **分类收敛为纯类型** | 分类只表达资产类型，不再包含机构/账户名。机构与账户身份由新表承担。 |
| D4 | **资产必须归属账户** | `assets.account_id` 必填。为无机构的资产生成内置系统机构/账户（现金、自有资产、未分类）。 |
| D5 | **固定枚举 + 自由名称** | 机构 6 种类型、账户 9 种类型，枚举固定；名称自由填写。不开放自定义类型表。 |
| D6 | **迁移零丢失、可回滚** | 确定性自动迁移，每个存量资产归入自动生成的账户；迁移后可在 UI 合并/重命名账户。不删除任何行。 |
| D7 | **`asset_type` 枚举不变** | 保持现有 11 值 `cash/stock/fund/bond/crypto/real_estate/vehicle/other_asset/loan/credit_card/other_liability`。活期/定期归 `cash`，货币基金/银行理财归 `fund`。避免 SQLite 重建表改 CHECK 的风险。 |

## 4. 数据模型

### 4.1 `institutions`（机构）

```sql
CREATE TABLE institutions (
  id          INTEGER PRIMARY KEY AUTOINCREMENT,           -- PostgreSQL: BIGSERIAL
  user_id     INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  ledger_id   INTEGER NOT NULL REFERENCES ledgers(id) ON DELETE CASCADE,
  name        TEXT    NOT NULL,                            -- 招商银行 / 支付宝 / 华泰证券
  type        TEXT    NOT NULL CHECK (type IN ('bank','broker','payment','fund_platform','cash','other')),
  code        TEXT,                                        -- 仅系统机构：'cash' / 'own_assets' / 'unclassified'
  icon        TEXT,
  sort_order  INTEGER NOT NULL DEFAULT 0,
  note        TEXT,
  is_system   INTEGER NOT NULL DEFAULT 0,                  -- PostgreSQL: BOOLEAN
  created_at  TEXT    NOT NULL DEFAULT (datetime('now')),  -- PostgreSQL: TIMESTAMPTZ
  updated_at  TEXT    NOT NULL DEFAULT (datetime('now')),
  UNIQUE (user_id, ledger_id, name),
  UNIQUE (user_id, ledger_id, code)
);
CREATE INDEX idx_institutions_user_ledger ON institutions(user_id, ledger_id);
```

机构类型：

| type | 含义 | 例子 |
|---|---|---|
| `bank` | 银行 | 招商银行、工商银行 |
| `broker` | 券商 | 华泰证券、富途 |
| `payment` | 第三方支付 | 支付宝、微信支付 |
| `fund_platform` | 基金销售平台 | 天天基金、蚂蚁财富 |
| `cash` | 现金 | 系统「现金」 |
| `other` | 其他 | 系统「自有资产」「未分类」 |

### 4.2 `accounts`（账户）

```sql
CREATE TABLE accounts (
  id             INTEGER PRIMARY KEY AUTOINCREMENT,
  user_id        INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  ledger_id      INTEGER NOT NULL REFERENCES ledgers(id) ON DELETE CASCADE,
  institution_id INTEGER NOT NULL REFERENCES institutions(id) ON DELETE RESTRICT,
  name           TEXT    NOT NULL,                         -- 储蓄卡(1234) / 余额账户 / 基金账户
  type           TEXT    NOT NULL CHECK (type IN ('debit_card','credit_card','securities','fund','payment','deposit','loan','cash','virtual')),
  code           TEXT,                                     -- 仅系统账户：'cash' / 'physical' / 'unclassified'
  account_no     TEXT,                                     -- 卡号/账号，可空
  sort_order     INTEGER NOT NULL DEFAULT 0,
  note           TEXT,
  is_system      INTEGER NOT NULL DEFAULT 0,
  created_at     TEXT    NOT NULL DEFAULT (datetime('now')),
  updated_at     TEXT    NOT NULL DEFAULT (datetime('now')),
  UNIQUE (user_id, ledger_id, institution_id, name),
  UNIQUE (user_id, ledger_id, code)
);
CREATE INDEX idx_accounts_user_ledger ON accounts(user_id, ledger_id);
CREATE INDEX idx_accounts_institution ON accounts(institution_id);
```

账户类型：

| type | 含义 | 对应旧分类 |
|---|---|---|
| `debit_card` | 储蓄卡/借记卡 | 银行存款 |
| `credit_card` | 信用卡 | 信用卡、花呗/白条 |
| `securities` | 证券账户 | 股票证券、港美股账户 |
| `fund` | 基金账户 | 基金理财、余额宝/零钱通、京东金融 |
| `payment` | 支付账户 | 支付宝、微信零钱 |
| `deposit` | 存款账户 | 定期存款、大额存单 |
| `loan` | 贷款账户 | 房贷/车贷/贷款、消费贷/网贷 |
| `cash` | 现金账户 | 现金账户 |
| `virtual` | 虚拟/实物归集账户 | 房产、车辆、应收款、系统账户 |

### 4.3 `assets` 变更

```sql
ALTER TABLE assets ADD COLUMN account_id INTEGER REFERENCES accounts(id) ON DELETE RESTRICT;
ALTER TABLE assets ADD COLUMN legacy_category_id INTEGER;   -- 迁移回滚专用
CREATE INDEX idx_assets_account ON assets(account_id);
```

- `account_no` 列**保留但不再读写**，迁移时搬到账户上；前端移除该字段。保留是为了零丢失与回滚。
- `legacy_category_id` 仅用于回滚；用户手动修改资产分类时清空（见 8.3）。

### 4.4 分类体系收敛

`type='asset'` 的分类收敛为**扁平一层**（`parent_id = NULL`），共 **15 个**：

| 分类 | asset_type | 分类 | asset_type |
|---|---|---|---|
| 现金 | `cash` | 股票 | `stock` |
| 活期存款 | `cash` | 债券 | `bond` |
| 定期存款 | `cash` | 加密货币 | `crypto` |
| 银行理财 | `fund` | 房产 | `real_estate` |
| 货币基金 | `fund` | 车辆 | `vehicle` |
| 基金 | `fund` | 应收款 | `other_asset` |
| 信用卡 | `credit_card` | 贷款 | `loan` |
| 其他负债 | `other_liability` | | |

- `expense` / `income` / `transaction` 三棵树不变。
- 分类仍按 `(user_id, ledger_id)` 隔离；根节点的 `UNIQUE(user_id, name, parent_id)` 在 `parent_id IS NULL` 时不去重（SQLite NULL 互不相等），`mf_category_repo_find_or_create` 的 ledger 过滤保证每账本幂等播种。
- `categories` 增加归档标记，用于保留迁移前的旧分类：

```sql
ALTER TABLE categories ADD COLUMN is_archived INTEGER NOT NULL DEFAULT 0;
```

### 4.5 不变量

1. 账户不持有金额，无 `balance` / `is_liability` 字段。
2. 账户不持有币种；币种只存在于资产行。账户详情页币种由子资产推导：单一币种显示该币种，多币种显示「多币种」。
3. 账户类型与负债无关；`credit_card` / `loan` 只描述账户性质，是否负债由子资产 `asset_type` 决定（沿用 `mf_asset_is_liability_type_str`）。
4. 归属链在同一账本内闭合：`asset.ledger_id == account.ledger_id == institution.ledger_id`，且三者 `user_id` 一致。usecase 层强校验，非法请求返回 1002。
5. 删除语义：机构下有账户不可删（RESTRICT → 1004）；账户下有资产不可删（RESTRICT → 1004）。
6. 系统行：每个 `(user_id, ledger_id)` 幂等生成三组系统机构/账户（见 4.6）；系统行不可删除、`type` 不可修改，但可改名。
7. `assets.account_id` 在 SQLite 中是可空列：SQLite 无法给存量表直接加 NOT NULL，重建 `assets` 会牵动多张子表外键，风险过高。采用「迁移全量回填 + 仓储层 `account_id<=0` 拒绝 + usecase 强制」；PostgreSQL 回填后执行 `ALTER COLUMN account_id SET NOT NULL`。
8. 所有机构/账户/资产查询强制 `user_id + ledger_id` 双条件。

### 4.6 系统机构与系统账户

| 机构 code | 机构 name / type | 账户 code | 账户 name / type | 用途 |
|---|---|---|---|---|
| `cash` | 现金 / `cash` | `cash` | 现金账户 / `cash` | 无机构的现金、预付卡 |
| `own_assets` | 自有资产 / `other` | `physical` | 实物资产 / `virtual` | 房产、车辆、应收款 |
| `unclassified` | 未分类 / `other` | `unclassified` | 未分类账户 / `virtual` | 迁移兜底、CSV 导入、离线同步兜底 |

- `users` 中不存在 `is_system` 语义冲突：`is_system` 只在机构/账户表内生效。
- 系统行由 `account_usecase_ensure_system_rows(pool, user_id, ledger_id)` 幂等生成，使用 `INSERT OR IGNORE`（SQLite）/ `ON CONFLICT DO NOTHING`（PostgreSQL）。

## 5. 数据迁移（V017 / V018）

### 5.1 版本拆分

| 文件 | 内容 |
|---|---|
| `backend/sql/migrations/sqlite/V017__accounts_schema.sql` | 建 `institutions` / `accounts`；`assets` 加 `account_id`、`legacy_category_id`；`categories` 加 `is_archived`；建索引 |
| `backend/sql/migrations/sqlite/V018__accounts_backfill.sql` | 纯数据迁移（播种 15 个类型分类、系统行、机构/账户推断、资产重挂、归档旧分类） |
| `backend/sql/migrations/postgres/V017__accounts_schema.sql` | PostgreSQL 等价结构 |
| `backend/sql/migrations/postgres/V018__accounts_backfill.sql` | PostgreSQL 等价数据迁移 + 回填后 `SET NOT NULL` |
| `backend/sql/migrations/rollback/V018_accounts_backfill_rollback.sql` | 手工回滚脚本，不参与自动发现 |

结构与数据分开，便于分别验证；每个版本由 csilk 迁移引擎事务化执行，失败整体回滚。

### 5.2 回填算法（确定性）

对每个存在的 `(user_id, ledger_id)`（取自 `SELECT DISTINCT user_id, ledger_id FROM assets` 与 `categories` 的并集，保证空账本也有系统行）依次执行：

**Step 1 — 播种 15 个扁平类型分类。**
按 4.4 的表插入 `parent_id = NULL` 的分类，`type='asset'`，幂等（`user_id + ledger_id + name + parent_id IS NULL` 判重）。

**Step 2 — 播种系统机构/账户。**
按 4.6 插入 3 组系统行，按 `code` 判重。

**Step 3 — 机构推断。**
对每条资产，拼接 `asset.name`、`category.name`、父分类名作为匹配文本，按**长词优先**匹配下表；未命中则归入系统「未分类」机构。

| 关键词 | 机构名 | 机构 type |
|---|---|---|
| 支付宝、花呗、借呗、余额宝、余利宝、蚂蚁财富 | 支付宝 | `payment` |
| 微信、零钱通 | 微信支付 | `payment` |
| 京东、京东金融 | 京东金融 | `fund_platform` |
| 招商、工商、建设、农业、中国银行、交通、邮储、中信、浦发、民生、兴业、光大、平安、华夏、广发 | 对应银行 | `bank` |
| 华泰、中信证券、东方财富、国泰君安、招商证券、富途、老虎 | 对应券商 | `broker` |
| 天天基金 | 天天基金 | `fund_platform` |
| 现金、实物、应收、房产、车辆 | 系统「现金」或「自有资产」 | `cash` / `other` |
| 其余 | 系统「未分类」 | `other` |

机构名取关键词对应的规范名（如「中国银行」不误判为「招商银行」）。

**Step 4 — 账户推断。**
按 `(user_id, ledger_id, institution_id, legacy_category_id)` 分组，每组生成一个账户：

- 账户名：组内仅一个资产时取资产名；否则取旧分类名；旧分类为空时取「默认账户」。
- `account_no`：组内所有资产共享同一非空 `account_no` 时搬迁该值，否则置 NULL。
- 账户类型映射（按旧分类名优先，其次 `asset_type`）：

| 旧分类 / asset_type | 账户 type |
|---|---|
| 现金账户 / 现金 | `cash` |
| 银行存款 / 活期存款 / 定期存款 / `cash` | `deposit` |
| 支付宝 / 微信零钱 / 支付类 | `payment` |
| 余额宝/零钱通 / 京东金融 / 基金理财 / `fund` / `bond` | `fund` |
| 股票证券 / 港美股账户 / `stock` / `crypto` | `securities` |
| 信用卡 / 花呗/白条 / `credit_card` | `credit_card` |
| 房贷/车贷/贷款 / 消费贷/网贷 / `loan` | `loan` |
| 房产 / 车辆 / 应收款项 / 预付卡储值卡 / `real_estate` / `vehicle` / `other_asset` | `virtual` |
| 其余 | `virtual` |

**Step 5 — 资产重挂分类。**
按 `asset_type` + 资产名关键词映射到 15 个类型分类：

1. 名称含「定期」→ 定期存款
2. 名称含「活期」→ 活期存款
3. 名称含「理财」→ 银行理财
4. 名称含「余额宝 / 零钱通 / 货币基金 / 货基」→ 货币基金
5. 其余按 `asset_type`：`cash`→现金，`fund`→基金，`stock`→股票，`bond`→债券，`crypto`→加密货币，`real_estate`→房产，`vehicle`→车辆，`other_asset`→应收款，`credit_card`→信用卡，`loan`→贷款，`other_liability`→其他负债
6. `asset_type` 为空或未知 → 应收款（`other_asset`）

**Step 6 — 写入归属。**
`UPDATE assets SET account_id = <inferred>, legacy_category_id = category_id WHERE account_id IS NULL`。

**Step 7 — 归档旧分类。**
`UPDATE categories SET is_archived = 1 WHERE type = 'asset' AND id NOT IN (<15 个新分类 id>)`。迁移后所有资产都已重挂到 15 个类型分类，旧分类（含用户自建）一律归档；名称信息保留在 `legacy_category_id` 与分类行本身中，不删除任何行。

**Step 8 — 校验。**
- `SELECT COUNT(*) FROM assets WHERE account_id IS NULL` 必须为 0。
- `SELECT COUNT(*) FROM assets a JOIN categories c ON a.category_id=c.id WHERE c.asset_type <> a.asset_type` 必须为 0。
- 资产总行数与迁移前一致。

### 5.3 回滚

`backend/sql/migrations/rollback/V018_accounts_backfill_rollback.sql`（手工执行）：

1. `UPDATE assets SET category_id = legacy_category_id WHERE legacy_category_id IS NOT NULL;`（只回滚未被用户改过分类的行）
2. `UPDATE categories SET is_archived = 0 WHERE type = 'asset';`
3. `UPDATE assets SET account_id = NULL, legacy_category_id = NULL;`
4. `DELETE FROM accounts; DELETE FROM institutions;`
5. PostgreSQL 额外 `DROP TABLE accounts; DROP TABLE institutions;` 与 `ALTER TABLE assets DROP COLUMN account_id, DROP COLUMN legacy_category_id;`
6. SQLite 保留新增列（空值），不重建表。

V018 播种的 15 个类型分类不回滚删除：它们可能已被迁移后新建的资产引用；若未引用，保留也无副作用。

### 5.4 新用户 / 新账本

- `category_usecase_seed_defaults` 的 asset 分支替换为 4.4 的 15 个扁平分类。
- `account_usecase_ensure_system_rows` 在每个业务入口进入逻辑前幂等调用，覆盖：
  1. `auth_usecase_register`
  2. `auth_usecase_oauth_callback`
  3. `system_setup`（`admin_controller.c`）
  4. `api_categories_list`（该处已在调用 `category_usecase_seed_defaults`）
  5. `api_categories_children`
  6. `api_accounts_list`（自愈：缺失系统行时补齐）
  7. 账本创建路径
- 该函数只做 `INSERT OR IGNORE` / `ON CONFLICT DO NOTHING`，可安全重复调用。

## 6. 后端分层设计

### 6.1 领域层

**新增 `backend/src/domain/institution/`**

```c
typedef struct {
    int64_t     id;
    int64_t     user_id;
    int64_t     ledger_id;
    char        name[128];
    char        type[32];
    char        code[32];
    char        icon[32];
    int64_t     sort_order;
    char        note[256];
    bool        is_system;
    csilk_time_t created_at;
    csilk_time_t updated_at;
} mf_institution_t;
```

`rules.h/.c`：名称非空且 ≤128；`type` ∈ 6 值枚举；系统行 `code` 与 `type` 不可修改。

**新增 `backend/src/domain/account/`**

```c
typedef struct {
    int64_t     id;
    int64_t     user_id;
    int64_t     ledger_id;
    int64_t     institution_id;
    char        name[128];
    char        type[32];
    char        code[32];
    char        account_no[64];
    int64_t     sort_order;
    char        note[256];
    bool        is_system;
    csilk_time_t created_at;
    csilk_time_t updated_at;
} mf_account_t;
```

`rules.h/.c`：名称非空且 ≤128；`type` ∈ 9 值枚举；`account_no` ≤64；系统行 `code` 与 `type` 不可修改。

**其他**
- 删除 `backend/src/domain/asset/account.h`（死代码）。
- `domain/asset/entity.h`：`mf_asset_t` 增加 `int64_t account_id`；资产规则要求 `account_id > 0`。

### 6.2 应用层

**`application/institution/`**
- `list(pool, user_id, ledger_id)`：返回机构 + `account_count` / `asset_count` / `total_value_cny`。
- `create` / `update` / `delete`：删除前 `account_count == 0`，否则 1004；重名由唯一约束兜底转 1004。

**`application/account/`**
- `ensure_system_rows(pool, user_id, ledger_id)`：幂等生成 4.6 的三组系统行。
- `list(pool, user_id, ledger_id, institution_id, with_totals)`：返回账户 + `institution_name` / `institution_type` / `asset_count` / 按币种 `totals[]` / `total_value_cny`（复用 `exchange_rate_get_to_cny()`）。
- `detail(pool, user_id, ledger_id, id)`：账户信息 + 资产列表。
- `create` / `update` / `delete`：校验机构同 `(user_id, ledger_id)`；删除前 `asset_count == 0` 否则 1004。
- `merge(pool, user_id, ledger_id, source_id, target_id)`：`db_tx_scope` 内批量更新 `assets.account_id`；非系统源账户随后删除；系统源账户只迁资产不删除（见 8.1）。
- `asset_count(pool, user_id, ledger_id, account_id)`：供删除守卫与详情使用。

**`application/asset/`**
- 创建/更新命令新增 `int64_t account_id`；`account_id <= 0` 直接 1002；账户必须存在且同账本。
- 移除 `account_no` 写入路径（列保留不读写）。
- 更新资产分类时若 `category_id` 发生变化，清空 `legacy_category_id`。

### 6.3 基础设施

**新增**
- `infrastructure/repositories/institution_repo_impl.c`：`institution_list` / `institution_get` / `institution_insert` / `institution_update` / `institution_delete` / `institution_count_accounts`。
- `infrastructure/repositories/account_repo_impl.c`：`account_list` / `account_get` / `account_insert` / `account_update` / `account_delete` / `account_count_assets` / `account_totals` / `account_reassign_assets`。

**改造 `asset_repo_impl.c`**
- `asset_list` / `asset_get` 增加 `LEFT JOIN accounts acc ON a.account_id = acc.id` 与 `LEFT JOIN institutions inst ON acc.institution_id = inst.id`，投影 `account_name` / `account_type` / `institution_id` / `institution_name` / `institution_type`。
- 新增 `account_id` / `institution_id` 过滤参数。
- INSERT / UPDATE 写入 `account_id`。
- 所有查询继续 `user_id + ledger_id` 双条件。

**改造 `category_repo_impl.c`**
- 列表类查询过滤 `is_archived = 0`；详情查询不过滤（归档分类仍可解析）。
- 报表/组合/资产 JOIN 不过滤 `is_archived`（见 8.2）。
- 项目统一 `csilk_json_t*` 直接返回 + `?` 占位符。

### 6.4 HTTP API

| 方法 | 路径 | 角色 | 说明 |
|---|---|---|---|
| GET | `/api/institutions` | viewer | 机构列表 + 汇总 |
| POST | `/api/institutions` | editor | 新建机构 |
| PUT | `/api/institutions/:id` | editor | 改名/图标/排序/备注 |
| DELETE | `/api/institutions/:id` | editor | 有账户时 1004 |
| GET | `/api/accounts` | viewer | `institution_id` 可选；`with_totals=1` 附汇总 |
| GET | `/api/accounts/:id` | viewer | 账户详情 + 资产列表 |
| POST | `/api/accounts` | editor | 新建账户 |
| PUT | `/api/accounts/:id` | editor | 改名/卡号/排序/备注 |
| DELETE | `/api/accounts/:id` | editor | 有资产时 1004 |
| POST | `/api/accounts/:id/merge` | editor | body `{ "target_id": 12 }` |

- 新增 `institution_controller.c` / `account_controller.c`，在 `main.c` 注册 `register_institution_routes` / `register_account_routes`。
- 账本上下文统一 `ctx_ledger_id(c, user_id, "viewer" | "editor")`。
- 资产创建/更新接口新增必填 `account_id`，缺失 1002。

**响应示例**

```json
// GET /api/accounts?with_totals=1&institution_id=3
{
  "code": 0,
  "data": {
    "list": [
      {
        "id": 7,
        "name": "储蓄卡(1234)",
        "type": "debit_card",
        "account_no": "1234",
        "institution_id": 3,
        "institution_name": "招商银行",
        "institution_type": "bank",
        "is_system": 0,
        "asset_count": 3,
        "totals": [
          { "currency": "CNY", "amount": 152300.5 },
          { "currency": "USD", "amount": 2000 }
        ],
        "total_value_cny": 166700.5
      }
    ]
  }
}
```

```json
// POST /api/accounts
{ "institution_id": 3, "name": "储蓄卡(1234)", "type": "debit_card", "account_no": "1234", "note": "工资卡" }
```

**错误码**

| code | 场景 |
|---|---|
| 1002 | 名称/类型非法、`account_id` 缺失、账户与资产不同账本 |
| 1003 | 机构/账户不存在 |
| 1004 | 重名、删除被阻止、合并源/目标非法、修改系统行类型 |

### 6.5 兼容路径

- **CSV 导入**：无账户列时落入「未分类」系统账户；有账户列时按 `机构名 + 账户名` 匹配，未匹配则自动创建。导出新增「机构」「账户」两列。
- **AI 工具**：`services/ai/tools/asset_tool.c` 的 `create_asset` / `update_asset` schema 增加必填 `account_id`；缺失返回校验错误。
- **移动端**：旧离线队列无 `account_id`，同步时映射到「未分类」系统账户，响应回显实际账户。
- **汇总口径**：不新增聚合接口；前端用 `/api/institutions` 或 `/api/accounts?with_totals=1`；现有 `/api/summary` 净资产口径不变。

## 7. 前端设计

### 7.1 路由与导航

- 新增 `/accounts`，挂在 Layout 下，资产组顺序：**dashboard → accounts → assets → holdings → reports → market**。
- `Layout.vue` 的 `pageTitle` 增加 `'/accounts': '账户'`；i18n 增加 `nav.accounts`。
- 移动端不新增底部 tab。

### 7.2 Accounts 页

- 顶部三张概览卡：机构数 / 账户数 / 账户总市值（CNY）。
- 主体 `el-collapse` 按机构分组，展开后为账户卡片网格：
  - 卡片显示：账户名、类型标签、卡号后四位、资产数、按币种金额、CNY 合计。
  - 操作：查看详情 / 编辑 / 合并 / 删除。系统账户隐藏删除按钮，类型字段只读。
- 页头操作：新建机构、新建账户。
- 账户详情使用 `el-drawer`（不新增 `/accounts/:id` 路由）：账户信息、子资产表格、汇总、「在资产页查看」跳 `/assets?account_id=<id>`。
- 删除失败 toast 明确原因：「该账户下有 N 项资产，请先迁移或合并」「该机构下有 N 个账户」。

### 7.3 Assets 页改造

- 表格：删除「账户编号」列，新增「机构 / 账户」列（`招商银行 · 储蓄卡(1234)`）。
- 顶部筛选：新增「机构」「账户」两个 `el-select`，账户选项随机构联动；URL query 支持 `account_id` / `institution_id` 深链。
- 新增「按账户分组」开关（默认关闭）：开启后按账户插入分组行，行尾显示该账户小计。
- 新建/编辑弹窗：
  - 分类 cascader 只剩 15 个扁平类型。
  - 新增**必填**「账户」选择器：按机构分组（`el-option-group`），末项提供「+ 新建账户」快捷入口。
  - 移除「账户编号」输入框。

### 7.4 其他页面

- **Dashboard 不改**：资产分布饼图仍按 `asset_type`。
- **Categories 页**：过滤 `is_archived=1` 的资产分类（放入「已归档」折叠区，只读）；`type=asset` 新建分类允许 `parent_id = NULL`。
- **Holdings / Transactions / Reports 不改**：交易仍以资产为标的，账户由资产派生。
- **移动端**：在线模式下资产必须选账户；离线创建时 `account_id` 为空，同步落「未分类」。离线 sql.js 不新增 institutions/accounts 本地表，离线列表不展示账户维度；`assets.account_no` 列保留以兼容旧本地库。

### 7.5 文件清单

```
frontend/src/api/institutions.ts                 新增
frontend/src/api/accounts.ts                     新增
frontend/src/stores/account.ts                   新增（缓存 + invalidate + reset）
frontend/src/views/Accounts.vue                  新增
frontend/src/components/AccountCard.vue          新增
frontend/src/components/AccountSelect.vue        新增（按机构分组 + 新建快捷入口）
frontend/src/components/AccountDialog.vue        新增
frontend/src/components/InstitutionDialog.vue    新增
frontend/src/components/MergeAccountDialog.vue   新增
frontend/src/components/AccountDetailDrawer.vue  新增
frontend/src/views/Assets.vue                    改造（列/筛选/分组/弹窗）
frontend/src/views-mobile/AssetsMobile.vue       改造（分组 + 账户选择器）
frontend/src/router/index.ts                     + /accounts
frontend/src/views/Layout.vue                    菜单 + pageTitle
frontend/src/types/index.ts                      + Institution / Account / AccountTotals；Asset 增加 account 字段
frontend/src/locales/{zh-CN,en-US}.ts            nav / navGroups / 页面文案
```

- `accountStore` 实现 `reset()` 并从 `auth.logout()` 调用（遵循登出隔离规则），监听 `minefolio:ledger-changed` 后 `invalidate()`。
- `AssetInput` 增加必填 `account_id: number`，移除 `account_no`。
- `Asset` 增加 `account_id` / `account_name` / `account_type` / `institution_id` / `institution_name` / `institution_type`。

## 8. 边界与规则补充

### 8.1 合并语义

合并条件为**同账本**（不再限制同机构）：

- 两账户机构不同时，目标账户保留其机构，源账户删除。
- **系统账户作为 source 时只迁移资产、不删除源账户**（系统账户不可删）；非系统 source 迁移后删除。
- 目标不能是 source 自身；目标必须存在且同账本。
- 整个操作在 `db_tx_scope` 内完成。

这样 `未分类` 中的历史资产可以一次性并入真实账户，同时系统账户保持幂等存在。

### 8.2 归档分类的可见性边界

`is_archived = 1` 只影响分类列表与新建下拉；资产、报表、组合汇总的 JOIN **不过滤归档**，避免迁移后报表金额凭空减少。

### 8.3 `legacy_category_id` 生命周期

仅用于回滚。用户手动修改资产分类时清空该字段；回滚脚本只处理 `legacy_category_id IS NOT NULL` 的行，绝不覆盖迁移后的用户改动。

## 9. 风险与缓解

| 风险 | 缓解 |
|---|---|
| SQLite 不能给存量表加 NOT NULL 外键 | 可空列 + 全量回填 + 仓储层拒绝 `account_id<=0` + usecase 强校验；PostgreSQL 回填后 `SET NOT NULL` |
| 回填关键词误判机构/账户 | 旧值保留在 `legacy_category_id` 与 `account_no`；提供回滚脚本；UI 支持改名/合并/迁移 |
| 系统行重复生成 | `UNIQUE(user_id, ledger_id, code)` + `INSERT OR IGNORE` / `ON CONFLICT DO NOTHING` |
| 多账本串数据 | 所有机构/账户/资产查询强制 `user_id + ledger_id`；集成测试覆盖跨账本越权 |
| 旧版二进制回退到新库 | 明确不支持；只支持执行回滚脚本后再用旧版 |
| AI / 移动端缺 `account_id` | 统一落「未分类」系统账户，响应回显实际账户 |
| 汇率缺失导致 CNY 合计偏差 | 复用 `exchange_rate_get_to_cny()` 行为，缺失时按 1:1 并在 UI 标注「汇率缺失」 |
| V018 数据迁移 SQL 冗长且两种方言易分叉 | 两边实现同一算法；迁移专项测试用同一 V016 样本库断言结果一致 |

## 10. 测试计划

### 单元测试

- 新增 `backend/tests/unit/test_domain_institution.c`：类型枚举、名称长度、系统行保护。
- 新增 `backend/tests/unit/test_domain_account.c`：类型枚举、`account_no` 长度、系统行保护。
- 改造 `backend/tests/unit/test_domain_asset.c`：`account_id` 校验。
- 改造 `backend/tests/unit/test_domain_repositories.c`：账户聚合、计数、合并。

### 集成测试

- 新增 `backend/tests/test_accounts.sh`：机构/账户 CRUD、资产必填归属、删除拦截、合并（同机构/跨机构/系统账户 source）、汇总金额、viewer 只读、RBAC。
- 回归：`test_link.sh`、`test_ledgers.sh`（账本隔离）、`test_ai_tool_call.sh`（工具必填 `account_id`）、`test_fx_oauth.sh`（注册播种系统行）、`test_dca_cashflow.sh`。

### 迁移专项

1. 用 V016 样本库启动新二进制，断言：资产行数不变、`account_id` 全非空、每条资产分类可解析、机构/账户数与预期一致。
2. 执行回滚脚本，断言 `category_id` 还原、`is_archived` 清零。
3. 重复启动一次，断言系统行与 15 个分类不重复。

### 前端测试

- vitest：`accountStore` 的 `invalidate()` / `reset()`；`AssetInput.account_id` 必填校验。
- Playwright：建机构 → 建账户 → 建资产 → 资产页按账户筛选 → 合并账户。

## 11. 交付顺序

| 里程碑 | 内容 | 完成标志 |
|---|---|---|
| M1 | V017 / V018 / 回滚脚本 | 迁移专项测试通过 |
| M2 | 领域层 + 仓储层 | 单元测试通过 |
| M3 | 应用层 + HTTP 接口 | `test_accounts.sh` 通过 |
| M4 | 分类收敛与系统行播种（5 个调用点） | 注册/建账本即有 15 分类 + 3 组系统行 |
| M5 | 桌面前端（Accounts 页 + Assets 改造） | `npm run build` 通过 + 手动冒烟 |
| M6 | 移动端 / AI / CSV 兼容 | 回归套件通过 |
| M7 | 全量回归 | `ctest` + 全部集成测试 + 前端测试通过 |
```
