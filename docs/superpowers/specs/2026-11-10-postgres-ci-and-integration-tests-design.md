# PostgreSQL CI 步骤与全量集成测试套件适配设计方案

- **日期**：2026-11-10
- **状态**：Pending Review (待评审)
- **目标范围**：GitHub Actions PostgreSQL CI Job、共享数据库抽象工具 `backend/tests/db_helper.sh`、8 个集成测试脚本（`test_link.sh`、`test_2fa.sh`、`test_dca_cashflow.sh`、`test_ai_trace.sh`、`test_ai_tool_call.sh`、`test_market_sync.sh`、`test_ledgers.sh`、`test_fx_oauth.sh`）双驱动适配

---

## 1. 意图与背景 (Intent & Why)

Minefolio 核心架构设计为支持 SQLite 和 PostgreSQL 双数据库引擎（DDD 基础设施层实现与 Flyway 风格双方言迁移脚本 V001~V021）。
然而，目前在 CI 流水线（`.github/workflows/ci.yml`）中：
1. 编译环境未安装 `libpq-dev`，CMake 未定义 `HAVE_LIBPQ`，未实际编译和链接 PostgreSQL 原生驱动；
2. 集成测试全量硬编码在 SQLite 上，且测试脚本内含大量对 `sqlite3` CLI 与 SQLite 专有方言（如 `printf`、`last_insert_rowid()`、`json_extract`）的直接依赖；
3. 代码仓库从未在真实的 PostgreSQL 环境中自动化验证过集成测试链路，存在潜在的 SQL 语法不兼容、迁移脚本失效或类型推断差异风险。

补齐 PostgreSQL CI 步骤并使集成测试系列支持 PostgreSQL，能够确保主干分支及 PR 对双数据库引擎的生产级可靠性守护。

---

## 2. 目标、非目标与反向目标 (Goals, Non-Goals, Anti-Goals)

### 2.1 目标 (Goals)
1. **独立 PostgreSQL CI Job**：在 `.github/workflows/ci.yml` 中新增 `backend-postgres` Job，基于 `services: postgres:16-alpine` 容器运行完整的 PostgreSQL 验证流程。
2. **构建开启 `HAVE_LIBPQ`**：安装 `libpq-dev` 和 `postgresql-client`，验证 CMake 正确检测到 `libpq` 并成功编译链接带有 PostgreSQL 驱动的 `minefolio` 二进制。
3. **抽取通用测试数据库助手 `backend/tests/db_helper.sh`**：
   - 提供跨引擎的 `db_init`、`db_clean`、`db_query`、`db_exec` 等标准化函数；
   - 抹平两端方言差异（如格式化浮点输出、JSON 路径提取、自增主键返回、布尔值判定）；
   - 在 PostgreSQL 模式下，测试前自动通过 `DROP SCHEMA public CASCADE; CREATE SCHEMA public;` 保证每个用例干净隔离，复用已有的 V001~V021 原生迁移初始化链路。
4. **全量适配 8 个集成测试脚本**：
   - `test_link.sh` (核心 40 个用例)
   - `test_2fa.sh` (2FA 与 TOTP)
   - `test_dca_cashflow.sh` (定投计划与现金流)
   - `test_ai_trace.sh` (AI 调用追踪)
   - `test_ai_tool_call.sh` (AI 工具调用与 SSE 流)
   - `test_market_sync.sh` (市场行情同步)
   - `test_ledgers.sh` (多账本隔离与共享)
   - `test_fx_oauth.sh` (汇率历史与 OAuth)
5. **100% 向后兼容**：在未指定 `MINEFOLIO_DB_DRIVER` 时，所有脚本默认继续以 SQLite 模式运行，且本地无 PG 环境时现有行为完全不变。

### 2.2 非目标 (Non-Goals)
- 本次不改动 C 语言 DDD 业务层（`domain/*`、`application/*`）既有业务逻辑，仅在发现基础设施层 SQL 与 PG 不兼容的真实 Bug 时进行修复。
- 不引入外部庞大的 Python/Node 测试框架重构，保持既有轻量 Bash + curl + jq/node 体系。

### 2.3 反向目标 (Anti-Goals)
- **禁止破坏 SQLite 测试**：不得在适配 PG 过程中破坏既有 SQLite 集成测试的执行速度、结果断言或环境依赖。
- **禁止在 CI 中串行拖慢主流程**：不得将 Postgres 测试直接追加在已有的单一 `backend` Job 中导致总耗时翻倍；必须并行运行。

---

## 3. 约束条件 (Constraints)

1. **GitHub Actions 限制**：CI 运行环境为 `ubuntu-24.04`，Service Container 使用 Docker 网络通信；测试脚本需支持通过标准 `PGHOST`、`PGPORT`、`PGUSER`、`PGPASSWORD`、`PGDATABASE` 环境变量与 `psql` 通信。
2. **csilk 迁移引擎契约**：数据库初始化必须严格走 `csilk_migration_apply`，读取 `backend/sql/migrations/postgres/` 目录下的 V001~V021。
3. **零外部新工具依赖**：测试脚本仅依赖 runner 自带及 apt 常规工具（`psql`、`sqlite3`、`curl`、`jq`、`node`）。

---

## 4. 备选方案权衡 (Approaches Considered & Decision)

### 4.1 CI 组织方案
- **备选方案 A（已采纳）：独立 `backend-postgres` Job**
  - **权衡**：与原有 `backend` (SQLite) 并行执行，互不阻塞，失败信息清晰；Release 制品构建逻辑仍保留在 SQLite job，无需复杂的 `if` 条件隔离。
- **备选方案 B：Job Matrix (`db: [sqlite, postgres]`)**
  - **权衡**：代码紧凑，但在 Tag 发布阶段需要为二进制归档和 Docker 构建做大量分支条件判断，复杂度转移到 YAML 模板中。
- **备选方案 C：单 Job 串行运行两套测试**
  - **权衡**：单任务总耗时由 ~2 分钟翻倍至 ~4 分钟，违背 CI 快速反馈原则，放弃。

### 4.2 脚本适配方案
- **备选方案 1（已采纳）：共享 `backend/tests/db_helper.sh` + 预置兼容函数与 Schema 隔离**
  - **权衡**：将数据库初始化、方言兼容、查询执行与 schema 清理下沉为单个辅助脚本，8 个测试脚本通过 `source "$(dirname "$0")/db_helper.sh"` 接入；
  - 在 PostgreSQL public schema 中利用轻量 SQL 兼容函数（如 `printf`、`lastval()` 兼容包装）与 `psql` 格式化参数，使既有 90+ 处 `test_link.sh` 测试断言无缝工作，改动成本最低、维护性最高。
- **备选方案 2：各脚本全量复制 if-else 分支重写**
  - **权衡**：重复代码量大，未来修改容易出现双端不一致，放弃。

---

## 5. 详细设计 (Detailed Design)

### 5.1 CI 流水线设计 (`.github/workflows/ci.yml`)

在 `ci.yml` 中新增 `backend-postgres` Job：

```yaml
  backend-postgres:
    name: Backend PostgreSQL Integration Tests
    runs-on: ubuntu-24.04
    services:
      postgres:
        image: postgres:16-alpine
        env:
          POSTGRES_USER: postgres
          POSTGRES_PASSWORD: postgres_password
          POSTGRES_DB: minefolio_test
        ports:
          - 5432:5432
        options: >-
          --health-cmd pg_isready
          --health-interval 5s
          --health-timeout 5s
          --health-retries 5
    steps:
      - name: Checkout repository
        uses: actions/checkout@v4

      - name: Install build dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y --no-install-recommends \
            build-essential gcc-14 g++-14 cmake pkg-config \
            libuv1-dev libyaml-dev libsqlite3-dev libssl-dev \
            libcurl4-openssl-dev zlib1g-dev sqlite3 curl python3 \
            libpq-dev postgresql-client nodejs

      - name: Build backend with PostgreSQL support
        run: |
          cd backend
          CC=gcc-14 CXX=g++-14 cmake -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
          cmake --build build --parallel $(nproc)

      - name: Run backend integration tests (PostgreSQL)
        env:
          MINEFOLIO_DB_DRIVER: postgres
          MINEFOLIO_DB_DSN: "host=localhost port=5432 user=postgres password=postgres_password dbname=minefolio_test sslmode=disable"
          PGHOST: localhost
          PGPORT: "5432"
          PGUSER: postgres
          PGPASSWORD: postgres_password
          PGDATABASE: minefolio_test
        run: |
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

---

### 5.2 共享数据库助手设计 (`backend/tests/db_helper.sh`)

`db_helper.sh` 提供核心接口：

1. **环境侦测与默认值**：
   - `DB_DRIVER="${MINEFOLIO_DB_DRIVER:-sqlite}"`
   - 若 `sqlite`：默认 `DB_FILE="${MINEFOLIO_DB_DSN:-/tmp/minefolio_test_$$.db}"`；
   - 若 `postgres`：默认 `PGHOST="${PGHOST:-localhost}"`，`PGPORT="${PGPORT:-5432}"`，`PGUSER="${PGUSER:-postgres}"`，`PGDATABASE="${PGDATABASE:-minefolio_test}"`，`PGPASSWORD="${PGPASSWORD:-postgres_password}"`。
2. **`db_clean()`**：
   - `sqlite`：`rm -f "$DB_FILE" "$DB_FILE-wal" "$DB_FILE-shm"`
   - `postgres`：
     ```bash
     psql -q -c "DROP SCHEMA public CASCADE; CREATE SCHEMA public;"
     # 注入轻量兼容函数，抹平两端查询方言差异
     psql -q << 'EOF'
     CREATE OR REPLACE FUNCTION printf(fmt text, val anyelement) RETURNS text AS $$
     BEGIN
       IF fmt = '%.1f' THEN RETURN to_char(val::numeric, 'FM999999999990.0');
       ELSIF fmt = '%.2f' THEN RETURN to_char(val::numeric, 'FM999999999990.00');
       ELSIF fmt = '%.4f' THEN RETURN to_char(val::numeric, 'FM999999999990.0000');
       ELSE RETURN to_char(val::numeric, 'FM999999999990.00');
       END IF;
     END;
     $$ LANGUAGE plpgsql IMMUTABLE;

     CREATE OR REPLACE FUNCTION last_insert_rowid() RETURNS bigint AS $$
     BEGIN
       RETURN lastval();
     END;
     $$ LANGUAGE plpgsql;

     CREATE OR REPLACE FUNCTION json_extract(doc text, path text) RETURNS text AS $$
     DECLARE
       parts text[];
       clean_path text;
     BEGIN
       -- 将 '$.providers[0].api_key' 转换为 '{providers,0,api_key}'
       clean_path := regexp_replace(path, '^\$\.?', '');
       clean_path := regexp_replace(clean_path, '\[([0-9]+)\]', '.\1', 'g');
       parts := string_to_array(clean_path, '.');
       RETURN doc::json #>> parts;
     EXCEPTION WHEN OTHERS THEN
       RETURN NULL;
     END;
     $$ LANGUAGE plpgsql IMMUTABLE;
     EOF
     ```
3. **`db_query <sql>`**：
   - `sqlite`：`sqlite3 "$DB_FILE" "$1"`
   - `postgres`：`psql -t -A -c "$1"`
4. **`db_exec <sql>`**：
   - `sqlite`：`sqlite3 "$DB_FILE" "$1"`
   - `postgres`：`psql -q -v ON_ERROR_STOP=1 -c "$1"`
5. **`db_start_server <port> <build_dir>`**：
   - 统一起动 `minefolio` 守护进程，注入 `MINEFOLIO_DB_DRIVER`、`MINEFOLIO_DB_DSN`、`MINEFOLIO_PORT`、`MINEFOLIO_JWT_SECRET`；
   - 轮询 `/healthz` 确保就绪，返回 `$SERVER_PID`。

---

### 5.3 8 个集成测试脚本适配改造要点

1. **`test_link.sh`**：
   - `source "$(dirname "$0")/db_helper.sh"`
   - 用 `db_init` 和 `db_clean` 替换原硬编码的 `rm -f /tmp/mf_link_test.db` 与 `sqlite3 "$DB"`；
   - 将行内 `sqlite3 "$DB" "..."` 调用替换为 `db_query "..."` 或 `db_exec "..."`；
   - 调整 Case 40 中极少数显式整数布尔值更新（如 `totp_enabled=1` 改为 `totp_enabled=TRUE`，`SELECT totp_enabled` 改为 `SELECT CASE WHEN totp_enabled THEN 1 ELSE 0 END`），使之在 SQLite/PG 双端语义恒等。
2. **`test_2fa.sh`**：
   - 移除写死的 `MINEFOLIO_DB_DRIVER=sqlite`，支持继承外部环境变量。
   - 启动前与退出时调用 `db_clean`。
3. **`test_dca_cashflow.sh`**：
   - 接入 `db_helper.sh`，将 5 处 `sqlite3 "$DB_PATH"` 转为 `db_query` / `db_exec`。
4. **`test_ai_trace.sh`**：
   - 接入 `db_helper.sh`，将 dummy trace 插入与查询转为 `db_exec` / `db_query`。
5. **`test_ai_tool_call.sh`**：
   - 接入 `db_helper.sh`，继承驱动与 DSN 配置，保持 mock LLM 服务联动测试。
6. **`test_market_sync.sh`**：
   - 接入 `db_helper.sh`，支持 PG 环境下启动与离线 mock 行情测试。
7. **`test_ledgers.sh`**：
   - 接入 `db_helper.sh`，将 4 处 `sqlite3` 转为 `db_exec` / `db_query`。
8. **`test_fx_oauth.sh`**：
   - 接入 `db_helper.sh`，将 5 处初始化插表转为 `db_exec`。

---

## 6. 验证计划 (Verification Plan)

1. **静态语法检查**：
   - `bash -n backend/tests/db_helper.sh`
   - 对 8 个 `backend/tests/*.sh` 逐个执行 `bash -n` 确保无语法错误。
2. **本地 SQLite 兼容性回归 (100% Pass)**：
   - 在未配置 PG 的环境下运行全部 8 个脚本：
     ```bash
     ./tests/test_link.sh
     ./tests/test_2fa.sh
     ./tests/test_dca_cashflow.sh
     ./tests/test_ai_trace.sh
     ./tests/test_ai_tool_call.sh
     ./tests/test_market_sync.sh
     ./tests/test_ledgers.sh
     ./tests/test_fx_oauth.sh
     ```
   - 断言 8 个脚本在 SQLite 模式下全部绿灯（PASS > 0, FAIL = 0）。
3. **本地 CTest 单元测试验证**：
   - `ctest --test-dir backend/build --output-on-failure`（55 个单元测试 100% 通过）。
4. **CI Workflow 语法与结构校验**：
   - 确认 `.github/workflows/ci.yml` 语法格式正确，Job 依赖、端口、环境变量完整无误。
