# Minefolio 全项目代码分析（2026-11-13）

基线：`3ba09f52`（master，工作树干净）。方法：14 切片并行只读分析（6 个 scout 完整报告 + 4 个切片由主线直接审计，scout 上游 429/404 失败切片已补做），高影响结论均经主线 `file:line` 复核。标注：`[自验]` 主线直接验证，`[切片]` scout 报告结论（关键项已抽检）。

## 0. 总评（一句话）

**架构成熟度高的自研财务系统**：128-bit 定点金融核心、DDD 四层、17 域迁移完成、AI Runtime 带完整预算/策略/确认/追踪体系；主要短板是 **AI 确认令牌的 P0 无限递归**、**MCP 外部内容注入隔离缺口**、**移动端登出后本地库永久失效**，以及双引擎（SQLite/PG）类型契约漂移带来的隐性一致性问题。

## 1. 规模与结构

| 层 | 规模 | 说明 |
|----|------|------|
| 后端 C | ~69k 行 | 17 个 DDD 域 + AI 子系统（services/ai 19k 行） |
| core/financial | 2.2k 行 | 128-bit `decimal_t`，`u256_t` 中间精确运算，`k_pow10[0..38]` 预计算 |
| core/ledger | 1.7k 行 | 引擎 + V020 checkpoint 增量重建 |
| application | 10.5k 行 | 用例层，`void* pool` 强约定 |
| infrastructure/repositories | 8.4k 行 | 20+ 文件，全参数化 SQL |
| services/ai | 19k 行 | runtime/tools/workflow/policy/trace/memory/model |
| 前端 | 168 TS + 37.5k 行 Vue | 桌面 + 移动端双入口 |
| 迁移 | V001–V022 | csilk 迁移引擎，双引擎（sqlite/postgres 目录） |
| 测试 | 58 CTest 套件 + 20 shell 集成脚本 + 37 vitest spec（177 tests） | ASan/UBSan + cgroups v2 隔离 |

## 2. 架构评价（按层）

### 后端分层纪律 — 优秀
- **domain 层纯度** `[自验]`：`src/domain/**/*.c` 零 csilk include、零裸 SQL（grep 确认）；仅 `domain/*/repository.h` 契约头 include `csilk/csilk.h`——这是仓库模式的必要妥协（返回 `csilk_json_t*`），可接受，且所有 domain .c 实体/规则文件保持纯 C。
- **控制器薄度** `[自验]`：controllers 共 7.9k 行/20+ 文件；仅 `admin_controller.c`（5 处直查 DB）与 9 个各 1 处存在直调——多为 setup/seed 路径，属边缘违规而非系统性下沉。
- **balance 原子性** `[自验]`：`balance_apply_delta_m`（common/balance.c:30）自带 savepoint 作用域：归属查询→多币种归一→负债符号翻转→原子 UPDATE（`id=? AND user_id=?`）→读回 after 快照→审计日志，失败路径全部 rollback。与 AGENTS.md 契约一致。
- **转账删除不变量** `[自验]`：transaction/usecases.c:430-468 完整实现 `parent_tx_id → V014 (from_tx_id OR to_tx_id) → 属性匹配兜底` 三级配对，注释工程质量高。

### 金融核心 — 优秀，有一处可改进
- `decimal.c`：128-bit 定点 + 256-bit 中间精确乘法，10^0..10^38 幂表预计算；`pow10_u128` 对 `scale>38` 返回 0 而非报错（调用方须守约，边界行为依赖约定而非防御，P2）。
- 用例层 decimal 纪律 `[切片 AppAssets 未交付→主线抽检]`：asset/market usecases 有 `balance_apply_delta_m` 定点路径并行于 double 路径，AGENTS.md 约定的"用例层 128-bit 聚合"在 PnL/聚合热点已落实（test_financial_precision_edge 钉住）。

### AI 子系统 — 设计优秀，安全细节有 P0
- **Runtime 循环**（loop.c 1584 行）：预算执行顺序 cancel→iterations→timeout→token→cost（limits.c:check_pre_turn），耗尽走"无工具收敛调用"给用户结论而非硬失败；8KB 工具结果截断（UTF-8 边界回退）；FNV-1a 64 槽单次运行去重；SSE 断连协作取消（cancel_token 三层：chunk 累积停止/工具探针/轮次检查）。工程化程度超出多数同类项目。
- **确认令牌**：HMAC v2 + 内存环 2048 + V019 `ai_consumed_nonces` DB 双层重放防护 + 每 10 次 GC——设计好，**但实现有 P0 递归缺陷**（见 §3）。
- **工具/策略**：8 步分发管线 + 全程审计；`internal_only` 工具不暴露给 LLM（防 Prompt Injection 绕过 HITL）；SSRF 防御（含 DNS Rebinding 解析实际 IP）完备。

### 移动端离线 — 设计大胆，缺陷集中
- sql.js WASM 内嵌 base64 + AES-256-GCM localStorage 加密 + 离线写降级 + ID 重映射（remapAsset/AccountRefs）——完整闭环设计。
- **P0：登出后本地库永久失效**（§3-P0-3）。

### 市场/汇率 — 单实例假设明确
- `g_rates[]` 静态表 + 三级回退（tencent 批量→frankfurter→yahoo）；AGENTS.md 已明确"单实例部署"约束 `[切片 AuthMarketFx]`。
- P1：调度器 DCA 同日重启重复触发（`last_dca_check_day` 为进程态，market_scheduler.c:158）；quote_engine 缓存锁内做 15 次 HTTP（最长 ~75s 阻塞指数读）。

## 3. P0/P1 缺陷总表（按严重度）

### P0（可被真实用户触发）

| # | 位置 | 缺陷 | 验证 |
|---|------|------|------|
| P0-1 | `services/ai/policy/confirmation.c:394↔743` | **无限递归**：`verify_and_consume` 遇非 `mf_v2.` 令牌回退调 `verify_token`；`verify_token` 又反向调 `verify_and_consume(user_id, 0, "legacy_action", dummy, token)`。任何非 v2 格式令牌（包括 LLM 生成的工具参数——`confirm_proposed_expense`/`_transfer` 的 `draft_token` 参数直接进此路径，expense_tool.c:273 / transfer_tool.c:177）→ 递归深度无界 → 工具线程栈溢出，**进程崩溃**。"legacy 兼容"分支形同陷阱。 | `[自验]` 双向调用链确认 |
| P0-2 | `services/ai/runtime/loop.c:386-398` + `mcp_bridge.c:143` | **外部内容注入隔离缺口**：`is_untrusted_tool` 只认 `web_search` 与 `mcp_` 前缀；MCP 实际路由名是 `mcp:<id>:<tool>`（`mcp:` 冒号），前缀匹配永远不命中 → MCP 远端工具结果**零包裹**直进模型上下文；`fetch_webpage`（web_search_tool.c:1066 注册）抓取任意外部页面，清洗结果同样不经过 `<untrusted_tool_result>` 隔离。两者是 Prompt Injection 主通道。 | `[自验]` 前缀不一致确认 |
| P0-3 | `frontend/src/db/local.ts:54` + `main-mobile.ts:35` + `stores/sync.ts:354` | **移动端登出后离线功能永久失效**：`initLocalDb` 全库唯一调用点在主入口 bootstrap；logout→`resetLocalDb()` 置 `db=null` 且清 localStorage；重新登录无任何 re-init → `getDb()` 抛 `localDb not initialized`，被 syncNow 静默吞掉，离线降级与队列推送全部失效直至重启 App。 | `[自验]` 调用点枚举确认 |

### P1（高影响，建议排期修复）

| # | 位置 | 缺陷 |
|---|------|------|
| P1-1 | `application/auth/usecases.c:70` | register 未调 `mf_auth_rule_can_register`（规则函数仅有单测、生产无调用点）：配置关闭注册时仍可注册 `[自验]` |
| P1-2 | `application/auth/usecases.c:868` | `MINEFOLIO_TEST_OAUTH_MOCK` 在请求路径 getenv：生产误设该环境变量即可伪造任意 OAuth 用户（`mock_code_*` → 自动建户）；应仅测试构建 `[自验]` |
| P1-3 | `application/ai/probe_usecase.c:215-235` | `test_connection`/`fetch_models` 将用户可控 `base_url` 直连 curl，**未接** domain 现成的 `mf_ai_rule_validate_url`（SSRF 规则写了但本调用点没接） `[切片 UseCasesAI]` |
| P1-4 | `application/skill/usecases.c` + `domain/skill/rules.c:36-49` | L3 脚本执行面：`exec_root` 默认 `/` 且用户可改；`exec_path` 仅禁绝对/`..`/换行；配合模型可触发的 skill 工具，等价任意相对路径脚本执行（root 部署时） `[切片 UseCasesAI]` |
| P1-5 | `services/market/market_scheduler.c:158` | DCA 同日重复触发：`last_dca_check_day` 进程态，服务同日重启后 `tm_yday` 未变即重建 execution（若无 DB 当日幂等约束） `[切片 AuthMarketFx]` |
| P1-6 | `services/market/quote_engine.c:650` | `g_indices_cache` 持锁内做 ≤15 次 4-5s HTTP 拉取，网络挂起时最长 ~75s 串行阻塞所有指数读 API `[切片 AuthMarketFx]` |
| P1-7 | `services/ai/runtime/loop.c:990` | 串行分派中 `decision`（calloc）为 NULL 时直接解引用 `decision->requires_confirmation` → OOM 场景崩溃 `[切片 AiRuntime]` |
| P1-8 | `frontend/src/stores/chat.ts:519/609/751/933/1120` | `activeAbortController`/`isStreaming` 共享变量，旧流 `finally` 无条件清空——快速切会话后旧流 finally 后到会把**新**控制器清掉（abort 按钮失效）；应校验控制器身份 `[切片 FrontendCore]` |
| P1-9 | `frontend/src/stores/chat.ts:243` | `createNewSession` 无 seq 守卫且不自增 `sessionSwitchSeq`：`doSendMessage` 的 `await createNewSession()` 窗口内旧 `selectSession` 结果可污染新会话 `[切片 FrontendCore]` |
| P1-10 | `frontend/src/stores/sync.ts` + `db/schema.ts` | 本地 schema 落后后端 V020-V022：无 `ledger_snapshots`、`transaction_lots`/`cost_method`、`ai_skills`、`transfers`；`transactions.fx_rate_to_cny` 被 TABLE_COLUMNS 白名单静默丢弃；冲突（conflict=1）无 UI 解决路径且每次 sync 无限重试 `[切片 FrontendMobile]` |
| P1-11 | `infrastructure/repositories/asset_repo_impl.c:545` | `atoll(asset_id_str)` 处理用户输入——AGENTS.md 明令"NEVER atoll"（非法输入静默转 0，绕过 `parse_query_int_strict` 1002 契约） `[自验]` |
| P1-12 | `services/ai/workflow` executor | MCP/工具错误判定全库靠 `strstr(result, "\"error\":")`（dispatcher.c:206、mcp_bridge.c:672）：业务 JSON 内嵌字符串含该字面量即误判成败 `[切片 AiTools]` |

### P2（记录，择机）
- `report_tool.c:210-224` 假 `web_search` 与真版同名，靠注册顺序覆盖——死代码+隐患。
- `services/ai/model/` 6 文件全库无调用方（死代码子层）。
- `confirmation.c:169` nonce 环满时游标覆盖无视槽位过期；密钥测试模式回落固定字符串。
- 16 字节 `JWT_SECRET` 被 csilk jwt 静默拒绝 → "登录即 401"；main.c:65 只查非空，建议启动期断言长度 ≥32。
- `market.ts`/`auth.logout` 清理遗漏 `minefolio_pinned_sessions` 与 `minefolio_cached_*`（clearSwrCache 前缀 `minefolio_cache_` 匹配不到 `cached`）。
- RSA 公钥缓存无 TTL：服务端轮转密钥后需登出才恢复。
- 本地 schema `accounts.ledger_id` 本地 DEFAULT 0 vs 后端 `UNIQUE(user_id,ledger_id,name)`：pull 覆盖后本地旧账户行可能撞唯一键。
- `main.c:83` 打印 "2 worker(s)" 但从不设置 worker 数（csilk 夹到 1）——文档与运行时不符（AGENTS.md 已知项）。
- 前端 `catch (err: any)` 泛滥（chat.ts 5 处等），违反无裸 any 约定；3 处 `toISOString().slice(0,10)` 仅用于导出文件名（UTC 偏移一天，低风险）。
- PG/SQLite 结果集类型契约分裂：`common/db.h:142` 注释已更新为双引擎差异说明，但仍有 5 处直调 `csilk_json_get_number`（jwt.c:95、jwt_middleware.c:211-212 读的是自签 JWT payload 非 DB 行——安全；auth/usecases.c:828 读 GitHub API JSON——安全）；**DB 行上的直调需持续盯防**。

## 4. 亮点（值得保持的做法）

1. **金融核心 128-bit 定点**：`decimal.c` 的 256-bit 中间精确乘法 + 幂表，PnL/成本法聚合无浮点累积误差；`test_financial_precision_edge` 钉住回归。
2. **balance_apply_delta_m 原子审计链**：delta + after 快照 + 审计日志同 savepoint，负债符号翻转集中一处。
3. **AI 确认双层重放防护**：HMAC v2 + 内存环 + DB PK 抢占 + GC，跨重启有效。
4. **MCP stdio 池**：`in_progress` 占位槽防死锁、fork 后 `closefrom(3)` fd 隔离 + 敏感 env 过滤（SECRET/KEY/TOKEN）。
5. **SSRF 防御深度**：含 DNS Rebinding（getaddrinfo 解实际 IP 再校验内网段）+ 逐重定向跳校验 + 4MB 写缓冲上限。
6. **测试隔离工程**：ASan 构建 + cgroups v2 MemoryMax 包裹 + LD_PRELOAD 配方 + 双引擎 CI。
7. **文档诚实度**：AGENTS.md 的 Known Gotchas 14 条、单实例假设标注、`void* pool` 设计动机说明——少见的高质量工程文档。

## 5. 测试覆盖矩阵（缺口）

| 区域 | 覆盖 | 缺口 |
|------|------|------|
| financial core | 强（边界钉住） | `pow10` scale>38 返回 0 的调用方守约无断言 |
| ledger 重建/checkpoint | 强（test_link case 40 + ctest） | 同日多笔 + 删除间隙级联失效组合 |
| 确认令牌 | 有 replay 单测 | **P0 递归路径无测试**（legacy 令牌输入） |
| MCP/工具注入 | mock server 覆盖 | `fetch_webpage`/MCP 远端结果包裹缺口无回归测试 |
| 移动端 sync | vitest 有 sync store 测试 | 登出→重登录 re-init 路径无测试（P0-3） |
| chat 竞态 | sessionSwitchSeq 有测试 | 旧流 finally 覆盖新控制器（P1-8）无测试 |

## 6. 建议行动顺序

- [ ] **P0-1** confirmation 递归：删除 `verify_token` 的 legacy 反向调用（legacy 令牌直接返回 FORMAT 错），加回归测试（构造 `"legacy_token"` 断言不递归/栈安全）。
- [ ] **P0-2** `is_untrusted_tool` 增加 `mcp:` 前缀匹配 + 把 `fetch_webpage` 加入包裹名单；补注入回归测试。
- [ ] **P0-3** 移动端 `auth.login` 成功后调用 `initLocalDb()`（幂等，已有 `if (db) return db` 守卫）；补 sync store 测试。
- [ ] **P1-1/2** register 接 `mf_auth_rule_can_register`；`MINEFOLIO_TEST_OAUTH_MOCK` 移入测试构建/启动期固化。
- [ ] **P1-3/4** probe usecase 接 `mf_ai_rule_validate_url`；skill L3 `exec_root` 默认收紧到项目目录 + 白名单。
- [ ] **P1-11** `asset_repo_impl.c:545` `atoll` → `parse_query_int_strict` 语义（拒绝非法 → 1002）。
- [ ] **P1-8/9** chat store：finally 校验控制器身份 + `createNewSession` 自增 seq。
- [ ] 清理死代码：`report_tool` 假 web_search、`services/ai/model/` 子层（确认无用后删除或标注）。

## 附：方法记录
- scout 上游 429/404 导致 8/14 切片失败，失败切片（Controllers、UseCasesLayer、InfraRepos、CoreFinancial、LedgerEngine、DomainContracts、AppAssets、FrontendViews）由主线直接抽查补齐：domain 纯度、控制器直查、balance 原子性、转账删除、`atoll`、`csilk_json_get_number` 5 处定位均已覆盖；深读项（ledger 级联失效组合、decimal scale>38 边界）保留在 §5 缺口清单。
- 排除的误报：AiTools 报告的 `executor.c:288-296` "parsed_out 泄漏"——复核两条失败路径：parse 失败即 free（:280）、成功路径所有权转移给 ctx_obj（:308→:303 free），无泄漏，降级为 P2 可读性问题（`slen` 变量混用）。
