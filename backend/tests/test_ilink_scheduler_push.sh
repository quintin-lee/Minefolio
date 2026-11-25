#!/usr/bin/env bash
# Dual-engine integration test for WeChat iLink scheduler push & Outbox delivery.
set -uo pipefail

TEST_PORT="${TEST_PORT:-18083}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/db_helper.sh"

db_init_context "$TEST_PORT" "ilink_sched_push_test"
BASE="http://localhost:${PORT}/api"
BUILD_DIR="${MINEFOLIO_BUILD_DIR:-$(cd "$SCRIPT_DIR/../build" && pwd)}"
GW_TOK="${MINEFOLIO_GATEWAY_TOKEN:-test-gateway-token}"
export MINEFOLIO_GATEWAY_TOKEN="$GW_TOK"
export MINEFOLIO_ILINK_TEST_TIMEOUT_MS="${MINEFOLIO_ILINK_TEST_TIMEOUT_MS:-4000}"
SERVER_LOG="$(mktemp)"
TEST_OUT=""
TEST_PID=""
PASS=0
FAIL=0

cleanup() {
  if [ -n "${TEST_PID:-}" ] && kill -0 "$TEST_PID" 2>/dev/null; then
    kill "$TEST_PID" 2>/dev/null || true
    wait "$TEST_PID" 2>/dev/null || true
  fi
  db_stop_server
  if [ "$FAIL" -gt 0 ] && [ -s "$SERVER_LOG" ]; then
    echo "=== Tail of server log on failure ==="
    tail -n 60 "$SERVER_LOG" || true
  fi
  rm -f "$SERVER_LOG" "${TEST_OUT:-}"
  db_clean
}
trap cleanup EXIT INT TERM

export TEST_JWT_SECRET
TEST_JWT_SECRET=$(node -e "console.log(require('crypto').randomBytes(32).toString('hex'))")

db_clean
db_start_server "$PORT" "$BUILD_DIR" "$SERVER_LOG"

req() { # req METHOD PATH [JSON] [BEARER_TOKEN]
  local method="$1" path="$2" data="${3:-}" tok="${4:-}"
  local -a args=(-s -X "$method")
  [ -n "$tok" ] && args+=(-H "Authorization: Bearer $tok")
  if [ -n "$data" ]; then
    args+=(-H 'Content-Type: application/json' -d "$data")
  fi
  curl "${args[@]}" "$BASE$path"
}

gw_req() { # gw_req METHOD PATH [JSON] [GW_TOKEN]
  local method="$1" path="$2" data="${3:-}" tok="${4:-}"
  local -a args=(-s -X "$method")
  [ -n "$tok" ] && args+=(-H "X-Gateway-Token: $tok")
  if [ -n "$data" ]; then
    args+=(-H 'Content-Type: application/json' -d "$data")
  fi
  curl "${args[@]}" "$BASE$path"
}

check() {
  if [ "$2" = "$3" ]; then
    echo "  PASS: $1"; PASS=$((PASS+1))
  else
    echo "  FAIL: $1 (expected '$3', got '$2')"; FAIL=$((FAIL+1))
  fi
}

grep_check() {
  if echo "$3" | grep -qE "$2"; then
    echo "  PASS: $1"; PASS=$((PASS+1))
  else
    echo "  FAIL: $1 (no match /$2/): $3"; FAIL=$((FAIL+1))
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

# --- Setup Alice (admin) & Bob (user) ---
PASS_A=$(rsa_encrypt "passA1234")
SETUP=$(req POST /system/setup "{\"username\":\"pushalice\",\"password_enc\":\"$PASS_A\"}")
TOKEN_A=$(echo "$SETUP" | jq -r '.data.token // empty')
[ -z "$TOKEN_A" ] && TOKEN_A=$(req POST /auth/login '{"username":"pushalice","password":"passA1234"}' | jq -r '.data.token')

PASS_B=$(rsa_encrypt "passB1234")
REG_B=$(req POST /auth/register "{\"username\":\"pushbob\",\"password_enc\":\"$PASS_B\"}")
TOKEN_B=$(echo "$REG_B" | jq -r '.data.token // empty')
[ -z "$TOKEN_B" ] && TOKEN_B=$(req POST /auth/login '{"username":"pushbob","password":"passB1234"}' | jq -r '.data.token')
if [ -z "$TOKEN_A" ] || [ -z "$TOKEN_B" ]; then
  echo "ERROR: failed to obtain user tokens"
  exit 1
fi

# --- 1. Create instances A & B, save wechat_ilink channel with empty url/secret & verify bad type=1002 ---
INST_A=$(req POST /im/instances '{"platform":"wechat","adapter":"ilink"}' "$TOKEN_A" | jq -r '.data.id')
INST_B=$(req POST /im/instances '{"platform":"wechat","adapter":"ilink"}' "$TOKEN_B" | jq -r '.data.id')

BAD_CH=$(req PUT /scheduler/channel '{"channel_type":"unsupported_foo","channel_name":"bad","webhook_url":""}' "$TOKEN_A")
grep_check "不支持的 channel_type 返回 1002" '"code":1002' "$BAD_CH"

SAVE_A=$(req PUT /scheduler/channel '{"channel_type":"wechat_ilink","channel_name":"微信iLink","webhook_url":""}' "$TOKEN_A")
grep_check "A 保存 wechat_ilink 通道成功" '"code":0' "$SAVE_A"
GET_CH=$(req GET /scheduler/channel "" "$TOKEN_A")
check "GET channel type=wechat_ilink" "$(echo "$GET_CH" | jq -r '.data.channel_type')" "wechat_ilink"
check "GET channel has_instance=true" "$(echo "$GET_CH" | jq -r '.data.ilink_status.has_instance')" "true"

# --- 2. Activate A, trigger custom job -> notification_status == queued ---
ACT_A=$(gw_req PATCH "/gateway/im/instances/$INST_A/status" '{"status":"active","ilink_bot_id":"bot_pa@im.bot","ilink_user_id":"wx_pa"}' "$GW_TOK")
grep_check "实例 A 激活成功" '"code":0' "$ACT_A"

CREATE_JOB=$(req POST /scheduler/jobs '{"name":"iLink Custom Push","job_type":"structured_report","trigger_type":"cron","trigger_config":"{\"cron\":\"0 9 * * *\"}","payload_config":"{}","delivery_channel":"user_default"}' "$TOKEN_A")
JOB_ID=$(echo "$CREATE_JOB" | jq -r '.data.id // empty')
grep_check "创建定时任务成功" '"code":0' "$CREATE_JOB"

RUN_RES=$(req POST "/scheduler/jobs/${JOB_ID}/run" '{}' "$TOKEN_A")
grep_check "手动触发任务成功" '"code":0' "$RUN_RES"

LOG_STATUS=""
for _ in $(seq 1 30); do
  LOGS=$(req GET "/scheduler/jobs/${JOB_ID}/logs?page=1&page_size=5" "" "$TOKEN_A")
  ST=$(echo "$LOGS" | jq -r '.data.items[0].status // empty')
  NS=$(echo "$LOGS" | jq -r '.data.items[0].notification_status // empty')
  if [ "$ST" = "success" ] && [ -n "$NS" ]; then
    LOG_STATUS="$NS"
    break
  fi
  sleep 0.1
done
check "自定义任务进入 queued" "$LOG_STATUS" "queued"

# --- 3. claim(has_context=false) skips scheduler row ---
CLAIM_NOCTX=$(gw_req POST "/gateway/im/instances/$INST_A/pushes/claim" '{"has_context":false,"limit":5}' "$GW_TOK")
check "has_context=false 不领取 scheduler 推送" "$(echo "$CLAIM_NOCTX" | jq -r '.data.items | length')" "0"

# --- 4. Closed-loop test push + scheduler push completion ---
PA_ID=$(db_query "SELECT id FROM im_push_outbox WHERE instance_id=$INST_A ORDER BY id DESC LIMIT 1" | tr -d '[:space:]')
TEST_OUT=$(mktemp)
(req POST /scheduler/channel/test '{}' "$TOKEN_A" > "$TEST_OUT") &
TEST_PID=$!

SENT_CNT=0
for _ in $(seq 1 40); do
  CLAIM_OK=$(gw_req POST "/gateway/im/instances/$INST_A/pushes/claim" '{"has_context":true,"context_updated_at":"2026-11-25T15:00:00Z","limit":10}' "$GW_TOK")
  IDS=$(echo "$CLAIM_OK" | jq -r '.data.items[].id // empty')
  for PID_IT in $IDS; do
    gw_req POST "/gateway/im/instances/$INST_A/pushes/$PID_IT/ack" '{"result":"sent"}' "$GW_TOK" >/dev/null
    SENT_CNT=$((SENT_CNT+1))
  done
  [ "$SENT_CNT" -ge 2 ] && break
  sleep 0.1
done
wait "$TEST_PID" || true
TEST_PID=""
grep_check "通道测试闭环返回 code=0" '"code":0' "$(cat "$TEST_OUT")"
rm -f "$TEST_OUT"
TEST_OUT=""

GET_CH2=$(req GET /scheduler/channel "" "$TOKEN_A")
grep_check "last_inbound_at 已写入" '2026-11-25' "$(echo "$GET_CH2" | jq -r '.data.ilink_status.last_inbound_at // ""')"

LOGS_AFTER=$(req GET "/scheduler/jobs/${JOB_ID}/logs?page=1&page_size=5" "" "$TOKEN_A")
check "回执 sent 后日志变更为 sent" "$(echo "$LOGS_AFTER" | jq -r '.data.items[0].notification_status')" "sent"

# --- 5. Cross-instance ack blocked with 1003 ---
CROSS_ACK=$(gw_req POST "/gateway/im/instances/$INST_B/pushes/$PA_ID/ack" '{"result":"sent"}' "$GW_TOK")
grep_check "跨实例 ack 返回 1003" '"code":1003' "$CROSS_ACK"

# --- 6. Built-in cron broadcast (user_id=0) to Alice & Bob ---
ACT_B=$(gw_req PATCH "/gateway/im/instances/$INST_B/status" '{"status":"active","ilink_bot_id":"bot_pb@im.bot","ilink_user_id":"wx_pb"}' "$GW_TOK")
grep_check "实例 B 激活成功" '"code":0' "$ACT_B"
SAVE_B=$(req PUT /scheduler/channel '{"channel_type":"wechat_ilink","channel_name":"Bob iLink","webhook_url":""}' "$TOKEN_B")
grep_check "B 保存 wechat_ilink 通道成功" '"code":0' "$SAVE_B"

db_exec "INSERT INTO scheduled_jobs (user_id, name, description, job_type, trigger_type, trigger_config, payload_config, delivery_channel, delivery_config, enabled, is_builtin, next_run_at) VALUES (0, 'E2E Builtin iLink Broadcast', 'test', 'system_builtin', 'cron', '{\"cron\":\"* * * * *\"}', '{\"handler\":\"mcp_pool_cleanup\"}', 'user_default', '{}', 1, 1, '2000-01-01 00:00:00')"
BI_ID=$(db_query "SELECT id FROM scheduled_jobs WHERE name='E2E Builtin iLink Broadcast' LIMIT 1" | tr -d '[:space:]')
TOGGLE_BI=$(req POST "/scheduler/jobs/${BI_ID}/toggle" '{"enabled":true}' "$TOKEN_A")
grep_check "唤醒定时引擎执行内置广播" '"code":0' "$TOGGLE_BI"

BI_LOG_STATUS=""
for _ in $(seq 1 50); do
  BL=$(req GET "/scheduler/jobs/${BI_ID}/logs?page=1&page_size=5" "" "$TOKEN_A")
  ST=$(echo "$BL" | jq -r '.data.items[0].status // empty')
  NS=$(echo "$BL" | jq -r '.data.items[0].notification_status // empty')
  if [ "$ST" = "success" ] && [ "$NS" = "queued" ]; then
    BI_LOG_STATUS="$NS"
    break
  fi
  sleep 0.1
done
check "内置广播日志进入 queued" "$BI_LOG_STATUS" "queued"

CLAIM_A_BI=$(gw_req POST "/gateway/im/instances/$INST_A/pushes/claim" '{"has_context":true,"limit":5}' "$GW_TOK")
CLAIM_B_BI=$(gw_req POST "/gateway/im/instances/$INST_B/pushes/claim" '{"has_context":true,"limit":5}' "$GW_TOK")
check "广播 Alice 收到 1 条" "$(echo "$CLAIM_A_BI" | jq -r '.data.items | length')" "1"
check "广播 Bob 收到 1 条" "$(echo "$CLAIM_B_BI" | jq -r '.data.items | length')" "1"
PA_BI=$(echo "$CLAIM_A_BI" | jq -r '.data.items[0].id')
PB_BI=$(echo "$CLAIM_B_BI" | jq -r '.data.items[0].id')
gw_req POST "/gateway/im/instances/$INST_A/pushes/$PA_BI/ack" '{"result":"sent"}' "$GW_TOK" >/dev/null
gw_req POST "/gateway/im/instances/$INST_B/pushes/$PB_BI/ack" '{"result":"failed","error":"simulated err"}' "$GW_TOK" >/dev/null
BL_PARTIAL=$(req GET "/scheduler/jobs/${BI_ID}/logs?page=1&page_size=5" "" "$TOKEN_A")
check "广播一成功一失败聚合为 partial" "$(echo "$BL_PARTIAL" | jq -r '.data.items[0].notification_status')" "partial"

# --- 7. Expired push transitions to expired & rolls up failed ---
RUN2=$(req POST "/scheduler/jobs/${JOB_ID}/run" '{}' "$TOKEN_A")
grep_check "第二次触发自定义任务成功" '"code":0' "$RUN2"
for _ in $(seq 1 30); do
  LOGS2=$(req GET "/scheduler/jobs/${JOB_ID}/logs?page=1&page_size=5" "" "$TOKEN_A")
  TOT2=$(echo "$LOGS2" | jq -r '.data.total // 0 | tonumber | floor')
  ST2=$(echo "$LOGS2" | jq -r '.data.items[0].status // empty')
  NS2=$(echo "$LOGS2" | jq -r '.data.items[0].notification_status // empty')
  if [ "$TOT2" -ge 2 ] && [ "$ST2" = "success" ] && [ "$NS2" = "queued" ]; then
    break
  fi
  sleep 0.1
done
LATEST_PID=$(db_query "SELECT MAX(id) FROM im_push_outbox WHERE instance_id=$INST_A" | tr -d '[:space:]')
db_exec "UPDATE im_push_outbox SET expires_at_ts = 100 WHERE id = $LATEST_PID"
gw_req POST "/gateway/im/instances/$INST_A/pushes/claim" '{"has_context":true,"limit":5}' "$GW_TOK" >/dev/null
EXP_ST=$(db_query "SELECT status FROM im_push_outbox WHERE id = $LATEST_PID" | tr -d '[:space:]')
check "超时推送转为 expired" "$EXP_ST" "expired"
LOGS_EXP=$(req GET "/scheduler/jobs/${JOB_ID}/logs?page=1&page_size=5" "" "$TOKEN_A")
check "过期回滚日志为 failed" "$(echo "$LOGS_EXP" | jq -r '.data.items[0].notification_status')" "failed"

echo "----------------------"
echo "PASS: $PASS  FAIL: $FAIL"
[ "$FAIL" -eq 0 ]
