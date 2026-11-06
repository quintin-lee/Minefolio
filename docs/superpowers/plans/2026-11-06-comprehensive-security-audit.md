# Minefolio 全量代码安全审查实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 对 Minefolio C23 后端系统开展覆盖四大攻击面（C 内存底层、Web 通信边界、AI/MCP 运行时、多租户金融一致性）的全量深度安全审查，并在 `docs/security-audit-2026-11-06.md` 输出工业级安全审计报告。

**Architecture:** 采用切片并发审计法，将系统解耦为底层 C 内存与系统资源、Web 与网络边界、AI 与 MCP 运行时、多租户与金融一致性 4 个独立审计切片；逐层下钻 6.4 万行源码与 148 条路由调用链，基于 CWE 与 CVSS 框架定级漏洞，并给出精准修复建议代码。

**Tech Stack:** C23, POSIX 系统调用, libpq / sqlite3, csilk HTTP/WebSocket 框架, JSON-RPC 2.0 (MCP), 128-bit decimal 金融核心。

**Spec:** [`docs/superpowers/specs/2026-11-06-comprehensive-security-audit-design.md`](file:///data/home/quintin/workspace/source/c/Minefolio/docs/superpowers/specs/2026-11-06-comprehensive-security-audit-design.md)

## Global Constraints

- **只读审查原则**：审查期间不对现有生产代码及测试套件做侵入式修改，保持工作树干净，保证现有 51 个单元测试和 184 个集成测试无衰退。
- **无武器化代码**：报告仅对漏洞机理、触发条件和防御修复方案做代码级剖析，禁止生成武器化攻击脚本或破坏性渗透载荷。
- **证据链严谨性**：每一项确认的漏洞或安全缺陷，必须包含精确到文件的代码行号（`file:line`）与完整的调用栈溯源。
- **定级体系标准**：严格依据 CVSS v3.1 与 CWE 通用漏洞枚举对风险进行分级（Critical / High / Medium / Low）。

## Review Focus

1. **UAF / 悬垂指针使用**：释放外部连接或对象后依然引用内部成员指针（如 `PQfinish` 后读取 `PQerrorMessage`）。
2. **SQL 动态片段注入风险**：在动态条件拼接中漏用 `?` 参数化占位符，或格式化字符串缺少 `%s` 匹配符。
3. **HTTP 查询参数越权解析**：使用 `atoll` 导致非法字符隐式转为 0，穿透资源归属或分类校验。
4. **AI 批量动作验签被绕过**：篡改 actions 数组后能否利用空 nonce 或哈希冲突绕过 `POST /api/ai/actions/confirm-batch` 事务拦截。
5. **多租户跨户漏滤**：仓储层 SQL 查询或更新缺少 `user_id = ?` 条件，导致任意用户越权访问。

---

### Task 1: Slice 1 — 底层 C 内存与系统安全深度审查

**Files:**
- Audit: `backend/src/infrastructure/database/postgres/postgres_adapter.c`
- Audit: `backend/src/infrastructure/database/sqlite/sqlite_adapter.c`
- Audit: `backend/src/infrastructure/database/database.c`
- Audit: `backend/src/infrastructure/database/statement.c`
- Audit: `backend/src/core/financial/decimal.c`
- Audit: `backend/src/services/file_parser.c`
- Target: `docs/security-audit-2026-11-06.md` (Slice 1 Section)

**Interfaces:**
- Consumes: `docs/superpowers/specs/2026-11-06-comprehensive-security-audit-design.md` Section 5.1
- Produces: `SEC-MEM-*` 漏洞清单（含 UAF、缓冲区、内存/文件描述符泄漏判定）

- [ ] **Step 1: 扫描指针生命周期与释放时序**
  审查 `postgres_adapter.c` 与 `sqlite_adapter.c` 中的句柄释放，核查所有 `free()`、`PQfinish()`、`sqlite3_finalize()` 调用前后是否有悬垂指针访问（特别验证连接失败错误提取时序）。
- [ ] **Step 2: 审查字符串与缓冲区操作安全性**
  排查目标文件中所有 `snprintf`、`strncpy`、`strdup`、`memcpy` 调用，检查是否存在目标缓冲区容量不足导致截断、未补 `\0` 或格式化字符串注入漏洞。
- [ ] **Step 3: 审查文件解析与临时文件处理安全性**
  审查 `services/file_parser.c` 中 `mkstemps` 创建临时文件的权限位（0600）、文件描述符生命周期与 `unlink` 异常处理，排查符号链接劫持与 FD 泄漏。
- [ ] **Step 4: 汇总 Slice 1 发现并形成结构化记录**
  整理出漏洞条目，记录缺陷位置 `file:line`、CWE 编号（如 CWE-416、CWE-120、CWE-404）、危害评级与精准修复代码。

---

### Task 2: Slice 2 — Web 边界与通信安全深度审查

**Files:**
- Audit: `backend/src/interfaces/http/controllers/*_controller.c`（148 条路由）
- Audit: `backend/src/middlewares/*.c`（jwt.c, csrf.c, rate_limit.c, security_headers.c, cors.c）
- Audit: `backend/src/common/response.c`
- Audit: `backend/src/application/auth/usecases.c`
- Target: `docs/security-audit-2026-11-06.md` (Slice 2 Section)

**Interfaces:**
- Consumes: Task 1 基础、`docs/superpowers/specs/2026-11-06-comprehensive-security-audit-design.md` Section 5.2
- Produces: `SEC-WEB-*` 漏洞清单（SQL 注入、参数解析绕过、鉴权缺失、CSRF 隐患）

- [ ] **Step 1: 全量排查 SQL 注入与动态查询生成**
  逐一检查所有 Controller 与调用下游的 SQL 构建方式，确认是否存在动态字符串拼接，确认是否全部采用 `?` 预编译占位符，检查 `snprintf` 动态拼接 WHERE 语句时的参数化完备性。
- [ ] **Step 2: 审查 HTTP 路由参数绑定与严格性校验**
  审查全部 148 条路由中 path 和 query 参数解析逻辑，排查是否存在直接使用 `atoll` 或未校验负数、未校验溢出的解析逻辑；确认是否全量迁移至 `parse_query_int_strict`。
- [ ] **Step 3: 审查中间件鉴权闭环与越权防护**
  核对 `jwt_middleware.c` 的公共免认证路径白名单（如 `/api/auth/login`, `/api/system/setup` 等），确认除显式放行外，敏感资产、交易、配置路由均受有效 JWT 保护；检查 CSRF 中间件对非幂等方法（POST/PUT/DELETE）的防范。
- [ ] **Step 4: 汇总 Slice 2 发现并形成结构化记录**
  整理出漏洞条目，记录缺陷位置 `file:line`、CWE 编号（如 CWE-89、CWE-287、CWE-352、CWE-704）、危害评级与精准修复建议。

---

### Task 3: Slice 3 — AI 运行时与 MCP 协议安全深度审查

**Files:**
- Audit: `backend/src/services/ai/policy/*.c`（confirmation.c, risk.c, mcp_risk.c）
- Audit: `backend/src/services/ai/tools/*.c`（projection.c, batch_tool.c, registry.c）
- Audit: `backend/src/services/ai/runtime/*.c`（context.c, session.c, executor.c）
- Audit: `backend/src/services/ai/mcp/*.c`（client.c, pool.c, bridge.c）
- Audit: `backend/src/interfaces/http/controllers/ai_controller.c`
- Target: `docs/security-audit-2026-11-06.md` (Slice 3 Section)

**Interfaces:**
- Consumes: Task 1-2 基础、`docs/superpowers/specs/2026-11-06-comprehensive-security-audit-design.md` Section 5.3
- Produces: `SEC-AI-*` 漏洞清单（HMAC 令牌、防重放、只读脱敏、子进程管道）

- [ ] **Step 1: 审查复合操作防重放与 HMAC 令牌安全**
  审查 `services/ai/policy/confirmation.c` 与 `ai_controller.c` 中 `mf_batch_v1` 令牌的生成与核验流程，排查 HMAC 密钥来源强度、Nonce 验证与落库持久化（`ai_consumed_nonces`）的并发竞态与失效窗口。
- [ ] **Step 2: 审查只读工具语义投影数据泄露风险**
  审查 `services/ai/tools/projection.c` 中对各工具返回结果的过滤白名单，确认系统物理路径、哈希密钥、未脱敏凭据及跨租户字段是否被坚决剥离。
- [ ] **Step 3: 审查 MCP 子进程拉起与管道通信安全**
  排查 `services/ai/mcp/client.c` 中拉起外部 STDIO MCP 进程时的 `fork`/`exec` 或命令参数传递逻辑，确保不存在命令注入攻击（CWE-78）；排查子进程异常退出时的僵尸进程回收与管道关闭处理。
- [ ] **Step 4: 汇总 Slice 3 发现并形成结构化记录**
  整理出漏洞条目，记录缺陷位置 `file:line`、CWE 编号（如 CWE-294、CWE-200、CWE-78）、危害评级与精准防御代码。

---

### Task 4: Slice 4 — 多租户隔离与金融一致性深度审查

**Files:**
- Audit: `backend/src/infrastructure/repositories/*_repo_impl.c`（22 个仓储实现）
- Audit: `backend/src/application/{transaction,asset,transfer,account,daily_expense}/usecases.c`
- Audit: `backend/src/core/ledger/ledger_engine.c`
- Target: `docs/security-audit-2026-11-06.md` (Slice 4 Section)

**Interfaces:**
- Consumes: Task 1-3 基础、`docs/superpowers/specs/2026-11-06-comprehensive-security-audit-design.md` Section 5.4
- Produces: `SEC-FIN-*` 漏洞清单（水平越权、保存点回滚、定点数金融溢出）

- [ ] **Step 1: 排查仓储层多租户隔离与水平越权**
  全面审查 22 个 `*_repo_impl.c` 中所有的 SQL 查询与写入语句，验证每一个针对资源的 SELECT/UPDATE/DELETE 是否均无死角绑定了 `user_id = ?`，排查跨用户通过递增 ID 操纵资产的可能性（IDOR）。
- [ ] **Step 2: 审查事务嵌套保存点原子性与一致性**
  审查各业务用例（转账、记账、批量入账、资产重建）中的 `db_tx_scope_begin()` / `db_tx_scope_commit()`，核对所有失败分支（如余额不足、资产不存在、分类错误）是否 100% 触发回滚，是否存在孤立手续费子记录或单边记账落库的窗口。
- [ ] **Step 3: 审查 128 位定点数金融算术边界**
  审查 `core/ledger/ledger_engine.c` 与相关用例，排查是否存在中间计算隐式转 `double` 导致的精度失真，检查除法计算（如单价、汇率折算、持仓占比）前的分母零值防御。
- [ ] **Step 4: 汇总 Slice 4 发现并形成结构化记录**
  整理出漏洞条目，记录缺陷位置 `file:line`、CWE 编号（如 CWE-284、CWE-670、CWE-682）、危害评级与精准修复建议。

---

### Task 5: 全量安全审计报告综合整理与长效防御路线图

**Files:**
- Create/Finalize: `docs/security-audit-2026-11-06.md`

**Interfaces:**
- Consumes: Task 1–4 汇总的所有安全发现
- Produces: 完整、专业、通过规范核验的 `docs/security-audit-2026-11-06.md`

- [ ] **Step 1: 整合四大切片漏洞清单并建立全局风险矩阵**
  汇总 `SEC-MEM-*`、`SEC-WEB-*`、`SEC-AI-*`、`SEC-FIN-*` 所有已确认的漏洞项，按 Critical、High、Medium、Low 建立全局统计矩阵与雷达评估表。
- [ ] **Step 2: 编写执行摘要与系统整体安全成熟度评估**
  撰写执行摘要（Executive Summary），分析当前 Minefolio 架构的优势防御点与薄弱环节。
- [ ] **Step 3: 逐项校验源码定位与代码修复建议**
  对报告中引用的每一个 `file:line` 进行基线代码比对，确认行号与代码片段 100% 准确真实，并提供完备的防御修复代码范例。
- [ ] **Step 4: 规划长效防御与工程加固路线图**
  编写包含编译期检测强化（如 AddressSanitizer、UndefinedBehaviorSanitizer 持续集成）、静态分析工具集成（Clang-Tidy / Cppcheck）、自动化安全回归用例的长效改进建议。
- [ ] **Step 5: 提交审查报告文档**
  将 `docs/security-audit-2026-11-06.md` 提交至 git，验证全套单测与集成测试依然 100% 绿灯。
