# 多账本与家庭协同空间系统设计规范 (Multi-Ledger & Family Spaces Design Specification)

## 1. 业务目标与核心场景 (Goals & Use Cases)

Minefolio 当前支持个人单账本的多资产记账与投资追踪。随着使用场景扩展，用户通常具有多维度资产隔离与家庭协作需求：
1. **多场景核算隔离**：个人日常消费、家庭公共资产、副业/工作室、子女教育基金等账本独立核算，避免不同场景流水相互干扰。
2. **多用户家庭/团队协同**：支持将家庭成员或合伙人邀请加入特定账本，共同记账（`editor`）或仅授权查账（`viewer`），实现家庭资产透明与统一管理。
3. **平滑升级与向后兼容**：存量用户数据无缝迁移至默认账本，不破坏已有 API、报表与移动端离线核算能力。

---

## 2. 系统架构与数据模型 (Architecture & Data Models)

### 2.1 数据表定义 (`migration.sql` & `migration_postgres.sql`)

```sql
-- 1. 账本主表
CREATE TABLE IF NOT EXISTS ledgers (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    owner_id INTEGER NOT NULL,
    name TEXT NOT NULL,
    description TEXT,
    currency TEXT NOT NULL DEFAULT 'CNY',
    icon TEXT DEFAULT 'ph:wallet',
    color TEXT DEFAULT '#3b82f6',
    is_default INTEGER NOT NULL DEFAULT 0,
    invite_code TEXT UNIQUE,
    invite_expires_at DATETIME,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (owner_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_ledgers_owner ON ledgers(owner_id);
CREATE INDEX IF NOT EXISTS idx_ledgers_invite ON ledgers(invite_code);

-- 2. 账本成员关系表
CREATE TABLE IF NOT EXISTS ledger_members (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ledger_id INTEGER NOT NULL,
    user_id INTEGER NOT NULL,
    role TEXT NOT NULL CHECK (role IN ('owner', 'editor', 'viewer')),
    joined_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (ledger_id, user_id),
    FOREIGN KEY (ledger_id) REFERENCES ledgers(id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_ledger_members_user ON ledger_members(user_id);
CREATE INDEX IF NOT EXISTS idx_ledger_members_ledger ON ledger_members(ledger_id);
```

### 2.2 存量业务表关联扩充
在以下数据表中增加 `ledger_id INTEGER REFERENCES ledgers(id)`：
- `assets` (资产账户与投资标的)
- `transactions` (主交易流水)
- `daily_expenses` (日常收支记录)
- `categories` (账本自定义分类，`NULL` 代表系统预置公共分类)
- `dca_plans` (定投计划)
- `cashflow_schedules` (被动现金流计划)

### 2.3 自动平滑数据迁移策略 (`db_run_migrations`)
系统启动执行迁移时：
1. 检查是否存在 `ledgers` 与 `ledger_members` 表，若无则创建。
2. 检查 `assets` 等表是否存在 `ledger_id` 字段，若无则执行 `ALTER TABLE ADD COLUMN ledger_id INTEGER`。
3. 对每个已存在的用户（`users`），若无账本，则自动创建一条 `is_default=1, name='默认账本'` 的记录，并在 `ledger_members` 中建立 `owner` 角色关系。
4. 将该用户历史所有 `ledger_id IS NULL` 的资产、流水、分类、定投、现金流规则回填为此默认账本 ID。

---

## 3. 后端权限控制与 API 接口 (Backend Security & APIs)

### 3.1 账本上下文解析与鉴权工具 (`ctx.h` & `ledger_service.h`)
- **上下文提取函数**：`int64_t ctx_ledger_id(csilk_ctx_t* c, int64_t user_id, const char* required_role)`
  1. 优先从 Header `X-Ledger-Id`（或 Query/Body `ledger_id`）提取。
  2. 若未指定，自动查询当前用户所属的 `is_default=1` 默认账本。
  3. 校验 `user_id` 在 `ledger_members` 中是否存在。
  4. 角色检查：
     - 若 `required_role == "owner"`：要求角色必须为 `owner`。
     - 若 `required_role == "editor"`：要求角色为 `owner` 或 `editor`（写权限）。
     - 若 `required_role == "viewer"`：要求角色为 `owner`、`editor` 或 `viewer`（读权限）。
  5. 鉴权失败直接返回 HTTP 200 + Code `1004 Forbidden`。

### 3.2 账本管理 RESTful 接口矩阵

| Method | Path | 权限要求 | 描述 |
|---|---|---|---|
| `GET` | `/api/ledgers` | Logged In | 列出当前用户拥有的或参与的所有账本及角色 |
| `POST` | `/api/ledgers` | Logged In | 创建新账本（自动设创建者为 Owner） |
| `GET` | `/api/ledgers/:id` | `viewer`+ | 获取账本详情及资产/收支概览统计 |
| `PUT` | `/api/ledgers/:id` | `owner` | 更新账本名称、说明、图标、色彩、默认币种 |
| `DELETE` | `/api/ledgers/:id` | `owner` | 解散并删除账本（级联删除私有资产及流水） |
| `GET` | `/api/ledgers/:id/members` | `viewer`+ | 获取账本成员列表 |
| `POST` | `/api/ledgers/:id/members` | `owner` | 按用户名添加成员并指定角色 (`editor`/`viewer`) |
| `PUT` | `/api/ledgers/:id/members/:user_id` | `owner` | 修改成员角色 (`editor`/`viewer`) 或转让所有权 |
| `DELETE` | `/api/ledgers/:id/members/:user_id` | `owner`/Self | 移除成员或成员主动退出账本 |
| `POST` | `/api/ledgers/:id/invite-code` | `owner` | 生成/刷新 6 位加入邀请码（有效期 7 天） |
| `POST` | `/api/ledgers/join` | Logged In | 通过邀请码直接加入账本 |

---

## 4. 前端状态与交互设计 (Frontend UX & State Management)

### 4.1 全局状态管理 (`useLedgerStore`)
- **Store 状态**：
  - `currentLedger`: 当前选中的账本对象（含 `id`, `name`, `currency`, `role`）。
  - `ledgers`: 用户参与的所有账本列表。
  - `isViewer`: 计算属性，当前用户在当前账本是否为只读角色。
- **请求拦截器 (`http.ts`)**：
  - 自动向所有发往 `/api/*` 的请求头注入 `X-Ledger-Id: currentLedger.id`。
  - 切换账本时更新 LocalStorage，并触发页面组件重新加载当前账本数据。

### 4.2 顶部导航栏账本切换器 (`Layout.vue`)
- 在 Header 左侧（或 Logo 旁）展示**账本切换器组件 (`LedgerSelector.vue`)**：
  - 显示当前账本名称、主题图标与角色标签（如 `👨‍👩‍👧‍👦 家庭账本 [管理员]`、`💼 副业账本 [所有者]`）。
  - 下拉菜单列出所有账本，点击无缝快速切换。
  - 底部提供快捷操作：「创建新账本」、「输入邀请码加入」、「账本设置与成员管理」。

### 4.3 账本管理与成员协作弹窗 (`LedgerSettingsDialog.vue`)
- **账本基本信息**：名称、描述、货币、图标、颜色。
- **成员协作管理**：
  - 成员列表（用户名、加入时间、角色 Tag）。
  - 邀请方式 1：输入系统用户名直接添加并赋予权限。
  - 邀请方式 2：一键生成/复制 6 位邀请码与分享链接。
  - 角色变更与移除成员。
  - 账本解散/退出操作。

### 4.4 权限感知与只读保护
- 当 `isViewer === true` 时：
  - 隐藏或禁用「新建资产」、「记一笔支出/收入」、「新建交易」、「新增定投」等所有写入按钮。
  - 页面顶部提示「当前处于只读查账模式」。

---

## 5. 测试与验证策略 (Testing & QA)

1. **多账本数据隔离自动化测试 (`test_ledgers.sh`)**：
   - 用户 A 创建私有账本与家庭账本；
   - 用户 B 仅被邀请加入家庭账本；
   - 验证用户 B 无法读取或写入用户 A 的私有账本数据；
   - 验证用户 B 作为 `viewer` 时写操作被 1004 拦截，作为 `editor` 时正常记账；
   - 验证邀请码生成与加入链路。
2. **存量数据回归验证 (`test_link.sh`)**：
   - 确保全部原有单用户自动化测试与回归测试 100% 保持通过。
3. **双端编译与构建校验**：
   - `cmake --build backend/build --parallel && npm --prefix frontend run build` 确保 0 警告 0 错误。
