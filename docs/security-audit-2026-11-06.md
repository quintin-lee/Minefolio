# Minefolio 全量代码安全审查与漏洞审计报告

- **审计日期**：2026-11-06
- **审计基准分支**：`master @ 613d9f5a`
- **代码规模**：~6.4 万行 C 源码、148 条 API/MCP 路由、17 个 DDD 业务领域、22 个数据库仓储实现
- **规范标准**：OWASP Top 10, CWE/SANS Top 25, SEI CERT C Coding Standard, CVSS v3.1

---

## 1. 执行摘要 (Executive Summary)

Minefolio 是一套纯 C23 + csilk HTTP 框架构建的自托管个人资产与投资追踪系统。系统在架构设计上体现了极高的安全与工程成熟度：
- **DDD 四层强解耦**：核心领域模型零外部依赖，仓储层广泛采用参数化预编译查询（`?` 占位符）；
- **定点数金融核心**：使用 128 位 `decimal_t` 杜绝裸浮点数累加导致的资金漂移；
- **AI 运行时防御**：建立了基于 HMAC-SHA256 的多动作草案确认与数据库防重放 Nonce 栈（`ai_consumed_nonces`），只读工具引入了严格白名单脱敏的语义投影层；
- **Web 边界防护**：全栈落地 CSRF 双重 Cookie 防护、CORS 凭证安全匹配以及严格整型参数校验。

然而，在针对全量代码四大攻击面（底层 C 内存安全、Web 边界与通信、AI 运行时与 MCP、多租户金融一致性）的静态代码深层审查中，共定位出 **11 项安全缺陷与潜在风险隐患**。其中包括 **1 项 Critical 严重缺陷**（PostgreSQL 适配器中的 UAF 悬垂指针访问）与 **2 项 High 高危缺陷**（MCP 子进程敏感环境变量泄露、批次更新缺失多租户过滤），以及 7 项 Medium 中危和 1 项 Low 低危优化项。

---

## 2. 漏洞分类统计矩阵 (Vulnerability Matrix)

| 攻击切片 | 漏洞编号 | 严重级别 | CWE 编号 | 简要描述 | 状态 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Slice 1: C 内存与底层安全** | `SEC-MEM-001` | **Critical** | CWE-416 | PostgreSQL 适配器连接失败时释放后使用 (UAF) | 待修复 |
| **Slice 1: C 内存与底层安全** | `SEC-MEM-002` | Medium | CWE-89 / CWE-20 | 保存点名称未做字符白名单与长度校验直接嵌入 SQL | 待修复 |
| **Slice 1: C 内存与底层安全** | `SEC-MEM-003` | Medium | CWE-400 | `popen()` 执行外部解析器缺少超时限制存在 DoS 阻塞风险 | 待修复 |
| **Slice 1: C 内存与底层安全** | `SEC-MEM-004` | Medium | CWE-252 | `mf_db_wrap_csilk` 未检查适配器打开返回值导致空指针解引用隐患 | 待修复 |
| **Slice 2: Web 边界与通信安全**| `SEC-WEB-001` | Medium | CWE-704 | 市场外汇历史接口使用 `atoi` 解析天数参数违背严格整型规范 | 待修复 |
| **Slice 2: Web 边界与通信安全**| `SEC-WEB-002` | Medium | CWE-20 | JWT 中间件 `sub` 判定边界使用 `< 0` 导致 `0` 绕过前置校验 | 待修复 |
| **Slice 2: Web 边界与通信安全**| `SEC-WEB-003` | Medium | CWE-770 | 认证限流全局环形缓冲容量过小（64项）易被恶意冲刷淘汰状态 | 待修复 |
| **Slice 3: AI 运行时与 MCP 安全**| `SEC-AI-001` | **High** | CWE-214 / CWE-200 | STDIO MCP 子进程无条件继承服务端环境变量泄露核心密钥与 DSN | 待修复 |
| **Slice 3: AI 运行时与 MCP 安全**| `SEC-AI-002` | Low | CWE-676 | 使用已弃用不安全函数 `sprintf` 格式化 MCP JSON 帧 | 待修复 |
| **Slice 4: 多租户与金融一致性**| `SEC-FIN-001` | **High** | CWE-284 / CWE-639 | `tx_lot_update_remaining` 缺乏 `user_id` 过滤违反多租户隔离规范 | 待修复 |
| **Slice 4: 多租户与金融一致性**| `SEC-FIN-002` | Medium | CWE-404 | 资产删除直接级联交易缺乏手续费与资金账户联动逆向回滚 | 待修复 |

---

## 3. 安全隐患详情清单 (Detailed Findings)

### 3.1 Slice 1: 底层 C 内存与系统底层安全

#### `SEC-MEM-001` [Critical] PostgreSQL 适配器连接失败时释放后使用 (UAF)
- **CWE 映射**：[CWE-416: Use After Free](https://cwe.mitre.org/data/definitions/416.html)
- **源码位置**：`backend/src/infrastructure/database/postgres/postgres_adapter.c:146-154`
- **缺陷原理分析**：
  在 `postgres_adapter_open` 中，当连接状态不为 `CONNECTION_OK` 时：
  ```c
  const char* conn_err = PQerrorMessage(conn);
  PQfinish(conn); // <-- conn 及其关联的内存已被底层 libpq 释放
  if (!config->allow_mock) {
      fprintf(stderr,
              "FATAL: postgres adapter: connection failed (%s); "
              "refusing to fall back to mock in non-test mode (allow_mock=false)\n",
              conn_err ? conn_err : "unknown"); // <-- 访问已释放的内存
      ...
  ```
  `PQerrorMessage(conn)` 返回的指针指向 `PGconn` 对象的内部存储。调用 `PQfinish(conn)` 会立即销毁并释放该对象。在 `PQfinish` 之后向 `fprintf` 传入 `conn_err` 属于经典的 UAF 悬垂指针访问，在生产崩溃排查路径下可能导致打印乱码或直接引发 `SIGSEGV` 崩溃。
- **业务危害与影响面**：生产环境在 PostgreSQL 不可达时，非但无法干净地记录失败日志，反而会触发进程非预期段错误崩溃。
- **防御性修复建议**：
  在调用 `PQfinish` 前将错误信息安全拷贝至栈上定长缓冲区，并顺便剔除末尾多余的换行符：
  ```c
  char conn_err[512] = {0};
  const char* raw_err = PQerrorMessage(conn);
  if (raw_err) {
      snprintf(conn_err, sizeof(conn_err), "%s", raw_err);
      size_t len = strlen(conn_err);
      while (len > 0 && (conn_err[len - 1] == '\n' || conn_err[len - 1] == '\r')) {
          conn_err[--len] = '\0';
      }
  }
  PQfinish(conn);
  ```

---

#### `SEC-MEM-002` [Medium] 保存点名称未做字符白名单与长度校验直接嵌入 SQL
- **CWE 映射**：[CWE-89: SQL Injection](https://cwe.mitre.org/data/definitions/89.html) / [CWE-20: Improper Input Validation](https://cwe.mitre.org/data/definitions/20.html)
- **源码位置**：
  - `backend/src/infrastructure/database/postgres/postgres_adapter.c:259,270,281`
  - `backend/src/infrastructure/database/sqlite/sqlite_adapter.c:135,146,155`
- **缺陷原理分析**：
  ```c
  static int sqlite_adapter_tx_savepoint(void* handle, const char* name) {
      char buf[128];
      snprintf(buf, sizeof(buf), "SAVEPOINT %s;", name);
      return sqlite_adapter_execute(handle, buf);
  }
  ```
  `name` 字符串未作合法标识符字符校验（如是否仅由 `[a-zA-Z0-9_]` 构成）。若未来上层业务或测试传入超长名称（>110字节），`snprintf` 会发生静默截断导致 SQL 语法破坏；若传入带分号或单引号的恶意构造名称，存在潜在 SQL 破坏风险。
- **防御性修复建议**：
  在生成 SQL 前对 `name` 进行严格的合法标识符正则/字符扫描与长度限制（<=64 字符）：
  ```c
  if (!name || strlen(name) > 64) return -1;
  for (const char* p = name; *p; p++) {
      if (!isalnum((unsigned char)*p) && *p != '_') return -1;
  }
  ```

---

#### `SEC-MEM-003` [Medium] `popen()` 执行外部解析器缺少超时限制存在 DoS 阻塞风险
- **CWE 映射**：[CWE-400: Uncontrolled Resource Consumption](https://cwe.mitre.org/data/definitions/400.html)
- **源码位置**：`backend/src/services/file_parser.c:80,113`
- **缺陷原理分析**：
  在解析上传的 PDF 和 ZIP 文件时，代码使用 `popen("pdftotext ...", "r")` 和 `popen("unzip ...", "r")`。当遇到畸形文件或 Zip Bomb 时，外部进程可能耗尽 CPU 或挂起，而 `pclose(fp)` 会一直阻塞等待子进程退出，进而导致 HTTP 工作线程被无限期占用冻结。
- **防御性修复建议**：
  建议将 `popen` 改造为 `fork` + `execve` + `pipe` 模式，使用 `waitpid` 配合 `SIGALRM` 或超时检测（如 5 秒），超时则直接 `SIGKILL` 终止子进程。

---

#### `SEC-MEM-004` [Medium] `mf_db_wrap_csilk` 未检查适配器打开返回值导致空指针解引用隐患
- **CWE 映射**：[CWE-252: Unchecked Return Value](https://cwe.mitre.org/data/definitions/252.html)
- **源码位置**：`backend/src/infrastructure/database/database.c:83,90`
- **缺陷原理分析**：
  ```c
  if (engine == MF_DB_ENGINE_POSTGRES) {
      db->ops = mf_postgres_adapter_get_ops();
      if (native_conn) {
          mf_postgres_adapter_wrap_native(native_conn, &db->native_handle);
      } else {
          db->ops->open(&cfg, &db->native_handle); // <-- 忽略返回值！
      }
  }
  ```
  若连接池无有效 `native_conn`，代码回退调用 `open`，但未检查返回值。若 `open` 失败返回 `-1`，`db->native_handle` 将保持为 NULL，而函数最终依然返回 0（成功），后续执行任何 SQL 时将触发空指针解引用。
- **防御性修复建议**：
  校验 `db->ops->open` 的返回值，若非 0 则销毁互斥锁、释放 `db` 并返回 `-1`。

---

### 3.2 Slice 2: Web 边界与通信安全

#### `SEC-WEB-001` [Medium] 市场外汇历史接口使用 `atoi` 解析天数参数违背严格整型规范
- **CWE 映射**：[CWE-704: Incorrect Type Conversion](https://cwe.mitre.org/data/definitions/704.html)
- **源码位置**：`backend/src/interfaces/http/controllers/market_controller.c:229`
- **缺陷原理分析**：
  ```c
  const char* days_str = csilk_get_query(c, "days");
  int days = (days_str && days_str[0]) ? atoi(days_str) : 30;
  ```
  整个 Minefolio 架构规约已全面禁止使用 `atoll` / `atoi`，要求全量采用 `parse_query_int_strict`。`atoi` 在解析包含非法字符或负数时会隐式静默转换为 0，违背输入校验与类型安全准则。
- **防御性修复建议**：
  使用 `parse_query_int_strict` 替代 `atoi`，并在解析失败时返回 HTTP 1002。

---

#### `SEC-WEB-002` [Medium] JWT 中间件 `sub` 判定边界使用 `< 0` 导致 `0` 绕过前置校验
- **CWE 映射**：[CWE-20: Improper Input Validation](https://cwe.mitre.org/data/definitions/20.html)
- **源码位置**：`backend/src/middlewares/jwt_middleware.c:196`
- **缺陷原理分析**：
  ```c
  int64_t user_id = (int64_t)csilk_json_get_number(payload, "sub");
  if (user_id < 0) { ... }
  ```
  在关系型数据库中，有效自增 `user_id` 必然为正整数（`>= 1`）。若 JWT 载荷中缺失 `"sub"` 字段，`csilk_json_get_number` 会返回默认浮点数 `0.0`，此时 `user_id < 0` 判定为假，导致 `user_id = 0` 绕过前置拦截，进入下游版本号比对。
- **防御性修复建议**：
  将校验收紧为 `if (user_id <= 0)`，坚决拒绝 0 值用户身份。

---

#### `SEC-WEB-003` [Medium] 认证限流全局环形缓冲容量过小（64项）易被恶意冲刷淘汰状态
- **CWE 映射**：[CWE-770: Allocation of Resources Without Limits or Throttling](https://cwe.mitre.org/data/definitions/770.html)
- **源码位置**：`backend/src/middlewares/rate_limit.c:34,135-138`
- **缺陷原理分析**：
  登录与敏感接口限流使用固定容量 64 的全局单一静态数组 `ring[RATE_LIMIT_RING]`。当 `ring_count >= 64` 时，系统直接强制向前移动 `ring_head` 淘汰最早的历史项。攻击者只需向端点高频突发 64 次请求，即可将其他正常客户端或其自身稍早的限流滑动窗口记录全部冲刷挤出，导致限流防护失效。
- **防御性修复建议**：
  扩大环形缓冲区容量至 1024 以上，或引入按 IP 地址哈希的分桶式 Leaky-Bucket 计数器，避免全局单环被单个 IP 恶意冲刷。

---

### 3.3 Slice 3: AI 运行时与 MCP 协议安全

#### `SEC-AI-001` [High] STDIO MCP 子进程无条件继承服务端环境变量泄露核心密钥与 DSN
- **CWE 映射**：[CWE-214: Invocation of Process Initialized with Insecure Environment](https://cwe.mitre.org/data/definitions/214.html) / [CWE-200: Exposure of Sensitive Information](https://cwe.mitre.org/data/definitions/200.html)
- **源码位置**：`backend/src/services/ai/tools/mcp/mcp_client.c:603`
- **缺陷原理分析**：
  ```c
  execve(argv[0], argv, envp ? (char* const*)envp : environ);
  ```
  当拉起外部 STDIO MCP 子进程且未显式指定 `envp` 时，子进程直接继承了宿主进程的全量 `environ` 环境变量！这包括 `JWT_SECRET`、`AI_SECRET`、`MINEFOLIO_JWT_SECRET` 以及带有数据库明文账号密码的 `MINEFOLIO_DB_DSN`。任何第三方或潜在不可信的 MCP Server 均可在启动后直接读取宿主机的金融数据库凭证与签名主密钥。
- **业务危害与影响面**：一旦用户配置或导入了不受信任的外部 MCP 服务，该服务可通过环境直接盗取服务器根凭据，导致整个金融数据库失陷。
- **防御性修复建议**：
  严禁向子进程透传宿主机的 `environ`。当 `envp == NULL` 时，必须构建最小白名单环境字典（仅传递 `PATH`, `HOME`, `LANG`, `USER`, `TMPDIR`），并严格清洗任何包含 `SECRET`, `KEY`, `DSN`, `PASSWORD` 的环境变量。

---

#### `SEC-AI-002` [Low] 使用已弃用不安全函数 `sprintf` 格式化 MCP JSON 帧
- **CWE 映射**：[CWE-676: Use of Potentially Dangerous Function](https://cwe.mitre.org/data/definitions/676.html)
- **源码位置**：`backend/src/services/ai/tools/mcp/mcp_client.c:631,908`
- **缺陷原理分析**：
  代码使用 `sprintf(frame, "%s\n", request_json);` 写入管道帧。虽然前置已 `malloc(strlen + 2)`，但使用裸 `sprintf` 违背 CERT C 编码安全指南（Rule STR30-C），应统一替换为带边界保证的 `snprintf`。
- **防御性修复建议**：
  替换为 `snprintf(frame, frame_len, "%s\n", request_json);`。

---

### 3.4 Slice 4: 多租户隔离与金融一致性

#### `SEC-FIN-001` [High] `tx_lot_update_remaining` 缺乏 `user_id` 过滤违反多租户隔离规范
- **CWE 映射**：[CWE-284: Improper Access Control](https://cwe.mitre.org/data/definitions/284.html) / [CWE-639: Authorization Bypass Through User-Controlled Key](https://cwe.mitre.org/data/definitions/639.html)
- **源码位置**：`backend/src/infrastructure/repositories/transaction_lot_repo_impl.c:90-112`
- **缺陷原理分析**：
  ```c
  int tx_lot_update_remaining(csilk_db_pool_t* pool,
                              int64_t lot_id,
                              quantity_t new_remaining_qty,
                              bool is_closed) {
      ...
      const char* sql =
          "UPDATE transaction_lots SET remaining_quantity=?, status=? WHERE id=? RETURNING id";
  ```
  函数签名及执行的 SQL 完全缺失了 `user_id` 参数与约束，直接违反了项目核心架构安全准则："All asset mutations are ownership-filtered (WHERE id=? AND user_id=?) as defense-in-depth: caller can never read or write another user's asset"。若出现并发逻辑混乱或上层传参偏差，可能篡改属于其他租户的投资批次状态。
- **业务危害与影响面**：违背多租户纵深防御设计，存在潜在的跨用户批次污染或批次状态被错误清零风险。
- **防御性修复建议**：
  在仓储契约中将 `user_id` 显式引入函数参数：
  ```c
  int tx_lot_update_remaining(csilk_db_pool_t* pool,
                              int64_t user_id,
                              int64_t lot_id,
                              quantity_t new_remaining_qty,
                              bool is_closed);
  ```
  并将 SQL 更新为 `WHERE id=? AND user_id=? RETURNING id`。

---

#### `SEC-FIN-002` [Medium] 资产删除直接级联交易缺乏手续费与资金账户联动逆向回滚
- **CWE 映射**：[CWE-404: Improper Resource Shutdown or Release](https://cwe.mitre.org/data/definitions/404.html)
- **源码位置**：`backend/src/infrastructure/repositories/asset_repo_impl.c:426-446`
- **缺陷原理分析**：
  `asset_delete` 直接执行 `DELETE FROM assets WHERE id=? AND user_id=?`。由于底层外键配置了 `ON DELETE CASCADE`，关联的 `transactions` 会被数据库底层级联清除。这绕过了用例层的 `tx_child_fee_rows` 手续费回滚和资金账户的借贷差量冲正（`balance_apply_delta`），导致关联资金账户出现永久性账目失衡。
- **防御性修复建议**：
  在 `asset_usecase_delete` 中增加前置防护断言：当资产下存在任何有效交易时，拒绝删除并返回 HTTP 1004（引导用户先归档资产或先行清理交易流水），避免底层静默级联删除导致账目逻辑断裂。

---

## 4. 系统加固与长效防御建议 (Roadmap)

为了从机制上杜绝同类安全隐患并构建主动防御体系，建议在后续实施阶段推进以下四项加固措施：

1. **持续集成 AddressSanitizer (ASan) & UBSan**：
   - 在 CMake 中建立专项 Sanitizer 构建选项（`-fsanitize=address,undefined`），将现有 51 个单元测试和 184 个集成测试纳入 ASan 自动化回归，在 CI 阶段实时拦截 UAF、越界读写与内存泄漏。
2. **MCP 子进程运行沙箱化 (Sandbox Hardening)**：
   - 彻底重构 STDIO MCP 拉起机制，构建严谨的白名单环境变量数组，杜绝父进程敏感 DSN 与 JWT 凭据向任何外部子进程外泄。
3. **仓储层静态 SQL 契约门禁 (Linter Rule)**：
   - 增加静态检查脚本，对所有在 `infrastructure/repositories/` 中声明的写操作 SQL，强制断言必须包含 `user_id = ?`（仅除系统全局设置表与公共字典表外），杜绝单 ID 更新漏洞。
4. **统一整型解析防呆机制**：
   - 全面禁用 `atoi` / `atoll`，在编译告警中开启 `-Wformat-security`，并在 Controller 契约层推行严格的 `parse_query_int_strict` 门禁。
