#!/usr/bin/env bash
# Regression test: AI chat history truncation on edit-resend + real message ID
# SSE events + regenerate delete semantics.
#
# Covers the edit-resend data-correctness fix:
#   * POST /api/ai/chat with `truncate_from_id` must delete that message and
#     every later row BEFORE loading context / inserting the new user message,
#     so an edited old turn never leaves duplicate user rows or ghost replies;
#   * the stream must carry `user_message_id` / `assistant_message_id` events
#     whose ids match the persisted rows (the frontend relies on them because
#     locally-created messages only have Date.now() placeholder ids);
#   * `regenerate: true` must drop only the assistant rows that follow the LAST
#     user row — a dangling user row from a failed turn must not cause the
#     previous turn's answer to be deleted.
#
# Drives a real POST /api/ai/chat against a locally started backend whose AI
# provider points at a mock OpenAI-compatible server (mock_ai_tool_server.js).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$(cd "$SCRIPT_DIR/../build" && pwd)"
PORT="${PORT:-18095}"
MOCK_PORT="${MOCK_PORT:-18096}"
BASE="http://127.0.0.1:$PORT/api"
DB="$(mktemp -d)/mf_ai_truncate.db"
MOCK_LOG="$(dirname "$DB")/mock.log"
SERVER_LOG="$(dirname "$DB")/server.log"
TMP_DIR="$(dirname "$DB")"
PASS=0
FAIL=0
SERVER_PID=""
MOCK_PID=""

cleanup() {
  [ -n "$SERVER_PID" ] && kill "$SERVER_PID" 2>/dev/null || true
  [ -n "$MOCK_PID" ] && kill "$MOCK_PID" 2>/dev/null || true
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

check() {
  if [ "$2" = "$3" ]; then
    PASS=$((PASS + 1))
    echo "  ✅ $1"
  else
    FAIL=$((FAIL + 1))
    echo "  ❌ $1 (期望: $2, 实际: $3)"
  fi
}

sqlq() { sqlite3 "$DB" "$1"; }

echo "=== AI history truncation regression ==="

# --- mock OpenAI server ------------------------------------------------------
MOCK_TOOL_COUNT=1 MOCK_TOOL_NAMES=get_summary MOCK_PORT="$MOCK_PORT" \
  node "$SCRIPT_DIR/mock_ai_tool_server.js" > "$MOCK_LOG" 2>&1 &
MOCK_PID=$!
for _ in $(seq 1 30); do
  curl -sf -o /dev/null "http://127.0.0.1:$MOCK_PORT/v1/models" && break
  sleep 0.2
done
echo "  mock up (port $MOCK_PORT)"

# --- backend -----------------------------------------------------------------
cat > "$TMP_DIR/ai.json" <<EOF
{"providers":[{"id":"mockai","name":"Mock AI","api_key":"dummy","base_url":"http://127.0.0.1:$MOCK_PORT/v1","models":["mock-model"]}],"default_provider":"mockai","default_model":"mock-model","context_size":20,"system_prompt":"你是测试助手"}
EOF
cd "$BUILD_DIR"
MINEFOLIO_PORT="$PORT" MINEFOLIO_DB_DRIVER=sqlite MINEFOLIO_DB_DSN="$DB" \
  MINEFOLIO_JWT_SECRET="truncate-test-secret-1234567890" AI_CONFIG="$TMP_DIR/ai.json" \
  ./minefolio > "$SERVER_LOG" 2>&1 &
SERVER_PID=$!
for _ in $(seq 1 40); do
  curl -sf "http://127.0.0.1:$PORT/healthz" >/dev/null 2>&1 && break
  sleep 0.25
done
if ! kill -0 "$SERVER_PID" 2>/dev/null; then
  echo "❌ backend failed to start"; tail -20 "$SERVER_LOG"; exit 1
fi

rsa_encrypt() {
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
    const enc = await crypto.subtle.encrypt({name:'RSA-OAEP'}, key, Buffer.from('$1'));
    process.stdout.write(Buffer.from(enc).toString('base64')
      .replace(/\+/g,'-').replace(/\//g,'_').replace(/=+$/,''));
  });
});"
}

JAR="$TMP_DIR/cookies.txt"
SP=$(rsa_encrypt "secret123")
RES=$(curl -s -c "$JAR" -b "$JAR" -X POST "$BASE/system/setup" -H "Content-Type: application/json" \
  -d "{\"username\":\"trunctester\",\"password_enc\":\"$SP\"}")
TOKEN=$(printf '%s' "$RES" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')
CSRF=$(awk '$6 == "csrf_token" { print $7 }' "$JAR" | head -1)
[ -n "$TOKEN" ] || { echo "❌ user setup failed: $RES"; exit 1; }
echo "  user setup ok"

chat() { # $1=json body  $2=out file
  curl -sN --max-time 60 -c "$JAR" -b "$JAR" -X POST "$BASE/ai/chat" \
    -H "Content-Type: application/json" -H "Authorization: Bearer $TOKEN" \
    -H "X-CSRF-Token: $CSRF" -d "$1" > "$2" || true
}

# === Scenario 1: message id SSE events match persisted rows ==================
chat '{"content":"round1","provider":"mockai","model":"mock-model"}' "$TMP_DIR/sse_1.txt"
SID=$(sqlq "SELECT id FROM ai_sessions ORDER BY id DESC LIMIT 1")
U1=$(sqlq "SELECT id FROM ai_messages WHERE session_id=$SID AND role='user' ORDER BY id LIMIT 1")
A1=$(sqlq "SELECT id FROM ai_messages WHERE session_id=$SID AND role='assistant' ORDER BY id LIMIT 1")
check "sse: one user_message_id event"      "1" "$(grep -c '^event: user_message_id' "$TMP_DIR/sse_1.txt")"
check "sse: one assistant_message_id event" "1" "$(grep -c '^event: assistant_message_id' "$TMP_DIR/sse_1.txt")"
SSE_U=$(grep -A1 '^event: user_message_id' "$TMP_DIR/sse_1.txt" | grep '^data:' | sed 's/.*"id":"\([0-9]*\)".*/\1/')
SSE_A=$(grep -A1 '^event: assistant_message_id' "$TMP_DIR/sse_1.txt" | grep '^data:' | sed 's/.*"id":"\([0-9]*\)".*/\1/')
check "sse: user id matches DB row"      "$U1" "$SSE_U"
check "sse: assistant id matches DB row" "$A1" "$SSE_A"

# === Scenario 2: edit-resend truncates server history ========================
chat "{\"content\":\"round2\",\"provider\":\"mockai\",\"model\":\"mock-model\",\"session_id\":$SID}" "$TMP_DIR/sse_2.txt"
check "two rounds persisted as 4 rows" "4" "$(sqlq "SELECT COUNT(*) FROM ai_messages WHERE session_id=$SID")"
chat "{\"content\":\"edited-round1\",\"provider\":\"mockai\",\"model\":\"mock-model\",\"session_id\":$SID,\"truncate_from_id\":$U1}" "$TMP_DIR/sse_3.txt"
check "truncate from first msg -> one fresh pair" "2" "$(sqlq "SELECT COUNT(*) FROM ai_messages WHERE session_id=$SID")"
check "no leftover round2 rows"      "0" "$(sqlq "SELECT COUNT(*) FROM ai_messages WHERE session_id=$SID AND content LIKE '%round2%'")"
check "no duplicate user rows"       "1" "$(sqlq "SELECT COUNT(*) FROM ai_messages WHERE session_id=$SID AND role='user'")"
check "first row holds edited text"  "edited-round1" "$(sqlq "SELECT content FROM ai_messages WHERE session_id=$SID AND role='user' ORDER BY id LIMIT 1")"
NEW_U=$(grep -A1 '^event: user_message_id' "$TMP_DIR/sse_3.txt" | grep '^data:' | sed 's/.*"id":"\([0-9]*\)".*/\1/')
DB_NEW_U=$(sqlq "SELECT id FROM ai_messages WHERE session_id=$SID AND role='user' ORDER BY id DESC LIMIT 1")
check "edit-resend returns fresh user id" "$DB_NEW_U" "$NEW_U"

# === Scenario 3: partial truncate keeps earlier turns ========================
chat "{\"content\":\"round4\",\"provider\":\"mockai\",\"model\":\"mock-model\",\"session_id\":$SID}" "$TMP_DIR/sse_4.txt"
U4=$(sqlq "SELECT id FROM ai_messages WHERE session_id=$SID AND role='user' ORDER BY id DESC LIMIT 1")
chat "{\"content\":\"edited-round4\",\"provider\":\"mockai\",\"model\":\"mock-model\",\"session_id\":$SID,\"truncate_from_id\":$U4}" "$TMP_DIR/sse_5.txt"
check "partial truncate -> 4 rows total" "4" "$(sqlq "SELECT COUNT(*) FROM ai_messages WHERE session_id=$SID")"
check "earlier edited turn kept intact"  "1" "$(sqlq "SELECT COUNT(*) FROM ai_messages WHERE session_id=$SID AND content LIKE '%edited-round1%'")"

# === Scenario 4: regenerate must not eat the previous answer =================
# fabricate a failed turn: user row persisted, no assistant row after it
sqlq "INSERT INTO ai_messages (session_id, role, content, model, thinking_content, tool_calls) VALUES ($SID,'user','failed-turn','mock-model','','')"
U_LAST=$(sqlq "SELECT id FROM ai_messages WHERE session_id=$SID AND role='user' ORDER BY id DESC LIMIT 1")
A_BEFORE=$(sqlq "SELECT COUNT(*) FROM ai_messages WHERE session_id=$SID AND role='assistant'")
chat "{\"content\":\"failed-turn\",\"provider\":\"mockai\",\"model\":\"mock-model\",\"session_id\":$SID,\"regenerate\":true}" "$TMP_DIR/sse_6.txt"
check "regenerate keeps earlier assistant rows" \
  "$A_BEFORE" "$(sqlq "SELECT COUNT(*) FROM ai_messages WHERE session_id=$SID AND role='assistant' AND id < $U_LAST")"
check "regenerate did not duplicate user row" "1" \
  "$(sqlq "SELECT COUNT(*) FROM ai_messages WHERE session_id=$SID AND role='user' AND content='failed-turn'")"

if [ "$FAIL" -gt 0 ]; then
  mkdir -p /tmp/ai_truncate_fail
  cp "$TMP_DIR"/sse_*.txt /tmp/ai_truncate_fail/ 2>/dev/null || true
  cp "$MOCK_LOG" /tmp/ai_truncate_fail/mock.log 2>/dev/null || true
  cp "$SERVER_LOG" /tmp/ai_truncate_fail/server.log 2>/dev/null || true
  echo "(debug artifacts copied to /tmp/ai_truncate_fail/)"
fi

echo "========================================="
echo "PASS: $PASS  FAIL: $FAIL"
[ "$FAIL" -eq 0 ]
