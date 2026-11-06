# Minefolio 全量代码安全审查设计方案

- **日期**：2026-11-06
- **状态**：Draft（等待构建者审查与用户审批）
- **目标路径**：`docs/superpowers/specs/2026-11-06-comprehensive-security-audit-design.md`
- **交付目标文件**：`docs/security-audit-2026-11-06.md`

---

## 1. 意图与背景 (Intent and The Why)

Minefolio 是一套纯 C23 + csilk HTTP 框架构建的自托管个人资产与投资追踪系统，承载了用户的账户凭证、资产账本、交易记录、敏感金融数据以及与大语言模型/外部 MCP 服务的深度协同。系统拥有约 6.4 万行 C 源码、148 条 API/MCP 路由与 17 个 DDD 业务领域。

由于底层采用纯 C 语言开发并包含大量指针计算、系统调用与底层内存管理，同时对外部暴露了丰富的 HTTP/WebSocket/SSE 接口以及 AI 工具执行通道，潜在的安全风险不仅涵盖传统的 Web 漏洞（SQLi、XSS、CSRF、未授权越权），还包含 C 语言特有的内存安全隐患（UAF、缓冲区越界、格式化字符串、空指针解引用）以及 AI Agent 时代的特有威胁（HMAC 令牌重放、工具权限越权、外部子进程命令注入、提示注入导致的数据穿透）。

为了全面夯实系统的安全防线，本方案旨在建立一次覆盖四大核心攻击面的**全量纵深安全审查机制**，并输出一份专业、详实、按严重级别（Critical, High, Medium, Low）分类的安全审计报告（`docs/security-audit-2026-11-06.md`），为系统后续的针对性安全加固与防御演进提供确切指引。

---

## 2. 目标、非目标与反目标 (Goals, Non-Goals, Anti-Goals)

### 2.1 目标 (Goals)
1. **四大攻击面全覆盖**：
   - 维度 1：底层 C 内存与资源安全（UAF、内存泄漏、悬垂指针、缓冲区越界、格式化字符串）。
   - 维度 2：Web 与网络通信边界（SQL 注入、未授权与垂直/水平越权、JWT 机制、CSRF 防护、参数解析健壮性、文件上传解析沙箱）。
   - 维度 3：AI 运行时与 MCP 协议安全（HMAC 批量令牌签名与防重放、语义投影脱敏、只读/写权限校验、MCP 子进程与 STDIO 管道隔离）。
   - 维度 4：多租户与金融一致性（多用户数据隔离、嵌套事务保存点原子回滚、128 位定点数金融溢出与除零边界）。
2. **生成规范化审计报告**：输出 `docs/security-audit-2026-11-06.md`，每个漏洞均具备唯一定位（精确到 `file:line`）、CWE 编号、严重度等级、成因机理阐述、业务危害评估及具体防御修复代码建议。
3. **保持测试与系统完整性**：审计过程中仅做静态推演与只读检测，不污染工作树，不破坏现有 51 个单元测试和 184 个集成测试。

### 2.2 非目标 (Non-Goals)
1. **暂不包含代码实时修改**：本阶段的核心交付物为审计报告与修复建议清单，不直接在业务代码库中落地功能变更（后续作为独立修复任务执行）。
2. **不生成破坏性利用载荷**：严禁编写武器化的实际漏洞利用脚本或 PoC 渗透工具，仅在报告中通过代码逻辑阐述触发条件与防御机理。
3. **不重构既有业务逻辑规范**：审计着眼于安全漏洞与防御薄弱点，不借机进行大规模纯风格重构。

### 2.3 反目标 (Anti-Goals)
1. **杜绝空泛的工具级输出与宏观套话**：严禁无源码依据的臆测，杜绝脱离业务场景的通用安全说教。
2. **拒绝单一浅层的正则匹配**：严禁仅靠 `grep` 关键词而忽略上下文的浮躁扫描，必须深入调用栈（Controller → Usecase → Repository → Database）与生命周期追踪。

---

## 3. 约束与边界 (Constraints)

1. **只读审查契约**：审查过程对生产源码与测试套件完全只读，所有审查中间产物与最终报告存放于 `docs/` 目录。
2. **安全规范对齐**：漏洞定级与分类参考 OWASP Top 10、CWE/SANS Top 25、SEI CERT C 安全编码标准及 CVSS v3.1 评分框架。
3. **架构规约一致性**：遵循项目既定的 DDD 四层依赖法则（Interfaces → Application → Domain / Core / Infrastructure），审查跨层调用中的安全边界。

---

## 4. 备选方案论证 (Approaches)

在方案制定阶段，我们系统性对比了以下三种实施路径：

| 维度 | 方案一：四维切片并行纵深审计（选定） | 方案二：外部攻击面调用链追踪 | 方案三：规则匹配启发式扫描 |
| :--- | :--- | :--- | :--- |
| **排查方式** | 拆分为内存、边界、AI、金融 4 个专业切片并行全量代码下钻 | 从 148 条路由自外向内追踪数据流向 | 基于危险函数关键词快速正则匹配 |
| **优点** | 覆盖面彻底，兼顾底层 C 内存陷阱与业务一致性，无死角 | 对外部直接可达的 API 漏洞检出快 | 速度极快，开销极小 |
| **局限与权衡**| 分析深度大，需要投入较高精力梳理上下文调用栈 | 容易遗漏离线引擎、后台重放任务及内部函数偶发 UAF | 误报漏报率高，无法识别业务越权与事务逻辑漏洞 |
| **决策理由** | **用户选定方案一**：Minefolio 架构具备强 DDD 与金融定点核心，浅层追踪无法有效触及事务状态机与批次核销漏洞，纵深切片能最大化保证审计报告的严谨度。 |

---

## 5. 详细审计实施规范 (Detailed Surface Breakdown)

```
+-----------------------------------------------------------------------------------------+
|                       Minefolio 全量安全审查四大攻击面                                   |
+-----------------------------------------------------------------------------------------+
| [Slice 1] 底层 C 内存与系统安全                                                         |
|  ├── UAF (Use-After-Free) 检查 (如连接/对象提前释放后打印或使用)                         |
|  ├── 缓冲区与字符串截断 (snprintf, strncpy, 堆栈溢出, 格式化字符串注入)                   |
|  └── 资源生命周期与泄漏 (未闭合的文件句柄, 内存未 free, 未释放的互斥锁)                  |
+-----------------------------------------------------------------------------------------+
| [Slice 2] Web 与网络通信边界                                                            |
|  ├── SQL 注入防线 (全量校验是否所有 SQL 均使用 ? 参数化，是否存在拼接)                   |
|  ├── 接口参数绑定 (强制使用 parse_query_int_strict，杜绝 atoll 隐式转 0 导致的安全逻辑穿透)|
|  ├── 认证与鉴权完整性 (JWT 签名防篡改、密钥轮转保护、CSRF 校验闭环、IDOR 越权)           |
|  └── 文件上传解析安全 (mkstemps 临时文件安全性、符号链接攻击防护、CSV 注入防护)          |
+-----------------------------------------------------------------------------------------+
| [Slice 3] AI 运行时与 MCP 协议安全                                                      |
|  ├── 复合操作防重放 (HMAC 签名算法强度、ai_consumed_nonces 防重放唯一性)                |
|  ├── 语义投影脱敏 (避免资产 account_id、系统物理路径、用户信息泄露入模型上下文)          |
|  ├── 高危工具调用门禁 (Policy 校验漏拦截、未授权强制执行保护)                            |
|  └── MCP 运行时防护 (STDIO 子进程参数注入、管道死锁与泄漏、-32001 未授权协议一致性)     |
+-----------------------------------------------------------------------------------------+
| [Slice 4] 金融一致性与多租户隔离                                                        |
|  ├── 多租户横向越权过滤 (所有 Repository 查询/写入必须强绑定 WHERE user_id = ?)           |
|  ├── 事务保存点原子性 (db_tx_scope_t 嵌套深度、失败路径是否 100% 回滚，杜绝部分状态落库)  |
|  └── 定点数金融运算安全 (128-bit decimal 溢出保护、除零异常、负债资产方向取反正确性)      |
+-----------------------------------------------------------------------------------------+
```

### 5.1 维度 1：底层 C 内存与系统安全
- **核心核查文件**：
  - `backend/src/infrastructure/database/postgres/postgres_adapter.c`
  - `backend/src/infrastructure/database/sqlite/sqlite_adapter.c`
  - `backend/src/infrastructure/database/database.c`
  - `backend/src/infrastructure/database/statement.c`
  - `backend/src/core/financial/decimal.c`
  - `backend/src/services/file_parser.c`
- **重点排查项**：
  1. 动态资源释放时序：排查所有 `free()`, `PQfinish()`, `sqlite3_finalize()`, `fclose()` 的先后时序，杜绝 UAF 与 Double-Free。
  2. 字符串边界：全面排查 `snprintf`, `strcpy`, `strcat`, `sprintf` 调用，确保缓冲区大小声明充裕，且无 `%n` 格式化漏洞。
  3. 异常路径资源释放：排查多分支 `return -1;` 路径前是否遗漏已分配内存和锁。

### 5.2 维度 2：Web 与通信边界
- **核心核查文件**：
  - `backend/src/interfaces/http/controllers/*_controller.c`（全量 148 条路由）
  - `backend/src/middlewares/*.c`（jwt, csrf, rate_limit, security_headers, cors）
  - `backend/src/common/response.c`
  - `backend/src/application/auth/usecases.c`
- **重点排查项**：
  1. SQL 注入零容忍：核查所有 `csilk_db_query_param_json` 调用，确保参数均为 `?` 绑定，严格禁止任何基于 `snprintf` 动态拼接用户输入的 SQL 片段。
  2. HTTP 参数严格解析：核查所有 query/path 参数获取逻辑，验证是否强制使用 `parse_query_int_strict()`，禁止使用 `atoll()`。
  3. 鉴权闭环：核查是否存在未挂载 JWT 中间件却访问敏感资产/财务数据的路由。
  4. CSRF 与敏感操作校验：排查所有 `POST/PUT/DELETE` 接口的 CSRF Token 校验覆盖度。

### 5.3 维度 3：AI 运行时与 MCP 协议安全
- **核心核查文件**：
  - `backend/src/services/ai/policy/*.c`
  - `backend/src/services/ai/tools/*.c`
  - `backend/src/services/ai/runtime/*.c`
  - `backend/src/services/ai/mcp/*.c`
  - `backend/src/interfaces/http/controllers/ai_controller.c`
- **重点排查项**：
  1. HMAC 签名防重放机制：排查 `POST /api/ai/actions/confirm-batch` 及单笔确认中 `ai_confirmation_verify_and_consume()` 的持久化 Nonce 防重放逻辑，确保跨进程/重启无法重放。
  2. 上下文投影脱敏：排查 `projection.c` 中暴露给模型的字段，核查是否遗漏密码哈希、用户敏感信息或系统内部路径。
  3. MCP 进程安全：审查 `mcp_client.c` 中拉起外部 MCP Server 时的参数传递，确保没有 shell 注入漏洞，并检查非正常断开时的僵尸进程处理。

### 5.4 维度 4：金融一致性与多租户隔离
- **核心核查文件**：
  - `backend/src/infrastructure/repositories/*_repo_impl.c`（22 个仓储）
  - `backend/src/application/{transaction,asset,transfer,account,daily_expense}/usecases.c`
  - `backend/src/core/ledger/ledger_engine.c`
- **重点排查项**：
  1. 水平越权隔离：每一个 SQL 仓储的 SELECT/UPDATE/DELETE/INSERT 语句，必须携带 `user_id = ?` 约束，禁止任何通过篡改 `id` 访问他人账单的操作。
  2. 嵌套事务作用域：审查所有包含多步更新（如扣款+转账+手续费记录）的业务场景，验证是否正确使用 `db_tx_scope_begin()` 和 `db_tx_scope_commit()`，在任一中间步骤失败时是否执行完整的 `db_tx_scope_rollback()`。
  3. 金融精度溢出：核查是否存在中间结果转裸 `double` 计算导致的浮点失真，核查除法运算前是否有零值校验。

---

## 6. 审计报告结构规范 (`docs/security-audit-2026-11-06.md`)

报告统一遵循以下标准工业级安全规范格式：

1. **执行摘要 (Executive Summary)**：
   - 总体安全态势评估、安全防线成熟度打分、核心风险雷达图。
2. **漏洞分类统计矩阵 (Vulnerability Matrix)**：
   - 按严重度（Critical, High, Medium, Low）与攻击面（Slice 1~4）的统计表格。
3. **安全隐患详情清单 (Detailed Findings)**：
   每个发现均包含：
   - **漏洞编号**：如 `SEC-MEM-001`、`SEC-WEB-002` 等。
   - **漏洞标题**：简明扼要的危害总结。
   - **严重等级**：Critical / High / Medium / Low。
   - **CWE 映射**：对应行业通用 CWE 编号（如 CWE-416, CWE-89, CWE-284）。
   - **源码位置**：具体文件与起始行号（如 `backend/src/.../file.c:146`）。
   - **缺陷原理分析**：代码缺陷与触发路径分析。
   - **业务危害与影响面**：在生产环境可能造成的后果。
   - **防御性修复建议**：提供精准的代码级修复示例。
4. **系统加固与防御演化建议 (Roadmap)**：
   - 针对发现问题的长期架构防御机制优化建议（编译期警告、静态分析 CI、动态 Sanitizer 等）。

---

## 7. 留给构建者的工程决策 (What's Left to the Builder)

1. **定级标准权衡**：
   - 影响生产环境崩溃或任意代码执行、未授权任意数据删除/修改者定为 **Critical**；
   - 存在越权访问、安全限制被绕过但受限于特定条件者定为 **High**；
   - 内部潜在资源泄漏、日志信息泄露、非关键边界未校验定为 **Medium**；
   - 编码规范偏差或边缘代码健壮性优化定为 **Low**。
2. **多线程/并发静态审查重点**：
   - 构建者可重点关注连接池获取、AI 会话全局并发锁及后台汇率同步线程的同步安全性。
