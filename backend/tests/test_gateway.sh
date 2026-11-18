#!/usr/bin/env bash
# test_gateway.sh — IM 网关 (gateway/) 最小闭环验证
# 起后端 (db_helper), 签发 im_bind token, 用 gateway CLI 模拟 IM 用户:
#   /bind → 只读查账 → 写操作被收敛 → 抢占防护
set -o pipefail

TEST_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$TEST_DIR/db_helper.sh"

GW="$TEST_DIR/../../gateway"
[ -f "$GW/index.js" ] || { echo "gateway/index.js 不存在" >&2; exit 1; }

# 后端起后设 BASE, 用于 rsa_encrypt 取公钥
BASE=""
rsa_encrypt() {
  local plain="$1"
  node -e "
const http = require('http');
const crypto = require('crypto');
http.get('$BASE/auth/public-key', (res) => {
  let d = '';
  res.on('data', c => d += c);
  res.on('end', async () => {
    const outer = JSON.parse(d);
    const jwk = typeof outer.data.public_key === 'string'
      ? JSON.parse(outer.data.public_key) : outer.data.public_key;
    const key = await crypto.subtle.importKey('jwk', jwk,
      {name:'RSA-OAEP', hash:'SHA-256'}, false, ['encrypt']);
    const enc = await crypto.subtle.encrypt({name:'RSA-OAEP'}, key, Buffer.from('$plain'));
    process.stdout.write(Buffer.from(enc).toString('base64').replace(/\+/g,'-').replace(/\//g,'_').replace(/=+\$/,''));
  });
});"
}

api() { # api METHOD PATH BODY TOKEN
  local m="$1" p="$2" b="$3" t="${4:-}"
  if [ -n "$t" ]; then
    curl -s -X "$m" -H 'Content-Type: application/json' -H "Authorization: Bearer $t" "http://localhost:$PORT/api$p" -d "$b"
  else
    curl -s -X "$m" -H 'Content-Type: application/json' "http://localhost:$PORT/api$p" -d "$b"
  fi
}
check() { if [ "$2" = "$3" ]; then echo "  PASS: $1"; PASS=$((PASS+1)); else echo "  FAIL: $1 (got: $2, want: $3)"; FAIL=$((FAIL+1)); fi; }

PASS=0; FAIL=0

# --- 起后端 ---
db_init_context 18100 gw_probe
export TEST_JWT_SECRET=$(node -e "console.log(require('crypto').randomBytes(32).toString('hex'))")
db_clean
db_start_server "$PORT" "$TEST_DIR/../build" || { echo "start backend failed" >&2; exit 1; }
# 等后端 ready (首次启动加载迁移需数秒)
for i in $(seq 1 40); do
  if curl -s -o /dev/null "http://localhost:$PORT/api/system/status"; then break; fi
  sleep 1
done
BASE="http://localhost:$PORT/api"
# 网关 CLI 默认连 8080, 这里指向 test server 端口
export MINEFOLIO_API="http://localhost:$PORT"

# --- setup 管理员 + 登录 + 绑定码 ---
PASS_ENC=$(rsa_encrypt "pass1234")
api POST /system/setup "{\"username\":\"gwuser\",\"password_enc\":\"$PASS_ENC\"}" > /dev/null
LOGIN=$(api POST /auth/login '{"username":"gwuser","password":"pass1234"}')
APP_TOKEN=$(echo "$LOGIN" | jq -r '.data.token')
[ -n "$APP_TOKEN" ] || { echo "no APP_TOKEN" >&2; db_stop_server; db_clean; exit 1; }

# 绑定 qq_user_001 到 gwuser
BIND_TOKEN=$(api POST /im/bindings/token "" "$APP_TOKEN" | jq -r '.data.bind_token')
api POST /im/qq/bind '{"im_user_id":"qq_user_001"}' "$BIND_TOKEN" > /dev/null

# --- Case 1: 网关 CLI 未绑定引导 (用别人的 bind token 查未绑定的 im_user_id) ---
REPLY=$(node "$GW/cli.js" qq qq_user_002 "$BIND_TOKEN" "我最近花了多少" 2>&1)
check "gateway CLI: 未绑定 → 引导绑定" "$(echo "$REPLY" | grep -c '尚未绑定' || true)" "1"

# --- Case 2: 网关 CLI 已绑定查账 (绑定码归属 = im_user_id 所属用户) ---
REPLY=$(node "$GW/cli.js" qq qq_user_001 "$BIND_TOKEN" "我最近一个月花了多少" 2>&1)
# 未配 AI 模型时后端回 "AI 查询失败" 或正常结果; 关键: 不报越权/绑定错误, 且是 bound=1 路径
case "$REPLY" in
  *尚未绑定*|*被拒*|*已被其他用户*|*empty*response*) check "gateway CLI: 已绑定查账 (无越权)" "FAIL" "PASS";;
  *) check "gateway CLI: 已绑定查账 (正常路径, 含 AI 未配置的友好降级)" "PASS" "PASS";;
esac

# --- Case 3: 只读收敛 — 写操作被拦截 (LLM 无写工具) ---
# 写类请求不会触发动账; 验证网关正常返回(不崩), 且后端不产生新 transaction
TX_BEFORE=$(db_query "SELECT count(*) FROM transactions;" 2>/dev/null)
node "$GW/cli.js" qq qq_user_001 "$BIND_TOKEN" "帮我记一笔 500 吃饭" > /dev/null 2>&1
TX_AFTER=$(db_query "SELECT count(*) FROM transactions;" 2>/dev/null)
check "gateway CLI: 写操作不产生动账 (只读收敛)" "$TX_AFTER" "$TX_BEFORE"

# --- Case 4: 抢占防护 — 他人绑定码绑同一 im_user_id 被拒 (1004) ---
OTHER_PASS_ENC=$(rsa_encrypt "other1234")
api POST /auth/register "{\"username\":\"gwuser2\",\"password_enc\":\"$OTHER_PASS_ENC\"}" > /dev/null
OTHER_LOGIN=$(api POST /auth/login '{"username":"gwuser2","password":"other1234"}')
OTHER_APP=$(echo "$OTHER_LOGIN" | jq -r '.data.token // empty')
if [ -n "$OTHER_APP" ]; then
  OTHER_BIND=$(api POST /im/bindings/token "" "$OTHER_APP" | jq -r '.data.bind_token // empty')
  STEAL=$(api POST /im/qq/bind '{"im_user_id":"qq_user_001"}' "$OTHER_BIND")
  check "抢占防护: 他人绑定码被拒 (1004)" "$(echo "$STEAL" | grep -c '"code":1004' || true)" "1"
fi

# --- Case 5: /bind 幂等 + 归属保持 ---
OWNER=$(db_query "SELECT count(*) FROM im_bot_bindings WHERE platform='qq' AND im_user_id='qq_user_001';" 2>/dev/null)
check "抢占防护后: 绑定行仍 1 行" "$OWNER" "1"

db_stop_server
db_clean
echo "----------------------"
echo "PASS: $PASS  FAIL: $FAIL"
[ "$FAIL" -eq 0 ]
