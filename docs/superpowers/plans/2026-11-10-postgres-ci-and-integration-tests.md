# PostgreSQL CI 步骤与全量集成测试套件适配实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 Minefolio 增加基于 GitHub Actions 的独立 PostgreSQL CI 验证 Job，并抽取跨引擎数据库助手以全量适配 8 个集成测试脚本，实现 SQLite 与 PostgreSQL 双引擎生产级自动化保障。

**Architecture:** 
1. 抽取 `backend/tests/db_helper.sh` 作为跨引擎测试接入层，统一抽象生命周期、查询执行与两端方言差异（通过 PG Schema 清洗与兼容函数）；
2. 改造 8 个既有集成测试脚本，剥离对 `sqlite3` CLI 的强耦合，支持外部驱动与连接串注入，且未配置时 100% 保持 SQLite 默认行为；
3. 在 `.github/workflows/ci.yml` 中新增独立的 `backend-postgres` Job，挂载 `postgres:16-alpine` 服务容器，编译链接 `libpq` 并全量执行 8 个集成测试。

**Tech Stack:** C23, CMake, csilk framework, PostgreSQL 16 (`libpq`), SQLite3, Bash, GitHub Actions CI.

**Spec:** [`docs/superpowers/specs/2026-11-10-postgres-ci-and-integration-tests-design.md`](file:///data/home/quintin/workspace/source/c/Minefolio/docs/superpowers/specs/2026-11-10-postgres-ci-and-integration-tests-design.md)

## Global Constraints

- 保持 100% 向后兼容：所有测试脚本在未设置 `MINEFOLIO_DB_DRIVER` 时保持 SQLite 默认路径与无损运行。
- 保留 DDD 分层与各域业务代码不变：不擅自改动 `domain/*` 或 `application/*`。
- CI 独立且并行：PostgreSQL CI 作为独立 Job（`backend-postgres`），不拖慢 SQLite 构建与 Release 制品打包。
- 零外部测试框架新增：保持纯 Bash + curl + jq/node 既有轻量测试工具链。

## Review Focus

1. **Schema 重置彻底性**：PostgreSQL 模式下每次测试执行前后必须完全清理已有数据，避免上一个测试遗留的 users/accounts/transactions 污染后续测试。
2. **SQL 方言一致性**：`printf('%.Nf', val)`、`json_extract()`、`last_insert_rowid()` 在 PostgreSQL 环境下由兼容函数正确处理，确保数值与文本断言与 SQLite 完全一致。
3. **布尔字段兼容**：SQLite 中 `0`/`1` 与 PostgreSQL 中 `boolean` (`TRUE`/`FALSE`) 的 DDL/DML 与查询返回在断言时保持等价。
4. **服务就绪检测**：在启动 `minefolio` 守护进程后，必须通过 `/healthz` 端点轮询就绪状态，防止请求过早发送引发 502/连接失败。
5. **进程与现场清理**：无论测试成功或失败退出（EXIT/SIGINT/SIGTERM），服务器子进程必须被可靠 kill，临时文件被清理。

---

### Task 1: 编写跨引擎测试数据库助手 `backend/tests/db_helper.sh`

**Files:**
- Create: `backend/tests/db_helper.sh`
- Create: `backend/tests/test_db_helper.sh`

**Interfaces:**
- Produces: 
  - `db_init_context(default_port, suite_name)`: 解析 `MINEFOLIO_DB_DRIVER`（默认为 `sqlite`）、设置 DSN 与端口。
  - `db_clean()`: SQLite 下清理 db/wal/shm 文件；Postgres 下清理 public schema 并建立兼容函数。
  - `db_query(sql)`: 输出纯文本结果（无表头/无边框）。
  - `db_exec(sql)`: 执行 DDL/DML。
  - `db_start_server(port, build_dir, [log_file])`: 启动 `minefolio` 并在 `/healthz` 成功时返回 `SERVER_PID`。
  - `db_stop_server(pid)`: 优雅停止服务器进程。

- [ ] **Step 1: 编写 `test_db_helper.sh` 单元自测脚本**

创建 `backend/tests/test_db_helper.sh`，覆盖：
1. SQLite 模式下上下文初始化、表创建、数据插入与查询；
2. 服务启动与 `/healthz` 就绪检测与优雅停止；
3. 退出时清理逻辑。

- [ ] **Step 2: 运行自测脚本验证其在未实现时失败**

```bash
bash backend/tests/test_db_helper.sh
```
Expected: FAIL (No such file: db_helper.sh)

- [ ] **Step 3: 实现 `backend/tests/db_helper.sh`**

包含：
- 驱动侦测（`MINEFOLIO_DB_DRIVER:-sqlite`）；
- SQLite 与 PostgreSQL 双分支的 `db_clean`、`db_query`、`db_exec`；
- PostgreSQL 下在 schema 清理后注入 `printf`、`last_insert_rowid`、`json_extract` 兼容函数；
- `db_start_server` 封装 `/healthz` 轮询和进程监控；
- `db_stop_server` 封装 SIGTERM 与 wait。

- [ ] **Step 4: 运行自测脚本验证通过**

```bash
bash backend/tests/test_db_helper.sh
```
Expected: PASS (所有断言通过)

- [ ] **Step 5: 提交 Task 1 产物**

```bash
git add backend/tests/db_helper.sh backend/tests/test_db_helper.sh
git commit -m "feat(tests): 🛠️ create shared cross-engine test db helper"
```

---

### Task 2: 适配核心集成测试套件 `test_link.sh`

**Files:**
- Modify: `backend/tests/test_link.sh`

**Interfaces:**
- Consumes: `backend/tests/db_helper.sh` 中的 `db_init_context`, `db_clean`, `db_query`, `db_exec`, `db_start_server`, `db_stop_server`
- Produces: 支持 `MINEFOLIO_DB_DRIVER=sqlite` 与 `MINEFOLIO_DB_DRIVER=postgres` 的双驱动 `test_link.sh`

- [ ] **Step 1: 引入 `db_helper.sh` 并替换服务启动/清理模板**

在 `backend/tests/test_link.sh` 头部 source `db_helper.sh`，调用 `db_init_context 18080 "linktest"`，并在 cleanup trap 中调用 `db_clean` 与 `db_stop_server`。

- [ ] **Step 2: 批量替换 `sqlite3 "$DB"` 为 `db_query` / `db_exec`**

将所有的查询类 `$(sqlite3 "$DB" "SELECT ...")` 统一替换为 `$(db_query "SELECT ...")`；
将所有的插表/更新类 `sqlite3 "$DB" "INSERT/UPDATE ..."` 统一替换为 `db_exec "..."`。

- [ ] **Step 3: 调整方言微小差异（布尔值与数值）**

- Case 40 中：
  `UPDATE users SET totp_secret='JBSWY3DPEHPK3PXP', totp_enabled=1 WHERE id=1`
  调整为：
  `UPDATE users SET totp_secret='JBSWY3DPEHPK3PXP', totp_enabled=TRUE WHERE id=1`
- `SELECT totp_enabled FROM users WHERE id=1`
  调整为：
  `SELECT CASE WHEN totp_enabled THEN 1 ELSE 0 END FROM users WHERE id=1`

- [ ] **Step 4: 本地 SQLite 模式执行并验证 100% 通过**

```bash
./backend/tests/test_link.sh
```
Expected: 全部测试通过，FAIL=0，PASS >= 194。

- [ ] **Step 5: 提交 Task 2 产物**

```bash
git add backend/tests/test_link.sh
git commit -m "feat(tests): 🔄 adapt test_link.sh for dual-engine sqlite and postgres support"
```

---

### Task 3: 适配其余 7 个集成测试脚本

**Files:**
- Modify: `backend/tests/test_2fa.sh`
- Modify: `backend/tests/test_dca_cashflow.sh`
- Modify: `backend/tests/test_ai_trace.sh`
- Modify: `backend/tests/test_ai_tool_call.sh`
- Modify: `backend/tests/test_market_sync.sh`
- Modify: `backend/tests/test_ledgers.sh`
- Modify: `backend/tests/test_fx_oauth.sh`

**Interfaces:**
- Consumes: `backend/tests/db_helper.sh`
- Produces: 支持环境变量驱动覆盖的 7 个独立集成测试脚本

- [ ] **Step 1: 适配 `test_2fa.sh`**
  - 支持外部 `MINEFOLIO_DB_DRIVER` 与 `MINEFOLIO_DB_DSN`，接入 `db_helper.sh`。

- [ ] **Step 2: 适配 `test_dca_cashflow.sh`**
  - 接入 `db_helper.sh`，替换 5 处 `sqlite3 "$DB_PATH"` 为 `db_query`/`db_exec`。

- [ ] **Step 3: 适配 `test_ai_trace.sh` 与 `test_ai_tool_call.sh`**
  - 接入 `db_helper.sh`，替换 trace 插入与查询逻辑。

- [ ] **Step 4: 适配 `test_market_sync.sh`、`test_ledgers.sh` 与 `test_fx_oauth.sh`**
  - 接入 `db_helper.sh`，替换 `sqlite3` 直查为 `db_query`/`db_exec`。

- [ ] **Step 5: 本地回归执行全部 7 个脚本**

```bash
cd backend
./tests/test_2fa.sh
./tests/test_dca_cashflow.sh
./tests/test_ai_trace.sh
./tests/test_ai_tool_call.sh
./tests/test_market_sync.sh
./tests/test_ledgers.sh
./tests/test_fx_oauth.sh
```
Expected: 7 个测试脚本在 SQLite 模式下全部成功（0 失败）。

- [ ] **Step 6: 提交 Task 3 产物**

```bash
git add backend/tests/test_*.sh
git commit -m "feat(tests): 🔄 adapt auxiliary integration test suites for postgres compatibility"
```

---

### Task 4: 在 GitHub Actions CI 中配置 PostgreSQL 步骤与 Job

**Files:**
- Modify: `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: `backend/tests/*.sh` (全部 8 个适配好的集成脚本)
- Produces: 独立的 `backend-postgres` CI 任务

- [ ] **Step 1: 在 `.github/workflows/ci.yml` 中新增 `backend-postgres` Job**

配置：
- `services: postgres:16-alpine` 容器（用户/密码/数据库为 `postgres` / `postgres_password` / `minefolio_test`，5432 端口映射，带 healthcheck）；
- 安装依赖时包含 `libpq-dev postgresql-client`；
- 执行 CMake 构建并输出编译结果；
- 注入环境变量 `MINEFOLIO_DB_DRIVER=postgres` 与标准 `PG*` 变量；
- 依次执行 8 个集成测试脚本。

- [ ] **Step 2: 校验 CI YAML 文件语法与结构**

```bash
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/ci.yml'))"
```
Expected: 0 错误输出，退出码 0。

- [ ] **Step 3: 提交 Task 4 产物**

```bash
git add .github/workflows/ci.yml
git commit -m "ci(backend): 👷 add dedicated PostgreSQL CI job with full integration test matrix"
```

---

### Task 5: 全量回归与最终验证

**Files:**
- Test across whole backend

- [ ] **Step 1: 运行全量静态语法校验**

```bash
bash -n backend/tests/*.sh
```
Expected: 全部脚本通过，无任何语法错误。

- [ ] **Step 2: 运行全部 8 个集成测试脚本 (SQLite 模式)**

```bash
cd backend
./tests/test_link.sh
./tests/test_2fa.sh
./tests/test_dca_cashflow.sh
./tests/test_ai_trace.sh
./tests/test_ai_tool_call.sh
./tests/test_market_sync.sh
./tests/test_ledgers.sh
./tests/test_fx_oauth.sh
```
Expected: 全部 8 个套件 100% 通过。

- [ ] **Step 3: 运行 CTest 单元测试套件**

```bash
ctest --test-dir backend/build --output-on-failure
```
Expected: 55/55 测试通过（100% pass）。

- [ ] **Step 4: 运行前端构建确保全局完好**

```bash
npm --prefix frontend run build
```
Expected: 0 错误，打包成功。
