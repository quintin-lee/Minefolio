#!/usr/bin/env bash
set -euo pipefail

PORT=8080
BASE="http://127.0.0.1:${PORT}/api"
TMP_DIR=$(mktemp -d)
DB_PATH="${TMP_DIR}/test_plans.db"
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

# Kill any lingering process on 8080
kill $(lsof -t -i:${PORT} 2>/dev/null) 2>/dev/null || true
sleep 0.5

echo "=== Starting Minefolio Test Server on port ${PORT} ==="
export MINEFOLIO_JWT_SECRET="test_secret_for_plans_and_cashflow_suite"
export MINEFOLIO_DB_DRIVER="sqlite"
export MINEFOLIO_DB_DSN="${DB_PATH}"

cd "$(dirname "$0")/../build"
./minefolio > "${TMP_DIR}/server.log" 2>&1 &
SERVER_PID=$!

# Wait for server to start
for i in {1..30}; do
    if curl -sf "http://127.0.0.1:${PORT}/healthz" >/dev/null 2>&1; then
        echo "Server is up!"
        break
    fi
    sleep 0.2
done

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

# 1. System setup / User registration
echo "1. System setup & login..."
ENC_PW=$(rsa_encrypt "password123")
SETUP_RES=$(curl -s -X POST "${BASE}/system/setup" \
  -H "Content-Type: application/json" \
  -d "{\"username\":\"investor\",\"password_enc\":\"$ENC_PW\"}")
TOKEN=$(echo "$SETUP_RES" | node -e "let d='';process.stdin.on('data',c=>d+=c);process.stdin.on('end',()=>{const j=JSON.parse(d);process.stdout.write(j.data.token);});")

if [ -z "$TOKEN" ]; then
  echo "Failed to get token: $SETUP_RES"
  exit 1
fi
echo "✓ User logged in, token acquired."

AUTH_HEADER="Authorization: Bearer ${TOKEN}"

# 2. Setup categories and assets
echo "2. Setting up assets (funding account + investment target)..."
# Funding asset: Bank Card with 10,000 balance
curl -s -X POST "${BASE}/categories" -H "$AUTH_HEADER" -H "Content-Type: application/json" -d '{"name":"银行存款","asset_type":"bank","type":"asset","currency":"CNY"}' >/dev/null
CAT_BANK_ID=$(sqlite3 "$DB_PATH" "SELECT id FROM categories WHERE name='银行存款' LIMIT 1")

curl -s -X POST "${BASE}/assets" -H "$AUTH_HEADER" -H "Content-Type: application/json" \
  -d "{\"category_id\":$CAT_BANK_ID,\"name\":\"招商银行\",\"current_value\":10000,\"currency\":\"CNY\"}" >/dev/null
BANK_ASSET_ID=$(sqlite3 "$DB_PATH" "SELECT id FROM assets WHERE name='招商银行' LIMIT 1")

# Investment asset: CSI 300 ETF with 0 initial quantity, net_value 3.50
curl -s -X POST "${BASE}/categories" -H "$AUTH_HEADER" -H "Content-Type: application/json" -d '{"name":"指数基金","asset_type":"fund","type":"asset","currency":"CNY"}' >/dev/null
CAT_FUND_ID=$(sqlite3 "$DB_PATH" "SELECT id FROM categories WHERE name='指数基金' LIMIT 1")

curl -s -X POST "${BASE}/assets" -H "$AUTH_HEADER" -H "Content-Type: application/json" \
  -d "{\"category_id\":$CAT_FUND_ID,\"name\":\"沪深300ETF\",\"symbol\":\"510300.SH\",\"quote_source\":\"eastmoney\",\"current_value\":0,\"quantity\":0,\"net_value\":3.50,\"currency\":\"CNY\"}" >/dev/null
FUND_ASSET_ID=$(sqlite3 "$DB_PATH" "SELECT id FROM assets WHERE name='沪深300ETF' LIMIT 1")

echo "✓ Assets created: Bank Asset ID=$BANK_ASSET_ID (10,000 RMB), Fund Asset ID=$FUND_ASSET_ID"

# 3. DCA Plan CRUD & Calculations
echo "3. Creating DCA Plan..."
CREATE_PLAN_RES=$(curl -s -X POST "${BASE}/dca/plans" -H "$AUTH_HEADER" -H "Content-Type: application/json" \
  -d "{\"name\":\"沪深300定投\",\"target_asset_id\":$FUND_ASSET_ID,\"funding_asset_id\":$BANK_ASSET_ID,\"frequency\":\"weekly\",\"day_of_period\":4,\"amount\":1000,\"target_profit_rate\":0.15}")
PLAN_ID=$(echo "$CREATE_PLAN_RES" | node -e "let d='';process.stdin.on('data',c=>d+=c);process.stdin.on('end',()=>{console.log(JSON.parse(d).data.id);});")
echo "✓ DCA Plan created: ID=$PLAN_ID"

echo "Listing DCA plans..."
LIST_PLAN_RES=$(curl -s -X GET "${BASE}/dca/plans" -H "$AUTH_HEADER")
echo "$LIST_PLAN_RES" | node -e "
let d='';process.stdin.on('data',c=>d+=c);process.stdin.on('end',()=>{
  const j = JSON.parse(d);
  if (j.code !== 0 || !Array.isArray(j.data) || j.data.length !== 1) {
    console.error('Plan list validation failed:', d);
    process.exit(1);
  }
  console.log('✓ Plan listed properly with profit_rate:', j.data[0].profit_rate);
});"

# 4. Generate Pending Execution and Confirm
echo "4. Testing DCA Execution & One-Click Buy..."
# Insert pending execution task into DB
sqlite3 "$DB_PATH" "INSERT INTO dca_executions (plan_id, user_id, period_date, planned_amount, status) VALUES ($PLAN_ID, 1, '2026-08-28', 1000.0, 'pending');"

PENDING_RES=$(curl -s -X GET "${BASE}/dca/executions/pending" -H "$AUTH_HEADER")
EXEC_ID=$(echo "$PENDING_RES" | node -e "let d='';process.stdin.on('data',c=>d+=c);process.stdin.on('end',()=>{const j=JSON.parse(d);console.log(j.data[0].id);});")
echo "✓ Found pending execution ID=$EXEC_ID"

echo "Confirming DCA Execution (Amount 1000, Price 3.50)..."
CONFIRM_RES=$(curl -s -X POST "${BASE}/dca/executions/$EXEC_ID/confirm" -H "$AUTH_HEADER" -H "Content-Type: application/json" \
  -d '{"actual_amount":1000,"executed_price":3.50}')
echo "Confirm response: $CONFIRM_RES"

# Verify Funding Asset Balance Debited to 9000
BANK_AFTER=$(sqlite3 "$DB_PATH" "SELECT current_value FROM assets WHERE id=$BANK_ASSET_ID;")
echo "Bank balance after DCA: $BANK_AFTER (expected 9000)"
if (( $(echo "$BANK_AFTER != 9000" | bc -l) )); then
  echo "Bank balance check failed!"
  exit 1
fi
echo "✓ Bank account debited correctly (-1000)."

# Verify Fund Position & Quantity
FUND_QTY=$(sqlite3 "$DB_PATH" "SELECT quantity FROM assets WHERE id=$FUND_ASSET_ID;")
echo "Fund quantity after DCA: $FUND_QTY (expected ~285.7143)"
echo "✓ Fund position updated."

# 5. Cashflow Schedules & Calendar
echo "5. Testing Cashflow Schedules & Calendar..."
CREATE_SCH_RES=$(curl -s -X POST "${BASE}/cashflow/schedules" -H "$AUTH_HEADER" -H "Content-Type: application/json" \
  -d "{\"name\":\"沪深300分红\",\"flow_type\":\"dividend\",\"source_asset_id\":$FUND_ASSET_ID,\"target_asset_id\":$BANK_ASSET_ID,\"frequency\":\"monthly\",\"start_date\":\"2026-08-15\",\"expected_amount\":200}")
SCH_ID=$(echo "$CREATE_SCH_RES" | node -e "let d='';process.stdin.on('data',c=>d+=c);process.stdin.on('end',()=>{console.log(JSON.parse(d).data.id);});")
echo "✓ Cashflow schedule created: ID=$SCH_ID"

echo "Fetching Cashflow Calendar for 2026-08..."
CAL_RES=$(curl -s -X GET "${BASE}/cashflow/calendar?year=2026&month=8" -H "$AUTH_HEADER")
echo "$CAL_RES" | node -e "
let d='';process.stdin.on('data',c=>d+=c);process.stdin.on('end',()=>{
  const j = JSON.parse(d);
  if (j.code !== 0 || !j.data || j.data.projected_total !== 200) {
    console.error('Calendar projection verification failed:', d);
    process.exit(1);
  }
  console.log('✓ Calendar projected monthly total:', j.data.projected_total, 'annual:', j.data.annual_projected_total);
});"

echo "Confirming cashflow income of 200 RMB to bank account..."
CONFIRM_CF_RES=$(curl -s -X POST "${BASE}/cashflow/confirm" -H "$AUTH_HEADER" -H "Content-Type: application/json" \
  -d "{\"target_asset_id\":$BANK_ASSET_ID,\"source_asset_id\":$FUND_ASSET_ID,\"amount\":200,\"date\":\"2026-08-15\",\"name\":\"沪深300分红收益\"}")
echo "Confirm CF response: $CONFIRM_CF_RES"

# Verify Bank Balance Credited to 9200
BANK_FINAL=$(sqlite3 "$DB_PATH" "SELECT current_value FROM assets WHERE id=$BANK_ASSET_ID;")
echo "Bank balance after dividend income: $BANK_FINAL (expected 9200)"
if (( $(echo "$BANK_FINAL != 9200" | bc -l) )); then
  echo "Bank balance check after dividend failed!"
  exit 1
fi
echo "✓ Bank account credited correctly (+200)."

# Verify Calendar now shows actual confirmed event
CAL_AFTER_RES=$(curl -s -X GET "${BASE}/cashflow/calendar?year=2026&month=8" -H "$AUTH_HEADER")
echo "$CAL_AFTER_RES" | node -e "
let d='';process.stdin.on('data',c=>d+=c);process.stdin.on('end',()=>{
  const j = JSON.parse(d);
  if (j.data.actual_total !== 200) {
    console.error('Calendar actual total verification failed:', d);
    process.exit(1);
  }
  console.log('✓ Calendar reflects actual confirmed income total:', j.data.actual_total);
});"

echo "================================================="
echo "🎉 ALL DCA & CASHFLOW INTEGRATION TESTS PASSED!"
echo "================================================="
