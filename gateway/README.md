# Minefolio IM 网关 (gateway/)

桥接 QQ / 微信消息到后端只读查账端点 `/api/im/*`。零外部依赖，仅用 Node ≥ 18 内置 `fetch`。

## 文件

| 文件 | 职责 |
|------|------|
| `index.js` | 网关核心：`handleImMessage(platform, imUserId, text, bindToken)` + `/bind` 处理 + `refreshBindToken` |
| `cli.js` | CLI 模拟 IM 用户，跑通「绑定 → 查账 → 写操作收敛」闭环（不依赖真实 QQ/微信 运行时） |
| `package.json` | 零依赖，`npm start` / `npm run cli` |

## 数据流

```
IM 框架 (NapCat/OneBot | wechaty | cli.js)
   │  收消息 handleImMessage(platform, imUserId, text, bindToken)
   ▼
gateway/index.js
   │  /bind <code>  → POST /api/im/{platform}/bind   (code 作 Bearer)
   │  其他文本      → POST /api/im/{platform}/chat    (bindToken 作 Bearer)
   ▼
backend /api/im/*  (P0: 只读工具白名单 + 抢占防护 + bound==user 一致性校验)
```

## 最小闭环（CLI，无真实 IM 运行时）

```bash
# 1. 起后端 (配好 AI 模型, 让只读查账 LLM 可用)
./backend/build/minefolio            # 默认 :8080

# 2. App 侧拿绑定码 (此处用 curl 模拟; 真实场景是 App「IM 机器人」卡片)
APP_TOKEN=$(curl -s -X POST -H 'Content-Type: application/json' \
  http://localhost:8080/api/auth/login -d '{"username":"u","password":"p"}' | jq -r .data.token)
BIND=$(curl -s -X POST -H "Authorization: Bearer $APP_TOKEN" \
  http://localhost:8080/api/im/bindings/token | jq -r .data.bind_token)

# 3. CLI 模拟 QQ 用户
node gateway/cli.js qq 12345 "$BIND" "/bind $BIND"        # 绑定
node gateway/cli.js qq 12345 "$BIND" "我最近一个月花了多少"  # 只读查账
node gateway/cli.js qq 12345 "$BIND" "帮我记一笔 500 吃饭"   # 写操作 → 只读收敛, 不产生动账
```

## QQ / 微信 适配（P1+，本仓库未实现）

适配器把平台消息喂给 `handleImMessage`，并把返回的 reply 回平台：

- **QQ**：NapCat / LLObot（OneBot v11）。订阅 `message` 事件，`imUserId = sender.id`，`platform='qq'`；回消息用 OneBot `send-private-msg`。
- **微信个人号**：wechaty + puppet。`bot.on('message', m => handleImMessage('wechat', m.from(), m.text(), bindToken))`；`bot.say(reply)`。

绑定码管理（生产）：
- `refreshBindToken(appToken)` 可周期刷新某用户的 300s 绑定码；
- 更稳的是网关持服务账号 + 后端网关鉴权（设计文档 §5.1），避免 300s 码频繁过期。

## 安全不变量（后端 P0 已保证，网关无需重复）

- 绑定码 `type=im_bind`，仅放行 `/api/im/*`（路径白名单）
- `bound == ctx_user_id` 一致性校验：绑定码不可被未绑定他人借用
- 抢占防护：`im_user_id` 归属不可被他人绑定码改写（1004）
- 只读工具白名单：LLM 物理上无写工具，写操作回"请回 App 完成"

## 测试

```bash
./backend/tests/test_gateway.sh     # 起后端 + CLI 模拟: 未绑定引导/查账/写收敛/抢占防护 (4 断言)
```
