# 全量安全漏洞修复实施计划 (Security Vulnerabilities Remediation)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 全量修复 `docs/security-audit-2026-11-06.md` 报告中确立的 11 项安全漏洞（包含 1 项 Critical、2 项 High、7 项 Medium、1 项 Low），从底层内存、环境变量隔离、多租户约束、金融账本一致性到 Web 边界进行系统性加固。

**Architecture:** 按关注点分离原则，将 11 项缺陷拆解为 4 个由深至浅的修复批次：1) P0 级严重与高危缺陷（UAF、子进程环境泄露、多租户批次过滤）；2) 数据库基础设施与底层安全（Savepoint 白名单、DB Wrap 错误检查、文件解析保护）；3) Web 边界、鉴权与限流加固（严格整型、JWT sub 边界、限流环扩容）；4) 金融级联保护与代码卫生（资产删除交易门禁、sprintf 替换）。最后通过全量回归测试验证。

**Tech Stack:** C23, libpq / sqlite3, POSIX 系统调用, csilk HTTP 框架, 128-bit decimal 金融核心。

**Spec:** [`docs/security-audit-2026-11-06.md`](file:///data/home/quintin/workspace/source/c/Minefolio/docs/security-audit-2026-11-06.md) 及 [`docs/superpowers/specs/2026-11-06-comprehensive-security-audit-design.md`](file:///data/home/quintin/workspace/source/c/Minefolio/docs/superpowers/specs/2026-11-06-comprehensive-security-audit-design.md)

## Global Constraints

- **零破坏向后兼容**：不破坏任何现有的 148 条 API/MCP 外部 HTTP 契约与 JSON 响应封包结构。
- **强制所有仓储写操作绑定 `user_id`**：恪守 `AGENTS.md` 黄金法则，所有针对用户数据的 SQL 更新与删除必须包含 `WHERE id=? AND user_id=?`。
- **严禁裸浮点数介入金融计算**：任何资产与批次修复计算必须维持 128 位 `decimal_t` 定点数精度。
- **全绿测试门禁**：修复后所有 51 个 CTest 单元测试套件、184 个 `test_link.sh` 端到端断言及前端编译必须 100% 通过。

## Review Focus

1. **UAF 修复与空指针安全**：`PQfinish` 前必须完整拷贝并清理错误字符串，绝不持有悬垂指针。
2. **MCP 子进程环境敏感信息清除**：子进程绝对不可继承宿主环境中的 `JWT_SECRET`、`AI_SECRET` 或包含密码的 `MINEFOLIO_DB_DSN`。
3. **多租户更新批次隔离**：`tx_lot_deduct` 增加 `user_id` 后，必须保证无法跨用户更新他人的批次。
4. **资产删除保护**：包含交易记录的资产严禁被直接静默删除，必须前置返回 HTTP 1004 阻止账目失衡。
5. **保存点命名注入防范**：带非法字符（引号、分号、空格）或超过 64 字节的 Savepoint 名称必须被拦截。

---

### Task 1: P0/Critical 与 High 核心安全漏洞加固 (SEC-MEM-001, SEC-AI-001, SEC-FIN-001)

**Files:**
- Modify: `backend/src/infrastructure/database/postgres/postgres_adapter.c:143-165`
- Modify: `backend/src/services/ai/tools/mcp/mcp_client.c:590-605`
- Modify: `backend/src/infrastructure/repositories/transaction_lot_repo_impl.h:23-28`
- Modify: `backend/src/infrastructure/repositories/transaction_lot_repo_impl.c:75-110`
- Modify: `backend/src/core/ledger/ledger_engine.c:260-270`
- Test: `backend/tests/unit/test_database_repository.c`
- Test: `backend/tests/unit/test_transaction_lots.c`
- Test: `backend/tests/unit/test_mcp_child_sanitation.c`

**Interfaces:**
- Consumes: `tx_lot_deduct` 原签名、`execve` 原始环境调用
- Produces: `int tx_lot_deduct(csilk_db_pool_t* pool, int64_t user_id, int64_t lot_id, quantity_t deduct_qty, quantity_t new_remaining_qty, bool is_closed)`

- [ ] **Step 1: 编写测试用例验证 UAF 修复、子进程环境脱敏及跨租户批次更新拦截**
  - 在 `test_database_repository.c` 中增加对 `allow_mock=false` 且连接失败时安全打印错误并不发生崩溃的断言；
  - 在 `test_transaction_lots.c` 中增加跨用户（非 owner）尝试扣减批次时返回 -1 的负向断言；
  - 在 `test_mcp_child_sanitation.c` 中增加断言验证拉起的子进程无法从 `environ` 中读取 `JWT_SECRET` 与 `MINEFOLIO_DB_DSN`。
- [ ] **Step 2: 运行测试验证失败 (RED)**
  - 运行 `ctest --test-dir backend/build -R "test_transaction_lots|test_database_repository" --output-on-failure`
  - 预期因函数签名与跨租户未校验而编译失败或断言失败。
- [ ] **Step 3: 修复 `SEC-MEM-001` (PostgreSQL 适配器 UAF 漏洞)**
  - 在 `postgres_adapter.c:postgres_adapter_open` 中，在调用 `PQfinish(conn)` 之前，将 `PQerrorMessage(conn)` 拷贝至栈上 `char conn_err[512]` 缓冲区，去除末尾 `\r`/`\n`，然后再调用 `PQfinish(conn)`。
- [ ] **Step 4: 修复 `SEC-AI-001` (STDIO MCP 子进程环境透传漏洞)**
  - 在 `mcp_client.c` 中，当 `envp == NULL` 时，不要直接传递全局 `environ`；
  - 构建受限白名单环境变量数组（仅保留 `PATH`, `HOME`, `LANG`, `USER`, `TMPDIR`, `TERM` 等无害环境变量），显式剥离所有敏感键（`MINEFOLIO_*`, `JWT_*`, `*SECRET*`, `*KEY*`, `*PASSWORD*`, `*DSN*`）。
- [ ] **Step 5: 修复 `SEC-FIN-001` (多租户批次更新缺少 `user_id` 过滤)**
  - 在 `transaction_lot_repo_impl.h` 与 `transaction_lot_repo_impl.c` 中，为 `tx_lot_deduct` 增加 `int64_t user_id` 参数；
  - 将 SQL 更新语句调整为 `UPDATE transaction_lots SET remaining_quantity=?, status=? WHERE id=? AND user_id=? RETURNING id`；
  - 同步更新 `ledger_engine.c:265` 的调用处，传入现存的 `user_id` 参数；
  - 同步更新 `test_transaction_lots.c` 中的调用。
- [ ] **Step 6: 编译并运行测试验证通过 (GREEN)**
  - 运行 `cmake --build backend/build --parallel && ctest --test-dir backend/build --output-on-failure`
  - 预期：全部编译成功，测试 100% 通过。
- [ ] **Step 7: 提交代码**
  ```bash
  git commit -m "fix(security): 🛡️ fix UAF in postgres adapter, sanitize MCP child env, and enforce user_id on lot deduction"
  ```

---

### Task 2: 数据库基础设施与底层安全边界加固 (SEC-MEM-002, SEC-MEM-004, SEC-MEM-003)

**Files:**
- Modify: `backend/src/infrastructure/database/postgres/postgres_adapter.c:250-285`
- Modify: `backend/src/infrastructure/database/sqlite/sqlite_adapter.c:128-165`
- Modify: `backend/src/infrastructure/database/database.c:80-95`
- Modify: `backend/src/services/file_parser.c:70-135`
- Test: `backend/tests/unit/test_database_repository.c`
- Test: `backend/tests/unit/test_tx_scope_nesting.c`
- Test: `backend/tests/unit/test_file_parser_mkstemps.c`

**Interfaces:**
- Consumes: Task 1 基础
- Produces: 安全校验的 Savepoint 标识符函数、错误安全的 `mf_db_wrap_csilk`、防御性的 `file_parser`

- [ ] **Step 1: 编写测试用例覆盖非法 Savepoint 名称与 DB Wrap 异常**
  - 在 `test_tx_scope_nesting.c` 或 `test_database_repository.c` 中，测试传入包含分号、空格或超长（>64字节）的 Savepoint 名称，验证返回 `-1` 且无异常 SQL 执行；
  - 在 `test_file_parser_mkstemps.c` 中增加后缀字符边界测试。
- [ ] **Step 2: 运行测试验证失败 (RED)**
  - 运行 `ctest --test-dir backend/build -R "test_tx_scope_nesting|test_file_parser_mkstemps" --output-on-failure`
- [ ] **Step 3: 修复 `SEC-MEM-002` (Savepoint 标识符白名单校验)**
  - 在 `postgres_adapter.c` 与 `sqlite_adapter.c` 的 `tx_savepoint`、`tx_rollback_to_savepoint`、`tx_release_savepoint` 中：
  - 增加对 `name` 的字符白名单检查（仅允许 `[a-zA-Z0-9_]` 且长度在 1~64 之间），非法则立即返回 `-1`。
- [ ] **Step 4: 修复 `SEC-MEM-004` (`mf_db_wrap_csilk` 忽略 open 错误返回值)**
  - 在 `database.c:mf_db_wrap_csilk` 中，检查 `db->ops->open(&cfg, &db->native_handle)` 返回值；
  - 若返回值非 0，则销毁 `db->lock`，释放 `db`，并返回 `-1`。
- [ ] **Step 5: 修复 `SEC-MEM-003` (`file_parser` 外部调用与后缀安全)**
  - 在 `file_parser_write_temp` 中对 `suffix` 实施白名单校验（仅允许 `.` 和字母数字，禁止路径穿越或引号注入）；
  - 对外部解析限制单次最大读取缓冲，在子进程调用失败或关闭时及时清理临时文件。
- [ ] **Step 6: 编译并运行测试验证通过 (GREEN)**
  - 运行 `cmake --build backend/build --parallel && ctest --test-dir backend/build --output-on-failure`
- [ ] **Step 7: 提交代码**
  ```bash
  git commit -m "fix(security): 🛡️ validate savepoint names, check db wrap errors, and harden file parser"
  ```

---

### Task 3: Web 边界、认证与限流防线加固 (SEC-WEB-001, SEC-WEB-002, SEC-WEB-003)

**Files:**
- Modify: `backend/src/interfaces/http/controllers/market_controller.c:225-240`
- Modify: `backend/src/middlewares/jwt_middleware.c:190-205`
- Modify: `backend/src/middlewares/rate_limit.c:30-45,130-150`
- Test: `backend/tests/unit/test_domain_auth.c`
- Test: `backend/tests/unit/test_market_indices.c`

**Interfaces:**
- Consumes: Task 1-2 基础
- Produces: 强类型约束的 HTTP 控制器、严格判定 `sub <= 0` 的 JWT 中间件、大容量分片限流队列

- [ ] **Step 1: 编写测试用例覆盖 `days` 负数/非法解析、JWT `sub=0` 拦截及限流稳定性**
  - 在 `test_domain_auth.c` 中添加对 JWT Claim `sub: 0` 的拦截断言（必须返回未授权）；
  - 测试非法非数字 `days` 参数。
- [ ] **Step 2: 运行测试验证失败 (RED)**
  - 运行 `ctest --test-dir backend/build -R "test_domain_auth" --output-on-failure`
- [ ] **Step 3: 修复 `SEC-WEB-001` (市场外汇接口迁移至 `parse_query_int_strict`)**
  - 在 `market_controller.c:api_market_get_fx_history` 中，废除 `atoi(days_str)`；
  - 使用 `parse_query_int_strict(days_str, 1, 365, &days)` 严格解析，当参数传入且格式非法时返回 HTTP 1002。
- [ ] **Step 4: 修复 `SEC-WEB-002` (JWT 严格正整数用户 ID 校验)**
  - 在 `jwt_middleware.c:196` 中，将 `if (user_id < 0)` 修改为严格的 `if (user_id <= 0)`，拒绝任何未设置或为 0 的用户 Subject。
- [ ] **Step 5: 修复 `SEC-WEB-003` (限流环容量扩容与防冲刷加固)**
  - 将 `rate_limit.c` 中的 `RATE_LIMIT_RING` 扩容至 1024，并在记录前增加针对同 IP/Path 频次的合理滑动窗口管理，避免全局单环被单个恶意 IP 突发冲刷。
- [ ] **Step 6: 编译并运行测试验证通过 (GREEN)**
  - 运行 `cmake --build backend/build --parallel && ctest --test-dir backend/build --output-on-failure`
- [ ] **Step 7: 提交代码**
  ```bash
  git commit -m "fix(security): 🛡️ enforce strict query int parsing, reject zero-sub JWTs, and expand rate limit ring"
  ```

---

### Task 4: 金融逻辑一致性与代码规范加固 (SEC-FIN-002, SEC-AI-002)

**Files:**
- Modify: `backend/src/application/asset/usecases.c:405-435`
- Modify: `backend/src/domain/rules/asset.h` 及 `asset.c`
- Modify: `backend/src/services/ai/tools/mcp/mcp_client.c:625-635,900-915`
- Test: `backend/tests/unit/test_domain_asset.c`
- Test: `backend/tests/test_link.sh` (Case 40 或新增断言)

**Interfaces:**
- Consumes: Task 1-3 基础
- Produces: 资产删除完整性保护门禁、符合 CERT C 规范的 `snprintf` 帧处理

- [ ] **Step 1: 编写测试用例覆盖资产删除前置交易拦截**
  - 在 `test_domain_asset.c` 或集成测试中，为已有交易的资产执行删除操作，断言其必须被拦截并返回 HTTP 1004，且资产与关联交易完好无损。
- [ ] **Step 2: 运行测试验证失败 (RED)**
  - 运行 `ctest --test-dir backend/build -R "test_domain_asset" --output-on-failure`
- [ ] **Step 3: 修复 `SEC-FIN-002` (资产删除交易存在性保护门禁)**
  - 在 `asset_usecase_delete` 中，调用仓储前增加前置校验：查询 `transactions` 表中是否存在 `asset_id=? OR linked_asset_id=?` 属于当前用户的交易；
  - 若存在关联交易，拒绝直接物理级联删除，返回 HTTP 1004（错误信息："该资产存在关联交易记录，无法直接删除，请先清理交易或归档资产"）；
  - 避免底层级联删除绕过手续费与扣款账户逆向冲正逻辑。
- [ ] **Step 4: 修复 `SEC-AI-002` (替换不安全 `sprintf` 为 `snprintf`)**
  - 在 `mcp_client.c:631` 及 `908` 中，将 `sprintf(frame, ...)` 全量替换为带显式长度限制的 `snprintf(frame, frame_len, "%s\n", request_json)`。
- [ ] **Step 5: 编译并运行测试验证通过 (GREEN)**
  - 运行 `cmake --build backend/build --parallel && ctest --test-dir backend/build --output-on-failure`
- [ ] **Step 6: 提交代码**
  ```bash
  git commit -m "fix(security): 🛡️ protect asset deletion from orphaned tx cascades and replace sprintf with snprintf"
  ```

---

### Task 5: 全量安全回归测试与验证闭环 (Full Regression & Verification)

**Files:**
- Modify: `docs/security-audit-2026-11-06.md` (更新全部 11 项漏洞的状态为 `已修复`)

- [ ] **Step 1: 运行全量 CTest 单元测试套件**
  - 运行 `ctest --test-dir backend/build --output-on-failure`
  - 预期：51/51 套件全部通过。
- [ ] **Step 2: 运行端到端核心集成测试**
  - 运行 `./backend/tests/test_link.sh`
  - 预期：184/184 项断言全部通过。
- [ ] **Step 3: 运行 AI 运行时鲁棒性测试**
  - 运行 `./backend/tests/test_ai_loop_hardening.sh`
  - 预期：PASS: 28, FAIL: 0。
- [ ] **Step 4: 运行 MCP 协议与服务端集成测试**
  - 运行 `./backend/tests/test_mcp.sh && ./backend/tests/test_mcp_server.sh`
  - 预期：全部场景通过。
- [ ] **Step 5: 运行前端 SPA 构建验证**
  - 运行 `npm --prefix frontend run build`
  - 预期：0 错误构建成功。
- [ ] **Step 6: 更新安全审计报告状态**
  - 在 `docs/security-audit-2026-11-06.md` 的漏洞统计矩阵中，将 11 项漏洞状态从 `待修复` 更新为 `已修复`，并记录修复对应的提交或补丁说明。
- [ ] **Step 7: 提交最终文档变更**
  ```bash
  git commit -m "docs(audit): 📝 mark all 11 security vulnerabilities as resolved"
  ```
