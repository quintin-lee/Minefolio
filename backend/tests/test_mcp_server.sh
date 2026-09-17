#!/usr/bin/env bash
# Path B 集成测试：以外部 MCP 客户端视角验证 POST /mcp 的
# initialize / tools/list / tools/call JSON-RPC 端点。
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN="$REPO_DIR/build/minefolio"
PORT=8080
BASE="http://127.0.0.1:$PORT/api"

PASS=0
FAIL=0
MINEFOLIO_PID=""
DB=""
JWT=""
JAR=""

cleanup() {
    [ -n "$MINEFOLIO_PID" ] && kill "$MINEFOLIO_PID" 2>/dev/null || true
    [ -n "$DB" ] && rm -f "$DB" 2>/dev/null
}
trap cleanup EXIT

grep_check() {
    local desc="$1" pattern="$2" input="$3"
    if echo "$input" | grep -qE "$pattern"; then
        echo "PASS: $desc"
        PASS=$((PASS+1))
    else
        echo "FAIL: $desc (no match for /$pattern/)"
        echo "  input: $input"
        FAIL=$((FAIL+1))
    fi
}

# 1. 确保二进制已构建
if [ ! -f "$BIN" ]; then
    echo "ERROR: $BIN 不存在，请先 cmake --build build"
    exit 1
fi

# 2. 起 minefolio（临时 SQLite DB + MCP_ALLOW_LOCAL）
DB="$(mktemp -u /tmp/mf_mcp_srv.XXXXXX.db)"
JAR="$DB.jar"
rm -f "$JAR"
cd "$REPO_DIR"
MINEFOLIO_PORT="$PORT" \
MINEFOLIO_DB_DRIVER=sqlite \
MINEFOLIO_DB_DSN="$DB" \
MINEFOLIO_JWT_SECRET=test-secret-abcdef-0000 \
MINEFOLIO_MCP_ALLOW_LOCAL=1 \
"$BIN" > /tmp/mcp_srv.log 2>&1 &
MINEFOLIO_PID=$!

for i in $(seq 1 40); do
    if curl -s -o /dev/null "http://127.0.0.1:$PORT/healthz" 2>/dev/null; then
        break
    fi
    sleep 0.5
done
if ! kill -0 "$MINEFOLIO_PID" 2>/dev/null; then
    echo "ERROR: minefolio 启动失败"; tail -20 /tmp/mcp_srv.log; exit 1
fi

# 3. RSA 加密 setup（/system/setup 的 password_enc 必须为 RSA-OAEP 密文）
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

SETUP_PASS=$(rsa_encrypt "secret123")
SETUP=$(curl -s -c "$JAR" -b "$JAR" -X POST "$BASE/system/setup" \
    -H 'Content-Type: application/json' \
    -d "{\"username\":\"mcp_srv\",\"password_enc\":\"$SETUP_PASS\"}" || true)
TOKEN=$(echo "$SETUP" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')
if [ -z "$TOKEN" ]; then
    LOGIN=$(curl -s -X POST "$BASE/auth/login" \
        -H 'Content-Type: application/json' \
        -d '{"username":"mcp_srv","password":"secret123"}' || true)
    TOKEN=$(echo "$LOGIN" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')
fi
if [ -z "$TOKEN" ]; then
    echo "ERROR: 无法获取 token"; echo "setup=$SETUP"; exit 1
fi
JWT="$TOKEN"

# 4. 外部 MCP 客户端视角调 /mcp
INIT=$(curl -s -X POST "http://127.0.0.1:$PORT/mcp" \
    -H "Authorization: Bearer $JWT" -H 'Content-Type: application/json' \
    -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' || true)
grep_check "mcp: initialize returns protocolVersion" '"protocolVersion":"2024-11-05"' "$INIT"
grep_check "mcp: initialize returns serverInfo" '"name":"minefolio"' "$INIT"

LIST=$(curl -s -X POST "http://127.0.0.1:$PORT/mcp" \
    -H "Authorization: Bearer $JWT" -H 'Content-Type: application/json' \
    -d '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}' || true)
grep_check "mcp: tools/list returns get_assets" '"get_assets"' "$LIST"
grep_check "mcp: tools/list returns inputSchema" 'inputSchema' "$LIST"

CALL=$(curl -s -X POST "http://127.0.0.1:$PORT/mcp" \
    -H "Authorization: Bearer $JWT" -H 'Content-Type: application/json' \
    -d '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"get_assets","arguments":{}}}' || true)
grep_check "mcp: tools/call returns content array" '"content":\[' "$CALL"
grep_check "mcp: tools/call content has type=text" '"type":"text"' "$CALL"

# calculate_date_range requires "range_type"; an empty {} must be rejected with -32602
# (not executed). This case FAILS until Task 3 adds schema validation to tools/call.
BADCALL=$(curl -s -X POST "http://127.0.0.1:$PORT/mcp" \
    -H "Authorization: Bearer $JWT" -H 'Content-Type: application/json' \
    -d '{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"calculate_date_range","arguments":{}}}' || true)
grep_check "mcp: tools/call rejects missing required field with -32602" '"code":-32602' "$BADCALL"

UNK=$(curl -s -X POST "http://127.0.0.1:$PORT/mcp" \
    -H "Authorization: Bearer $JWT" -H 'Content-Type: application/json' \
    -d '{"jsonrpc":"2.0","id":4,"method":"bogus/method","params":{}}' || true)
grep_check "mcp: unknown method returns -32601" '"code":-32601' "$UNK"

# 未认证 /mcp（不带 Bearer）：/mcp 挂在 jwt 中间件组，缺 Bearer 会被中间件拦为 401 信封
# （{"error":"Bearer token required"}）而非 handler 的 -32001；以"未出现 protocolVersion
# 且出现鉴权拒绝字样"作为弱断言
NOAUTH=$(curl -s -X POST "http://127.0.0.1:$PORT/mcp" \
    -H 'Content-Type: application/json' \
    -d '{"jsonrpc":"2.0","id":5,"method":"initialize","params":{}}' || true)
if echo "$NOAUTH" | grep -qE '"code":-32001|unauthorized|401|Bearer token required'; then
    echo "PASS: mcp: unauthenticated rejected"
    PASS=$((PASS+1))
else
    echo "FAIL: mcp: unauthenticated rejected (got: $NOAUTH)"
    FAIL=$((FAIL+1))
fi

echo "PASS: $PASS  FAIL: $FAIL"
[ "$FAIL" -eq 0 ] || exit 1
