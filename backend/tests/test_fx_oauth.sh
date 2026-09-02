#!/bin/bash
set -e

PORT=8089
BASE="http://127.0.0.1:$PORT/api"
PASS=0
FAIL=0

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

DB_FILE=$(mktemp /tmp/minefolio_fx_oauth_XXXXXX.db)
cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill -9 "$SERVER_PID" 2>/dev/null || true
    fi
    rm -f "$DB_FILE"
}
trap cleanup EXIT

echo "=== Starting Test Server on port $PORT ==="
MINEFOLIO_DB_DRIVER=sqlite MINEFOLIO_DB_DSN="$DB_FILE" MINEFOLIO_PORT="$PORT" \
MINEFOLIO_JWT_SECRET="test_fx_oauth_secret_12345678" \
MINEFOLIO_OAUTH_GITHUB_CLIENT_ID="gh_client_id_test" \
./backend/build/minefolio > /dev/null 2>&1 &
SERVER_PID=$!

for i in $(seq 1 30); do
    if curl -s "$BASE/system/status" > /dev/null 2>&1; then
        break
    fi
    sleep 0.1
done

echo "=== 1. System Setup ==="
ENC_PASS=$(rsa_encrypt "password123")
SETUP_RES=$(curl -s -X POST "$BASE/system/setup" \
    -H "Content-Type: application/json" \
    -d "{\"username\":\"admin\",\"password_enc\":\"$ENC_PASS\"}")
TOKEN=$(echo "$SETUP_RES" | jq -r '.data.token')
assert_nonzero "Admin token obtained" "$TOKEN"

echo "=== 2. Test FX Rates Endpoints ==="
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

echo "=== 3. Test Multi-Currency Summary Report ==="
# Create USD asset and CNY asset
sqlite3 "$DB_FILE" "INSERT INTO categories (user_id, name, type, asset_type, currency) VALUES (1, '美股账户', 'asset', 'stock', 'USD');"
sqlite3 "$DB_FILE" "INSERT INTO assets (user_id, category_id, name, current_value, net_value, quantity, currency, ledger_id) VALUES (1, 1, 'Apple Stock', 0, 150, 10, 'USD', 1);"
sqlite3 "$DB_FILE" "INSERT INTO assets (user_id, category_id, name, current_value, net_value, quantity, currency, ledger_id) VALUES (1, 1, '招商银行', 50000, 50000, 1, 'CNY', 1);"

SUMMARY_RES=$(curl -s -H "Authorization: Bearer $TOKEN" "$BASE/reports/multi-currency-summary?base_currency=CNY")
S_CODE=$(echo "$SUMMARY_RES" | jq -r '.code | floor')
assert_eq "Multi-currency report returns code 0" "$S_CODE" "0"

HAS_USD=$(echo "$SUMMARY_RES" | jq -r '.data.currencies[] | select(.currency == "USD") | .currency')
assert_eq "Report contains USD bucket" "$HAS_USD" "USD"

HAS_CNY=$(echo "$SUMMARY_RES" | jq -r '.data.currencies[] | select(.currency == "CNY") | .currency')
assert_eq "Report contains CNY bucket" "$HAS_CNY" "CNY"

echo "=== 4. Test OAuth Providers & Callback ==="
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
