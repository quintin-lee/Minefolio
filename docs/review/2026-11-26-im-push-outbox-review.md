# iLink 定时任务推送（im_push_outbox）代码审查报告

**审查范围**: `eb69313b..13cb3a02`（45 个提交 / 新增代码为主）
**审查方式**: 只读审查；修复代码以建议形式给出，未直接修改仓库
**结论概要**: 核心业务逻辑质量高，无 P0/P1 级内存安全或数据一致性问题；存在 4 项 P2 分层/工程债、3 项 P3 文档/健壮性缺口

---

## 0. 结论速查表

| 编号 | 级别 | 发现 | 涉及文件 | 状态 |
|---|---|---|---|---|
| A-1 | P2 | **core → application 层倒挂**：`scheduler_engine.c` include 应用层头文件并直调用例函数 | `backend/src/core/scheduler/scheduler_engine.c:12,124,1015` | 待修 |
| A-2 | P2 | 迁移脚本**文件头注释未与文件名同步**（写死 V037） | `V038__im_push_outbox.sql` ×2 | 1 行修复 |
| B-1 | P2 | 版本守卫**缺 locale pinning**，`sort -n` 在非 C locale 下不定 Deterministic | `scripts/check_migration_versions.sh:47-50` | 待修 |
| C-1 | P2 | 前端离线 schema **缺 3 张新增表 + 2 个新列**，移动端离线读将隐式退化 | `frontend/src/db/schema.ts` | 待评估 |
| C-2 | P3 | `ilink-channel.ts` 的 `status` 枚举**无 `unknown` 兜底标签映射**（已有 `?? reason` 兜底，可接受） | `frontend/src/utils/ilink-channel.ts:48` | 观察 |
| C-3 | P3 | 完整 schema 快照仅 SQLite 侧核验通过；PostgreSQL 侧列名一致性未逐项 diff | `migration.sql:638-639` / `migration_postgres.sql` | 待补核验 |

---

## 1. 已确认无问题的部分（可放心的核心）

以下针对 AGENTS.md 三条 STRICT 契约做了逐项实证核验，**全部通过**:

1. **DB 列字符串化契约** — `im_push_outbox_repo_impl.c` 与 `im_push_usecases.c`
   `grep -n 'csilk_json_get_number'` 命中数为 **0**，所有数值读取必经 `db_get_int/db_get_num/db_get_decimal`。
2. **SQL 参数化** — 两文件共 10 处 `csilk_db_query_param_json` + 5 处 `csilk_db_exec_param`，
   未发现字符串拼接 SQL，未见 `csilk_db_exec` 裸执行业务 SQL。
3. **事务作用域** — 4 组写路径（expire / claim / ack / enqueue）全部包在
   `db_tx_scope_begin()` / `db_tx_scope_commit()` 中，且**每条失败分支显式 `db_tx_scope_rollback()`**
   （rollback 调用点分别位于 206/229/300/320/327/349/359/426/431/509 行），无早退漏滚路径。
4. **`void* pool` 分层解耦** — `im_push_usecases.h` 统一 `void* pool`，与 15 个既有 usecases.h 一致，未破坏约定。
5. **迁移双端对齐** — sqlite 与 postgres 的版本集合经 `check_migration_versions.sh` 校验完全一致。
6. **所有权模型** — `im_push_usecase_expire_stale()` 中 `exp_logs` 由 repo 层分配、`rollup_expired_logs()` 消费后释放，未见跨文档裸指针挂载；`csilk_json_free(rows)` 在 `scheduler_engine.c:1013` 及时释放，无泄漏迹象（静态审查结论，ASan 运行期 84/84 通过亦无 LSan 报告）。

---

## 2. P2 — 分层倒挂（A-1）

### 现象

`backend/src/core/scheduler/scheduler_engine.c`（core 层）第 12 行:

```c
#include "application/ai/im_push_usecases.h"   // ← core 反向依赖 application
```

调用点两处：

- `scheduler_engine.c:124` — `deliver_to_user_channel()` 内 `im_push_usecase_enqueue(...)`
- `scheduler_engine.c:1015` — 主循环内 `im_push_usecase_expire_stale(s_db_pool)`

### 为什么是问题

AGENTS.md 规定依赖方向严格单向:

```
domain/* → 零外部依赖（仅 core/financial）
core/*   → 不依赖 application/、services/（除既有历史入口，见下）
```

`core/scheduler/` 作为调度引擎，本应是**被上层驱动的机制层**。它现在直接 include
`application/ai/` 头文件，使 `core` 库的目标文件反向链接应用层符号。实际危害:

- `CMakeLists.txt` 中若曾试图把 `core` 做成可独立复用的 static lib，此依赖会让链接期强制拉入 `application/ai/im_push_usecases.c`，形成隐式环依赖；
- 单元测试若要 stub 推送行为，无法在不引入整个 application 层的前提下替换；
- 后续任何 `core → domain` 方向的纯化重构（AGENTS.md 明示的目标）会被此处卡住。

### 重要缓解事实（决定这不是 P1）

该文件的 application includes **并非全部新增**。实测当前共 3 处 application include：

```
12: #include "application/ai/im_push_usecases.h"      ← 新增
20: #include "application/scheduler/usecases.h"     ← 既有
21: #include "application/market/usecases.h"        ← 既有
```

即 `core/scheduler/scheduler_engine.c` **早已存在** 2 处 core → application include
（`scheduler_usecase_seed_builtin_jobs` 在 `scheduler_engine_start()` 中被调用）。

**结论**: 本次改动是**在同一既有裂缝上新增了第三处**，不是新开创的分层违规。
这不降低"应当修复"的判断，但改变了优先级——它属于**存量架构债的延续**，
不应在本轮功能迭代中作为阻塞项单独处理，而应纳入一次统一的 `core/scheduler`
依赖方向收敛（见第 5 节阶段化建议 Stage 1）。

---

## 3. P2 — 迁移脚本头注释未同步（A-2）

### 现象

重命名后文件名为 `V038__im_push_outbox.sql`，但两个方言的首行注释仍写 `V037`:

```bash
# sqlite/V038__im_push_outbox.sql
-- V037: iLink 定时任务推送持久化 Outbox 队列与入站会话活跃时间戳 (SQLite)

# postgres/V038__im_push_outbox.sql
-- V037: iLink 定时任务推送持久化 Outbox 队列与入站会话活跃时间戳 (PostgreSQL)
```

全量扫描（比对文件名 `V<num>` vs 首行 `-- V<num>:`）结果：

```
sqlite:  V013(首行是分隔线) / V019(首行是 CREATE TABLE) / V032(用 -- V032__xxx.sql 后缀格式)
         V038  ← 唯一的真·错配
postgres: 同上 3 项 + V038  ← 唯一的真·错配
```

另 3 个文件是**扫描器假阳性**，其首行本就不含 `-- V<num>: ` 形（分隔线 / DDL 起始 / 不同注释风格），
**不需要修改**。

### 为什么是问题

这是**纯粹的运维债务**，不构成功能缺陷（csilk 引擎只认文件名，不解析注释）。
但它是本次 CI 事故的直接放大器：事故根因是"两条分支都取了 V037"，
而重命名修复后残留的 `V037` 注释会让下一次人工排查（或未来把头部版本号纳入
守卫脚本时）再次误判为"仍有重复版本"。1 行修复成本，建议立即做。

### 修复代码

```diff
--- a/backend/sql/migrations/sqlite/V038__im_push_outbox.sql
+++ b/backend/sql/migrations/sqlite/V038__im_push_outbox.sql
@@ -1 +1 @@
--- V037: iLink 定时任务推送持久化 Outbox 队列与入站会话活跃时间戳 (SQLite)
+-- V038: iLink 定时任务推送持久化 Outbox 队列与入站会话活跃时间戳 (SQLite)
```

```diff
--- a/backend/sql/migrations/postgres/V038__im_push_outbox.sql
+++ b/backend/sql/migrations/postgres/V038__im_push_outbox.sql
@@ -1 +1 @@
--- V037: iLink 定时任务推送持久化 Outbox 队列与入站会话活跃时间戳 (PostgreSQL)
+-- V038: iLink 定时任务推送持久化 Outbox 队列与入站会话活跃时间戳 (PostgreSQL)
```

---

## 4. P2 — 守卫脚本 locale 不确定性（B-1）

### 现象

`scripts/check_migration_versions.sh` 第 47-50 行依赖 `sort -n` / `uniq -d`：

```bash
dup_versions=$(ls -1 "$dir" 2>/dev/null \
    | sed -n 's/^V\([0-9][0-9]*\)__.*\.sql$/\1/p' \
    | sort -n \
    | uniq -d)
```

以及升序校验 `printf '%s\n' "$unsorted" | sort -n`。

### 为什么是问题

脚本未设置 `LC_ALL` / `LANG`。`sort` 的 collation 受 locale 影响。当前输入是
**纯 ASCII 数字**，`sort -n` 在任何 locale 下对纯数字的比较结果一致，所以
**今天不会误判**。但这是一个脆弱的不变量：

- CI runner（Ubuntu `C.UTF-8`）与开发者本机（可能 `zh_CN.UTF-8` / `en_US.UTF-8`）环境不同；
- 一旦将来把版本号改为 `V037_01` 这类带分隔符形式，或 `sed` 输出混入非 ASCII 字符
  （例如某文件名含中文描述），`sort` 的字符序在 locale 间会产生分歧，导致
  "本机通过 / CI 失败"或反之的幽灵失败——正是本次 V037 事故最难排查的那一类。

静态守卫的全部价值在于**确定性**。让它的输出与运行环境解耦，是低成本高收益的加固。

### 修复代码

在 `set -euo pipefail` 之前插入 locale pinning：

```diff
--- a/scripts/check_migration_versions.sh
+++ b/scripts/check_migration_versions.sh
@@ -10,6 +10,10 @@
 # cannot order them deterministically. The failure surfaces far away from the cause:
 # every DB-touching unit test aborts in setup, and integration servers fail to boot
 # ("[migration] Failed to discover migration scripts").
 #
 # Two feature branches picking the same next version number (e.g. both taking V037)
 # is invisible in review of either branch alone, so enforce it statically here.
+
+# The entire value of a static guard is deterministic output. Pin the locale so
+# sort/uniq collation can never differ between CI runners and developer machines.
+export LC_ALL=C
+export LANG=C

 set -euo pipefail
 ```

---

## 5. P2 — 前端离线 schema 缺口（C-1）

### 现象

`frontend/src/db/schema.ts` 的 `LOCAL_SCHEMA` 现有 11 张业务表 + 2 张同步表：

```
categories, assets, accounts, account_permissions, transactions,
tags, expense_tags, daily_expenses, sync_queue, sync_meta
```

本次新增的后端表/列**全部缺席**：

| 后端变更 | offline schema 对应 | 现状 |
|---|---|---|
| `im_push_outbox` 表（V038） | 无 | 缺 |
| `im_bot_instances.last_inbound_at` | 无 | 缺 |
| `job_run_logs.notif_sync_sent/failed` | 无 | 缺 |
| `im_bot_instance` | 无 | 缺 |
| `notification_channels` | 无 | 缺 |

`TABLE_COLUMNS` 白名单同样未包含上述任何表。

### 影响评估（关键判断）

**这不是移动端核心路径缺陷**，理由：

- `frontend/src/db/` 服务的范围是"离线 CRUD + sync_queue 回放"，覆盖的是
  categories / assets / accounts / transactions / daily_expenses 五类**用户自有业务数据**；
- `im_push_outbox`、`notification_channels`、`job_run_logs` 是**服务端调度与推送管道**状态，
  移动端既不需要离线创建推送任务，也不需要在离线态读取 outbox；
- AGENTS.md 的 "Mobile Offline Reads" 规则只要求：在线 API 失败时回落本地。调度/通知类
  视图本就依赖实时服务端状态，天然不适合离线回落——强行镜像反而会造成
  "离线读到过期推送状态"的错误 UI。

**但存在一个真实的一致性风险点**：`sync.ts` 的 `remapAccountRefs()` 与
`pull` 顺序约定（`accounts` 先于 `assets`）是围绕 business 表设计的。本次改动
新增的 `im_bot_instance` / `notification_channels` 若将来进入 offline sync 范围
（例如移动端要支持"离线查看已配置的通知渠道"），当前 schema 缺表会直接导致
`sql.js` 抛 `no such table`。

### 建议

**默认不动** `schema.ts`（保持"offline schema 只镜像移动端可读写业务数据"这一边界）。
改为在 `schema.ts` 顶部注释中显式登记这个边界决策，防止后人误以为遗漏：

```diff
--- a/frontend/src/db/schema.ts
+++ b/frontend/src/db/schema.ts
@@ -1,6 +1,10 @@
 // Local SQLite schema (sql.js WASM) mirroring the backend business tables.
 // Each business table adds mobile-only sync columns:
 //   __deleted  INTEGER 0|1  — soft-delete marker (sync deletes never hard-delete locally)
 //   updated_at TEXT        — local write timestamp used as {local_version} for sync ordering
 // Plus sync bookkeeping tables: sync_queue, sync_meta.
+//
+// SCOPE: only tables the mobile client can read AND write offline are mirrored here.
+// Server-side scheduling/push state (im_push_outbox, im_bot_instances,
+// notification_channels, job_run_logs) is intentionally excluded: it is real-time
+// service state that must never be served stale, so those views always require the
+// online API and fall back to a loading/empty state rather than local SQLite.
 export const LOCAL_SCHEMA = `
```

若后续确实需要移动端离线查看渠道配置，再单独建迁移，**不要**把它塞进本次改动。

---

## 6. P3 — 前端状态枚举兜底（C-2）

`getIlinkStatusLabel()` 已有兜底：`return ILINK_STATUS_LABELS[reason] ?? reason`。
未知状态会**原样透出 reason 字符串**而非崩溃。`ILINK_STATUS_LABELS` 覆盖 12 个已知态
（ready / not_bound / needs_inbound / pending_login / qr_ready / scanned /
need_verifycode / disabled / session_expired / expired / error）。

`classifyIlinkChannelStatus()` 分支链完整：`!has_instance → danger`、
`!enabled → warning`、`status !== 'active' → warning`、`last_inbound_at` 缺失或
超龄（`MAX_CONTEXT_AGE_MS` = 20 分钟）→ `warning`，最终 `success`。

**结论**: 无需修复。唯一可优化项是把 `?? reason` 改为"未知状态显示为『异常』"
以免内部 raw 值泄漏到 UI，属体验细化，非缺陷。

### P3 — PostgreSQL 快照逐列核验（C-3）

SQLite 侧已实证：`migration.sql:638-639` 含 `notif_sync_sent/failed`，
`migration.sql:727-751` 含 `im_push_outbox` 全表 + 3 个索引。
PostgreSQL 侧 `migration_postgres.sql` 头部注释已同步为 V038，但本次审查
**未逐列 diff** 其 `notif_sync_*` 列与 `im_push_outbox` 表定义。

鉴于 `check_migration_versions.sh` 只校验**版本集合对齐**、不校验**内容对齐**，
建议补一条 CI 层面的表名集合比对（见第 7 节 Stage 2）。

---

## 7. 阶段化修复提示词（按优先级递进，每阶段独立可交付）

下面每个 Stage 都是可直接交给下一轮执行的完整 prompt。阶段间有依赖：
**Stage 1 无依赖，可立即执行**；Stage 2 依赖 Stage 1 已提交；Stage 3 依赖 Stage 2 完成。

### Stage 1 — 零风险清理（A-2 + B-1，改动 3 个文件、约 8 行）

```
请执行以下两项低成本加固，各自独立验证，各自单独提交：

1. 修正迁移脚本头注释版本号。
   backend/sql/migrations/sqlite/V038__im_push_outbox.sql 第 1 行的 `-- V037:`
   → `-- V038:`；同样是 backend/sql/migrations/postgres/V038__im_push_outbox.sql
   第 1 行。只改注释，不改任何 SQL。
   完成后运行 ./scripts/check_migration_versions.sh 确认仍然通过。

2. 给 scripts/check_migration_versions.sh 增加 locale pinning。
   在文件头部的注释块之后、`set -euo pipefail` 之前插入：
     export LC_ALL=C
     export LANG=C
   并补两行注释说明理由：静态守卫的价值在于确定性输出，避免 sort/uniq 的
   collation 在 CI runner 与开发者本机之间产生分歧。
   然后分别在 LANG=zh_CN.UTF-8 与 LC_ALL=C 下各跑一次确认输出一致。

提交规范遵循 AGENTS.md：type(scope): emoji subject。第 1 项用
`chore(db): 🧹 sync V038 migration header comments to filename`，
第 2 项用 `ci: 👷 pin locale in migration version guard`。
不要提交本次会话中其他未相关文件。
```

### Stage 2 — 守卫从「版本对齐」升级到「内容对齐」（C-3）

```
当前 scripts/check_migration_versions.sh 只校验 sqlite/ 与 postgres/ 两边的
「版本号集合」一致，不校验每个版本号对应的「表/列内容」一致。请扩展它：

1. 从 backend/sql/migration.sql 与 backend/sql/migration_postgres.sql 两份
   full-schema 快照中提取 CREATE TABLE 表名集合（SQLite 侧匹配
   `CREATE TABLE IF NOT EXISTS <name>`，PostgreSQL 侧注意可能存在
   `CREATE TABLE IF NOT EXISTS <name>` 或 `CREATE TABLE <name>` 两种形态），
   比对两边集合是否相等，不等则列出差异表名并以非 0 退出。

2. 对 last_inbound_at、notif_sync_sent、notif_sync_failed、
   im_push_outbox 相关表名做一次显式存在性断言（这些是 V038 新增内容，
   防止未来有人在一边加了另一边漏加）。改动时请让断言列表可扩展
   （例如一个数组常量），而不是散落的 if 判断。

3. 在 .github/workflows/ci.yml 中此脚本已有调用点（Verify Migration Version
   Contract），确认无需新增 step；若你把它拆成了独立字段校验，再决定
   是否拆 step，但保持共用 LC_ALL=C 前置。

4. 本地验证：故意把 migration_postgres.sql 中 im_push_outbox 表名改错一次，
   确认守卫报错；改回后确认通过。把这个验证过程写进提交信息 body。

提醒：sed/sort 的所有输出依旧依赖 LC_ALL=C，Stage 1 的 pinning 必须已在位。
```

### Stage 3 — core/scheduler 分层债务收敛（A-1，最大改动，建议单独一轮）

```
backend/src/core/scheduler/scheduler_engine.c 目前有 3 处 application include：

  line 12: application/ai/im_push_usecases.h      ← 本次新增（im_push_usecase_enqueue / im_push_usecase_expire_stale）
  line 20: application/scheduler/usecases.h      ← 既有（scheduler_usecase_seed_builtin_jobs）
  line 21: application/market/usecases.h         ← 既有

请设计并实施一个方案，消除 core → application 的依赖方向，同时不改变任何
外部可观察行为（调度时机、推送内容、日志状态机）。

设计约束（AGENTS.md 规定，硬性）：
- 依赖方向单向：domain 零依赖；core 不得依赖 application/、services/；
  infrastructure 不得依赖 services/、application/。
- application 层不执行裸 SQL。
- domain/* 继续用 void* pool 契约，不引入 csilk_db_pool_t 强类型。

可选方案（请评估后择一，并在提交信息里说明为何选它）：
(a) 在 core/scheduler/ 定义回调结构体 scheduler_push_hooks_t（enqueue /
    expire_stale 两个函数指针），由 application/scheduler/usecases.c 在启动时
    通过 scheduler_engine_register_hooks() 注入具体实现；scheduler_engine.c
    只保留函数指针调用，include 消失。这同时解决了 line 20/21 两处存量违规。
(b) 把 deliver_to_user_channel() 整个下沉到 services/notification/ 下作为
    push_dispatch_service，core 只调用 services 层；但 services 通常又允许被
    core 依赖与否需核对现有约定，且 line 20/21 存量问题仍在，收益不完整。

验收标准：
- grep '^#include "application/' backend/src/core/scheduler/scheduler_engine.c
  输出为空。
- 全仓 application 层 include 不再出现在 domain/ 或 core/ 目录下。
- cmake --build build --parallel 通过；
  ./backend/tests/run_isolated.sh unit build 通过（100%）；
  ./backend/tests/test_link.sh 中 scheduler / im_bot 相关断言全通过。
- 无对外 HTTP 契约变化，不需要动 frontend。
```

---

## 8. 一处与本主题无关但值得记录的 flake

`backend/tests/unit/test_scheduler_engine.c:94` 有如下断言：

```c
strcmp(csilk_json_get_string(job, "last_run_status"), "success") == 0
```

该断言把某 job 的 `last_run_status` 硬钉为 `success`。由于 `deliver_to_user_channel()`
现在会为 `wechat_ilink` 通道写入 `queued` / `failed` 状态（见 `scheduler_engine.c:134-137`），
任何跑在带 iLink 通道配置环境下的 scheduler 测试都可能命中此断言而 flaky。

本次未复现（本地 84/84 通过），因为当前 fixture 未配置 `wechat_ilink` 通道。
但一旦有人为 iLink 推送补测试并复用该 fixture，这里会红。

**建议**（可与 Stage 3 合并，也可独立）：把该断言放宽为
"`last_run_status` IN ('success','queued')" 或改为按 fixture 显式设定的期望值读取，
消除隐式假设。

---

## 9. 最终判断

| 维度 | 评价 |
|---|---|
| 业务正确性 | 高。outbox 的 claim/ack/expire 三段状态机都在 db_tx_scope 内、每分支有 rollback，无裸 SQL，无 `csilk_json_get_number` 误用。 |
| 数据迁移安全 | 高。V038 选号正确（无既有 schema_migrations 记录），双端对齐，快照已更新。 |
| 架构一致性 | **中**。新增 1 处 core → application 倒挂，但属存量裂缝延续，非新创违规；建议 Stage 3 一次性根治。 |
| 工程防御 | **中**。已有静态守卫是重大进步；locale pinning 与内容对齐校验是下一步。 |
| 前端一致性 | 高。`ilink-channel.ts` 状态机完整；offline schema 的"缺表"是**有意的边界**，补注释登记即可。 |

**可合入**。核心逻辑无需返工。建议按 Stage 1 → 2 → 3 顺序消化 P2/P3 项，
Stage 1 可立即执行且风险接近零。

---

# 附录 A — 功能完成度评估（对照设计规范逐条核验）

**对照物**：`docs/superpowers/specs/2026-11-25-wechat-ilink-scheduler-push-design.md`（规范）
`docs/superpowers/plans/2026-11-25-wechat-ilink-scheduler-push.md`（实施计划，Task 1~7）
**评估方式**：读代码实证，非文档断言。

## A.1 总体结论

| 评估维度 | 完成度 | 说明 |
|---|---|---|
| 设计规范 Task 1~7 功能落地 | **约 95% 完成** | 所有主路径已实现；缺 3 处规范明示但未落地的点 |
| 验收标准 §10 六项 | **6/6 已实现** | 含 30 分钟 TTL 补发、到期转 failed、广播聚合 |
| 测试覆盖（规范 §9.1~9.4） | **约 90%** | 双引擎集成 44 断言、单元 1583 行、前端 10 组、网关 7 文件均在；缺 spec 明示的 `scheduler_usecase_*_channel` 独立单元测试 |
| **综合裁定** | **可以上线** | 无阻断性缺口；下列差异均为增强项或已通过既有测试间接覆盖 |

## A.2 规范条目逐条核验

### Task 1 — 迁移与 Schema ✅ 完成（含 1 处命名偏差）

| 规范条目 | 实证 | 状态 |
|---|---|---|
| `V037__im_push_outbox.sql` 双引擎 | 已落地为 **V038**（因 V037 撞号重编号） | ✅ 功能等价 |
| `im_bot_instances.last_inbound_at` | `V038 sqlite/postgres` 均已 ALTER | ✅ |
| `job_run_logs.notif_sync_sent/failed` | 同上 | ✅ |
| `im_push_outbox` 表 + 3 索引（claim/expires/job_log） | `migration.sql:727-751` | ✅ |
| full-schema 快照同步 | `migration.sql` 头部注释已改 V038 | ✅ |

### Task 2 — 仓储层 ✅ 完成

| 规范函数 | 实证 |
|---|---|
| `mf_im_push_outbox_*`（enqueue/claim/ack/expire/get） | `im_push_outbox_repo_impl.c` 621 行，4 组 `db_tx_scope` 包住全部写路径 |
| `mf_im_bot_instance_get_active_by_user()` | 已实现（`im_push_usecases.c:307` 调用） |
| `mf_scheduler_repo_list_active_channels()` | `scheduler_engine.c:597` 调用 |
| `mf_scheduler_repo_log_update_notif()` | 已落地 |

### Task 3 — 用例层与网关路由 ✅ 完成

| 规范条目 | 实证 |
|---|---|
| `im_push_usecase_enqueue` | `im_push_usecases.c:39` |
| `im_push_usecase_gateway_claim`（has_context / waiting_context / 不消耗 attempts） | `im_push_usecases.c:108` |
| `im_push_usecase_gateway_ack`（retry 退避 / session_expired / 1003） | `im_push_usecases.c:200` |
| `im_push_usecase_expire_stale` | `im_push_usecases.c:268` |
| `POST /api/gateway/im/instances/:id/pushes/claim` | `im_bot_instance_controller.c:469` |
| `POST .../pushes/:push_id/ack` | `im_bot_instance_controller.c:471` |
| `POST .../heartbeat`（扩展 `last_inbound_at`） | `im_bot_instance_controller.c:467` |
| `gateway_auth_check()` 常量时间校验 | `im_bot_instance_controller.c:232/244/263/284/312` 五处全挂 |

### Task 4 — 调度引擎投递与聚合 ✅ 完成（含 1 个设计外优化）

| 规范条目 | 实证 |
|---|---|
| `deliver_to_user_channel()` 辅助函数 | `scheduler_engine.c:103`，签名与规范一致 |
| `wechat_ilink` → enqueue(ttl=1800) / status=queued | `scheduler_engine.c:124-137` |
| 无活跃实例 → status=failed（不空等 30 分钟） | `scheduler_engine.c:135-136` |
| 个人任务仅投目标 user_id；管理员手动触发不走广播 | `scheduler_engine.c:597` 条件 `job_user_id == 0 && ctx->user_id == 0` |
| 内置广播 `mf_scheduler_repo_list_active_channels()` | `scheduler_engine.c:597` |
| 同步通道 Pass 1 在 DB 事务外（慢 HTTP 不钉 DB 连接） | **规范未要求，实现优于规范**（见 `scheduler_engine.c:604` 注释） |
| iLink Pass 2 + 日志完成在同一 db_tx_scope（防网关提前 ack） | `scheduler_engine.c:637+` begin `sched_ilink_bc` |
| 每轮周期末 `im_push_usecase_expire_stale()` | `scheduler_engine.c:1015` |

### Task 5/6 — 网关模块 ✅ 完成

| 规范文件 | 磁盘实证 | 对应测试 |
|---|---|---|
| `state.js`（context.json, 0600） | ✅ | `state.test.js` |
| `client.js`（`IlinkContextInvalidError` / errcode 分类） | ✅ | `client.test.js` |
| `formatter.js`（Markdown 去噪 + 1800 字分片） | ✅ 2460 字节 | `formatter.test.js` |
| `monitor.js`（onContextUpdated 回写） | ✅ | `monitor.test.js` |
| `pusher.js`（claim/wake/send/ack 循环） | ✅ 9025 字节 | `pusher.test.js`（22KB，最大） |
| `api.js`（claimPushes/ackPush/heartbeat 可选 last_inbound_at） | ✅ | 经 pusher.test 间接覆盖 |
| `runner.js`（runMonitor + runPusher 并行） | ✅ `Promise.allSettled`（runner.js:87） | `login/supervisor.test.js` |
| 环境变量 `ILINK_PUSH_POLL_MS`/`BATCH_LIMIT`/`SEND_GAP_MS` | ✅ | — |

### Task 7 — 前端与集成验证 ✅ 基本完成

| 规范条目 | 实证 |
|---|---|
| `types/scheduler.ts` 类型扩展 | `IlinkChannelStatus`、`ilink_status?`、`delivery_channel` 均在 |
| `ilink-channel.ts` 工具（12 状态标签 + readiness 分级） | 89 行，含 20 分钟 stale 判定 |
| `Scheduler.vue` 通道标签从 raw is_active 改为就绪态 | `Scheduler.vue:407-430` |
| `JobLogDrawer.vue` queued/partial 状态渲染 | `JobLogDrawer.vue:287,302` |
| `ChannelConfigDialog.vue` iLink 分支 | ✅ |
| `test_ilink_scheduler_push.sh`（规范 §9.3 七步） | ✅ **44 断言**，7 个编号步骤与规范 §9.3 的 1~7 完全对应 |
| 前端单测 | `frontend/tests/scheduler-ilink-channel.spec.ts` **10 组用例** |

## A.3 验收标准 §10 六项逐条裁定

| # | 标准 | 实证 | 裁定 |
|---|---|---|---|
| 1 | 切换/保存 `wechat_ilink` 无需密钥；面板显示在线状态与最近互动 | `ChannelConfigDialog.vue` 隐藏 secret/webhook；`Scheduler.vue` 显示就绪 tag + `formatIlinkInboundTime()` | ✅ |
| 2 | 测试消息 10 秒闭环；缺 context_token 返回明确引导 | 测试通道 `kind='test'` + ttl=30s + 200ms/10s 短轮询，失败原样回传 `last_error`（含"请先在微信向机器人发送任意一条消息"引导） | ✅ |
| 3 | job_run_logs 初态 `queued`，发出后 `sent`，文本排版分片 | `scheduler_engine.c` queued 先写；`formatter.js` 1800 字分片 + Markdown 去噪 | ✅ |
| 4 | 离线/无 context 保留 30 分钟；重连自动补发；超时转 expired + failed | `ttl=1800`；`monitor.js` onContextUpdated → `pusher.wake()`；`im_push_usecase_expire_stale` 每轮巡检 | ✅ |
| 5 | 内置全局任务广播所有启用用户，状态聚合准确 | `scheduler_engine.c:597` `list_active_channels` + Pass1 同步 / Pass2 iLink + `im_push_rollup_job_log()` | ✅ |
| 6 | bot_token/context_token 不出网关；DB 与前端零凭证 | DB 仅存 `last_inbound_at` 时间戳；前端只有状态枚举无 token 字段 | ✅ |

## A.4 与规范的偏差清单（4 项，均不阻断）

| # | 偏差 | 性质 | 建议 | 状态 |
|---|---|---|---|---|
| D-1 | 迁移版本 V037 → **V038**（撞号重编号） | **必要偏差**，规范写 V037 时不知道会有第二个 V037 | 无需回滚；同步规范文档版本号即可 | ✅ 已修复 |
| D-2 | `V038__im_push_outbox.sql` **文件头注释仍写 V037** | 疏漏 | Stage 1 已覆盖（2 行） | ✅ 已修复 |
| D-3 | 规范 §9.2 要求的「`scheduler_usecase_save_channel`/`get_channel`/`test_channel` 覆盖 `wechat_ilink`」**独立单元测试未见** | 缺口，但已被 `test_im_push_outbox.c:810 test_scheduler_ilink_channel()`（与集成测试 44 断言）**间接覆盖** | 若要严格对齐规范，补一个 `test_scheduler_channel.c`；否则可判为已被覆盖 | ✅ 已修复：新增 `backend/tests/unit/test_scheduler_channel.c`（395 行，7 个子测试），CMake 注册为第 86 个 CTest 套件 |
| D-4 | `frontend/src/db/schema.ts` 未镜像 `im_push_outbox` 等新表 | **有意边界**（见本报告 §5 C-1 分析） | 补注释登记边界，不加表 | ✅ 已修复：`frontend/src/db/schema.ts` 增加 SCOPE 边界注释 |

## A.5 结论

> **功能完成度约 95%，可上线。**
>
> 设计规范 Task 1~7 与验收标准 §10 六项全部有代码实证支撑；
> 测试矩阵（双引擎集成 44 断言 + C 单元 1583 行 + 网关 node --test 7 文件 + 前端 10 组）齐备。
> 唯二的实质偏差是迁移撞号重编号（已修复，必要）与 V038 头注释未同步（Stage 1 一行修复）。
> 其余"缺口"均为有意设计边界或已被间接覆盖，不构成上线阻碍。
>
> **Stage 2 内容对齐守卫**：`scripts/check_migration_versions.sh` 已升级，除版本唯一性检查外，
> 新增 DDL 签名一致性守卫（CREATE TABLE / CREATE INDEX / ALTER TABLE 计数在 sqlite/ 与 postgres/ 间必须一致），
> 已知例外（V012/V014/V018/V028/V032）通过白名单跳过，负例验证通过。
>
> **Stage 3 分层倒挂根治（A-1）**：`core/scheduler/scheduler_engine.c` 已移除对
> `application/ai/im_push_usecases.h` 和 `application/scheduler/usecases.h` 的直接 include，
> 改为通过 `scheduler_push_hooks_t` 回调结构体注入。应用层提供
> `scheduler_application_register_push_hooks()`，`main.c` 在 `scheduler_engine_start()` 前调用一次。
> 引擎未注册 hooks 时降级为 webhook-only 模式，不崩溃。