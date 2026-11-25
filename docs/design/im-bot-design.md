# IM 机器人接入设计（QQ / 微信个人号）

> 目标：让 Minefolio 的 AI 记账助手能在 QQ 与微信个人号里直接对话，**仅开放查账/查询能力**（最安全档）。
> 状态：设计文档，已实施基础通道；**微信 iLink 通道主路径已升级为用户级独立实例方案**，详见 [im-bot-ilink-instances.md](./im-bot-ilink-instances.md)。

---

## 0. 设计原则（一句话）

**IM 只是又一个"客户端"**。把 AI 对话、会话历史、工具调度、防注入、确认链全部复用现有
`services/ai/` Runtime；IM 适配层只做三件事——**协议翻译 + 身份映射 + 消息推送**，
绝不触碰 AI Runtime 内部。能力收敛为「仅查账」，写操作引导回 App。

```
 QQ (OneBot 网关)         微信个人号 (wechaty)
 go-cqhttp / NapCat       web 协议 puppet
        │  消息/回执        │  消息/回执
        ▼                  ▼
 ┌──────────────────────────────────────────────┐
 │  IM 适配网关 (独立 Node 服务, 无业务逻辑)        │
 │  1. 归一化 → (platform, im_uid, text, media)  │
 │  2. 查 im_bot_bindings → mf_user_id            │
 │  3. 会话映射 im_uid → ai_sessions              │
 │  4. POST /api/im/:platform/chat (user 级 JWT)  │
 │  5. 把 AI 回复/查询结果推回 IM 消息              │
 └──────────────────────────────────────────────┘
                        │ HTTP (JWT: 绑定码签发的 user 级 token)
                        ▼
 ┌──────────────────────────────────────────────┐
 │  Minefolio 后端 (新增薄端点, 不碰 Runtime)       │
 │  POST /api/im/:platform/chat                  │
 │   → 非流式 ai_runtime_execute (复用会话/工具)    │
 │   能力 = 只读工具集 (持仓/汇率/报告/交易历史)      │
 │   写工具默认禁用 (IM 入口 scope=readonly)         │
 └──────────────────────────────────────────────┘
```

---

## 1. 平台通道选型（现实约束）

| 平台 | 官方通道 | 第三方通道 | 本方案选用 | 风险 |
|------|---------|-----------|-----------|------|
| **QQ** | 官方机器人需开放平台审核（群机器人/应用） | OneBot v11：`go-cqhttp` / `NapCat`（社区事实标准，免官方审核） | **OneBot 网关（NapCat）** | 低；社区协议稳定 |
| **微信个人号** | 个人微信无开放 API | `wechaty`（web 协议 puppet） | **wechaty** | 中——web 协议有封号风险，仅自用 |

要点：
- **QQ 走 OneBot 最成熟**：网关起 NapCat/go-cqhttp，订阅 `message` 事件，把 `text`+`sender.user_id` 转发后端，响应调 `send-message` 回执。
- **微信个人号走 wechaty**：同一网关抽象下 `platform=wechat`。**必须**在网关启动日志与 README 里显式标注「个人微信 web 协议有封号风险，仅自用，建议生产走企业微信机器人」。
- 两平台**共用同一个后端薄端点**，仅 `platform` 枚举不同——后端不感知 IM 协议差异。

---

## 2. 身份映射与绑定（最安全敏感点）

IM 用户 ≠ Minefolio 用户。机器人代用户查账必须锁定到具体 `mf_user_id`，
否则 AI 会操作「机器人自己」的数据。

### 2.1 绑定表（迁移 `V027__im_bot_bindings.sql`）

```sql
CREATE TABLE IF NOT EXISTS im_bot_bindings (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    platform     TEXT NOT NULL,            -- 'qq' | 'wechat'
    im_user_id   TEXT NOT NULL,           -- QQ: 群/个人 uid; wechat: wechaty selfId/peer
    mf_user_id   INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    scope        TEXT NOT NULL DEFAULT 'readonly',  -- 'readonly' | 'query+confirm'
    enabled      INTEGER NOT NULL DEFAULT 1,
    created_at   TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE(platform, im_user_id)
);
CREATE INDEX idx_im_bindings_mf ON im_bot_bindings(mf_user_id);
```

> 本方案能力档 = 仅查账 → `scope` 固定 `readonly`。保留 `query+confirm` 枚举位是为未来扩展，**本期不启用写操作**。

### 2.2 绑定流程（App 生成一次性绑定码）

1. **App 侧**（`Settings` 或新 `IM 机器人` 卡片）：调 `POST /api/im/bindings/token`，
   后端签发 **`type=im_bind` 短 TTL（300s）受限 JWT**（`sub=mf_user_id`、`v=token_version`）。
   该令牌被 `jwt_middleware` 限制只能访问 `/api/im/*`，不能碰其他业务端点。
   前端展示二维码 / 文本码，用户复制到 IM 发给机器人。
2. **首次对话**：用户在 IM 里发 `/bind`（机器人主动提示，或在 App 侧生成后发送）。
3. **网关**收到 `/bind` → 调 `POST /api/im/:platform/bind { im_user_id }`（携带 `im_bind` 绑定码 Bearer）。
4. **后端**验 `im_bind` 令牌（签名 + 未过期 + 路径白名单）→ **幂等 upsert** `im_bot_bindings`（`ON CONFLICT DO UPDATE`，同码重复 bind 结果一致）→ 回执「绑定成功，现在可以问我持仓/汇率了」。
5. **后续查账**：网关以同一 `im_bind` 令牌调 `POST /api/im/:platform/chat { im_user_id, message }`，后端 lookup 绑定→查账→回 `reply`。

### 2.3 安全收敛

- **无绑定 → 只回帮助/绑定提示**，绝不执行任何查询。
- 绑定码：`type=im_bind` JWT 短 TTL(300s) + `jwt_middleware` 路径白名单（仅 `/api/im/*`）+ 显式 `scope`；`/bind` 幂等 upsert 无需 `jti` 单次消费。
- 一个 `im_user_id` 只能绑一个 `mf_user_id`（UNIQUE 约束）。
- 可随时在 App 里「解绑」→ `DELETE /api/im/bindings`（网关下次查不到即降级为未绑定）。

---

## 3. 后端新增（最小改动，不碰 Runtime）

> 全部放在现有 DDD 分层里：controller → usecase → (复用 ai_runtime)。SQL 进 `infrastructure/repositories/`。

### 3.1 路由（`im_bot_controller.c`，挂 JWT + rate-limit 中间件）

```
POST /api/im/bindings/token      签发 im_bind 绑定码 (App 侧, 需登录, 返回 300s 受限 token)
GET  /api/im/bindings            列我的绑定 (App 侧)
DELETE /api/im/bindings          解绑 (App 侧)
POST /api/im/:platform/bind      网关回调: 幂等落库绑定 (im_bind 绑定码 JWT)
POST /api/im/:platform/chat      网关回调: 只读查账 (im_bind 绑定码 JWT, 非流式)
```

- `:platform` 校验 ∈ {qq, wechat}，否则 1002。
- 所有 `/api/im/*` 挂 JWT 中间件（`jwt_middleware.c`）+ 限流（`rate_limit.c`）。
- `platform` 非法、无绑定 → `respond_bad_request` / `respond_error(1004)`。

### 3.2 `POST /api/im/:platform/chat`（核心薄端点）

入参：`{ im_user_id, message, session_key?, media? }`。

```c
// 伪代码 (im_bot_controller.c)
int64_t mf_uid = ctx_user_id(c);                 // 网关已注入 user 级 JWT
// 1. 校验 binding 存在且 enabled
// 2. 能力收敛: 只读工具集 (见 §3.3)
// 3. 会话映射: (platform, im_user_id) → ai_sessions (一个 IM 用户一个 Minefolio 会话)
//    无会话则 ai_usecase_sessions_create (title="IM: qq@12345")
// 4. 非流式执行: ai_runtime_execute (对齐 ai_news_service 的用法, 拿最终文本)
//    入参 content = message; tool 白名单 = 只读集; policy scope = readonly
// 5. 回 { reply: text, pending_actions: [] }  // 仅查账, pending_actions 恒空
```

**为什么非流式**：IM 消息是一次性文本回执，不适合 SSE。`ai_runtime_execute`
（非流式，`ai_news_service.c` 已验证可用）拿 `res.final_content` 直接回 IM。

### 3.3 能力收敛：只读工具白名单

IM 入口 `scope=readonly` → 工具调度时只放行**只读工具**，写工具直接禁用：

| 工具 | IM 入口 | App 入口 |
|------|--------|---------|
| 查持仓 / 查汇率 / 查交易历史 / 查 AI 报告 | ✅ 放行 | ✅ |
| `propose_*`（记账/转账拟录入） | ❌ 禁用 | ✅ |
| `confirm_proposed_*`（落账确认） | ❌ 禁用 | ✅ |

实现（P0 已落）：注册表新增 `ai_tool_get_readonly_csilk_definitions()`，按 `is_mutation` 过滤掉动账工具、并排除 `internal_only` 工具；IM 端点构造 `ai_runtime_context_t` 时显式注入该只读子集（`ctx.tools` 非空，runtime 不再自动 `mcp_bridge_merge_tools` 拉全量），写操作物理上无法被 LLM 调用。写意图命中 → 回「请回到 App 完成记账」。

**数据统一**：IM 会话复用 `ai_sessions`，用户在 App 里也能看到 IM 里问过的对话与结果。

---

## 4. IM 适配网关（独立 Node 服务，无业务逻辑）

> 单独容器（或旁路进程），与 Minefolio 同 compose 部署。**不引入 IM 协议进 C 后端**。

### 4.1 目录

```
im-bot-gateway/            # 新子项目 (Node 20, 与前端同 Node 运行时)
├── src/
│   ├── index.ts          # 启动: 起 QQ(OneBot) + 微信(wechaty) 两个 driver
│   ├── drivers/
│   │   ├── onebot.ts     # QQ: 连 NapCat/go-cqhttp, 订阅 message, send-message 回执
│   │   └── wechaty.ts    # 微信: wechaty puppet, 收 onText, sendText 回执
│   ├── bridge.ts         # 协议翻译 + 调后端 /api/im/:platform/{bind,chat}
│   ├── bindings.ts       # im_uid → mf user 级 token (从后端 /bind 拿) 的缓存
│   └── format.ts         # AI 文本 → IM 消息格式 (markdown / 分段 / 超长截断)
├── config.example.json   # 网关地址, 各平台凭证
└── package.json
```

### 4.2 消息流（以 QQ 为例）

```
QQ 用户 → NapCat 收 message 事件 (text, sender.user_id, message_type)
  → bridge: platform=qq, im_uid=sender.user_id
  → 若 text 匹配 /^\/bind (.+)$/ → POST /api/im/qq/bind {im_user_id, code} → 回执
  → 否则查 bindings cache 得 mf_user_id 的 user 级 token (无 → 回绑定提示)
  → POST /api/im/qq/chat {im_user_id, message=text, Bearer: user_token}
  → 取 response.reply → OneBot send-message (同群/同私聊)
```

### 4.3 媒体（可选，二期）

图片/CSV 收据 → 走现有 `receipt` OCR 工具（`services/file_parser` 的 mkstemps 临时解析契约），
网关把 `media.url` 透传给 `/chat`，后端下载解析。本期不做。

---

## 5. 安全清单（必须实现）

1. **能力档 readonly**：IM 入口默认只读 + 查询；写工具白名单禁用，命中写意图回「回 App 完成」。
2. **绑定码**：`type=im_bind` 短 TTL（300s）受限 JWT + 显式 scope。`jwt_middleware` 仅放行 `im_bind` 令牌访问 `/api/im/*` 路径（非 /im 路径一律 401）；`/bind` 走 **幂等 upsert**（同码重复 bind 结果一致，身份由 JWT sub 决定无法伪造，无需 jti 单次消费表）。
3. **网关↔后端**：用 **user 级 token**（非实例 token），`/api/im/*` 挂 JWT + rate-limit。
4. **防注入**：IM 文本进 AI 前走现有 `ai_prompt_injection_defense`（已有单测覆盖，自动生效）。
5. **多实例**：网关无状态，`im_bot_bindings` 落库，天然多实例安全（与汇率单实例假设无关）。
6. **微信风险标注**：网关启动日志 + README 显式标注个人微信 web 协议封号风险。
7. **审计**：IM 查询写 `ai_traces`（复用现有 trace），可追溯哪个 IM 号在何时查了什么。

---

## 6. 端到端时序（查账）

```
QQ用户: "我这周花了多少？"
  │
  ▼ NapCat message 事件
网关 bridge: platform=qq, im_uid=100001
  │ bindings cache → mf_user_id=42 (user 级 token)
  │ POST /api/im/qq/chat {im_user_id:"100001", message:"我这周花了多少？"}
  ▼
后端:
  ctx_user_id → 42
  校验 im_bot_bindings(qq,100001) enabled
  会话映射 → ai_sessions(42, "IM: qq@100001")
  ai_policy_evaluate(scope=readonly) → 只读工具集
  ai_runtime_execute(...) → final_content="本周支出合计 ¥320.50，餐饮占 40%…"
  回 {reply: "..."}
  ▲
网关: OneBot send-message → QQ用户收到 "本周支出合计 ¥320.50…"
```

---

## 7. 数据模型与 API 汇总

### 迁移
- `V027__im_bot_bindings.sql`（sqlite + postgres 双份）：`im_bot_bindings` 表（§2.1）。

### 新端点
| 方法/路径 | 调用方 | 鉴权 | 作用 |
|----------|-------|------|------|
| `POST /api/im/bindings/token` | App | 登录 user | 生成一次性绑定码 |
| `GET  /api/im/bindings` | App | 登录 user | 列我的绑定 |
| `DELETE /api/im/bindings` | App | 登录 user | 解绑 |
| `POST /api/im/:platform/bind` | 网关 | 绑定码 JWT | 落库绑定 |
| `POST /api/im/:platform/chat` | 网关 | user 级 token | 查账（只读） |

### 前端（App 侧，二期可选）
- `Settings` 加「IM 机器人」卡片：展示绑定码、列出已绑平台、解绑。

---

## 8. 部署

- **compose 新增服务**：`im-bot-gateway`（Node 20，无状态，可多副本）。
- 网关 `config` 注入：Minefolio 后端地址、QQ OneBot 网关地址、wechaty puppet token。
- 微信/ QQ 凭据走 env（不落库），与 `MINEFOLIO_JWT_SECRET` 同级管理。
- 网关独立重启不影响后端；后端 `/api/im/*` 端点无状态。

---

## 9. 实施路径（MVP → 完整）

| 阶段 | 内容 | 验收 | 状态 |
|------|------|------|------|
| **P0 后端** | `V027` 迁移 + `im_bot_controller`（绑定码/bind/chat 只读端点）+ 隔离编译测试 | curl 直测 `/api/im/qq/chat` 查账通过；只读工具白名单生效；无绑定 1004 | ✅ 已落地 |
| **P0+ 网关核心** | `gateway/index.js`（`handleImMessage` + `/bind`）+ `cli.js` 闭环 + 后端崩溃/UAF 修复 | `test_gateway.sh` 4/5 断言 + ASan CTest 全绿 | ✅ 已落地 |
| **P1 QQ 网关** | `gateway/adapters/onebot.js`（OneBot v11 桥接，可注入 transport/getBindToken）+ `onebot-server.js`（可运行 NapCat WS/HTTP 服务）+ `mock-events.js` + `test/` | 适配器 mock 单测（桥接/群忽略）；`ONEBOT_WS_URL`+`ONEBOT_BIND_TOKEN` 起服务对接 NapCat | ✅ 核心桥接已落地（真人 NapCat 冒烟待手工） |
| **P1 微信** | `gateway/adapters/wechaty.js`（可选依赖）+ wechaty guard 单测 | wechaty 未装时清晰报错；装 puppet 后微信个人号发查账 → 回执（标注封号风险） | ✅ 桥接已落地（真人 wechaty 冒烟待手工） |
| **P2-A IM 自助绑定** | 后端 `POST /im/{platform}/bind-auth`（校验用户名密码 → 写绑定）替换 P1 静态 `getBindToken` | IM 内 `/bind 用户名密码` 自助绑定，无需 App 生成码 | ❌ 待做 |
| **P3 App 绑定 UI** | Settings「IM 机器人」卡片（生成绑定码/解绑） | App 生成码 → IM `/bind` 完成闭环 | ❌ 待做 |
| **P4 媒体(可选)** | 图片/CSV 收据走 receipt OCR | IM 发图 → OCR 记账建议 | ❌ 待做 |

**先落 P0（后端薄端点）**：可独立 curl 自测，不依赖网关；P1/P2 网关做纯协议翻译。P1 已把
OneBot/wechaty 桥接与可运行 QQ 服务落地，`getBindToken` 当前为静态配置，P2-A 换后端自助绑定。

---

## 10. 与现有架构的边界（不改什么）

- **不动** `services/ai/runtime/*`（Runtime 内核）、工具注册、policy/HMAC 确认链、防注入。
- **复用**：`ai_runtime_execute`（非流式）、`ai_usecase_sessions_*`、`ai_policy_evaluate`、`ai_prompt_injection_defense`、`ai_traces` 审计。
- IM 是**新增 controller + usecase + 迁移 + 独立网关服务**，对既有 Web/移动端零影响。
