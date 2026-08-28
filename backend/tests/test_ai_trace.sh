#!/usr/bin/env bash
set -euo pipefail

BASE="http://127.0.0.1:8080/api"
DB="/tmp/mf_trace_test.db"
BUILD_DIR="$(cd "$(dirname "$0")/../build" && pwd)"
PASS=0; FAIL=0

cleanup() {
  [ -n "${SERVER_PID:-}" ] && kill "$SERVER_PID" 2>/dev/null || true
  rm -f "$DB" "$DB-wal" "$DB-shm"
}
trap cleanup EXIT

export TEST_JWT_SECRET="test-jwt-secret-for-trace-verification-1234567890"
rm -f "$DB" "$DB-wal" "$DB-shm"
cd "$BUILD_DIR"
MINEFOLIO_PORT=8080 MINEFOLIO_DB_DSN="$DB" MINEFOLIO_JWT_SECRET="$TEST_JWT_SECRET" ./minefolio >/tmp/minefolio_trace_test.log 2>&1 &
SERVER_PID=$!

for _i in $(seq 1 30); do
  curl -sf http://127.0.0.1:8080/healthz >/dev/null 2>&1 && break
  sleep 0.2
done

req() {
  local method="$1" path="$2" data="${3:-}"
  if [ -n "$data" ]; then
    curl -s -X "$method" -H "Content-Type: application/json" -H "${AUTH:-}" "$BASE$path" -d "$data"
  else
    curl -s -X "$method" -H "${AUTH:-}" "$BASE$path"
  fi
}

check() {
  if [ "$2" = "$3" ]; then
    PASS=$((PASS+1))
    echo "  ✅ $1"
  else
    FAIL=$((FAIL+1))
    echo "  ❌ $1 (期望: $2, 实际: $3)"
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
    const arr = new Uint8Array(enc);
    process.stdout.write(btoa(String.fromCharCode(...arr))
      .replace(/\+/g,'-').replace(/\//g,'_').replace(/=+$/,''));
  });
});"
}

echo "=== 1. Setup User ==="
SETUP_PASS=$(rsa_encrypt "pass1234")
SETUP_RES=$(req POST /system/setup "{\"username\":\"tracetester\",\"password_enc\":\"$SETUP_PASS\"}")
TOKEN=$(echo "$SETUP_RES" | jq -r '.data.token')
AUTH="Authorization: Bearer $TOKEN"

echo "=== 2. Seed 3 Traces in SQLite ==="
USER_ID=$(sqlite3 "$DB" "SELECT id FROM users WHERE username='tracetester'")
sqlite3 "$DB" "INSERT INTO ai_traces (user_id, provider, model, input_messages, output_content, system_prompt, prompt_tokens, completion_tokens, total_tokens, latency_ms, first_token_ms, tokens_per_sec, cost_usd, temperature, max_tokens, top_p, status, error_message, metadata) VALUES ($USER_ID, 'deepseek', 'deepseek-chat', '[{\"role\":\"user\",\"content\":\"分析我本月支出\"}]', '支出报告内容...', '系统提示词', 1200, 450, 1650, 1850, 240, 243.2, 0.002, 0.7, 4096, 0.95, 'ok', '', '{\"tool_spans\":[{\"name\":\"get_monthly_expenses\",\"latency_ms\":15,\"bytes\":320,\"ok\":1}]}')"

sqlite3 "$DB" "INSERT INTO ai_traces (user_id, provider, model, input_messages, output_content, system_prompt, prompt_tokens, completion_tokens, total_tokens, latency_ms, first_token_ms, tokens_per_sec, cost_usd, temperature, max_tokens, top_p, status, error_message, metadata) VALUES ($USER_ID, 'openai', 'gpt-4o', '[{\"role\":\"user\",\"content\":\"再平衡资产建议\"}]', '资产再平衡方案...', '系统提示词', 2100, 600, 2700, 3200, 510, 187.5, 0.015, 0.5, 4096, 1.0, 'ok', '', '{}')"

sqlite3 "$DB" "INSERT INTO ai_traces (user_id, provider, model, input_messages, output_content, system_prompt, prompt_tokens, completion_tokens, total_tokens, latency_ms, first_token_ms, tokens_per_sec, cost_usd, temperature, max_tokens, top_p, status, error_message, metadata) VALUES ($USER_ID, 'ollama', 'qwen2.5', '[{\"role\":\"user\",\"content\":\"预算超支了吗\"}]', '', '系统提示词', 300, 0, 300, 500, 0, 0.0, 0.0, 0.7, 2048, 0.9, 'error', 'Connection refused', '{}')"

echo "=== 3. Query AI Trace Stats ==="
STATS_RES=$(req GET /ai/traces/stats)
check "trace stats code=0" "0" "$(echo "$STATS_RES" | jq -r '.code | floor')"
check "trace stats total_traces=3" "3" "$(echo "$STATS_RES" | jq -r '.data.total_traces | floor')"
check "trace stats total_tokens=4650" "4650" "$(echo "$STATS_RES" | jq -r '.data.total_tokens | floor')"

echo "=== 4. Query AI Trace List (All) ==="
LIST_RES=$(req GET "/ai/traces?page=1&page_size=20")
check "trace list code=0" "0" "$(echo "$LIST_RES" | jq -r '.code | floor')"
check "trace list total=3" "3" "$(echo "$LIST_RES" | jq -r '.data.total | floor')"
check "trace list items length=3" "3" "$(echo "$LIST_RES" | jq -r '.data.list | length')"
check "first trace model=qwen2.5" "qwen2.5" "$(echo "$LIST_RES" | jq -r '.data.list[0].model')"
check "first trace status=error" "error" "$(echo "$LIST_RES" | jq -r '.data.list[0].status')"
check "second trace provider=openai" "openai" "$(echo "$LIST_RES" | jq -r '.data.list[1].provider')"
check "third trace provider=deepseek" "deepseek" "$(echo "$LIST_RES" | jq -r '.data.list[2].provider')"

echo "=== 5. Query AI Trace List (Filter by Provider) ==="
FILTER_PROV_RES=$(req GET "/ai/traces?page=1&page_size=20&provider=deepseek")
check "filter deepseek list count=1" "1" "$(echo "$FILTER_PROV_RES" | jq -r '.data.list | length')"
check "filter deepseek item provider=deepseek" "deepseek" "$(echo "$FILTER_PROV_RES" | jq -r '.data.list[0].provider')"

echo "=== 6. Query AI Trace Detail ==="
FIRST_ID=$(echo "$LIST_RES" | jq -r '.data.list[2].id')
DETAIL_RES=$(req GET "/ai/traces/$FIRST_ID")
check "trace detail code=0" "0" "$(echo "$DETAIL_RES" | jq -r '.code | floor')"
check "trace detail provider=deepseek" "deepseek" "$(echo "$DETAIL_RES" | jq -r '.data.provider')"
check "trace detail model=deepseek-chat" "deepseek-chat" "$(echo "$DETAIL_RES" | jq -r '.data.model')"
check "trace detail has input_messages" "true" "$(echo "$DETAIL_RES" | jq -r '.data.input_messages | length > 0')"
check "trace detail has metadata tool_spans" "true" "$(echo "$DETAIL_RES" | jq -r '.data.metadata | contains("tool_spans")')"

echo ""
echo "Summary: PASS=$PASS FAIL=$FAIL"
if [ "$FAIL" -gt 0 ]; then
  exit 1
fi
