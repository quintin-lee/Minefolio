#!/usr/bin/env bash
set -euo pipefail

PORT=8091
BASE_URL="http://127.0.0.1:${PORT}"
TMP_DIR=$(mktemp -d)
DB_PATH="${TMP_DIR}/test_market.db"
SERVER_PID=""

cleanup() {
    echo "Cleaning up..."
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT

echo "=== Starting Minefolio Test Server on port ${PORT} ==="
export MINEFOLIO_JWT_SECRET="test_secret_for_market_integration_suite"
export MINEFOLIO_DB_DRIVER="sqlite"
export MINEFOLIO_DB_DSN="${DB_PATH}"
export PORT="${PORT}"

cd "$(dirname "$0")/.."
./build/minefolio > "${TMP_DIR}/server.log" 2>&1 &
SERVER_PID=$!

# Wait for server to start
for i in {1..30}; do
    if curl -s "${BASE_URL}/healthz" >/dev/null 2>&1 || grep -q "Starting Minefolio server" "${TMP_DIR}/server.log" 2>/dev/null; then
        echo "Server is up!"
        break
    fi
    sleep 0.2
done

rsa_encrypt() {
  local plain="$1"
  node -e "
const http = require('http');
http.get('$BASE_URL/api/auth/public-key', (res) => {
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
      .replace(/\\+/g,'-').replace(/\\//g,'_').replace(/=+$/,''));
  });
});
"
}

echo "=== 1. Register and Obtain Token ==="
ENC_PW=$(rsa_encrypt "password123")
REGISTER_RES=$(curl -s -X POST "${BASE_URL}/api/auth/register" \
    -H "Content-Type: application/json" \
    -d "{\"username\":\"market_tester\",\"password\":\"$ENC_PW\"}")
echo "Register response: ${REGISTER_RES}"

TOKEN=$(node -e "const r=JSON.parse(process.argv[1]); console.log(r.data.token);" "$REGISTER_RES")
if [ -z "$TOKEN" ]; then
    echo "FATAL: Failed to obtain JWT token"
    exit 1
fi
AUTH_HEADER="Authorization: Bearer ${TOKEN}"

echo "=== 2. Test Market Symbol Search ==="
SEARCH_RES=$(curl -s -H "$AUTH_HEADER" "${BASE_URL}/api/market/search?keyword=%E8%8C%85%E5%8F%B0")
echo "Search '茅台': ${SEARCH_RES}"
if ! echo "$SEARCH_RES" | grep -q "600519"; then
    echo "FATAL: Symbol search did not find 600519"
    exit 1
fi

echo "=== 2b. Test Yahoo Finance Symbol Search (USD/CNY & Gold) ==="
SEARCH_YAHOO=$(curl -s -H "$AUTH_HEADER" "${BASE_URL}/api/market/search?keyword=USDCNY")
echo "Search 'USDCNY': ${SEARCH_YAHOO}"
if ! echo "$SEARCH_YAHOO" | grep -q "USDCNY=X"; then
    echo "FATAL: Symbol search did not find USDCNY=X"
    exit 1
fi

echo "=== 3. Test Single Market Quote Fetch ==="
QUOTE_RES=$(curl -s -H "$AUTH_HEADER" "${BASE_URL}/api/market/quote?symbol=sh600519&source=stock_cn")
echo "Quote 'sh600519': ${QUOTE_RES}"
if ! echo "$QUOTE_RES" | grep -q '"current_price":'; then
    echo "FATAL: Quote did not return current_price"
    exit 1
fi

echo "=== 3b. Test Yahoo Forex Quote Fetch (USDCNY=X) ==="
QUOTE_USD=$(curl -s -H "$AUTH_HEADER" "${BASE_URL}/api/market/quote?symbol=USDCNY=X&source=yahoo")
echo "Quote 'USDCNY=X': ${QUOTE_USD}"
if ! echo "$QUOTE_USD" | grep -q '"current_price":'; then
    echo "FATAL: Yahoo Quote did not return current_price for USDCNY=X"
    exit 1
fi

echo "=== 4. Create Category and Asset with Market Symbol ==="
CAT_RES=$(curl -s -X POST "${BASE_URL}/api/categories" \
    -H "$AUTH_HEADER" -H "Content-Type: application/json" \
    -d '{"name":"A股股票","type":"asset","asset_type":"stock","currency":"CNY"}')
echo "Category create: ${CAT_RES}"

CAT_LIST=$(curl -s -H "$AUTH_HEADER" "${BASE_URL}/api/categories")
CAT_ID=$(node -e "const r=JSON.parse(process.argv[1]); console.log(r.data[0].id);" "$CAT_LIST")

ASSET_RES=$(curl -s -X POST "${BASE_URL}/api/assets" \
    -H "$AUTH_HEADER" -H "Content-Type: application/json" \
    -d "{\"category_id\":${CAT_ID},\"name\":\"贵州茅台\",\"symbol\":\"sh600519\",\"quote_source\":\"stock_cn\",\"quantity\":10,\"net_value\":1000,\"cost_basis\":10000,\"currency\":\"CNY\"}")
echo "Asset create: ${ASSET_RES}"

ASSETS_LIST=$(curl -s -H "$AUTH_HEADER" "${BASE_URL}/api/assets")
echo "Assets list: ${ASSETS_LIST}"
ASSET_ID=$(node -e "const r=JSON.parse(process.argv[1]); console.log(r.data.list[0].id);" "$ASSETS_LIST")
echo "Asset ID: ${ASSET_ID}"

echo "=== 5. Test Single Asset Quote Sync ==="
SYNC_ONE_RES=$(curl -s -X POST "${BASE_URL}/api/market/sync/${ASSET_ID}" -H "$AUTH_HEADER")
echo "Sync single asset: ${SYNC_ONE_RES}"
if ! echo "$SYNC_ONE_RES" | grep -q '"net_value":'; then
    echo "FATAL: Single sync failed"
    exit 1
fi

echo "=== 6. Test Bulk Market Sync ==="
SYNC_ALL_RES=$(curl -s -X POST "${BASE_URL}/api/market/sync" -H "$AUTH_HEADER")
echo "Sync all: ${SYNC_ALL_RES}"
if ! echo "$SYNC_ALL_RES" | grep -q '"synced_count":1'; then
    echo "FATAL: Sync all did not sync asset"
    exit 1
fi

echo "=== 7. Test Asset Price History ==="
HIST_RES=$(curl -s -H "$AUTH_HEADER" "${BASE_URL}/api/market/history/${ASSET_ID}?limit=30")
echo "Price history: ${HIST_RES}"
if ! echo "$HIST_RES" | grep -q '"price":'; then
    echo "FATAL: Price history empty"
    exit 1
fi

echo "=== 8. Test Market Settings & Proxy Test ==="
SETTINGS_RES=$(curl -s -H "$AUTH_HEADER" "${BASE_URL}/api/market/settings")
echo "Settings: ${SETTINGS_RES}"

TEST_PROXY_RES=$(curl -s -X POST "${BASE_URL}/api/market/test-proxy" -H "$AUTH_HEADER" -H "Content-Type: application/json" -d '{"market_proxy":""}')
echo "Test connection: ${TEST_PROXY_RES}"
if ! echo "$TEST_PROXY_RES" | grep -q '"success":true'; then
    echo "FATAL: Connectivity test failed"
    exit 1
fi

echo "=== ALL MARKET INTEGRATION TESTS PASSED SUCCESSFULLY! ==="
