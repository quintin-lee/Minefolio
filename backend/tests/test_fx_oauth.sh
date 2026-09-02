#!/usr/bin/env bash
set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BACKEND_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$BACKEND_DIR/build"
BIN="$BUILD_DIR/minefolio"

PORT=8089
BASE="http://127.0.0.1:$PORT/api"
PASS=0
FAIL=0
SERVER_PID=""

if [ ! -f "$BIN" ]; then
    echo "❌ Error: minefolio binary not found at $BIN. Build it first."
    exit 1
fi

assert_eq() {
    local desc="$1"
    local actual="$2"
    local expected="$3"
    if [ "$actual" = "$expected" ]; then
        echo "  ✅ $desc"
        PASS=$((PASS + 1))
    else
        echo "  ❌ $desc (expected: '$expected', got: '$actual')"
        FAIL=$((FAIL + 1))
    fi
}

assert_nonzero() {
    local desc="$1"
    local val="$2"
    if [ -n "$val" ] && [ "$val" != "0" ] && [ "$val" != "null" ]; then
        echo "  ✅ $desc ($val)"
        PASS=$((PASS + 1))
    else
        echo "  ❌ $desc (expected non-zero, got: '$val')"
        FAIL=$((FAIL + 1))
    fi
}

rsa_encrypt() {
  local plain="$1"
  node -e "
const http = require('http');
const req = http.get('$BASE/auth/public-key', (res) => {
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
      const arr = new Uint8Array(enc);
      process.stdout.write(btoa(String.fromCharCode(...arr))
        .replace(/\+/g,'-').replace(/\//g,'_').replace(/=+$/,''));
    } catch (e) {
      console.error('RSA encryption error:', e.message);
      process.exit(1);
    }
  });
});
req.on('error', (e) => {
  console.error('HTTP connect error:', e.message);
  process.exit(1);
});
"
}

DB_FILE=$(mktemp /tmp/minefolio_fx_oauth_XXXXXX.db)
cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill -9 "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -f "$DB_FILE" "$DB_FILE-wal" "$DB_FILE-shm"
}
trap cleanup EXIT INT TERM

# Kill any lingering process on PORT
kill $(lsof -t -i:${PORT} 2>/dev/null) 2>/dev/null || true
sleep 0.3

echo "=== Starting Test Server on port $PORT ==="
cd "$BUILD_DIR"
MINEFOLIO_DB_DRIVER=sqlite MINEFOLIO_DB_DSN="$DB_FILE" MINEFOLIO_PORT="$PORT" \
MINEFOLIO_JWT_SECRET="test_fx_oauth_secret_12345678" \
MINEFOLIO_OAUTH_GITHUB_CLIENT_ID="gh_client_id_test" \
"$BIN" > /tmp/minefolio_fx_oauth.log 2>&1 &
SERVER_PID=$!

SERVER_UP=0
for i in $(seq 1 30); do
    if curl -s "$BASE/system/status" > /dev/null 2>&1; then
        SERVER_UP=1
        break
    fi
    sleep 0.2
done

if [ "$SERVER_UP" -eq 0 ]; then
    echo "❌ Server failed to start on port $PORT"
    cat /tmp/minefolio_fx_oauth.log 2>/dev/null || true
    exit 1
fi

echo "=== 1. System Setup ==="
ENC_PASS=$(rsa_encrypt "password123")
SETUP_RES=$(curl -s -X POST "$BASE/system/setup" \
    -H "Content-Type: application/json" \
    -d "{\"username\":\"admin\",\"password_enc\":\"$ENC_PASS\"}")
TOKEN=$(echo "$SETUP_RES" | jq -r '.data.token')
assert_nonzero "Admin token obtained" "$TOKEN"

echo "=== 2. Test FX Rates & History Endpoints ==="
FX_RES=$(curl -s -H "Authorization: Bearer $TOKEN" "$BASE/market/exchange-rates")
FX_CODE=$(echo "$FX_RES" | jq -r '.code | floor')
assert_eq "GET /api/market/exchange-rates returns code 0" "$FX_CODE" "0"

USD_RATE=$(echo "$FX_RES" | jq -r '.data.USD')
assert_nonzero "USD rate exists" "$USD_RATE"

# Update USD rate
UPD_RES=$(curl -s -X POST "$BASE/market/exchange-rates" \
    -H "Authorization: Bearer $TOKEN" \
    -H "Content-Type: application/json" \
    -d '{"currency":"USD","rate":7.35}')
UPD_CODE=$(echo "$UPD_RES" | jq -r '.code | floor')
assert_eq "POST /api/market/exchange-rates returns code 0" "$UPD_CODE" "0"

# Verify updated USD rate
FX_RES2=$(curl -s -H "Authorization: Bearer $TOKEN" "$BASE/market/fx-rates")
USD_RATE2=$(echo "$FX_RES2" | jq -r '.data.USD')
assert_eq "USD rate updated to 7.35" "$USD_RATE2" "7.35"

# Verify historical FX rate curve
FX_HIST=$(curl -s -H "Authorization: Bearer $TOKEN" "$BASE/market/fx-history?currency=USD&days=30")
HIST_CODE=$(echo "$FX_HIST" | jq -r '.code | floor')
assert_eq "GET /api/market/fx-history returns code 0" "$HIST_CODE" "0"
HIST_LEN=$(echo "$FX_HIST" | jq '.data | length')
assert_nonzero "FX history contains points" "$HIST_LEN"

echo "=== 3. Test Multi-Currency Summary & FX Gain/Loss Report ==="
# Create USD asset and CNY asset
sqlite3 "$DB_FILE" "INSERT INTO categories (user_id, name, type, asset_type, currency) VALUES (1, '美股账户', 'asset', 'stock', 'USD');"
sqlite3 "$DB_FILE" "INSERT INTO assets (user_id, category_id, name, current_value, net_value, quantity, cost_basis, currency, ledger_id) VALUES (1, 1, 'Apple Stock', 1500, 150, 10, 1200, 'USD', 1);"
sqlite3 "$DB_FILE" "INSERT INTO assets (user_id, category_id, name, current_value, net_value, quantity, cost_basis, currency, ledger_id) VALUES (1, 1, '招商银行', 50000, 50000, 1, 50000, 'CNY', 1);"

SUMMARY_RES=$(curl -s -H "Authorization: Bearer $TOKEN" "$BASE/reports/multi-currency-summary?base_currency=CNY")
S_CODE=$(echo "$SUMMARY_RES" | jq -r '.code | floor')
assert_eq "Multi-currency report returns code 0" "$S_CODE" "0"

HAS_USD=$(echo "$SUMMARY_RES" | jq -r '.data.currencies[] | select(.currency == "USD") | .currency')
assert_eq "Report contains USD bucket" "$HAS_USD" "USD"

HAS_CNY=$(echo "$SUMMARY_RES" | jq -r '.data.currencies[] | select(.currency == "CNY") | .currency')
assert_eq "Report contains CNY bucket" "$HAS_CNY" "CNY"

# FX Gain/Loss decomposition report
FX_PNL_RES=$(curl -s -H "Authorization: Bearer $TOKEN" "$BASE/reports/fx-pnl?base_currency=CNY")
FX_PNL_CODE=$(echo "$FX_PNL_RES" | jq -r '.code | floor')
assert_eq "GET /api/reports/fx-pnl returns code 0" "$FX_PNL_CODE" "0"

FX_PNL_ASSET=$(echo "$FX_PNL_RES" | jq -r '.data.assets[0].asset_name')
assert_eq "FX PnL includes Apple Stock" "$FX_PNL_ASSET" "Apple Stock"

COMBINED_PNL=$(echo "$FX_PNL_RES" | jq -r '.data.total_combined_pnl_base')
assert_nonzero "Total combined PnL is computed" "$COMBINED_PNL"

echo "=== 4. Test Receipt OCR Scanning ==="
# Mock receipt image base64
DUMMY_IMG="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg=="
SCAN_RES=$(curl -s -X POST "$BASE/receipts/scan" \
    -H "Authorization: Bearer $TOKEN" \
    -H "Content-Type: application/json" \
    -d "{\"image\":\"$DUMMY_IMG\"}")
SCAN_CODE=$(echo "$SCAN_RES" | jq -r '.code | floor')
assert_eq "POST /api/receipts/scan returns code 0" "$SCAN_CODE" "0"

SCAN_AMT=$(echo "$SCAN_RES" | jq -r '.data.amount')
assert_nonzero "Receipt scan recognized amount" "$SCAN_AMT"

echo "=== 5. Test OAuth Providers & Callback ==="
PROV_RES=$(curl -s "$BASE/auth/oauth/providers")
P_CODE=$(echo "$PROV_RES" | jq -r '.code | floor')
assert_eq "GET /api/auth/oauth/providers returns code 0" "$P_CODE" "0"

HAS_GH=$(echo "$PROV_RES" | jq -r '.data.providers[0].id')
assert_eq "Providers contains GitHub" "$HAS_GH" "github"

# Test OAuth Callback user provision
CALLBACK_RES=$(curl -s -X POST "$BASE/auth/oauth/callback" \
    -H "Content-Type: application/json" \
    -d '{"provider":"github","oauth_id":"gh_user_9988","username":"octocat"}')
CB_CODE=$(echo "$CALLBACK_RES" | jq -r '.code | floor')
assert_eq "OAuth callback creates user and returns code 0" "$CB_CODE" "0"

CB_TOKEN=$(echo "$CALLBACK_RES" | jq -r '.data.token')
assert_nonzero "OAuth login returns JWT token" "$CB_TOKEN"

# Test OAuth login with same user retrieves existing user
CALLBACK_RES2=$(curl -s -X POST "$BASE/auth/oauth/callback" \
    -H "Content-Type: application/json" \
    -d '{"provider":"github","oauth_id":"gh_user_9988"}')
CB_CODE2=$(echo "$CALLBACK_RES2" | jq -r '.code | floor')
assert_eq "OAuth callback for existing user returns code 0" "$CB_CODE2" "0"

echo ""
echo "Summary: PASS=$PASS FAIL=$FAIL"
if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
