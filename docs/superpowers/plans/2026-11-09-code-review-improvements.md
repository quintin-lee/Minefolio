# 代码审查改进点实施计划（4 项）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 落地 4 项代码审查改进点：清理 `transfer/usecases.c` 死代码；在 `README.md`/`AGENTS.md` 补全汇率单实例假设、`void*` 设计意图、SQLite 最低版本三类说明；在 `db_init` 启动时打印实际 SQLite 版本作为可观测性信号。

**Architecture:** 纯文档改动 + 一处极小 C 语言日志新增，不改变任何现有行为、接口或分层约束；按方案 A 拆成 2 个独立 commit（文档 + 死代码合并为 1 个；`db.c` 日志单独 1 个），便于单独回滚。

**Tech Stack:** C23 / csilk / SQLite 3.35+ / Markdown 文档（`AGENTS.md`、`README.md`）

---

## 文件结构

| 文件 | 改动类型 | 说明 |
|---|---|---|
| `backend/src/application/transfer/usecases.c` | 修改（删除 2 行） | `transfer_usecase_create()` 末尾不可达死代码 |
| `README.md` | 修改（1 行表格） | 数据库技术栈表 SQLite 版本要求 |
| `AGENTS.md` | 修改（新增 2 个小节 + 1 行表格） | Runtime/Tooling 表补 SQLite 最低版本说明；新增 `## Market / FX — 单实例假设`；`Code Conventions` 章节下新增 `void*` 设计意图小节 |
| `backend/src/common/db.c` | 修改（新增 3 行日志代码） | `db_init()` SQLite 分支打印 `sqlite3_libversion()` |

---

## 任务

### Task 1: 清理 `transfer/usecases.c` 死代码

**Files:**
- Modify: `backend/src/application/transfer/usecases.c:138-141`

- [ ] **Step 1: 确认当前代码形态**

  用 `read` 工具读 `backend/src/application/transfer/usecases.c` 第 130-142 行，确认尾部形如：

  ```c
      out_res->code = 0;
      return 0;
      out_res->code = 0;
      return 0;
  }
  ```

- [ ] **Step 2: 删除不可达的第二遍 `out_res->code = 0; return 0;`**

  用 `edit` 工具做精确字符串替换：

  ```c
  旧：
      out_res->code = 0;
      return 0;
      out_res->code = 0;
      return 0;
  }
  新：
      out_res->code = 0;
      return 0;
  }
  ```

- [ ] **Step 3: 编译验证无语法破坏**

  Run: `cmake --build backend/build --parallel`
  Expected: 编译成功，无新增告警（`transfer/usecases.c` 相关）

- [ ] **Step 4: 暂不提交（与 Task 2/3/4 合并为一个 commit，见 Task 5）**

### Task 2: 补全 `README.md` SQLite 最低版本说明

**Files:**
- Modify: `README.md`（技术栈表，数据库行，约第 48 行）

- [ ] **Step 1: 定位现有行**

  用 `grep` 确认当前文本为：
  `| **数据库** | SQLite 3 (WAL 模式) / PostgreSQL 16+ | 双引擎通用，支持动态运行时无感数据库迁移 |`

- [ ] **Step 2: 替换为含最低版本说明的文本**

  用 `edit` 工具做精确字符串替换：

  ```
  旧：
  | **数据库** | SQLite 3 (WAL 模式) / PostgreSQL 16+ | 双引擎通用，支持动态运行时无感数据库迁移 |
  新：
  | **数据库** | SQLite 3.35+ (WAL 模式) / PostgreSQL 16+ | 双引擎通用，支持动态运行时无感数据库迁移；RETURNING 语法要求 SQLite ≥ 3.35 |
  ```

- [ ] **Step 3: 暂不提交（见 Task 5）**

### Task 3: 补全 `AGENTS.md` 三处说明

**Files:**
- Modify: `AGENTS.md`（3 处独立改动）

- [ ] **Step 1: "Runtime / Tooling Preferences" 表 Backend 行下方新增 SQLite 最低版本一行**

  定位现有表格：

  ```
  | Layer | Runtime | Package Manager | Notes |
  |-------|---------|-----------------|-------|
  | Backend | Linux (gcc-14) | CMake 3.16+ | C23 standard; GNU extensions |
  ```

  在 Backend 行下方插入一行：

  ```
  | Backend (SQLite) | SQLite 3.35+ | — | 18 个 repository 实现文件依赖 `RETURNING` 语法（该特性于 SQLite 3.35 引入），版本过低时相关写入路径会失败 |
  ```

  注意：不要改动原有 Backend 行本身，仅新增一行（`Layer` 列写 "Backend (SQLite)"，与原 "Backend" 行区分开）。

- [ ] **Step 2: 新增 `## Market / FX — 单实例假设` 小节**

  定位 `AGENTS.md` 中 `## Key Directories` 章节（其下是一张 markdown 表格，末尾紧接 `## Development Commands` 章节标题）。在 `## Development Commands` 章节标题**之前**插入新小节：

  ```markdown
  ## Market / FX — 单实例假设

  汇率同步（`backend/src/services/market/exchange_rate_service.c`）当前按**单实例部署**设计：`g_rates[]` 是进程内静态内存表，`exchange_rate_refresh_all()` 无跨实例协调机制。多副本部署时不同实例的内存表可能不一致（读路径 `exchange_rate_get_to_cny` 优先查内存表，查不到时才回落到 `exchange_rate_history` 表）。

  **多副本部署前需改造或明确标注**，可选方向：
  - （a）读路径改为优先查 `exchange_rate_history` 最新一行，消除对内存表的依赖；
  - （b）引入类似 `schema_migration_lock` 的行锁机制做单写者仲裁；
  - （c）保持现状，并在部署文档中明确标注"汇率同步仅支持单实例部署"。

  本次改动仅为文档标注，不做代码改造。
  ```

- [ ] **Step 3: "Code Conventions & Common Patterns" 章节下新增 `void*` 设计意图小节**

  定位现有 `### C — Repository Pattern (STRICT)` 小节标题（位于 "Code Conventions & Common Patterns" 大章节下）。在该小节标题**之后**（即作为其相邻兄弟小节，而非嵌套进去）插入：

  ```markdown
  ### C — 用例层 `void* pool` 参数是刻意设计

  `application/*/usecases.h` 中所有用例函数签名统一使用 `void* pool`（而非 `csilk_db_pool_t*`），这是 DDD 分层刻意的解耦设计——用例层不直接 include 基础设施层（`csilk_db_pool_t` 定义所在）头文件，仅在 `.c` 实现里做一次性强转 `(csilk_db_pool_t*)pool`。

  这是**有意为之，不是疏漏**，请勿"修复"为强类型签名（改动面涉及 15 个 `usecases.h` 头文件 + 所有调用点，违背当前分层约束）。如需更强类型检查，可参考未来在 `common/db.h` 引入 `typedef csilk_db_pool_t* mf_db_pool_t` 的方案，但当前暂不实施。
  ```

- [ ] **Step 4: 暂不提交（见 Task 5）**

### Task 4: `db_init` 新增 SQLite 版本日志打印

**Files:**
- Modify: `backend/src/common/db.c:83-91`（`db_init()` 内 `if (!g_is_postgres)` 分支）

- [ ] **Step 1: 确认 `db.c` 顶部已包含 `<sqlite3.h>`**

  用 `read` 工具读 `backend/src/common/db.c` 第 1-22 行，确认已有 `#include <sqlite3.h>`（现有代码确认：第 18 行）。

- [ ] **Step 2: 在 PRAGMA 设置块之后插入版本日志**

  用 `edit` 工具做精确字符串替换（旧代码块完整粘贴自当前文件第 83-91 行）：

  ```c
  旧：
      if (!g_is_postgres) {
          csilk_db_exec(g_pool, "PRAGMA journal_mode=WAL;");
          csilk_db_exec(g_pool, "PRAGMA busy_timeout=5000;");
          csilk_db_exec(g_pool, "PRAGMA synchronous=NORMAL;");
          csilk_db_exec(g_pool, "PRAGMA foreign_keys=ON;");
          csilk_db_exec(g_pool, "UPDATE ai_traces SET top_p = 0.0 WHERE typeof(top_p) = 'text';");
          csilk_db_exec(g_pool,
                        "UPDATE ai_traces SET temperature = 0.0 WHERE typeof(temperature) = 'text';");
      }
  新：
      if (!g_is_postgres) {
          csilk_db_exec(g_pool, "PRAGMA journal_mode=WAL;");
          csilk_db_exec(g_pool, "PRAGMA busy_timeout=5000;");
          csilk_db_exec(g_pool, "PRAGMA synchronous=NORMAL;");
          csilk_db_exec(g_pool, "PRAGMA foreign_keys=ON;");
          csilk_db_exec(g_pool, "UPDATE ai_traces SET top_p = 0.0 WHERE typeof(top_p) = 'text';");
          csilk_db_exec(g_pool,
                        "UPDATE ai_traces SET temperature = 0.0 WHERE typeof(temperature) = 'text';");

          /* 可观测性：打印实际链接的 SQLite 库版本；RETURNING 语法要求 3.35+ */
          CSILK_LOG_I(
              "SQLite version: %s (min required 3.35 for RETURNING; "
              "older versions will fail on repository files using RETURNING)",
              sqlite3_libversion());
      }
  ```

- [ ] **Step 3: 编译验证**

  Run: `cmake --build backend/build --parallel`
  Expected: 编译成功；确认无未使用变量/未使用 include 类告警（`<sqlite3.h>` 已存在，无需新增 include）

- [ ] **Step 4: 手动启动验证日志输出**

  Run:
  ```bash
  cd backend
  MINEFOLIO_DB_DRIVER=sqlite MINEFOLIO_DB_DSN=./data/dev_check.db ./build/minefolio &
  sleep 2
  jobs -l
  ```
  Expected: 启动日志中包含一行 `SQLite version: 3.x.x.x (min required 3.35 for RETURNING; ...)`（具体版本号取决于本机实际链接的 SQLite）
  随后立即 `kill` 该后台进程（避免占用 8080 端口影响后续测试）：`kill %1`（或 `pkill -f ./build/minefolio`）

- [ ] **Step 5: 暂不提交（见 Task 6）**

### Task 5: 提交 commit 1（死代码 + 文档，合并提交）

**Files:** 无新增，仅 `git add` + `git commit`

- [ ] **Step 1: 暂存改动**

  Run:
  ```bash
  cd /home/quintin/Data/source/c_cpp/Minefolio
  git add backend/src/application/transfer/usecases.c README.md AGENTS.md
  git status
  ```
  Expected: `git status` 显示且仅显示这 3 个文件为 staged；`backend/src/common/db.c` 不在 staged 列表中（它属于 commit 2）

- [ ] **Step 2: 提交**

  Run:
  ```bash
  git commit -m "chore(db): 🧹 清理 transfer usecase 死代码 + 补全文档说明（汇率单实例假设 / SQLite 最低版本 / void* 设计意图）"
  ```
  Expected: `git commit` 成功，输出显示 3 files changed

### Task 6: 提交 commit 2（`db.c` 日志打印，单独提交）

- [ ] **Step 1: 暂存改动**

  Run:
  ```bash
  git add backend/src/common/db.c
  git status
  ```
  Expected: `git status` 显示且仅显示 `backend/src/common/db.c` 为 staged

- [ ] **Step 2: 提交**

  Run:
  ```bash
  git commit -m "perf(db): ⚡️ db_init 启动时打印实际 SQLite 版本作为可观测性信号（仅新增日志，无行为变化）"
  ```
  Expected: `git commit` 成功，输出显示 1 file changed

### Task 7: 全量验证（构建 + 集成测试）

- [ ] **Step 1: 完整重新构建**

  Run: `cmake --build backend/build --parallel`
  Expected: 编译成功，0 error

- [ ] **Step 2: 跑核心集成测试套件**

  Run: `cd backend && ./tests/test_link.sh`
  Expected: 40 个 case / 174 断言全部通过（与改动前基线一致，本次改动均为文档 + 纯新增日志，不应改变任何断言结果）

- [ ] **Step 3: 确认无需跑前端构建**

  本次改动不涉及 `frontend/` 任何文件，无需执行 `npm --prefix frontend run build`。

## 验收标准对照（与 spec 一致）

| Spec 验收项 | 对应 Task |
|---|---|
| 1. `transfer_usecase_create()` 末尾无不可达重复 `return 0` | Task 1 |
| 2. `README.md` 数据库行含 `SQLite 3.35+`；`AGENTS.md` Runtime/Tooling 表含 SQLite 最低版本说明 | Task 2 + Task 3 Step 1 |
| 3. `AGENTS.md` 含 `## Market / FX — 单实例假设` 小节与 `C — 用例层 void* pool 参数是刻意设计` 小节 | Task 3 Step 2 + Step 3 |
| 4. `db_init()` SQLite 分支启动时打印 `sqlite3_libversion()`，不阻断启动 | Task 4 |
| 5. `cmake --build` 通过；`test_link.sh` 全绿 | Task 7 |
| 6. 按方案 A 拆成 2 个 commit | Task 5 + Task 6 |

## 非目标（明确不做，与 spec 一致）

- 不改 `application/*/usecases.h` 的 `void*` 签名。
- 不改造 `exchange_rate_service.c` 的读/写路径或引入分布式锁。
- 不做全库死代码扫描（仅限 `transfer/usecases.c` 这一处）。
- 不把 SQLite 版本过低处理成启动失败/硬门禁。
