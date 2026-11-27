#!/usr/bin/env bash
# IM 机器人 (QQ / 微信个人号) 只读查账接入端点集成测试。
# 覆盖: V027 迁移建表、绑定码签发 (type=im_bind)、/bind upsert 幂等、
#       bindings 列表/解绑、未绑定 chat 引导文案、im_bind token 路径白名单。
# 用法: MINEFOLIO_BUILD_DIR=<build> ./test_im_bot.sh   (默认 backend/build)
set -uo pipefail

TEST_PORT="${TEST_PORT:-18080}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/db_helper.sh"

db_init_context "$TEST_PORT" "im_bot_test"
BASE="http://localhost:${PORT}/api"
BUILD_DIR="${MINEFOLIO_BUILD_DIR:-$(cd "$SCRIPT_DIR/../build" && pwd)}"
PASS=0
FAIL=0

cleanup() {
  db_stop_server
  db_clean
}
trap cleanup EXIT INT TERM

# 随机 JWT secret，避免与宿主机其它实例/库交叉
export TEST_JWT_SECRET=$(node -e "console.log(require('crypto').randomBytes(32).toString('hex'))")

db_clean
db_start_server "$PORT" "$BUILD_DIR"

req() { # req METHOD PATH [JSON] [TOKEN]
  local method="$1" path="$2" data="${3:-}" tok="${4:-}"
  local auth=""; [ -n "$tok" ] && auth="Authorization: Bearer $tok"
  if [ -n "$data" ]; then
    curl -s -X "$method" -H 'Content-Type: application/json' -H "$auth" "$BASE$path" -d "$data"
  else
    curl -s -X "$method" -H "$auth" "$BASE$path"
  fi
}
check() { # check DESC ACTUAL EXPECTED
  if [ "$2" = "$3" ]; then
    echo "  PASS: $1"; PASS=$((PASS+1))
  else
    echo "  FAIL: $1 (期望 '$3' 实际 '$2')"; FAIL=$((FAIL+1))
  fi
}
grep_check() { # grep_check DESC PATTERN INPUT
  if echo "$3" | grep -qE "$2"; then
    echo "  PASS: $1"; PASS=$((PASS+1))
  else
    echo "  FAIL: $1 (无匹配 /$2/): $3"; FAIL=$((FAIL+1))
  fi
}

rsa_encrypt() {
  local plain="$1"
  node -e "
const http = require('http');
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
    process.stdout.write(Buffer.from(enc).toString('base64')
      .replace(/\+/g,'-').replace(/\//g,'_').replace(/=+\$/,''));
  });
});"
}

# --- 系统初始化 + 登录 (App 侧鉴权用 7 天正常 token) ---
SETUP_PASS=$(rsa_encrypt "pass1234")
SETUP=$(req POST /system/setup "{\"username\":\"imbot\",\"password_enc\":\"$SETUP_PASS\"}")
TOKEN=$(echo "$SETUP" | jq -r '.data.token')
if [ -z "$TOKEN" ] || [ "$TOKEN" = "null" ]; then
  LOGIN=$(req POST /auth/login '{"username":"imbot","password":"pass1234"}')
  TOKEN=$(echo "$LOGIN" | jq -r '.data.token')
fi
if [ -z "$TOKEN" ] || [ "$TOKEN" = "null" ]; then
  echo "ERROR: 无法获取 token (setup=$SETUP login=$(req POST /auth/login '{"username":"imbot","password":"pass1234"}'))"
  exit 1
fi

# --- Case 1: V027 迁移建表 (跨引擎: 表不存在时 SELECT 报错 → 空/0) ---
HAS_TBL=$(db_query "SELECT count(*) FROM im_bot_bindings;" 2>/dev/null | tail -n 1)
check "V027: im_bot_bindings table exists" "$HAS_TBL" "0"

# --- Case 2: 绑定码签发 (App 侧正常 token) ---
BND=$(req POST /im/bindings/token "" "$TOKEN")
BIND_TOKEN=$(echo "$BND" | jq -r '.data.bind_token')
grep_check "bindings/token returns bind_token" '"bind_token":"' "$BND"
grep_check "bindings/token returns ttl_sec=300" '"ttl_sec":300' "$BND"

# --- Case 3: 未绑定 chat → 引导文案 (不依赖 AI 配置, 提前返回) ---
CHAT_NA=$(req POST /im/qq/chat '{"im_user_id":"qq_user_001","message":"查持仓"}' "$BIND_TOKEN")
grep_check "chat: unbound → 引导绑定" "尚未绑定" "$CHAT_NA"
grep_check "chat: unbound flag bound=0" '"bound":0' "$CHAT_NA"

# --- Case 4: /bind upsert (im_bind token) ---
BIND_RES=$(req POST /im/qq/bind '{"im_user_id":"qq_user_001"}' "$BIND_TOKEN")
grep_check "bind: 绑定成功文案" "绑定成功" "$BIND_RES"
DB_ROW=$(db_query "SELECT scope FROM im_bot_bindings WHERE platform='qq' AND im_user_id='qq_user_001';" 2>/dev/null)
check "bind: 行落库 scope=readonly" "$DB_ROW" "readonly"

# --- Case 5: 幂等 upsert (重复 bind 不增行) ---
req POST /im/qq/bind '{"im_user_id":"qq_user_001"}' "$BIND_TOKEN" > /dev/null
DUP=$(db_query "SELECT count(*) FROM im_bot_bindings WHERE platform='qq' AND im_user_id='qq_user_001';" 2>/dev/null)
check "bind: 幂等单行" "$DUP" "1"

# --- Case 6: bindings 列表 (App 侧正常 token) ---
LIST=$(req GET /im/bindings "" "$TOKEN")
grep_check "bindings list: 含 im_user_id" "qq_user_001" "$LIST"
check "bindings list: list 长度 1" "$(echo "$LIST" | jq -r '.data.list | length')" "1"
check "bindings list: total=1" "$(echo "$LIST" | jq -r '.data.total | floor')" "1"

# --- Case 7: im_bind token 不得访问非 /im 路径 (路径白名单 → 裸 401 {error}) ---
LEAK=$(req GET /assets "" "$BIND_TOKEN")
grep_check "im_bind token: /assets 被拒 (IM bind token 白名单)" 'IM bind token is only valid' "$LEAK"

# --- Case 8: 解绑 (App 侧正常 token) ---
req DELETE /im/bindings '{"platform":"qq","im_user_id":"qq_user_001"}' "$TOKEN" > /dev/null
DB_ROW2=$(db_query "SELECT count(*) FROM im_bot_bindings WHERE platform='qq' AND im_user_id='qq_user_001';" 2>/dev/null)
check "unbind: 行已删除" "$DB_ROW2" "0"

# --- Case 9: 抢占防护 (防他人绑定码改写本 im_user_id 的归属) ---
# 第一用户重新绑定 qq_user_001
req POST /im/qq/bind '{"im_user_id":"qq_user_001"}' "$BIND_TOKEN" > /dev/null
# 第二用户: register (RSA 加密密码) + login + 绑定码
OTHER_PUB=$(rsa_encrypt "other1234")
REG=$(req POST /auth/register "{\"username\":\"imbot2\",\"password_enc\":\"$OTHER_PUB\"}")
OTHER_TOKEN=$(echo "$REG" | jq -r '.data.token // empty')
if [ -z "$OTHER_TOKEN" ]; then
  LOGIN2=$(req POST /auth/login '{"username":"imbot2","password":"other1234"}')
  OTHER_TOKEN=$(echo "$LOGIN2" | jq -r '.data.token // empty')
fi
OTHER_BIND=$(req POST /im/bindings/token "" "$OTHER_TOKEN")
OTHER_BIND_TOKEN=$(echo "$OTHER_BIND" | jq -r '.data.bind_token // empty')
# 第二用户尝试绑同一 im_user_id → 已被第一用户绑定 → 1004
STEAL=$(req POST /im/qq/bind '{"im_user_id":"qq_user_001"}' "$OTHER_BIND_TOKEN")
grep_check "抢占防护: 他人绑定码被拒 (1004)" '"code":1004' "$STEAL"
# 原绑定未被改写 (仍 1 行)
check "抢占防护: 归属未改写 (仍 1 行)" "$(db_query "SELECT count(*) FROM im_bot_bindings WHERE platform='qq' AND im_user_id='qq_user_001';" 2>/dev/null)" "1"

# --- Case 10: 已绑定 chat 返回异步 task_id 并可通过 /api/im/tasks/:id 轮询 ---
CHAT_BOUND_SUB=$(req POST /im/qq/chat '{"im_user_id":"qq_user_001","message":"查持仓"}' "$BIND_TOKEN")
grep_check "chat: bound → 返回异步 task_id" '"task_id":' "$CHAT_BOUND_SUB"
grep_check "chat: bound → task_type=im_chat" '"task_type":"im_chat"' "$CHAT_BOUND_SUB"
CHAT_BOUND_RES=$(db_poll_task "http://127.0.0.1:${PORT}" "Authorization: Bearer $BIND_TOKEN" "$CHAT_BOUND_SUB" 120 "/api/im/tasks")
grep_check "chat: bound 轮询完成包含 reply" '"reply":' "$CHAT_BOUND_RES"
grep_check "chat: bound 轮询完成 bound=1" '"bound":1' "$CHAT_BOUND_RES"

echo "----------------------"
echo "PASS: $PASS  FAIL: $FAIL"
[ "$FAIL" -eq 0 ]
