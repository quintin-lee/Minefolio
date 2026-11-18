#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/db_helper.sh"

PORT=${MINEFOLIO_PORT:-18099}
db_init_context "$PORT" "scheduler"
BASE="http://127.0.0.1:${PORT}/api"
TMP_DIR=$(mktemp -d)
DB_PATH="$DB_FILE"
export MINEFOLIO_JWT_SECRET="test_secret_for_scheduler_suite"
export TEST_JWT_SECRET="$MINEFOLIO_JWT_SECRET"

cleanup() {
    echo "Cleaning up..."
    db_stop_server
    db_clean
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT INT TERM

echo "=== Starting Minefolio Test Server on port ${PORT} ==="
db_clean
db_start_server "$PORT" "${MINEFOLIO_BUILD_DIR:-$SCRIPT_DIR/../build}" "${TMP_DIR}/server.log"
echo "Server is up!"

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
    const arr = new Uint8Array(enc);
    process.stdout.write(btoa(String.fromCharCode(...arr))
      .replace(/\+/g,'-').replace(/\//g,'_').replace(/=+$/,''));
  });
});
"
}

# 1. Setup Alice (admin, user_id=1)
PASS_ENC_A=$(rsa_encrypt "Password123!")
AUTH_A=$(curl -s -X POST "$BASE/system/setup" \
  -H "Content-Type: application/json" \
  -d "{\"username\":\"alice\",\"password_enc\":\"$PASS_ENC_A\"}")
TOKEN_A=$(echo "$AUTH_A" | jq -r '.data.token')
USER_A_ID=$(db_query "SELECT id FROM users WHERE username='alice'")
echo "Alice setup OK (ID: $USER_A_ID)"

# 2. Setup Bob (standard user, user_id=2)
db_exec "INSERT INTO users (username, password) VALUES ('bob', 'dummyhash')"
USER_B_ID=$(db_query "SELECT id FROM users WHERE username='bob'")
TOKEN_B=$(node -e "
const crypto = require('crypto');
const header = Buffer.from(JSON.stringify({alg:'HS256',typ:'JWT'})).toString('base64url');
const now = Math.floor(Date.now()/1000);
const payload = Buffer.from(JSON.stringify({sub: $USER_B_ID, user_id: $USER_B_ID, token_version: 0, exp: now + 604800, iat: now})).toString('base64url');
const secret = '$MINEFOLIO_JWT_SECRET';
const sig = crypto.createHmac('sha256', secret).update(header + '.' + payload).digest('base64url');
process.stdout.write(header + '.' + payload + '.' + sig);
")
echo "Bob setup OK (ID: $USER_B_ID)"

# 3. Test GET /api/scheduler/dashboard
echo "Testing GET /api/scheduler/dashboard..."
DASH=$(curl -s -X GET "$BASE/scheduler/dashboard" -H "Authorization: Bearer $TOKEN_A")
echo "$DASH" | jq .
test "$(echo "$DASH" | jq -r '.code')" = "0"
test "$(echo "$DASH" | jq -r '.data.active_jobs | floor')" = "0"
test "$(echo "$DASH" | jq -r '.data.push_channel.channel_type')" = "none"

# 4. Test POST /api/scheduler/jobs (Create AI Task)
echo "Testing POST /api/scheduler/jobs..."
CREATE_RES=$(curl -s -X POST "$BASE/scheduler/jobs" \
  -H "Authorization: Bearer $TOKEN_A" \
  -H "Content-Type: application/json" \
  -d '{
    "name": "每日智能简报",
    "description": "美股收盘后每日资产与持仓总结",
    "job_type": "ai_prompt",
    "trigger_type": "cron",
    "trigger_config": "{\"cron\":\"0 5 * * *\"}",
    "payload_config": "{\"prompt\":\"总结美股持仓与重大新闻\",\"with_holdings\":true,\"with_web_search\":true}",
    "delivery_channel": "user_default"
  }')
echo "$CREATE_RES" | jq .
test "$(echo "$CREATE_RES" | jq -r '.code')" = "0"
JOB_ID=$(echo "$CREATE_RES" | jq -r '.data.id | floor')
test -n "$JOB_ID" && test "$JOB_ID" -gt 0
echo "Created Job ID: $JOB_ID"

# 5. Test GET /api/scheduler/jobs (List)
echo "Testing GET /api/scheduler/jobs..."
LIST_RES=$(curl -s -X GET "$BASE/scheduler/jobs" -H "Authorization: Bearer $TOKEN_A")
test "$(echo "$LIST_RES" | jq -r '.code')" = "0"
test "$(echo "$LIST_RES" | jq -r '.data.total | floor')" = "1"
test "$(echo "$LIST_RES" | jq -r '.data.items[0].name')" = "每日智能简报"

# 6. Test GET /api/scheduler/jobs/:id (Detail)
echo "Testing GET /api/scheduler/jobs/$JOB_ID..."
GET_RES=$(curl -s -X GET "$BASE/scheduler/jobs/$JOB_ID" -H "Authorization: Bearer $TOKEN_A")
test "$(echo "$GET_RES" | jq -r '.code')" = "0"
test "$(echo "$GET_RES" | jq -r '.data.name')" = "每日智能简报"
test "$(echo "$GET_RES" | jq -r '.data.enabled | floor')" = "1"

# 7. Test PUT /api/scheduler/jobs/:id (Update)
echo "Testing PUT /api/scheduler/jobs/$JOB_ID..."
UPDATE_RES=$(curl -s -X PUT "$BASE/scheduler/jobs/$JOB_ID" \
  -H "Authorization: Bearer $TOKEN_A" \
  -H "Content-Type: application/json" \
  -d '{
    "name": "每日智能简报 (已更新)",
    "description": "更新后的描述",
    "trigger_type": "cron",
    "trigger_config": "{\"cron\":\"0 6 * * *\"}"
  }')
test "$(echo "$UPDATE_RES" | jq -r '.code')" = "0"

GET_UPDATED=$(curl -s -X GET "$BASE/scheduler/jobs/$JOB_ID" -H "Authorization: Bearer $TOKEN_A")
test "$(echo "$GET_UPDATED" | jq -r '.data.name')" = "每日智能简报 (已更新)"

# 8. Test POST /api/scheduler/jobs/:id/toggle (Toggle)
echo "Testing POST /api/scheduler/jobs/$JOB_ID/toggle..."
TOGGLE_RES=$(curl -s -X POST "$BASE/scheduler/jobs/$JOB_ID/toggle" \
  -H "Authorization: Bearer $TOKEN_A" \
  -H "Content-Type: application/json" \
  -d '{"enabled": false}')
test "$(echo "$TOGGLE_RES" | jq -r '.code')" = "0"
test "$(echo "$TOGGLE_RES" | jq -r '.data.enabled')" = "false"

# Re-enable
curl -s -X POST "$BASE/scheduler/jobs/$JOB_ID/toggle" \
  -H "Authorization: Bearer $TOKEN_A" \
  -H "Content-Type: application/json" \
  -d '{"enabled": true}' > /dev/null

# 9. Test POST /api/scheduler/jobs/:id/run (Run immediately)
echo "Testing POST /api/scheduler/jobs/$JOB_ID/run..."
RUN_RES=$(curl -s -X POST "$BASE/scheduler/jobs/$JOB_ID/run" \
  -H "Authorization: Bearer $TOKEN_A")
test "$(echo "$RUN_RES" | jq -r '.code')" = "0"

# Allow background worker to execute
sleep 1

# 10. Test GET /api/scheduler/jobs/:id/logs
echo "Testing GET /api/scheduler/jobs/$JOB_ID/logs..."
LOGS_RES=$(curl -s -X GET "$BASE/scheduler/jobs/$JOB_ID/logs" \
  -H "Authorization: Bearer $TOKEN_A")
test "$(echo "$LOGS_RES" | jq -r '.code')" = "0"
TOTAL_LOGS=$(echo "$LOGS_RES" | jq -r '.data.total | floor')
test "$TOTAL_LOGS" -ge 1
test "$(echo "$LOGS_RES" | jq -r '.data.items[0].status')" = "success"
test "$(echo "$LOGS_RES" | jq -r '.data.items[0].trigger_reason')" = "manual_invoke"

# 11. Test Notification Channel API
echo "Testing PUT /api/scheduler/channel..."
CH_RES=$(curl -s -X PUT "$BASE/scheduler/channel" \
  -H "Authorization: Bearer $TOKEN_A" \
  -H "Content-Type: application/json" \
  -d '{
    "channel_type": "serverchan",
    "channel_name": "方糖推送",
    "webhook_url": "https://sctapi.ftqq.com",
    "secret_key": "SCT12345ABCDEF"
  }')
test "$(echo "$CH_RES" | jq -r '.code')" = "0"

echo "Testing GET /api/scheduler/channel..."
GET_CH=$(curl -s -X GET "$BASE/scheduler/channel" -H "Authorization: Bearer $TOKEN_A")
test "$(echo "$GET_CH" | jq -r '.code')" = "0"
test "$(echo "$GET_CH" | jq -r '.data.channel_type')" = "serverchan"
test "$(echo "$GET_CH" | jq -r '.data.channel_name')" = "方糖推送"
test "$(echo "$GET_CH" | jq -r '.data.is_active | floor')" = "1"
# Verify masking
MASKED=$(echo "$GET_CH" | jq -r '.data.secret_key_masked')
test "$MASKED" != "SCT12345ABCDEF"
test "$MASKED" = "SCT******DEF"

# 12. Test Multi-tenancy Isolation: Bob cannot access or modify Alice's job
echo "Testing Multi-tenancy Permission Defense (Bob accessing Alice's job)..."
BOB_PUT=$(curl -s -X PUT "$BASE/scheduler/jobs/$JOB_ID" \
  -H "Authorization: Bearer $TOKEN_B" \
  -H "Content-Type: application/json" \
  -d '{"name": "Hacked by Bob"}')
test "$(echo "$BOB_PUT" | jq -r '.code')" = "1004"

BOB_DEL=$(curl -s -X DELETE "$BASE/scheduler/jobs/$JOB_ID" \
  -H "Authorization: Bearer $TOKEN_B")
test "$(echo "$BOB_DEL" | jq -r '.code')" = "1004"

# 13. Test DELETE /api/scheduler/jobs/:id (Alice deletes her job)
echo "Testing DELETE /api/scheduler/jobs/$JOB_ID..."
DEL_RES=$(curl -s -X DELETE "$BASE/scheduler/jobs/$JOB_ID" -H "Authorization: Bearer $TOKEN_A")
test "$(echo "$DEL_RES" | jq -r '.code')" = "0"

# Verify 1003 (not found)
CHECK_DEL=$(curl -s -X GET "$BASE/scheduler/jobs/$JOB_ID" -H "Authorization: Bearer $TOKEN_A")
test "$(echo "$CHECK_DEL" | jq -r '.code')" = "1003"

echo "=== All Scheduler REST API Assertions Passed! ==="
