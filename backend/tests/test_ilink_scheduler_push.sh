#!/usr/bin/env bash
# Dual-engine integration test for WeChat iLink scheduler push & Outbox delivery.
set -uo pipefail

TEST_PORT="${TEST_PORT:-18083}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/db_helper.sh"

db_init_context "$TEST_PORT" "ilink_sched_push_test"
BASE="http://127.0.0.1:${PORT}/api"
BUILD_DIR="${MINEFOLIO_BUILD_DIR:-$(cd "$SCRIPT_DIR/../build" && pwd)}"
GW_TOK="${MINEFOLIO_GATEWAY_TOKEN:-test-gateway-token}"
export MINEFOLIO_GATEWAY_TOKEN="$GW_TOK"
export MINEFOLIO_ILINK_TEST_TIMEOUT_MS="${MINEFOLIO_ILINK_TEST_TIMEOUT_MS:-2500}"
SERVER_LOG="$(mktemp)"
TEST_OUT=""
TEST_PID=""
_CLEANED=0
PASS=0
FAIL=0

cleanup() {
  local rc=$?
  if [ "$_CLEANED" -eq 1 ]; then
    return
  fi
  _CLEANED=1
  if [ -n "${TEST_PID:-}" ] && kill -0 "$TEST_PID" 2>/dev/null; then
    kill "$TEST_PID" 2>/dev/null || true
    wait "$TEST_PID" 2>/dev/null || true
  fi
  db_stop_server
  if { [ "$rc" -ne 0 ] || [ "$FAIL" -gt 0 ]; } && [ -s "$SERVER_LOG" ]; then
    echo "=== Tail of server log on failure (rc=$rc FAIL=$FAIL) ==="
    tail -n 60 "$SERVER_LOG" || true
  fi
  rm -f "$SERVER_LOG"
  [ -n "${TEST_OUT:-}" ] && rm -f "$TEST_OUT"
  db_clean
}
trap cleanup EXIT

export TEST_JWT_SECRET
TEST_JWT_SECRET=$(node -e "console.log(require('crypto').randomBytes(32).toString('hex'))")

db_clean
db_start_server "$PORT" "$BUILD_DIR" "$SERVER_LOG" || exit 1

req() { # req METHOD PATH [JSON] [BEARER_TOKEN] -> outputs BODY\nHTTP_STATUS
  local method="$1" path="$2" data="${3:-}" tok="${4:-}"
  local -a args=(-s -w $'\n%{http_code}' -X "$method")
  [ -n "$tok" ] && args+=(-H "Authorization: Bearer $tok")
  if [ -n "$data" ]; then
    args+=(-H 'Content-Type: application/json' -d "$data")
  fi
  curl "${args[@]}" "$BASE$path"
}

gw_req() { # gw_req METHOD PATH [JSON] [GW_TOKEN] -> outputs BODY\nHTTP_STATUS
  local method="$1" path="$2" data="${3:-}" tok="${4:-}"
  local -a args=(-s -w $'\n%{http_code}' -X "$method")
  [ -n "$tok" ] && args+=(-H "X-Gateway-Token: $tok")
  if [ -n "$data" ]; then
    args+=(-H 'Content-Type: application/json' -d "$data")
  fi
  curl "${args[@]}" "$BASE$path"
}

resp_code() {
  printf '%s\n' "$1" | tail -n 1 | tr -d '[:space:]'
}

resp_body() {
  printf '%s\n' "$1" | sed '$d'
}

pass() {
  echo "  PASS: $1"
  PASS=$((PASS+1))
}

fail() {
  echo "  FAIL: $1"
  FAIL=$((FAIL+1))
}

check() {
  if [ "$2" = "$3" ]; then
    pass "$1"
  else
    fail "$1 (expected '$3', got '$2')"
  fi
}

check_api() { # check_api DESC RAW_RESP EXPECTED_CODE
  local desc="$1" raw="$2" exp_code="$3"
  local http_st body api_code
  http_st="$(resp_code "$raw")"
  body="$(resp_body "$raw")"
  if [ "$http_st" != "200" ]; then
    fail "$desc (HTTP $http_st != 200): $body"
    return 1
  fi
  api_code="$(printf '%s' "$body" | jq -r '.code // "null"' 2>/dev/null || echo "invalid_json")"
  if [ "$api_code" = "$exp_code" ]; then
    pass "$desc"
    return 0
  else
    fail "$desc (expected code=$exp_code, got code=$api_code): $body"
    return 1
  fi
}

check_api_nonzero() { # check_api_nonzero DESC RAW_RESP
  local desc="$1" raw="$2"
  local http_st body api_code msg
  http_st="$(resp_code "$raw")"
  body="$(resp_body "$raw")"
  if [ "$http_st" != "200" ]; then
    fail "$desc (HTTP $http_st != 200): $body"
    return 1
  fi
  api_code="$(printf '%s' "$body" | jq -r '.code // "null"' 2>/dev/null || echo "null")"
  msg="$(printf '%s' "$body" | jq -r '.message // ""' 2>/dev/null || echo "")"
  if [[ "$api_code" =~ ^-?[0-9]+$ ]] && [ "$api_code" != "0" ] && [ -n "$msg" ]; then
    pass "$desc"
    return 0
  else
    fail "$desc (expected non-zero code & non-empty message, got code=$api_code msg='$msg'): $body"
    return 1
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
    try {
      const outer = JSON.parse(d);
      const jwk = typeof outer.data.public_key === 'string'
        ? JSON.parse(outer.data.public_key) : outer.data.public_key;
      const key = await crypto.subtle.importKey('jwk', jwk,
        {name:'RSA-OAEP', hash:'SHA-256'}, false, ['encrypt']);
      const enc = await crypto.subtle.encrypt({name:'RSA-OAEP'}, key, Buffer.from('$plain'));
      process.stdout.write(Buffer.from(enc).toString('base64')
        .replace(/\+/g,'-').replace(/\//g,'_').replace(/=+\$/,''));
    } catch (e) {
      console.error(e);
      process.exit(1);
    }
  });
}).on('error', (e) => {
  console.error(e);
  process.exit(1);
});"
}

# --- Setup Alice (admin) & Bob (user) ---
PASS_A=$(rsa_encrypt "passA1234") || exit 1
SETUP_RAW=$(req POST /system/setup "{\"username\":\"pushalice\",\"password_enc\":\"$PASS_A\"}")
TOKEN_A=$(resp_body "$SETUP_RAW" | jq -r '.data.token // empty')
if [ -z "$TOKEN_A" ]; then
  LOGIN_A_RAW=$(req POST /auth/login '{"username":"pushalice","password":"passA1234"}')
  TOKEN_A=$(resp_body "$LOGIN_A_RAW" | jq -r '.data.token // empty')
fi

PASS_B=$(rsa_encrypt "passB1234") || exit 1
REG_B_RAW=$(req POST /auth/register "{\"username\":\"pushbob\",\"password_enc\":\"$PASS_B\"}")
TOKEN_B=$(resp_body "$REG_B_RAW" | jq -r '.data.token // empty')
if [ -z "$TOKEN_B" ]; then
  LOGIN_B_RAW=$(req POST /auth/login '{"username":"pushbob","password":"passB1234"}')
  TOKEN_B=$(resp_body "$LOGIN_B_RAW" | jq -r '.data.token // empty')
fi

if [ -z "$TOKEN_A" ] || [ -z "$TOKEN_B" ]; then
  echo "ERROR: failed to obtain user tokens"
  exit 1
fi

# --- 1. Create instances A & B, save wechat_ilink channel, pre-login rejection & bad type=1002 ---
CREATE_A_RAW=$(req POST /im/instances '{"platform":"wechat","adapter":"ilink"}' "$TOKEN_A")
check_api "创建实例 A 成功" "$CREATE_A_RAW" "0"
INST_A=$(resp_body "$CREATE_A_RAW" | jq -r '.data.id // empty')

CREATE_B_RAW=$(req POST /im/instances '{"platform":"wechat","adapter":"ilink"}' "$TOKEN_B")
check_api "创建实例 B 成功" "$CREATE_B_RAW" "0"
INST_B=$(resp_body "$CREATE_B_RAW" | jq -r '.data.id // empty')

if ! [[ "$INST_A" =~ ^[1-9][0-9]*$ ]] || ! [[ "$INST_B" =~ ^[1-9][0-9]*$ ]]; then
  echo "ERROR: invalid instance IDs: INST_A='$INST_A' INST_B='$INST_B'"
  exit 1
fi

BAD_CH_RAW=$(req PUT /scheduler/channel '{"channel_type":"unsupported_foo","channel_name":"bad","webhook_url":""}' "$TOKEN_A")
check_api "不支持的 channel_type 返回 1002" "$BAD_CH_RAW" "1002"

SAVE_A_RAW=$(req PUT /scheduler/channel '{"channel_type":"wechat_ilink","channel_name":"微信iLink","webhook_url":""}' "$TOKEN_A")
check_api "A 保存 wechat_ilink 通道成功" "$SAVE_A_RAW" "0"

GET_CH_RAW=$(req GET /scheduler/channel "" "$TOKEN_A")
check_api "GET channel 返回 0" "$GET_CH_RAW" "0"
GET_CH="$(resp_body "$GET_CH_RAW")"
check "GET channel type=wechat_ilink" "$(printf '%s' "$GET_CH" | jq -r '.data.channel_type')" "wechat_ilink"
check "GET channel has_instance=true" "$(printf '%s' "$GET_CH" | jq -r '.data.ilink_status.has_instance')" "true"

# Pre-login / pending_login test rejection (Spec §9.3)
PRELOGIN_TEST_RAW=$(req POST /scheduler/channel/test '{}' "$TOKEN_A")
check_api_nonzero "未激活实例测试推送被拒绝" "$PRELOGIN_TEST_RAW"

# --- 2. Activate A, trigger custom job -> notification_status == queued ---
ACT_A_RAW=$(gw_req PATCH "/gateway/im/instances/$INST_A/status" '{"status":"active","ilink_bot_id":"bot_pa@im.bot","ilink_user_id":"wx_pa"}' "$GW_TOK")
check_api "实例 A 激活成功" "$ACT_A_RAW" "0"

CREATE_JOB_RAW=$(req POST /scheduler/jobs '{"name":"iLink Custom Push","job_type":"structured_report","trigger_type":"cron","trigger_config":"{\"cron\":\"0 9 * * *\"}","payload_config":"{}","delivery_channel":"user_default"}' "$TOKEN_A")
check_api "创建定时任务成功" "$CREATE_JOB_RAW" "0"
JOB_ID=$(resp_body "$CREATE_JOB_RAW" | jq -r '.data.id // empty')

RUN_RAW=$(req POST "/scheduler/jobs/${JOB_ID}/run" '{}' "$TOKEN_A")
check_api "手动触发任务成功" "$RUN_RAW" "0"

LOG_STATUS=""
for _ in $(seq 1 40); do
  LOGS_RAW=$(req GET "/scheduler/jobs/${JOB_ID}/logs?page=1&page_size=5" "" "$TOKEN_A")
  LOGS=$(resp_body "$LOGS_RAW")
  ST=$(printf '%s' "$LOGS" | jq -r '.data.items[0].status // empty')
  NS=$(printf '%s' "$LOGS" | jq -r '.data.items[0].notification_status // empty')
  if [ "$ST" = "success" ] && [ -n "$NS" ]; then
    LOG_STATUS="$NS"
    break
  fi
  sleep 0.1
done
if [ -z "$LOG_STATUS" ]; then
  fail "自定义任务轮询等待 queued 超时"
else
  check "自定义任务进入 queued" "$LOG_STATUS" "queued"
fi

# --- 3. claim(has_context=false) skips scheduler row ---
CLAIM_NOCTX_RAW=$(gw_req POST "/gateway/im/instances/$INST_A/pushes/claim" '{"has_context":false,"limit":5}' "$GW_TOK")
check_api "has_context=false claim 返回 0" "$CLAIM_NOCTX_RAW" "0"
check "has_context=false 不领取 scheduler 推送" "$(resp_body "$CLAIM_NOCTX_RAW" | jq -r '.data.items | length')" "0"

# --- 4. Closed-loop test push + scheduler push completion ---
PA_ID=$(db_query "SELECT id FROM im_push_outbox WHERE instance_id=$INST_A ORDER BY id DESC LIMIT 1" | tr -d '[:space:]')
TEST_OUT=$(mktemp)
(req POST /scheduler/channel/test '{}' "$TOKEN_A" > "$TEST_OUT") &
TEST_PID=$!

SENT_CNT=0
for _ in $(seq 1 50); do
  CLAIM_OK_RAW=$(gw_req POST "/gateway/im/instances/$INST_A/pushes/claim" '{"has_context":true,"context_updated_at":"2026-11-25T12:00:00Z","limit":10}' "$GW_TOK")
  if [ "$(resp_code "$CLAIM_OK_RAW")" != "200" ] || [ "$(resp_body "$CLAIM_OK_RAW" | jq -r '.code // "null"')" != "0" ]; then
    fail "闭环 claim 失败: $CLAIM_OK_RAW"
    break
  fi
  IDS=$(resp_body "$CLAIM_OK_RAW" | jq -r '.data.items[].id // empty')
  for PID_IT in $IDS; do
    ACK_RAW=$(gw_req POST "/gateway/im/instances/$INST_A/pushes/$PID_IT/ack" '{"result":"sent"}' "$GW_TOK")
    check_api "闭环 ack sent ($PID_IT) 返回 0" "$ACK_RAW" "0"
    SENT_CNT=$((SENT_CNT+1))
  done
  [ "$SENT_CNT" -ge 2 ] && break
  sleep 0.1
done
wait "$TEST_PID" || true
TEST_PID=""
check_api "通道测试闭环返回 code=0" "$(cat "$TEST_OUT")" "0"
rm -f "$TEST_OUT"
TEST_OUT=""

GET_CH2_RAW=$(req GET /scheduler/channel "" "$TOKEN_A")
check_api "GET channel2 返回 0" "$GET_CH2_RAW" "0"
INB_AT=$(resp_body "$GET_CH2_RAW" | jq -r '.data.ilink_status.last_inbound_at // ""')
if [[ "$INB_AT" == *"2026-11-25"* ]]; then
  pass "last_inbound_at 已写入"
else
  fail "last_inbound_at 未含 2026-11-25: '$INB_AT'"
fi

LOGS_AFTER_RAW=$(req GET "/scheduler/jobs/${JOB_ID}/logs?page=1&page_size=5" "" "$TOKEN_A")
check "回执 sent 后日志变更为 sent" "$(resp_body "$LOGS_AFTER_RAW" | jq -r '.data.items[0].notification_status')" "sent"

# --- 4b. Test push failure (ack failed -> code=400 & error message) & timeout (expired) ---
TEST_OUT=$(mktemp)
(req POST /scheduler/channel/test '{}' "$TOKEN_A" > "$TEST_OUT") &
TEST_PID=$!

FAIL_ACKED=0
for _ in $(seq 1 40); do
  CLAIM_FAIL_RAW=$(gw_req POST "/gateway/im/instances/$INST_A/pushes/claim" '{"has_context":true,"limit":5}' "$GW_TOK")
  if [ "$(resp_code "$CLAIM_FAIL_RAW")" != "200" ] || [ "$(resp_body "$CLAIM_FAIL_RAW" | jq -r '.code // "null"')" != "0" ]; then
    fail "失败路径 claim 失败: $CLAIM_FAIL_RAW"
    break
  fi
  IDS=$(resp_body "$CLAIM_FAIL_RAW" | jq -r '.data.items[].id // empty')
  for PID_IT in $IDS; do
    ACK_FAIL_RAW=$(gw_req POST "/gateway/im/instances/$INST_A/pushes/$PID_IT/ack" '{"result":"failed","error":"模拟网关发送失败"}' "$GW_TOK")
    check_api "失败路径 ack failed 返回 0" "$ACK_FAIL_RAW" "0"
    FAIL_ACKED=1
  done
  [ "$FAIL_ACKED" -eq 1 ] && break
  sleep 0.1
done
wait "$TEST_PID" || true
TEST_PID=""
TEST_FAIL_RESP="$(cat "$TEST_OUT")"
rm -f "$TEST_OUT"
TEST_OUT=""
check_api "通道测试网关失败返回 code=400" "$TEST_FAIL_RESP" "400"
FAIL_MSG="$(resp_body "$TEST_FAIL_RESP" | jq -r '.message // ""')"
if [[ "$FAIL_MSG" == *"模拟网关发送失败"* ]]; then
  pass "通道测试失败返回透传错误信息"
else
  fail "通道测试失败消息未包含 '模拟网关发送失败': '$FAIL_MSG'"
fi

TEST_TIMEOUT_RAW=$(req POST /scheduler/channel/test '{}' "$TOKEN_A")
check_api "通道测试无网关响应超时返回 code=400" "$TEST_TIMEOUT_RAW" "400"
EXP_TEST_ST=$(db_query "SELECT status FROM im_push_outbox WHERE instance_id=$INST_A AND kind='test' ORDER BY id DESC LIMIT 1" | tr -d '[:space:]')
check "超时测试推送在 outbox 转为 expired" "$EXP_TEST_ST" "expired"

# --- 5. Cross-instance ack blocked with 1003 ---
CROSS_ACK_RAW=$(gw_req POST "/gateway/im/instances/$INST_B/pushes/$PA_ID/ack" '{"result":"sent"}' "$GW_TOK")
check_api "跨实例 ack 返回 1003" "$CROSS_ACK_RAW" "1003"

# --- 6. Built-in cron broadcast (user_id=0) to Alice & Bob ---
ACT_B_RAW=$(gw_req PATCH "/gateway/im/instances/$INST_B/status" '{"status":"active","ilink_bot_id":"bot_pb@im.bot","ilink_user_id":"wx_pb"}' "$GW_TOK")
check_api "实例 B 激活成功" "$ACT_B_RAW" "0"
SAVE_B_RAW=$(req PUT /scheduler/channel '{"channel_type":"wechat_ilink","channel_name":"Bob iLink","webhook_url":""}' "$TOKEN_B")
check_api "B 保存 wechat_ilink 通道成功" "$SAVE_B_RAW" "0"

db_exec "INSERT INTO scheduled_jobs (user_id, name, description, job_type, trigger_type, trigger_config, payload_config, delivery_channel, delivery_config, enabled, is_builtin, next_run_at) VALUES (0, 'E2E Builtin iLink Broadcast', 'test', 'system_builtin', 'cron', '{\"cron\":\"0 0 1 1 *\"}', '{\"handler\":\"mcp_pool_cleanup\"}', 'user_default', '{}', 1, 1, '2000-01-01 00:00:00')"
BI_ID=$(db_query "SELECT id FROM scheduled_jobs WHERE name='E2E Builtin iLink Broadcast' LIMIT 1" | tr -d '[:space:]')
TOGGLE_BI_RAW=$(req POST "/scheduler/jobs/${BI_ID}/toggle" '{"enabled":true}' "$TOKEN_A")
check_api "唤醒定时引擎执行内置广播" "$TOGGLE_BI_RAW" "0"

BI_LOG_STATUS=""
for _ in $(seq 1 50); do
  BL_RAW=$(req GET "/scheduler/jobs/${BI_ID}/logs?page=1&page_size=5" "" "$TOKEN_A")
  BL=$(resp_body "$BL_RAW")
  ST=$(printf '%s' "$BL" | jq -r '.data.items[0].status // empty')
  NS=$(printf '%s' "$BL" | jq -r '.data.items[0].notification_status // empty')
  if [ "$ST" = "success" ] && [ "$NS" = "queued" ]; then
    BI_LOG_STATUS="$NS"
    break
  fi
  sleep 0.1
done
if [ -z "$BI_LOG_STATUS" ]; then
  fail "内置广播等待 queued 超时"
else
  check "内置广播日志进入 queued" "$BI_LOG_STATUS" "queued"
fi

CLAIM_A_BI_RAW=$(gw_req POST "/gateway/im/instances/$INST_A/pushes/claim" '{"has_context":true,"limit":5}' "$GW_TOK")
CLAIM_B_BI_RAW=$(gw_req POST "/gateway/im/instances/$INST_B/pushes/claim" '{"has_context":true,"limit":5}' "$GW_TOK")
check_api "广播 claim A 返回 0" "$CLAIM_A_BI_RAW" "0"
check_api "广播 claim B 返回 0" "$CLAIM_B_BI_RAW" "0"
check "广播 Alice 收到 1 条" "$(resp_body "$CLAIM_A_BI_RAW" | jq -r '.data.items | length')" "1"
check "广播 Bob 收到 1 条" "$(resp_body "$CLAIM_B_BI_RAW" | jq -r '.data.items | length')" "1"
PA_BI=$(resp_body "$CLAIM_A_BI_RAW" | jq -r '.data.items[0].id')
PB_BI=$(resp_body "$CLAIM_B_BI_RAW" | jq -r '.data.items[0].id')
ACK_A_BI_RAW=$(gw_req POST "/gateway/im/instances/$INST_A/pushes/$PA_BI/ack" '{"result":"sent"}' "$GW_TOK")
ACK_B_BI_RAW=$(gw_req POST "/gateway/im/instances/$INST_B/pushes/$PB_BI/ack" '{"result":"failed","error":"simulated err"}' "$GW_TOK")
check_api "广播 Alice ack sent 返回 0" "$ACK_A_BI_RAW" "0"
check_api "广播 Bob ack failed 返回 0" "$ACK_B_BI_RAW" "0"
BL_PARTIAL_RAW=$(req GET "/scheduler/jobs/${BI_ID}/logs?page=1&page_size=5" "" "$TOKEN_A")
check "广播一成功一失败聚合为 partial" "$(resp_body "$BL_PARTIAL_RAW" | jq -r '.data.items[0].notification_status')" "partial"

# --- 7. Expired push transitions to expired & rolls up failed ---
RUN2_RAW=$(req POST "/scheduler/jobs/${JOB_ID}/run" '{}' "$TOKEN_A")
check_api "第二次触发自定义任务成功" "$RUN2_RAW" "0"
LOG2_QUEUED=0
for _ in $(seq 1 40); do
  LOGS2_RAW=$(req GET "/scheduler/jobs/${JOB_ID}/logs?page=1&page_size=5" "" "$TOKEN_A")
  LOGS2=$(resp_body "$LOGS2_RAW")
  TOT2=$(printf '%s' "$LOGS2" | jq -r '.data.total // 0 | tonumber | floor')
  ST2=$(printf '%s' "$LOGS2" | jq -r '.data.items[0].status // empty')
  NS2=$(printf '%s' "$LOGS2" | jq -r '.data.items[0].notification_status // empty')
  if [ "$TOT2" -ge 2 ] && [ "$ST2" = "success" ] && [ "$NS2" = "queued" ]; then
    LOG2_QUEUED=1
    break
  fi
  sleep 0.1
done
if [ "$LOG2_QUEUED" -ne 1 ]; then
  fail "第二次触发任务等待 queued 超时"
fi
LATEST_PID=$(db_query "SELECT MAX(id) FROM im_push_outbox WHERE instance_id=$INST_A" | tr -d '[:space:]')
db_exec "UPDATE im_push_outbox SET expires_at_ts = 100 WHERE id = $LATEST_PID"
EXP_CLAIM_RAW=$(gw_req POST "/gateway/im/instances/$INST_A/pushes/claim" '{"has_context":true,"limit":5}' "$GW_TOK")
check_api "过期触发 claim 返回 0" "$EXP_CLAIM_RAW" "0"
EXP_ST=$(db_query "SELECT status FROM im_push_outbox WHERE id = $LATEST_PID" | tr -d '[:space:]')
check "超时推送转为 expired" "$EXP_ST" "expired"
LOGS_EXP_RAW=$(req GET "/scheduler/jobs/${JOB_ID}/logs?page=1&page_size=5" "" "$TOKEN_A")
check "过期回滚日志为 failed" "$(resp_body "$LOGS_EXP_RAW" | jq -r '.data.items[0].notification_status')" "failed"

echo "----------------------"
echo "PASS: $PASS  FAIL: $FAIL"
[ "$FAIL" -eq 0 ]
