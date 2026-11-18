# 定时任务管理中心（Scheduler Center）设计方案

## 1. 意图与背景 (Intent & Why)

当前 Minefolio 的周期性后台逻辑（行情拉取、汇率同步、DCA 定投生成、AI 财经早报、MCP 进程清理）分散且硬编码在 `market_scheduler.c` 的后台循环中。前端仅在“设置”页提供了简易的行情轮询间隔选项，存在以下痛点：
1. **缺乏可观测性与运维控制**：用户无法直观查看后台各项任务的运行状态、执行历史、成功率、耗时与错误日志，也无法手动立即触发单次执行；
2. **缺乏个性化与自主性**：用户无法自主创建定时任务去定期收集感兴趣的外部资讯或周期性汇总生成财务报表；
3. **触达渠道被动**：任务跑完仅静默写库，缺乏主动触达手段，用户必须每次主动打开电脑或网页检索。

本项目的目标是在 Minefolio 中构建一个**通用的定时任务管理中心（Scheduler Center）**，提供完整的作业生命周期管理，支持“AI 智能分析”与“结构化报表”双模式，支持时钟与市场事件双触发，并将最终生成的精炼简报自动推送至用户的个人微信。

---

## 2. 目标、非目标与反向目标 (Goals, Non-Goals, Anti-Goals)

### 2.1 目标 (Goals)
1. **统一管理界面**：在 Web 页面左侧导航“系统”组增加独立的「定时任务」（`/scheduler`）一级页面，提供运行指标看板、任务列表、创建/编辑向导与执行历史日志抽屉。
2. **任务双模式创建**：
   - **AI 智能任务 (Prompt 驱动)**：用户以自然语言下达分析与搜集指令，可勾选授权注入持仓上下文、联网资讯搜索工具，由 Unified AI Runtime 自动整合并提炼。
   - **结构化报表任务 (表单规则驱动)**：用户通过表单选择系统内置模版（持仓盈亏汇总、多账本损益、收支复盘等），零 Token 消耗、精确计算。
3. **双触发机制 (Dual Triggers)**：
   - **定时时钟触发**：预设频次（每日/工作日/周末某时间点）或标准 5 段式 Cron 表达式。
   - **市场事件驱动**：行情时段事件（A股/美股收盘后）或资产波动阈值（持仓单日涨跌超幅预警）。
4. **个人微信主动交付**：将提炼好的结构化 Markdown 简报卡片通过免挂机推送服务（Server酱 / PushDeer / 企业微信代推 / 自定义 Webhook）秒级推送到用户个人微信。
5. **系统内置任务纳入管理**：原本硬编码的系统底层任务（行情同步、汇率刷新、DCA 检查等）以预置任务形式纳入列表，可观测、可调频、可手动触发。

### 2.2 非目标 (Non-Goals)
1. **不构建通用的远程 Shell 脚本执行平台**：不提供在宿主机上执行任意 Bash/Python 命令的入口，杜绝命令注入与越权安全隐患。
2. **不引入外部重型中间件**：不依赖 Redis、Celery、RabbitMQ 或外部进程守护器，保持 Minefolio 单二进制独立部署架构。
3. **不自建需要常驻挂机的个人微信客户端**：不内置需要微信扫码挂机的本地机器人容器，采用合规、稳定且免封号风险的通用 Webhook 推送协议。

### 2.3 反向目标 (Anti-Goals)
1. **防止后台任务阻塞主服务**：严禁在调度主循环中同步执行耗时较长的网络搜索、大模型流式调用或微信推送；必须完全异步入池。
2. **防止任务雪崩与资源耗尽**：对并发执行的任务数、超时时间及网络重试次数设下硬限，严禁无限制堆积作业。
3. **防止多用户间数据跨越泄露**：在多用户环境下，定时任务严格归属于创建它的 `user_id`，其 AI 上下文与报表范围绝不可越权读取其他用户数据。

---

## 3. 架构设计与方案选型 (Approaches)

在需求推演中，我们评估了三种技术路径：

| 方案 | 实现方式 | 优势 | 劣势 | 结论 |
| :--- | :--- | :--- | :--- | :--- |
| **方案一：原生 C 统一调度与异步事件工作池** | 在 C 后端重构 `market_scheduler.c` 为 `core/scheduler/` 调度器，结合定时器轮询与行情事件总线，Worker 线程池异步执行与推送。 | 零额外依赖，内存占用极低 (<1MB)，与 AI Runtime/仓储层零通信开销，毫秒级事件响应，完全契合单二进制自托管原则。 | 需要在 C 中实现稳健的时间解析与 Worker 线程池控制。 | **采纳 (Selected)** |
| **方案二：外部 Sidecar 进程模式 (Node/Python)** | C 后端仅开放 API，外部单独运行 Node/Python 进程跑 Cron 调度并负责发送微信。 | 现成 Cron 库丰富。 | 破坏了单二进制开箱即用体验；多进程部署复杂；无法原生高效捕获行情瞬时异动事件。 | 否决 (Dropped) |
| **方案三：简易数据库轮询器** | 纯数据库记录 `next_run_at`，由单线程每分钟扫描一次并同步执行。 | 实现最简单。 | 无法支持市场事件触发；长耗时 AI 任务会导致后续作业严重积压与漂移。 | 否决 (Dropped) |

---

## 4. 详细方案设计 (Detailed Design)

### 4.1 数据库架构 (Database Schema - Migration V021)

为支持 SQLite 与 PostgreSQL 双引擎兼容，新增版本迁移脚本：
`backend/sql/migrations/{sqlite,postgres}/V021__scheduled_jobs_and_logs.sql`

#### 表 1：`scheduled_jobs`（作业配置表）
```sql
CREATE TABLE scheduled_jobs (
    id                  BIGSERIAL PRIMARY KEY,           -- SQLite 为 INTEGER PRIMARY KEY AUTOINCREMENT
    user_id             BIGINT NOT NULL,                 -- 所属用户 ID (0 表示系统全局内置任务)
    name                VARCHAR(128) NOT NULL,           -- 任务名称
    description         TEXT DEFAULT '',                 -- 任务描述
    job_type            VARCHAR(32) NOT NULL,            -- 'ai_prompt' | 'structured_report' | 'system_builtin'
    trigger_type        VARCHAR(32) NOT NULL,            -- 'cron' | 'market_event' | 'market_session'
    trigger_config      TEXT NOT NULL,                   -- JSON 格式: 
                                                         -- cron: {"cron":"0 5 * * 2-6"}
                                                         -- market_event: {"event":"market_closed","market":"US"} 或 {"event":"price_spike","threshold":0.03,"asset_scope":"all_holdings","cooldown_minutes":240}
    payload_config      TEXT NOT NULL,                   -- JSON 格式: 
                                                         -- ai_prompt: {"prompt":"...", "with_holdings":true, "with_web_search":true}
                                                         -- structured_report: {"template":"portfolio_daily", "ledger_id":1}
    delivery_channel    VARCHAR(32) NOT NULL DEFAULT 'user_default', -- 'user_default' (采用用户配置的通道) | 'custom_webhook' | 'silent_db'
    delivery_config     TEXT DEFAULT '{}',               -- 覆写配置: {"webhook_url":"..."} (留空则读取用户级通道凭证)
    enabled             INTEGER NOT NULL DEFAULT 1,      -- 1: 启用, 0: 禁用
    last_run_at         VARCHAR(32) DEFAULT '',          -- 上次运行时间 (YYYY-MM-DD HH:MM:SS)
    last_run_status     VARCHAR(16) DEFAULT '',          -- 'success' | 'failed' | 'running'
    last_run_duration_ms INTEGER DEFAULT 0,              -- 上次耗时 (毫秒)
    next_run_at         VARCHAR(32) DEFAULT '',          -- 预计下次运行时间
    created_at          TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at          TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX idx_scheduled_jobs_user_enabled ON scheduled_jobs(user_id, enabled);

-- 表 1.1：user_notification_channels（用户推送通道配置表，一次配置全任务复用）
CREATE TABLE user_notification_channels (
    id                  BIGSERIAL PRIMARY KEY,
    user_id             BIGINT NOT NULL UNIQUE,          -- 每个用户维护自己的专属推送凭证
    channel_type        VARCHAR(32) NOT NULL,            -- 'serverchan' | 'pushdeer' | 'wechat_work' | 'custom_webhook'
    channel_name        VARCHAR(64) NOT NULL DEFAULT '个人微信',
    webhook_url         TEXT DEFAULT '',                 -- Webhook 地址
    secret_key          TEXT DEFAULT '',                 -- SendKey / Token / Secret (加密或受保护存储)
    is_active           INTEGER NOT NULL DEFAULT 1,
    created_at          TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at          TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);
```

#### 表 2：`job_run_logs`（运行历史与审计表）
```sql
CREATE TABLE job_run_logs (
    id                  BIGSERIAL PRIMARY KEY,
    job_id              BIGINT NOT NULL REFERENCES scheduled_jobs(id) ON DELETE CASCADE,
    user_id             BIGINT NOT NULL,
    started_at          TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    completed_at        TIMESTAMP,
    status              VARCHAR(16) NOT NULL,            -- 'success' | 'failed' | 'timeout'
    duration_ms         INTEGER NOT NULL DEFAULT 0,
    trigger_reason      VARCHAR(64) NOT NULL,            -- 'cron_tick' | 'manual_invoke' | 'market_closed' | 'price_spike'
    output_summary      TEXT DEFAULT '',                 -- 推送生成的简报内容摘要
    error_message       TEXT DEFAULT '',                 -- 错误堆栈/失败原因
    notification_status VARCHAR(16) DEFAULT 'none'       -- 'sent' | 'failed' | 'none'
);
CREATE INDEX idx_job_run_logs_job_id ON job_run_logs(job_id, started_at DESC);
CREATE INDEX idx_job_run_logs_user_started ON job_run_logs(user_id, started_at DESC);
```

---

### 4.2 后端核心架构 (`core/scheduler/` & `services/`)

```
backend/src/
├── core/scheduler/
│   ├── scheduler_engine.h/.c    # 调度主引擎：管理任务队列、秒级时间轮心跳、Cron 表达式解析
│   ├── scheduler_events.h/.c    # 事件总线：发布与监听市场事件（收盘、价格暴涨跌）
│   ├── scheduler_worker_pool.h/.c # 异步工作线程池：解耦主循环，异步派发任务执行
│   └── cron_parser.h/.c         # 纯 C 标准 5 段式 Cron 解析与下次触发时间计算
├── application/scheduler/
│   ├── usecases.h/.c            # 作业增删改查、启停开关、手动立即触发、日志查询用例
│   └── dtos.h                   # 请求与响应 DTO 结构
├── infrastructure/repositories/
│   └── scheduler_repo_impl.c    # scheduled_jobs 与 job_run_logs 的 DDD SQL 仓储实现
├── services/notification/
│   └── wechat_push_service.h/.c # 微信简报推送适配器 (Server酱, PushDeer, 通用 Webhook)
└── interfaces/http/controllers/
    └── scheduler_controller.h/.c # REST 接口注册与参数绑定
```

#### 关键时序与执行链路：
1. **启动自检**：`main.c` 启动时初始化 `scheduler_engine_start(pool)`，从数据库加载所有 `enabled=1` 的作业，并计算各作业的 `next_run_at`。
2. **时钟触发**：引擎线程通过 `pthread_cond_timedwait` 等待最近到期的作业；触发时生成运行日志记录并抛入 `scheduler_worker_pool`，随后计算该作业下一次执行时间。
3. **事件驱动触发**：`quote_engine` 在批量刷新行情后，检测是否有已注册的事件任务（如某资产当日涨跌幅绝对值达到设定阈值），匹配成功后立即向事件队列发布触发信号。
4. **异步作业执行**：
   - 若为 **`ai_prompt` 模式**：Worker 自动装载当前用户的持仓资产列表与基础指标作为上下文，构建系统 Prompt，调用 Unified AI Runtime（`ai_runtime_execute`），允许模型调用 Web 搜索等只读工具，最终生成总结简报。
   - 若为 **`structured_report` 模式**：Worker 直接调用 `application/report/usecases.c` 计算目标账本的周期盈亏、分类收支明细并拼装结构化数据卡片。
5. **微信推送交付**：
   - 调用 `wechat_push_service_send()`，将排版优美的 Markdown 简报（资产变动概览 + AI 资讯提炼 + 预警提醒）通过 HTTPS POST 推送至用户配置的个人微信服务通道。
   - 更新 `job_run_logs` 状态为 `success` 并记录耗时与微信投递响应。

---

### 4.3 REST API 接口设计

| 方法 | 路径 | 描述 | 权限要求 |
| :--- | :--- | :--- | :--- |
| `GET` | `/api/scheduler/dashboard` | 获取概览统计（活跃任务数、今日触发次数、下次就近触发、推送通道状态） | 登录用户 |
| `GET` | `/api/scheduler/jobs` | 分页/全量获取任务列表（普通用户查自身，管理员附带系统级底座任务） | 登录用户 |
| `POST` | `/api/scheduler/jobs` | 创建新的定时作业（支持 AI 提示词与结构化报表配置） | 登录用户 |
| `GET` | `/api/scheduler/jobs/:id` | 获取单个任务详情（需归属当前用户或管理员查看系统任务） | 登录用户 |
| `PUT` | `/api/scheduler/jobs/:id` | 修改任务配置（系统任务仅管理员可改） | 归属校验 / 管理员 |
| `DELETE` | `/api/scheduler/jobs/:id` | 删除自定义任务（内置系统任务禁止删除） | 归属校验 |
| `POST` | `/api/scheduler/jobs/:id/toggle` | 快速启用 / 禁用任务（系统任务仅管理员可操作） | 归属校验 / 管理员 |
| `POST` | `/api/scheduler/jobs/:id/run` | 手动立即执行一次该任务（调试验证） | 归属校验 / 管理员 |
| `GET` | `/api/scheduler/jobs/:id/logs` | 查询该任务的历史执行日志（带分页、耗时、状态与输出摘要） | 归属校验 / 管理员 |
| `GET` | `/api/scheduler/channel` | 获取当前用户的微信推送通道配置（脱敏密钥） | 登录用户 |
| `PUT` | `/api/scheduler/channel` | 更新当前用户的微信推送通道配置 (Server酱/PushDeer/Webhook) | 登录用户 |
| `POST` | `/api/scheduler/channel/test` | 测试个人微信通道连通性（立即发送一条测试卡片） | 登录用户 |

### 4.4 触发语义与防重防刷机制 (Trigger Semantics & Deduplication)
1. **时钟触发 (Cron / Interval)**：由调度引擎在内存维护有序事件最小堆，精确匹配触发时间并按原子操作前移 `next_run_at`，防止重复消费。
2. **事件驱动 (Market Events)**：
   - 资产异动预警（如涨跌幅超过设定阈值）：采用**边缘触发 (Edge Trigger)** 与**冷却窗口 (Cooldown)** 机制。任务触发一次后，在配置的冷却期（默认 240 分钟或当前交易日内）进入抑制状态，防止行情轮询每次拉取都触发推送骚扰微信。
   - 市场收盘事件：在 `quote_engine` 计算当日闭市状态翻转（由开盘变为收盘）的瞬时触发一次。

---

### 4.4 前端界面设计 (`frontend/src/views/Scheduler.vue`)

1. **左侧导航集成**：
   在 `Layout.vue` 的 `navGroups.system` 分组中增加：
   ```html
   <el-menu-item index="/scheduler" @click="goTo('/scheduler')">
     <Icon icon="ph:clock-countdown-duotone" class="nav-icon" />
     <template #title><span>{{ t('nav.scheduler') }}</span></template>
   </el-menu-item>
   ```
2. **主页面结构**：
   - **顶部指标卡片 (Metrics Grid)**：展示运行中任务数、今日调度总次数、微信推送成功率、就近下次触发倒计时。
   - **操作栏 (Action Bar)**：
     - `+ 新建定时任务` 按钮
     - `微信推送通道配置` 按钮
     - 任务类型过滤标签（全部 / AI 智能分析 / 结构化报表 / 底层系统任务）
   - **任务表格 (Jobs Table)**：
     - 字段：任务名称、类型 Tag、调度频次、交付通道、上次运行状态及耗时、启用开关（`el-switch`）。
     - 操作列：`立即执行`（带 loading 状态）、`编辑`、`运行日志`。
3. **新建/编辑弹窗 (JobDialog.vue)**：
   - 顶部 Tab 切换：`AI 智能任务 (Prompt 驱动)` 与 `结构化报表任务 (表单驱动)`。
   - 触发时机配置：支持常用单选（工作日收盘后、每天指定时刻、每周五等）与高级 Cron 输入框。
   - AI 任务专区：持仓上下文注入开关（带脱敏选项）、联网搜索授权开关。
4. **日志抽屉组件 (JobLogDrawer.vue)**：
   - 侧边抽屉展示最近 30 天运行记录。
   - 支持展开单次运行生成的完整微信简报文本或错误堆栈。

---

## 5. 约束与边界规则 (Constraints)

1. **DDD 分层与架构规范**：
   - 依赖方向严格单向：Controller → Application → Domain / Infrastructure / Core。
   - 用例层函数入参保持刻意设计的 `void* pool` 接口。
   - 所有 SQL 查询使用 `?` 占位符，写操作使用 `csilk_db_exec_param`，避免堆分配泄漏。
2. **双引擎与无差别回归**：
   - 迁移脚本 `V021` 必须同时编写 SQLite 与 PostgreSQL 版本，且在本地隔离环境（`run_isolated.sh`）下验证双引擎集成测试。
3. **并发安全与资源限制**：
   - 调度工作线程池最大并发数限制为 3，单任务执行超时时间为 60 秒。
   - AI 任务强制复用 Unified AI Runtime 的 Token 预算与超时守卫，杜绝死循环。

---

## 6. 留给构建者的实现细节 (Left to the Builder)

1. **Cron 解析算法**：推荐采用紧凑的纯 C 标准 Cron 匹配器（支持 `*`、`*/N`、`,`、`-` 以及标准 5 段）。
2. **微信推送工具适配细节**：优先实现 Server酱与 PushDeer 协议（只需单参数 URL/SendKey），以及通用 Webhook POST JSON 格式。
3. **UI 主题与多语言**：在 `frontend/src/utils/locale.ts` 中补充中英文多语言键值，Element Plus 样式保持与现有暗黑/明亮主题完全一致。
