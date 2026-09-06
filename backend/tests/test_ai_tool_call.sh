#!/usr/bin/env bash
# Regression test: the AI chat agent loop must survive tool calls.
#
# Covers fd8ca136 (fix(ai): 🐛 fix use-after-free in tool-call message append):
# the runtime appended each role="tool" message with a realloc() that could move
# the msgs array, leaving the cached tool_call pointer dangling → intermittent
# SIGSEGV mid-stream (stream cut right after `tool_result`, no `done`).
#
# Scenarios:
#   1. single tool call in one model turn  (get_summary)
#   2. two parallel tool calls in one turn (get_summary + get_assets)
#
# Each scenario drives a real POST /api/ai/chat against a locally started
# backend whose AI provider points at a mock OpenAI-compatible server that
# streams tool calls (see mock_ai_tool_server.js). Pass criteria: the SSE
# stream contains exactly N tool_call + N tool_result events, at least one
# delta, a final `done`, and the backend process is still alive afterwards.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$(cd "$SCRIPT_DIR/../build" && pwd)"
PORT="${PORT:-8080}"
MOCK_PORT="${MOCK_PORT:-18080}"
BASE="http://127.0.0.1:$PORT/api"
DB="$(mktemp -d)/mf_ai_tool.db"
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

# --- mock OpenAI server ------------------------------------------------------
start_mock() {
  local count="$1" names="$2"
  [ -n "$MOCK_PID" ] && kill "$MOCK_PID" 2>/dev/null || true
  MOCK_TOOL_COUNT="$count" MOCK_TOOL_NAMES="$names" MOCK_PORT="$MOCK_PORT" \
    node "$SCRIPT_DIR/mock_ai_tool_server.js" > "$MOCK_LOG" 2>&1 &
  MOCK_PID=$!
  for _ in $(seq 1 20); do
    curl -sf -o /dev/null "http://127.0.0.1:$MOCK_PORT/v1/models" && break
    sleep 0.2
  done
  echo "  mock up (tools=$count: $names)"
}

# --- backend ----------------------------------------------------------------
echo "=== AI tool-call loop regression ==="
rm -f "$DB" "$DB-wal" "$DB-shm"
cat > "$TMP_DIR/ai.json" <<EOF
{"providers":[{"id":"mockai","name":"Mock AI","api_key":"dummy","base_url":"http://127.0.0.1:$MOCK_PORT/v1","models":["mock-model"]}],"default_provider":"mockai","default_model":"mock-model","context_size":20,"system_prompt":"你是测试助手"}
EOF
export TEST_JWT_SECRET="test-jwt-secret-for-ai-tool-call-1234567890"
cd "$BUILD_DIR"
MINEFOLIO_PORT="$PORT" MINEFOLIO_DB_DRIVER=sqlite MINEFOLIO_DB_DSN="$DB" \
  MINEFOLIO_JWT_SECRET="$TEST_JWT_SECRET" AI_CONFIG="$TMP_DIR/ai.json" \
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

# first boot -> /system/setup creates the initial user
JAR="$TMP_DIR/cookies.txt"
SETUP_PASS=$(rsa_encrypt "secret123")
SETUP_RES=$(curl -s -c "$JAR" -b "$JAR" -X POST "$BASE/system/setup" \
  -H "Content-Type: application/json" \
  -d "{\"username\":\"aitooltester\",\"password_enc\":\"$SETUP_PASS\"}")
TOKEN=$(printf '%s' "$SETUP_RES" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')
if [ -z "$TOKEN" ]; then
  SETUP_RES=$(curl -s -c "$JAR" -b "$JAR" -X POST "$BASE/auth/register" \
    -H "Content-Type: application/json" \
    -d '{"username":"aitooltester","password":"secret123"}')
  TOKEN=$(printf '%s' "$SETUP_RES" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')
fi
CSRF=$(awk '$6 == "csrf_token" { print $7 }' "$JAR" | head -1)
[ -n "$TOKEN" ] && PASS=$((PASS + 1)) && echo "  ✅ user setup got token" \
  || { FAIL=$((FAIL + 1)); echo "  ❌ user setup failed: $SETUP_RES"; exit 1; }

# run one chat and count SSE events; returns nothing, sets globals via files
run_chat() {
  local label="$1" body_file="$TMP_DIR/sse_$2.txt"
  curl -sN --max-time 60 -c "$JAR" -b "$JAR" -X POST "$BASE/ai/chat" \
    -H "Content-Type: application/json" \
    -H "Authorization: Bearer $TOKEN" \
    -H "X-CSRF-Token: $CSRF" \
    -d '{"content":"请查询资产概况并总结","provider":"mockai","model":"mock-model"}' \
    > "$body_file" || true
}

# ---------------------------------------------------------------------------
echo "=== Scenario 1: single tool call ==="
start_mock 1 "get_summary"
run_chat "single" 1
BODY="$TMP_DIR/sse_1.txt"
check "single: tool_call seen"          "1" "$(grep -c 'event: tool_call' "$BODY")"
check "single: tool_result seen"        "1" "$(grep -c 'event: tool_result' "$BODY")"
check "single: get_summary named (call+result)" "2" "$(grep -c '"name":"get_summary"' "$BODY")"
D=$(grep -c 'event: delta' "$BODY" || true)
check "single: text deltas streamed"    "1" "$([ "${D:-0}" -ge 1 ] && echo 1 || echo 0)"
check "single: done event received"     "1" "$(grep -c 'event: done' "$BODY")"
check "single: stop finish reason"      "1" "$(grep -c '"finish_reason":"stop"' "$BODY")"
if kill -0 "$SERVER_PID" 2>/dev/null; then
  PASS=$((PASS + 1)); echo "  ✅ single: backend alive after tool call"
else
  FAIL=$((FAIL + 1)); echo "  ❌ single: backend crashed (use-after-free regression)"
fi

echo "=== Scenario 2: two parallel tool calls ==="
start_mock 2 "get_summary,get_assets"
run_chat "multi" 2
BODY="$TMP_DIR/sse_2.txt"
check "multi: two tool_calls seen"      "2" "$(grep -c 'event: tool_call' "$BODY")"
check "multi: two tool_results seen"    "2" "$(grep -c 'event: tool_result' "$BODY")"
check "multi: get_summary named (call+result)" "2" "$(grep -c '"name":"get_summary"' "$BODY")"
check "multi: get_assets named (call+result)"  "2" "$(grep -c '"name":"get_assets"' "$BODY")"
D=$(grep -c 'event: delta' "$BODY" || true)
check "multi: text deltas streamed"     "1" "$([ "${D:-0}" -ge 1 ] && echo 1 || echo 0)"
check "multi: done event received"      "1" "$(grep -c 'event: done' "$BODY")"
if kill -0 "$SERVER_PID" 2>/dev/null; then
  PASS=$((PASS + 1)); echo "  ✅ multi: backend alive after two parallel tools"
else
  FAIL=$((FAIL + 1)); echo "  ❌ multi: backend crashed (use-after-free regression)"
fi

if [ "$FAIL" -gt 0 ]; then
  mkdir -p /tmp/ai_tool_fail
  cp "$TMP_DIR"/sse_*.txt /tmp/ai_tool_fail/ 2>/dev/null || true
  cp "$MOCK_LOG" /tmp/ai_tool_fail/mock.log 2>/dev/null || true
  cp "$SERVER_LOG" /tmp/ai_tool_fail/server.log 2>/dev/null || true
  echo "(debug artifacts copied to /tmp/ai_tool_fail/)"
fi

echo "========================================="
echo "PASS: $PASS  FAIL: $FAIL"
[ "$FAIL" -eq 0 ]
