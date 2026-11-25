#!/usr/bin/env bash
# iLink 微信机器人用户级独立实例集成测试。
# 覆盖: V035 迁移、实例 CRUD、owner 隔离 (1003)、网关鉴权 (1001)、
#       二维码与验证码一次性消费、active 绑定事务与冲突 (1004)、
#       relogin 清理旧绑定、disable/enable 与 heartbeat、DELETE 级联清理、
#       >50 会话后按标题精确复用 IM 会话。
set -uo pipefail

TEST_PORT="${TEST_PORT:-18082}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/db_helper.sh"

db_init_context "$TEST_PORT" "im_bot_instance_test"
BASE="http://localhost:${PORT}/api"
BUILD_DIR="${MINEFOLIO_BUILD_DIR:-$(cd "$SCRIPT_DIR/../build" && pwd)}"
GW_TOK="${MINEFOLIO_GATEWAY_TOKEN:-test-gateway-token}"
export MINEFOLIO_GATEWAY_TOKEN="$GW_TOK"
PASS=0
FAIL=0

cleanup() {
  db_stop_server
  db_clean
}
trap cleanup EXIT INT TERM

export TEST_JWT_SECRET=$(node -e "console.log(require('crypto').randomBytes(32).toString('hex'))")

db_clean
db_start_server "$PORT" "$BUILD_DIR"

req() { # req METHOD PATH [JSON] [BEARER_TOKEN]
  local method="$1" path="$2" data="${3:-}" tok="${4:-}"
  local auth=""; [ -n "$tok" ] && auth="Authorization: Bearer $tok"
  if [ -n "$data" ]; then
    curl -s -X "$method" -H 'Content-Type: application/json' -H "$auth" "$BASE$path" -d "$data"
  else
    curl -s -X "$method" -H "$auth" "$BASE$path"
  fi
}

gw_req() { # gw_req METHOD PATH [JSON] [GW_TOKEN]
  local method="$1" path="$2" data="${3:-}" tok="${4:-}"
  if [ -n "$data" ]; then
    if [ -n "$tok" ]; then
      curl -s -X "$method" -H 'Content-Type: application/json' -H "X-Gateway-Token: $tok" "$BASE$path" -d "$data"
    else
      curl -s -X "$method" -H 'Content-Type: application/json' "$BASE$path" -d "$data"
    fi
  else
    if [ -n "$tok" ]; then
      curl -s -X "$method" -H "X-Gateway-Token: $tok" "$BASE$path"
    else
      curl -s -X "$method" "$BASE$path"
    fi
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

# --- 初始化用户 A 和注册用户 B ---
PASS_A=$(rsa_encrypt "passA1234")
SETUP=$(req POST /system/setup "{\"username\":\"instuserA\",\"password_enc\":\"$PASS_A\"}")
TOKEN_A=$(echo "$SETUP" | jq -r '.data.token // empty')
if [ -z "$TOKEN_A" ]; then
  TOKEN_A=$(req POST /auth/login '{"username":"instuserA","password":"passA1234"}' | jq -r '.data.token')
fi
PASS_B=$(rsa_encrypt "passB1234")
REG_B=$(req POST /auth/register "{\"username\":\"instuserB\",\"password_enc\":\"$PASS_B\"}")
TOKEN_B=$(echo "$REG_B" | jq -r '.data.token // empty')
if [ -z "$TOKEN_B" ]; then
  TOKEN_B=$(req POST /auth/login '{"username":"instuserB","password":"passB1234"}' | jq -r '.data.token')
fi
if [ -z "$TOKEN_A" ] || [ -z "$TOKEN_B" ]; then
  echo "ERROR: 无法获取用户 Token"
  exit 1
fi

# --- 1. 用户 A 创建实例；重复创建返回 1004；B 列表为空，GET/DELETE A 返回 1003 ---
CREATE_A=$(req POST /im/instances '{"platform":"wechat","adapter":"ilink"}' "$TOKEN_A")
INST_A=$(echo "$CREATE_A" | jq -r '.data.id // empty')
grep_check "A 创建实例成功" '"code":0' "$CREATE_A"
check "A 实例初始状态 pending_login" "$(echo "$CREATE_A" | jq -r '.data.status')" "pending_login"

DUP_A=$(req POST /im/instances '{}' "$TOKEN_A")
grep_check "A 重复创建返回 1004" '"code":1004' "$DUP_A"

LIST_B=$(req GET /im/instances "" "$TOKEN_B")
check "B 列表为空" "$(echo "$LIST_B" | jq -r '.data | length')" "0"
GET_B_ON_A=$(req GET "/im/instances/$INST_A" "" "$TOKEN_B")
grep_check "B GET A 实例返回 1003" '"code":1003' "$GET_B_ON_A"
DEL_B_ON_A=$(req DELETE "/im/instances/$INST_A" "" "$TOKEN_B")
grep_check "B DELETE A 实例返回 1003" '"code":1003' "$DEL_B_ON_A"

# --- 2. 网关无 token / 错 token → 1001 ---
GW_NO_TOK=$(gw_req GET /gateway/im/instances "" "")
grep_check "网关无 token → 1001" '"code":1001' "$GW_NO_TOK"
GW_BAD_TOK=$(gw_req GET /gateway/im/instances "" "wrong-token")
grep_check "网关错 token → 1001" '"code":1001' "$GW_BAD_TOK"
GW_OK_LIST=$(gw_req GET /gateway/im/instances "" "$GW_TOK")
check "网关列表含实例 A" "$(echo "$GW_OK_LIST" | jq -r '.data.instances | length')" "1"

# --- 3. 网关上报 qr_ready → A 可见，B 不可见 ---
QR_UP=$(gw_req PATCH "/gateway/im/instances/$INST_A/status" '{"status":"qr_ready","qr_content":"https://ilink.test/qr/1"}' "$GW_TOK")
grep_check "网关上报 qr_ready 成功" '"code":0' "$QR_UP"
GET_A_QR=$(req GET "/im/instances/$INST_A" "" "$TOKEN_A")
check "A 能看到 qr_content" "$(echo "$GET_A_QR" | jq -r '.data.qr_content')" "https://ilink.test/qr/1"
LIST_B2=$(req GET /im/instances "" "$TOKEN_B")
check "B 仍看不到任何实例" "$(echo "$LIST_B2" | jq -r '.data | length')" "0"

# --- 4. 验证码提交与一次性消费 ---
BAD_PIN=$(req POST "/im/instances/$INST_A/verify-code" '{"code":"ab12"}' "$TOKEN_A")
grep_check "非数字验证码被拒 (1002)" '"code":1002' "$BAD_PIN"
OK_PIN=$(req POST "/im/instances/$INST_A/verify-code" '{"code":"123456"}' "$TOKEN_A")
grep_check "提交 6 位数字验证码成功" '"code":0' "$OK_PIN"
# owner GET 不返回 pending_verify_code
GET_A_NO_PIN=$(req GET "/im/instances/$INST_A" "" "$TOKEN_A")
check "owner GET 不泄露 pending_verify_code" "$(echo "$GET_A_NO_PIN" | jq -r '.data.pending_verify_code // "NONE"')" "NONE"
# 网关 GET 一次性消费
GW_GET1=$(gw_req GET "/gateway/im/instances/$INST_A" "" "$GW_TOK")
check "网关首次 GET 消费验证码 123456" "$(echo "$GW_GET1" | jq -r '.data.verify_code // "NONE"')" "123456"
GW_GET2=$(gw_req GET "/gateway/im/instances/$INST_A" "" "$GW_TOK")
check "网关第二次 GET 验证码为空" "$(echo "$GW_GET2" | jq -r '.data.verify_code // "NONE"')" "NONE"

# --- 5. 网关上报 active → 绑定创建，二维码清空 ---
ACT_A=$(gw_req PATCH "/gateway/im/instances/$INST_A/status" '{"status":"active","ilink_bot_id":"bot_A@im.bot","ilink_user_id":"wx_user_A"}' "$GW_TOK")
grep_check "网关上报 active 成功" '"code":0' "$ACT_A"
GET_A_ACT=$(req GET "/im/instances/$INST_A" "" "$TOKEN_A")
check "active 后 status=active" "$(echo "$GET_A_ACT" | jq -r '.data.status')" "active"
check "active 后 qr_content 已清空" "$(echo "$GET_A_ACT" | jq -r '.data.qr_content // "NULL"')" "NULL"
check "active 后 im_user_id=ilink:wx_user_A" "$(echo "$GET_A_ACT" | jq -r '.data.im_user_id')" "ilink:wx_user_A"
BND_CNT=$(db_query "SELECT count(*) FROM im_bot_bindings WHERE platform='wechat' AND im_user_id='ilink:wx_user_A';" 2>/dev/null)
check "im_bot_bindings 已写入 wechat/ilink:wx_user_A" "$BND_CNT" "1"

# --- 6. 同一 ilink_user_id 被用户 B 的实例确认 → 1004 ---
CREATE_B=$(req POST /im/instances '{}' "$TOKEN_B")
INST_B=$(echo "$CREATE_B" | jq -r '.data.id // empty')
CONFLICT_B=$(gw_req PATCH "/gateway/im/instances/$INST_B/status" '{"status":"active","ilink_bot_id":"bot_B@im.bot","ilink_user_id":"wx_user_A"}' "$GW_TOK")
grep_check "同一 ilink_user_id 跨实例确认返回 1004" '"code":1004' "$CONFLICT_B"

# --- 7. 网关 chat + 任务轮询 + 错误 im_user_id 返回 1004 ---
CHAT_BAD=$(gw_req POST "/gateway/im/instances/$INST_A/chat" '{"im_user_id":"ilink:stranger","message":"查持仓"}' "$GW_TOK")
grep_check "非 owner im_user_id chat 返回 1004" '"code":1004' "$CHAT_BAD"

CHAT_OK=$(gw_req POST "/gateway/im/instances/$INST_A/chat" '{"im_user_id":"ilink:wx_user_A","message":"查持仓"}' "$GW_TOK")
grep_check "owner im_user_id chat 返回 task_id" '"task_id":' "$CHAT_OK"
CHAT_RES=$(db_poll_task "http://127.0.0.1:${PORT}" "X-Gateway-Token: $GW_TOK" "$CHAT_OK" 120 "/api/gateway/im/instances/$INST_A/tasks")
grep_check "网关 task 轮询完成含 reply" '"reply":' "$CHAT_RES"

# --- 8. >50 会话后仍复用同一 IM 会话 ---
USER_A_ID=$(db_query "SELECT mf_user_id FROM im_bot_instances WHERE id=$INST_A;" 2>/dev/null)
for i in $(seq 1 55); do
  db_exec "INSERT INTO ai_sessions (user_id, title, model, provider) VALUES ($USER_A_ID, 'Bulk Session $i', 'm', 'p');"
done
CHAT_OK2=$(gw_req POST "/gateway/im/instances/$INST_A/chat" '{"im_user_id":"ilink:wx_user_A","message":"再查一次"}' "$GW_TOK")
SESS1=$(echo "$CHAT_OK" | jq -r '.data.session_id')
SESS2=$(echo "$CHAT_OK2" | jq -r '.data.session_id')
check ">50 会话后仍复用同一 IM 会话" "$SESS2" "$SESS1"
db_poll_task "http://127.0.0.1:${PORT}" "X-Gateway-Token: $GW_TOK" "$CHAT_OK2" 120 "/api/gateway/im/instances/$INST_A/tasks" > /dev/null

# --- 9. relogin 与新微信号确认清理旧绑定 ---
EPOCH_BEFORE=$(req GET "/im/instances/$INST_A" "" "$TOKEN_A" | jq -r '.data.login_epoch')
RELOGIN=$(req POST "/im/instances/$INST_A/relogin" "" "$TOKEN_A")
grep_check "relogin 成功" '"code":0' "$RELOGIN"
GET_REL=$(req GET "/im/instances/$INST_A" "" "$TOKEN_A")
check "relogin epoch+1" "$(echo "$GET_REL" | jq -r '.data.login_epoch')" "$((EPOCH_BEFORE + 1))"
check "relogin 状态 pending_login" "$(echo "$GET_REL" | jq -r '.data.status')" "pending_login"
OLD_BND_STILL=$(db_query "SELECT count(*) FROM im_bot_bindings WHERE platform='wechat' AND im_user_id='ilink:wx_user_A';" 2>/dev/null)
check "relogin 前旧绑定暂保留" "$OLD_BND_STILL" "1"

ACT_NEW=$(gw_req PATCH "/gateway/im/instances/$INST_A/status" '{"status":"active","ilink_bot_id":"bot_A2@im.bot","ilink_user_id":"wx_user_A_new"}' "$GW_TOK")
grep_check "新确认成功" '"code":0' "$ACT_NEW"
OLD_BND_DEL=$(db_query "SELECT count(*) FROM im_bot_bindings WHERE platform='wechat' AND im_user_id='ilink:wx_user_A';" 2>/dev/null)
NEW_BND_ADD=$(db_query "SELECT count(*) FROM im_bot_bindings WHERE platform='wechat' AND im_user_id='ilink:wx_user_A_new';" 2>/dev/null)
check "新确认后旧绑定已删除" "$OLD_BND_DEL" "0"
check "新确认后新绑定已创建" "$NEW_BND_ADD" "1"

# --- 10. disable / heartbeat 1004 / enable epoch+1 ---
DIS=$(req POST "/im/instances/$INST_A/disable" "" "$TOKEN_A")
grep_check "disable 成功" '"code":0' "$DIS"
HB_DIS=$(gw_req POST "/gateway/im/instances/$INST_A/heartbeat" '{}' "$GW_TOK")
grep_check "disabled 后 heartbeat 返回 1004" '"code":1004' "$HB_DIS"

ENA=$(req POST "/im/instances/$INST_A/enable" "" "$TOKEN_A")
grep_check "enable 成功" '"code":0' "$ENA"
HB_ENA=$(gw_req POST "/gateway/im/instances/$INST_A/heartbeat" '{}' "$GW_TOK")
grep_check "enable 后 heartbeat 成功" '"code":0' "$HB_ENA"
check "enable 后 epoch+1" "$(echo "$HB_ENA" | jq -r '.data.login_epoch')" "$((EPOCH_BEFORE + 2))"

# --- 11. DELETE 删除实例与绑定，网关列表不再包含 ---
DEL_A=$(req DELETE "/im/instances/$INST_A" "" "$TOKEN_A")
grep_check "A DELETE 实例成功" '"code":0' "$DEL_A"
BND_AFTER_DEL=$(db_query "SELECT count(*) FROM im_bot_bindings WHERE platform='wechat' AND im_user_id='ilink:wx_user_A_new';" 2>/dev/null)
check "DELETE 后绑定已清理" "$BND_AFTER_DEL" "0"
GW_AFTER_DEL=$(gw_req GET /gateway/im/instances "" "$GW_TOK")
check "DELETE 后网关列表仅剩 B (1 条)" "$(echo "$GW_AFTER_DEL" | jq -r '.data.instances | length')" "1"

echo "----------------------"
echo "PASS: $PASS  FAIL: $FAIL"
[ "$FAIL" -eq 0 ]
