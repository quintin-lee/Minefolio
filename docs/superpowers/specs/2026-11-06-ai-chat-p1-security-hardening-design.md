# AI 对话 P1 安全收口设计规范

- **日期**：2026-11-06
- **范围**：`backend/src/services/ai/` 运行时（policy / rate limit / mcp_bridge / web_search_tool / mcp rules / trace 治理）
- **子项目**：四方向路线图（P1 安全收口 → P2 交互体验 + 移动端入口 → P3 稳定性与可观测 → P4 功能扩展）中的第 1 个子项目
- **关联文档**：`docs/ai-feature-analysis.md`（2026-10-27 全景分析）、`docs/security-audit-2026-11-06.md`（11 项全量漏洞，已修）、`docs/ai-runtime-architecture.md`（架构规范）

---

## 1. 背景与现状核实

2026-10-27 的 AI 全景分析列出 S1–S5 安全断点。本次设计前对当前代码逐项复核，**多数断点已修复**，真实剩余工作收敛为 5 项：

| # | 断点（10-27 原文） | 复核结论（本次） |
|---|---|---|
| S1 | AI 设置端点无 admin 门禁 | **已修**：`ai_controller.c:require_instance_admin` 覆盖 PUT/test/fetch-models |
| S2 | 确认机制不阻断，policy 确认分支死代码 | **已修**：`loop.c:829` 对 `requires_confirmation` 且不自带确认协议的工具拒绝执行并回注模型 |
| S3 | ActionCard 前端旁路 REST 直写 | **已修**：`ActionCard.vue` 要求 `draft_token`（HMAC 300s TTL），缺失禁用写入；走 `/ai/actions/confirm`（dispatcher + policy + audit + 验签） |
| S4 | 权限默认全开（任意用户 ADMIN） | **已修**：`permission.c` 普通用户默认 STANDARD（READ+WRITE），仅实例管理员 ADMIN；SENSITIVE 工具需 ADMIN |
| S5 | 频次双计（loop + dispatcher/bridge 各计 1 次） | **部分修**：dispatcher 二道门 `count=false` 不重复计数；但 **MCP 另有独立 LRU（30/分）与 loop 全局 LRU（60/分）双套并存**，语义不清 → 本 spec 模块 1 |
| — | policy 对 `mcp:` 前缀恒判 READ | **真实断点**：`ai_permission_get_level` 按工具名 pattern 判，`mcp:*` 落到 READ；而 `mcp_bridge` 自算 `is_mutation→WRITE` 走 `ctx->permissions` 位图，与 policy 角色体系双轨 → 模块 1 |
| — | 取消不覆盖内置 web 工具 | **部分已修**：`web_search` 主通道与 `fetch_webpage` 双通道均已传 `cancel_token`；**仅 Wikipedia 兜底通道传 NULL** → 模块 2 |
| — | 摘要线程与 SSE 共用 sqlite 单连接竞态 | **已修**：`summary.c` worker 全程不触库，落库由主线程 `ai_summary_flush_pending` 串行化 |
| — | `ai_traces` 无清理策略 | **部分已修**：V018 已加 `session_id` 索引 + FK CASCADE + `/stats` 补 `avg_tokens_per_sec`/`total_cost_usd`；**无 TTL 保留清理** → 模块 3 |
| — | MCP SSRF 仅文本校验，IP 字面量绕过 | **真实断点**：`url_host_is_internal` 未识别十进制/十六进制 IP 字面量与 IPv6 压缩短写 → 模块 4 |

**结论**：本次 P1 实施 5 个模块 —— ①频次与权限判定统一重构（方案 B 完整收口）、②取消传播补齐、③ai_traces 保留清理、④MCP SSRF IP 绕过加固、⑤配套测试与回归。

---

## 2. 设计目标与非目标

**目标**
1. 频次判定收敛为**单一 LRU 实现 + 通道维度 + 单一配置源**，消除 loop 全局 LRU 与 MCP 独立 LRU 双套并存导致的"有效限制语义不可解释"问题。
2. 权限判定**单点收口到 policy 层角色体系**，MCP mutation 工具的 WRITE 需求经 `ai_permission_get_level` 口径统一判定，消除 bridge 侧 `ctx->permissions` 位图与 policy 角色体系双轨矛盾。
3. 取消信号传播到 `web_search` 的 Wikipedia 兜底抓取通道。
4. `ai_traces` 增加可配置保留期清理（启动幂等触发，无后台线程）。
5. 堵住 MCP SSRF 的十进制/十六进制 IP 字面量与 IPv6 压缩短写绕过。
6. 全部改动保持向后兼容：默认行为不劣化，配置项缺省值沿用现状。

**非目标（YAGNI，留给 P2/P3）**
- 不引入后台定时清理线程（trace 清理仅启动幂等触发）。
- 不重做 confirm 确认流端到端（已由 propose/confirm HMAC 令牌 + loop 阻断收口，属 P3 补齐 E2E 测试）。
- 不做交互体验 / 移动端入口（P2 子项目）。
- 不做工具/工作流功能扩展、多模型策略（P4 子项目）。

---

## 3. 模块 1：频次与权限判定统一重构

### 3.1 现状

```
loop.c (每轮工具调用前, L689/L798)
  └─ ai_policy_evaluate(...)        ← 计数封装 (内部 count=true) → 统一 LRU
     ai_policy_evaluate_ex(..., risk_override, count=true)   ← 全局 LRU s_rate_limits[256] 60/分
dispatcher.c (二道门)
  └─ ai_policy_evaluate_ex(..., NO_OVERRIDE, count=false)    ← 只读不计
mcp_bridge.c (MCP 工具调用)
  ├─ ctx->permissions 位图自判 (mcp_perm_for_tool: is_mutation→WRITE)   ← 双轨 perm
  ├─ mcp_rate_limit_check(user_id, mf_mcp_config_rate_per_min())       ← 独立 LRU s_mcp_rate_limits 30/分
  └─ ai_policy_evaluate_ex(..., risk, count=false)                  ← 只读不计
```

问题：
- **双 LRU**：内置工具走 60/分、MCP 走 30/分，两套缓冲各自淘汰，叠加后"MCP 实际受 30/分约束"这一事实无法从代码单一处读出。
- **双轨 perm**：`ai_permission_get_level("mcp:x:y")` 恒判 READ（名字不以 create_/update_ 等前缀开头）；bridge 却按 `is_mutation` 给 WRITE 并查 `ctx->permissions` 位图（`tools/context.c:29` 默认 `0xFFFFFFFF` 全开）。两条判定互不知晓，且位图全开使 mutation 写操作实际未被角色体系约束。

### 3.2 目标架构

**(a) 权限单点收口到 policy 角色体系。**
- `ai_policy_evaluate_ex` 新增可选入参 `ai_permission_level_t perm_override`（与既有 `risk_override` 同款"NO_OVERRIDE 哨兵"传参风格；`AI_PERM_NO_OVERRIDE` 表示沿用 `ai_permission_get_level` 默认判定）。
- `mcp_bridge` 不再自行检查 `ctx->permissions` 位图，改为把 `mcp_perm_for_tool(tool)` 的结果作为 `perm_override` 传入 policy；由 policy 复用 `ai_permission_role_allows(user_role, perm)` 完成「角色 × perm」判定（STANDARD 可 WRITE、SENSITIVE 需 ADMIN 的既有语义不变）。
- `tools/context.c` 的 `permissions` 位图字段**保留但不作为权限门禁**（降级为可选的细粒度过滤，默认全开无害），消除"bridge 位图与 policy 角色两套门禁"的矛盾。

**(b) 频次 LRU 收敛为带通道维度的单一实现。**
- 新增 `services/ai/policy/rate_limit.c/.h`：单一 LRU（容量 256），条目维度为 `(user_id, channel, minute_window, count)`。
- 通道枚举：`RATE_CHANNEL_DEFAULT`（内置工具，上限 `s_rules.max_frequency_per_minute`=60）、`RATE_CHANNEL_MCP`（MCP，上限由 env `MINEFOLIO_MCP_RATE_PER_MIN` 读入 rules）。
- 计数规则不变：loop 外层评估 `count=true` 时按"该工具是否为 MCP 工具（`mcp:` 前缀）"选通道计数；dispatcher/bridge 二道门 `count=false` 只读。
- `mcp_bridge` 删除自有 `s_mcp_rate_limits` LRU 与 `mcp_rate_limit_check`，改调 `rate_limit_check_channel(RATE_CHANNEL_MCP)`；超限仍返回结构化 `mcp_rate_limited`。
- 配置源统一：MCP 通道上限从 `mf_mcp_config_rate_per_min()` 读入 policy rules（启动/热重载时），bridge 不再直接读 env。

### 3.3 行为兼容性

- 内置工具：仍 60/分，语义不变。
- MCP 工具：仍受 `MINEFOLIO_MCP_RATE_PER_MIN`（默认 30）/分约束，但现在从统一 LRU 的 MCP 通道判定，语义可解释为"该通道独立限额"而非"与全局 LRU 叠加取严"。
- 权限：MCP mutation 工具从"位图全开恒放行"变为"经 STANDARD/ADMIN 角色判定"——**行为收紧**：普通 STANDARD 用户可 WRITE（放行 mutation），SENSITIVE 级（如 `execute_transfer`）仍需 ADMIN。此为有意的安全收口。

### 3.4 受影响文件

| 文件 | 变更 |
|---|---|
| `policy/policy.h` / `policy.c` | `ai_policy_evaluate_ex` 增 `perm_override` 参；频次判定改调 `rate_limit_check_channel`；删除内联 `s_rate_limits` LRU |
| `policy/rate_limit.h` / `.c`（新增） | 带通道 LRU 实现 |
| `policy/permission.h` | 增 `AI_PERM_NO_OVERRIDE` 哨兵（若放 policy 头则复用） |
| `tools/mcp/mcp_bridge.c` | 删自有 LRU 与 `mcp_rate_limit_check`；bridge perm 判定改传 `perm_override`；删 `ctx->permissions` 位图门禁 |
| `runtime/loop.c` | 工具调用计数处按 `mcp:` 前缀选通道；`count=true` 调统一 LRU |
| `tools/context.c` | `permissions` 位图当前仅有默认值 `0xFFFFFFFF`（无任何其他代码路径写入，属死配置）；本模块后不再作为门禁，补注释说明其已废弃、勿在后续代码中读取 |
| `domain/ai/rules.*`（可选） | MCP 通道上限并入 policy rules 读取 |

---

## 4. 模块 2：取消传播补齐（web_search 兜底通道）

`exec_web_search` 主通道已传 `ctx->cancel_token`，唯 **Wikipedia 兜底抓取（`web_search_tool.c` 约 L734 `curl_http_get_with_redirect_check(..., NULL)`）传 NULL**。设计：
- 内部静态 helper `wikipedia_opensearch(query, max_results)` 增加 `const volatile bool* cancel_token` 参数；调用点 L840 改传 `ctx->cancel_token`，helper 内 curl 调用改传该 token（非 NULL）。
- 兜底通道 `rc==-4`（取消）时返回 `{"error":"cancelled","message":"用户已取消本次操作"}`。
- 在主搜索返回后、进入 Wikipedia 兜底解析前，加一次显式取消检查：`if (ctx->cancel_token && *ctx->cancel_token) return cancelled_json;`，避免取消后仍跑完兜底抓取。
- `exec_fetch_webpage` 双通道已传 cancel_token，无需改动（保持现状）。
- 改动仅 `web_search_tool.c` 单文件；**公共工具 API 不变**，但内部静态 helper 签名变更，计划中需列明。

---

## 5. 模块 3：ai_traces 保留清理策略

- 新增 env `MINEFOLIO_AI_TRACE_RETENTION_DAYS`（默认 `0`=不清理，向后兼容；>0 为保留天数）。
- 清理时机：`ai_init(pool)`（`ai_service.c`）启动/热重载时执行一次幂等清理。**首轮语义**：冷启动时若 `MINEFOLIO_AI_TRACE_RETENTION_DAYS>0`，立即执行一次清理；24h 幂等保护仅抑制**同一进程生命周期内**的后续热重载调用（`s_last_trace_cleanup` 为进程内 static，重启即重置）。
- 执行 `DELETE FROM ai_traces WHERE created_at < CURRENT_TIMESTAMP - N days`（PG 方言）/ 对应 SQLite 方言。
- 同步补 SQLite baseline（`sql/migration_sqlite.sql` 若缺 `ai_traces` 保留字段则无需 DDL，清理为运行时 DML，无迁移）。
- 影响文件：`ai_service.c`（`ai_init` 内触发）、新增 helper `common/ai_trace.c` 或 `infrastructure/repositories/ai_trace_repo_impl.c` 增 `mf_ai_trace_cleanup(pool, days)`。

---

## 6. 模块 4：MCP SSRF IP 形式绕过加固

`domain/mcp/rules.c:url_host_is_internal` 现仅识别点分十进制 IP 与已知 IPv6 常量。**注意**：`domain/ai/rules.c` 中存在**同名但独立实现**的 `url_host_is_internal`（`mf_ai_rule_validate_url` 调用），其实现**更完整**——已用 `inet_aton`（glibc 下接受十进制/十六进制/八进制 IP 字面量）与 `inet_pton(AF_INET6)` 处理 v6，意味着 `mf_ai_rule_validate_url` 路径**已实际拒绝** `2130706433`/`0x7f000001` 等绕过形式；而 `domain/mcp/rules.c` 的拷贝缺少这些处理，是真实残留绕过面。设计（以 `ai/rules.c` 的较完整实现为基准）：
1. 抽出共用 helper `domain/net/internal_ip.c/.h`，实现为 `ai/rules.c` 现行逻辑的超集：`inet_pton(AF_INET)` + `inet_pton(AF_INET6)` + 纯数字字面量当 IPv4 int32 + `0x` 前缀十六进制 uint32，逐段判回环/私有/链路本地/0.0.0.0/255.255.255.255。
2. `domain/mcp/rules.c` 与 `domain/ai/rules.c` 的本地 `url_host_is_internal` 均删除，改调共用 helper（消除双实现分叉风险）。
3. 域名仍走既有 `getaddrinfo` 全 IP 扫描（保留现状，nip.io 类已覆盖）。
影响文件：新增 `domain/net/internal_ip.c/.h`；`domain/mcp/rules.c`、`domain/ai/rules.c` 删本地实现改调 helper；CMake 增 `domain/net` 源目录（若独立成目录则需同步 `cmake/source-list`）。

---

## 7. 模块 5：测试与回归

| 套件 | 新增/修改断言 |
|---|---|
| `tests/unit/test_ai_policy.c` | MCP 通道 LRU 计数；`perm_override` 判定；MCP mutation 经 STANDARD 放行 WRITE、经 ADMIN 放行 SENSITIVE |
| `tests/unit/test_policy_mcp_risk.c` | mutation 工具 risk 升级（low→medium）与 perm 联动 |
| `tests/unit/test_ai_runtime.c` 或 loop 套件 | 内置工具 60/分、MCP 工具 30/分双通道独立计数 |
| `tests/test_ai_loop_hardening.sh` | web_search 取消传播到兜底通道（rc=-4 → cancelled） |
| `tests/test_mcp.sh` / `test_mcp_server.sh` | **行为保持回归**：`mf_ai_rule_validate_url("http://2130706433/")`、`.../0x7f000001/` 本已返回 false（`ai/rules.c` 既有 `inet_aton` 路径），helper 化后须维持该行为；**新增能力**：`mf_mcp_rule_validate_url` 对相同输入由（可能误放行）变为明确拒绝，补断言；MCP mutation 权限收紧回归 |
| `tests/test_link.sh` case 34-37 | AI 端点核心回归（含 `/ai/...` 不破坏 148 条契约） |

全量门禁：`cmake --build` + `ctest`（全部套件）+ `test_link.sh` + `test_ai_loop_hardening.sh` + `test_mcp.sh` + `test_mcp_server.sh` + `npm --prefix frontend run build`。

---

## 8. 约束与验收标准

- **零破坏 API/MCP 外部契约**：148 条 HTTP 路由与 JSON 封包结构不变；仅内部判定路径调整。
- **所有仓储写操作绑定 `user_id`**（AGENTS.md 黄金法则）。
- **无裸浮点介入金融计算**（本 spec 不涉及金额计算）。
- **验收**：
  - 非 admin 用户无法改写 AI 配置（已修，回归）；
  - MCP 工具限频可从单一 LRU + 单一 env 读出其有效限额；
  - MCP mutation 工具经 STANDARD 角色放行 WRITE、SENSITIVE 需 ADMIN（权限收紧生效）；
  - 用户取消后 web_search 兜底通道立即返回 cancelled；
  - `MINEFOLIO_AI_TRACE_RETENTION_DAYS=30` 时启动清理 30 天前 trace，缺省 0 不清理；
  - `http://2130706433/`、`http://0x7f000001/`、`http://[::1]/` 均被 MCP SSRF 校验拒绝。

---

## 9. 实施顺序（供 writing-plans 拆解）

1. 模块 4（SSRF：先抽出 `domain/net/internal_ip.c` 共用 helper（以 `ai/rules.c` 现行较完整实现为基准），再让 `mcp/rules.c`、`ai/rules.c` 改调）——独立、低风险，先行；**须在模块 1 动 `mcp_bridge.c` 之前完成**，避免同一文件两处顺序改写。
2. 模块 2（取消传播：`wikipedia_opensearch` helper 签名加 cancel_token + 兜底通道传参）——单文件，独立。
3. 模块 3（trace 保留清理：`ai_init` 首轮幂等触发 + `mf_ai_trace_cleanup`）——独立。
4. 模块 1（频次 + 权限统一重构）——最大改动面，依赖前三不冲突后实施。
5. 模块 5（测试补齐与全量回归）——贯穿各模块，最后统一跑门禁。

> 注：模块 1 改动 policy/bridge/loop 核心路径，须在全量回归通过后提交；其余模块可独立提交。
