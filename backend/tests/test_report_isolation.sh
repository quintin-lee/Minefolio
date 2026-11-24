#!/usr/bin/env bash
# Regression test for expense monthly/report isolation by user_id + ledger_id.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/db_helper.sh"

db_init_context 8182 "report_isolation"
BASE="http://127.0.0.1:${PORT}/api"
TMP_DIR=$(mktemp -d)
DB_PATH="$DB_FILE"
export MINEFOLIO_JWT_SECRET="report_isolation_test_secret_32_bytes_minimum"
export TEST_JWT_SECRET="$MINEFOLIO_JWT_SECRET"

cleanup() {
    local rc=$?
    db_stop_server
    if [ "$rc" -ne 0 ] && [ -f "${TMP_DIR}/server.log" ]; then
        tail -n 40 "${TMP_DIR}/server.log" || true
    fi
    db_clean
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT

db_clean
db_start_server "$PORT" "${MINEFOLIO_BUILD_DIR:-$SCRIPT_DIR/../build}" "${TMP_DIR}/server.log"

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

json_num() { echo "$1" | jq -r "$2"; }

SETUP=$(curl -s -X POST "$BASE/system/setup" -H 'Content-Type: application/json' \
  -d "{\"username\":\"report-owner\",\"password_enc\":\"$(rsa_encrypt 'password123')\"}")
TOKEN_A=$(json_num "$SETUP" '.data.token')
UID_A=$(sqlite3 "$DB_PATH" "SELECT id FROM users WHERE username='report-owner'")

# Create a second user directly for a deterministic signed test token.
sqlite3 "$DB_PATH" "INSERT INTO users (username, password) VALUES ('report-member', 'dummyhash')"
UID_B=$(sqlite3 "$DB_PATH" "SELECT id FROM users WHERE username='report-member'")
TOKEN_B=$(node -e "
const crypto = require('crypto');
const header = Buffer.from(JSON.stringify({alg:'HS256',typ:'JWT'})).toString('base64url');
const now = Math.floor(Date.now()/1000);
const payload = Buffer.from(JSON.stringify({sub: $UID_B, user_id: $UID_B, v: 0, exp: now + 604800, iat: now})).toString('base64url');
const sig = crypto.createHmac('sha256', process.env.MINEFOLIO_JWT_SECRET).update(header + '.' + payload).digest('base64url');
process.stdout.write(header + '.' + payload + '.' + sig);
")

DEFAULT_LEDGER=$(sqlite3 "$DB_PATH" "SELECT id FROM ledgers WHERE owner_id=$UID_A AND is_default=1")
CREATE_LEDGER=$(curl -s -X POST "$BASE/ledgers" \
  -H "Authorization: Bearer $TOKEN_A" -H 'Content-Type: application/json' \
  -d '{"name":"Report Family Ledger","currency":"CNY"}')
FAMILY_LEDGER=$(json_num "$CREATE_LEDGER" '.data.id | tonumber | floor')

# Make user B a member of the second ledger so both users can query the same scope.
sqlite3 "$DB_PATH" "INSERT INTO ledger_members (ledger_id, user_id, role) VALUES ($FAMILY_LEDGER, $UID_B, 'editor')"

# Seed three deliberately distinct scopes. The current implementation does not populate
# ledger_id on normal expense writes yet, so the fixture writes it explicitly to exercise
# the read boundary independently.
SEED_SQL=$(cat <<SQL
INSERT INTO categories (user_id, ledger_id, name, type, currency) VALUES
  ($UID_A, $DEFAULT_LEDGER, 'Report-A-Category', 'expense', 'CNY'),
  ($UID_A, $FAMILY_LEDGER, 'Report-Family-Category', 'expense', 'CNY'),
  ($UID_B, $FAMILY_LEDGER, 'Report-B-Category', 'expense', 'CNY');
INSERT INTO assets (user_id, ledger_id, category_id, name, current_value, currency)
  SELECT $UID_A, $DEFAULT_LEDGER, id, 'Report-A-Asset', 0, 'CNY' FROM categories WHERE name='Report-A-Category';
INSERT INTO assets (user_id, ledger_id, category_id, name, current_value, currency)
  SELECT $UID_A, $FAMILY_LEDGER, id, 'Report-Family-Asset', 0, 'CNY' FROM categories WHERE name='Report-Family-Category';
INSERT INTO assets (user_id, ledger_id, category_id, name, current_value, currency)
  SELECT $UID_B, $FAMILY_LEDGER, id, 'Report-B-Asset', 0, 'CNY' FROM categories WHERE name='Report-B-Category';
INSERT INTO daily_expenses (user_id, ledger_id, category_id, asset_id, expense_type, amount, currency, expense_date, note)
  SELECT $UID_A, $DEFAULT_LEDGER, c.id, a.id, 'expense', 10, 'CNY', '2026-08-01', 'scope-a'
    FROM categories c JOIN assets a ON a.category_id=c.id WHERE c.name='Report-A-Category';
INSERT INTO daily_expenses (user_id, ledger_id, category_id, asset_id, expense_type, amount, currency, expense_date, note)
  SELECT $UID_A, $FAMILY_LEDGER, c.id, a.id, 'expense', 20, 'CNY', '2026-08-02', 'scope-family'
    FROM categories c JOIN assets a ON a.category_id=c.id WHERE c.name='Report-Family-Category';
INSERT INTO daily_expenses (user_id, ledger_id, category_id, asset_id, expense_type, amount, currency, expense_date, note)
  SELECT $UID_B, $FAMILY_LEDGER, c.id, a.id, 'expense', 30, 'CNY', '2026-08-03', 'scope-b'
    FROM categories c JOIN assets a ON a.category_id=c.id WHERE c.name='Report-B-Category';
SQL
)
db_exec "$SEED_SQL"

assert_num() {
    local description="$1" expected="$2" actual="$3"
    if [ "$expected" != "$actual" ]; then
        echo "FAIL: $description: expected=$expected actual=$actual"
        exit 1
    fi
    echo "PASS: $description"
}

report_total() {
    local token="$1" ledger="$2" path="$3"
    curl -s -H "Authorization: Bearer $token" -H "X-Ledger-Id: $ledger" "$BASE$path" | jq -r '(.data.total_expense // 0) | floor'
}

report_category_count() {
    local token="$1" ledger="$2" path="$3"
    curl -s -H "Authorization: Bearer $token" -H "X-Ledger-Id: $ledger" "$BASE$path" | jq -r '[.data.by_category[]? // .data.items[]?] | length'
}

# Ledger aggregation: in a collaborative ledger, all members see the combined total.
# owner default ledger monthly total = 10 (only user A's data in default ledger)
assert_num "owner default ledger monthly total" "10" "$(report_total "$TOKEN_A" "$DEFAULT_LEDGER" '/reports/expense/monthly?year=2026&month=8')"
# owner family ledger: user A (20) + user B (30) = 50 (aggregated)
assert_num "owner family ledger monthly total" "50" "$(report_total "$TOKEN_A" "$FAMILY_LEDGER" '/reports/expense/monthly?year=2026&month=8')"
# member family ledger: user A (20) + user B (30) = 50 (aggregated)
assert_num "member family ledger monthly total" "50" "$(report_total "$TOKEN_B" "$FAMILY_LEDGER" '/reports/expense/monthly?year=2026&month=8')"
assert_num "owner default category count" "1" "$(report_category_count "$TOKEN_A" "$DEFAULT_LEDGER" '/reports/expense/monthly?year=2026&month=8')"
# family ledger has 2 categories (one per user)
assert_num "owner family category count" "2" "$(report_category_count "$TOKEN_A" "$FAMILY_LEDGER" '/reports/expense/monthly?year=2026&month=8')"

echo "All report isolation tests passed"
