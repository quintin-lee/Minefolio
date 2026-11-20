#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/db_helper.sh"

PORT=${MINEFOLIO_PORT:-18088}
db_init_context "$PORT" "ledgers"
BASE="http://127.0.0.1:${PORT}/api"
TMP_DIR=$(mktemp -d)
DB_PATH="$DB_FILE"
export MINEFOLIO_JWT_SECRET="test_secret_for_ledgers_suite"
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

# 1. Setup Alice (admin)
PASS_ENC_A=$(rsa_encrypt "Password123!")
AUTH_A=$(curl -s -X POST "$BASE/system/setup" \
  -H "Content-Type: application/json" \
  -d "{\"username\":\"alice\",\"password_enc\":\"$PASS_ENC_A\"}")
TOKEN_A=$(echo "$AUTH_A" | jq -r '.data.token')
USER_A_ID=$(sqlite3 "$DB_PATH" "SELECT id FROM users WHERE username='alice'")
echo "Alice setup OK (ID: $USER_A_ID, Token: $TOKEN_A)"

# 2. Insert Bob and create his token
sqlite3 "$DB_PATH" "INSERT INTO users (username, password) VALUES ('bob', 'dummyhash')"
USER_B_ID=$(sqlite3 "$DB_PATH" "SELECT id FROM users WHERE username='bob'")
TOKEN_B=$(node -e "
const crypto = require('crypto');
const header = Buffer.from(JSON.stringify({alg:'HS256',typ:'JWT'})).toString('base64url');
const now = Math.floor(Date.now()/1000);
const payload = Buffer.from(JSON.stringify({sub: $USER_B_ID, user_id: $USER_B_ID, token_version: 0, exp: now + 604800, iat: now})).toString('base64url');
const secret = '$MINEFOLIO_JWT_SECRET';
const sig = crypto.createHmac('sha256', secret).update(header + '.' + payload).digest('base64url');
process.stdout.write(header + '.' + payload + '.' + sig);
")
echo "Bob created OK (ID: $USER_B_ID)"

# 3. Check Alice's default ledger
LEDGERS_A=$(curl -s -X GET "$BASE/ledgers" -H "Authorization: Bearer $TOKEN_A")
DEF_LID_A=$(echo "$LEDGERS_A" | jq -r '.data[0].id | floor')
DEF_NAME_A=$(echo "$LEDGERS_A" | jq -r '.data[0].name')
test "$DEF_NAME_A" = "默认账本"
echo "Alice has default ledger: $DEF_LID_A ($DEF_NAME_A)"

# 4. Alice creates a new family ledger
CREATE_RES=$(curl -s -X POST "$BASE/ledgers" \
  -H "Authorization: Bearer $TOKEN_A" \
  -H "Content-Type: application/json" \
  -d '{"name":"家庭公共账本","description":"家庭资产与日常开销","currency":"CNY","color":"#10b981","icon":"ph:house"}')
FAM_LID=$(echo "$CREATE_RES" | jq -r '.data.id | floor')
echo "Alice created family ledger: $FAM_LID"

# 5. Alice generates an invite code
INV_RES=$(curl -s -X POST "$BASE/ledgers/${FAM_LID}/invite-code" \
  -H "Authorization: Bearer $TOKEN_A")
INV_CODE=$(echo "$INV_RES" | jq -r '.data.invite_code')
echo "Alice generated invite code: $INV_CODE"
test -n "$INV_CODE"

# 6. Bob joins Alice's family ledger via invite code
JOIN_RES=$(curl -s -X POST "$BASE/ledgers/join" \
  -H "Authorization: Bearer $TOKEN_B" \
  -H "Content-Type: application/json" \
  -d "{\"invite_code\":\"$INV_CODE\"}")
JOIN_LID=$(echo "$JOIN_RES" | jq -r '.data.id | floor')
test "$JOIN_LID" = "$FAM_LID"
echo "Bob joined family ledger OK!"

# 7. Bob checks his ledger list (should have 2: his default + Alice's family ledger)
LEDGERS_B=$(curl -s -X GET "$BASE/ledgers" -H "Authorization: Bearer $TOKEN_B")
echo "LEDGERS_B=$LEDGERS_B"
B_COUNT=$(echo "$LEDGERS_B" | jq '.data | length')
test "$B_COUNT" -ge 2
echo "Bob now has $B_COUNT ledgers"

# 8. Alice lists members of the family ledger
MEMBERS=$(curl -s -X GET "$BASE/ledgers/${FAM_LID}/members" \
  -H "Authorization: Bearer $TOKEN_A")
BOB_MEM=$(echo "$MEMBERS" | jq -r ".data[] | select((.user_id|floor|tostring) == \"$USER_B_ID\")")
BOB_ROLE=$(echo "$BOB_MEM" | jq -r '.role')
test "$BOB_ROLE" = "editor"
echo "Bob is member with role: $BOB_ROLE"

# 9. Alice updates Bob's role to viewer
UPD_ROLE=$(curl -s -X PUT "$BASE/ledgers/${FAM_LID}/members/${USER_B_ID}" \
  -H "Authorization: Bearer $TOKEN_A" \
  -H "Content-Type: application/json" \
  -d '{"role":"viewer"}')
UPD_CODE=$(echo "$UPD_ROLE" | jq -r '.code | floor')
test "$UPD_CODE" = "0"
echo "Alice updated Bob to viewer role"

# 10. Verify Bob is now viewer
MEMBERS_2=$(curl -s -X GET "$BASE/ledgers/${FAM_LID}/members" \
  -H "Authorization: Bearer $TOKEN_B")
BOB_ROLE_2=$(echo "$MEMBERS_2" | jq -r ".data[] | select((.user_id|floor|tostring) == \"$USER_B_ID\") | .role")
test "$BOB_ROLE_2" = "viewer"
echo "Bob role verified as: $BOB_ROLE_2"

# 10b. RBAC: Bob as viewer attempts write operations -> MUST be rejected with 1004 Forbidden
VIEWER_WRITE_EXP=$(curl -s -X POST "$BASE/daily-expenses" \
  -H "Authorization: Bearer $TOKEN_B" \
  -H "X-Ledger-Id: ${FAM_LID}" \
  -H "Content-Type: application/json" \
  -d '{"category_id":1,"asset_id":1,"expense_type":"expense","amount":100,"expense_date":"2026-09-02"}')
VIEWER_EXP_CODE=$(echo "$VIEWER_WRITE_EXP" | jq -r '.code | floor')
test "$VIEWER_EXP_CODE" = "1004"
echo "✅ RBAC: Viewer write to daily-expenses blocked (code 1004)"

VIEWER_WRITE_TX=$(curl -s -X POST "$BASE/transactions" \
  -H "Authorization: Bearer $TOKEN_B" \
  -H "X-Ledger-Id: ${FAM_LID}" \
  -H "Content-Type: application/json" \
  -d '{"category_id":1,"asset_id":1,"type":"expense","amount":100,"transaction_date":"2026-09-02"}')
VIEWER_TX_CODE=$(echo "$VIEWER_WRITE_TX" | jq -r '.code | floor')
test "$VIEWER_TX_CODE" = "1004"
echo "✅ RBAC: Viewer write to transactions blocked (code 1004)"

VIEWER_WRITE_ASSET=$(curl -s -X POST "$BASE/assets" \
  -H "Authorization: Bearer $TOKEN_B" \
  -H "X-Ledger-Id: ${FAM_LID}" \
  -H "Content-Type: application/json" \
  -d '{"name":"非法只读创建","category_id":1,"value":500}')
VIEWER_ASSET_CODE=$(echo "$VIEWER_WRITE_ASSET" | jq -r '.code | floor')
test "$VIEWER_ASSET_CODE" = "1004"
echo "✅ RBAC: Viewer write to assets blocked (code 1004)"

# 10c. Alice promotes Bob back to editor -> Bob writes successfully
PROMOTE_RES=$(curl -s -X PUT "$BASE/ledgers/${FAM_LID}/members/${USER_B_ID}" \
  -H "Authorization: Bearer $TOKEN_A" \
  -H "Content-Type: application/json" \
  -d '{"role":"editor"}')
test "$(echo "$PROMOTE_RES" | jq -r '.code | floor')" = "0"
echo "Alice promoted Bob to editor"

# 10d. Test Ledger Data Isolation across Portfolio & Asset Reports
# Seed asset in Alice's default ledger
sqlite3 "$DB_PATH" "INSERT INTO assets (user_id, category_id, name, current_value, net_value, quantity, cost_basis, currency, ledger_id) VALUES ($USER_A_ID, 1, 'Alice Personal Stock', 10000, 100, 100, 8000, 'CNY', $DEF_LID_A);"
# Seed asset in Family ledger
sqlite3 "$DB_PATH" "INSERT INTO assets (user_id, category_id, name, current_value, net_value, quantity, cost_basis, currency, ledger_id) VALUES ($USER_A_ID, 1, 'Family Shared Fund', 5000, 50, 100, 4000, 'CNY', $FAM_LID);"

# Query Alice's default ledger summary
DEF_SUMMARY=$(curl -s -H "Authorization: Bearer $TOKEN_A" -H "X-Ledger-Id: $DEF_LID_A" "$BASE/reports/asset/summary")
DEF_ASSETS=$(echo "$DEF_SUMMARY" | jq -r '.data.total_assets | floor')
test "$DEF_ASSETS" = "10000"
echo "✅ Default ledger total assets isolated: $DEF_ASSETS"

# Query Family ledger summary
FAM_SUMMARY=$(curl -s -H "Authorization: Bearer $TOKEN_A" -H "X-Ledger-Id: $FAM_LID" "$BASE/reports/asset/summary")
FAM_ASSETS=$(echo "$FAM_SUMMARY" | jq -r '.data.total_assets | floor')
test "$FAM_ASSETS" = "5000"
echo "✅ Family ledger total assets isolated: $FAM_ASSETS"

# Query Bob's view of Family ledger dashboard (Bob is member of Family ledger)
BOB_DASH=$(curl -s -H "Authorization: Bearer $TOKEN_B" -H "X-Ledger-Id: $FAM_LID" "$BASE/summary")
BOB_DASH_CODE=$(echo "$BOB_DASH" | jq -r '.code | floor')
test "$BOB_DASH_CODE" = "0"
echo "✅ Bob accesses Family ledger dashboard OK"

# Query Bob's view of holdings for Family ledger
BOB_HOLD=$(curl -s -H "Authorization: Bearer $TOKEN_B" -H "X-Ledger-Id: $FAM_LID" "$BASE/reports/holdings")
BOB_HOLD_CODE=$(echo "$BOB_HOLD" | jq -r '.code | floor')
test "$BOB_HOLD_CODE" = "0"
echo "✅ Bob accesses Family ledger holdings OK"

# -------------------------------------------------------------
# 10.5 Account-level & Field-level RBAC Tests
# -------------------------------------------------------------
echo "=== Testing Account-level & Field-level RBAC ==="

# Alice creates Account A and Account B in Family ledger
CR_ACCT_A=$(curl -s -X POST "$BASE/accounts" \
  -H "Authorization: Bearer $TOKEN_A" \
  -H "X-Ledger-Id: $FAM_LID" \
  -H "Content-Type: application/json" \
  -d '{"name":"招行家庭卡","institution":"bank_card","account_no":"6225888812345678","currency":"CNY"}')
test "$(echo "$CR_ACCT_A" | jq -r '.code | floor')" = "0"
ACCT_A_ID=$(echo "$CR_ACCT_A" | jq -r '.data.id')

CR_ACCT_B=$(curl -s -X POST "$BASE/accounts" \
  -H "Authorization: Bearer $TOKEN_A" \
  -H "X-Ledger-Id: $FAM_LID" \
  -H "Content-Type: application/json" \
  -d '{"name":"私密金库账户","institution":"cash_wallet","account_no":"SECRET999","currency":"CNY"}')
test "$(echo "$CR_ACCT_B" | jq -r '.code | floor')" = "0"
ACCT_B_ID=$(echo "$CR_ACCT_B" | jq -r '.data.id')

# Alice creates Asset A under Account A
CR_ASSET_A=$(curl -s -X POST "$BASE/assets" \
  -H "Authorization: Bearer $TOKEN_A" \
  -H "X-Ledger-Id: $FAM_LID" \
  -H "Content-Type: application/json" \
  -d "{\"name\":\"家庭活期\",\"category_id\":1,\"account_id\":$ACCT_A_ID,\"current_value\":3000}")
test "$(echo "$CR_ASSET_A" | jq -r '.code | floor')" = "0"

# Alice configures Bob's permissions:
# Account A: viewer, mask_balance=1, mask_account_no=1
SET_PERM_A=$(curl -s -X PUT "$BASE/ledgers/${FAM_LID}/accounts/${ACCT_A_ID}/permissions" \
  -H "Authorization: Bearer $TOKEN_A" \
  -H "Content-Type: application/json" \
  -d "{\"permissions\":[{\"user_id\":$USER_B_ID,\"permission_level\":\"viewer\",\"mask_balance\":1,\"mask_account_no\":1}]}")
test "$(echo "$SET_PERM_A" | jq -r '.code | floor')" = "0"

# Account B: none (completely hidden from Bob)
SET_PERM_B=$(curl -s -X PUT "$BASE/ledgers/${FAM_LID}/accounts/${ACCT_B_ID}/permissions" \
  -H "Authorization: Bearer $TOKEN_A" \
  -H "Content-Type: application/json" \
  -d "{\"permissions\":[{\"user_id\":$USER_B_ID,\"permission_level\":\"none\",\"mask_balance\":0,\"mask_account_no\":0}]}")
test "$(echo "$SET_PERM_B" | jq -r '.code | floor')" = "0"

# Bob queries accounts in Family ledger
BOB_ACCOUNTS=$(curl -s -H "Authorization: Bearer $TOKEN_B" -H "X-Ledger-Id: $FAM_LID" "$BASE/accounts")
test "$(echo "$BOB_ACCOUNTS" | jq -r '.code | floor')" = "0"

# Verify Account A is masked for Bob
BOB_ACCT_A_MASK_BAL=$(echo "$BOB_ACCOUNTS" | jq -r ".data[] | select((.id|tostring) == \"$ACCT_A_ID\") | .is_balance_masked")
test "$BOB_ACCT_A_MASK_BAL" = "true"
BOB_ACCT_A_MASK_NO=$(echo "$BOB_ACCOUNTS" | jq -r ".data[] | select((.id|tostring) == \"$ACCT_A_ID\") | .is_account_no_masked")
test "$BOB_ACCT_A_MASK_NO" = "true"
BOB_ACCT_A_VAL=$(echo "$BOB_ACCOUNTS" | jq -r ".data[] | select((.id|tostring) == \"$ACCT_A_ID\") | .total_value")
test "$BOB_ACCT_A_VAL" = "0" -o "$BOB_ACCT_A_VAL" = "0.0" -o "$BOB_ACCT_A_VAL" = "0.00"
BOB_ACCT_A_NO=$(echo "$BOB_ACCOUNTS" | jq -r ".data[] | select((.id|tostring) == \"$ACCT_A_ID\") | .account_no")
test "$BOB_ACCT_A_NO" = "************5678"
echo "✅ Account RBAC: Bob sees Account A with masked balance and masked account_no"

# Verify Account B is completely hidden from Bob
BOB_ACCT_B=$(echo "$BOB_ACCOUNTS" | jq -r ".data[] | select((.id|tostring) == \"$ACCT_B_ID\") | .id")
test -z "$BOB_ACCT_B"
echo "✅ Account RBAC: Account B ('none' permission) is completely hidden from Bob"

# Verify Bob cannot create asset under Account A when role is viewer
BOB_ASSET_FAIL=$(curl -s -X POST "$BASE/assets" \
  -H "Authorization: Bearer $TOKEN_B" \
  -H "X-Ledger-Id: $FAM_LID" \
  -H "Content-Type: application/json" \
  -d "{\"name\":\"Bob活期卡\",\"category_id\":1,\"account_id\":$ACCT_A_ID,\"current_value\":500}")
test "$(echo "$BOB_ASSET_FAIL" | jq -r '.code | floor')" = "1004"
echo "✅ Account RBAC: Bob creating asset under viewer Account A rejected with 1004"

# Alice grants Bob operator permission on Account A
SET_PERM_A_OP=$(curl -s -X PUT "$BASE/ledgers/${FAM_LID}/accounts/${ACCT_A_ID}/permissions" \
  -H "Authorization: Bearer $TOKEN_A" \
  -H "Content-Type: application/json" \
  -d "{\"permissions\":[{\"user_id\":$USER_B_ID,\"permission_level\":\"operator\",\"mask_balance\":1,\"mask_account_no\":1}]}")
test "$(echo "$SET_PERM_A_OP" | jq -r '.code | floor')" = "0"

# Now Bob creates his asset under Account A
BOB_ASSET_OK=$(curl -s -X POST "$BASE/assets" \
  -H "Authorization: Bearer $TOKEN_B" \
  -H "X-Ledger-Id: $FAM_LID" \
  -H "Content-Type: application/json" \
  -d "{\"name\":\"Bob活期卡\",\"category_id\":1,\"account_id\":$ACCT_A_ID,\"current_value\":500}")
test "$(echo "$BOB_ASSET_OK" | jq -r '.code | floor')" = "0"
BOB_ASSETS_LIST=$(curl -s -H "Authorization: Bearer $TOKEN_B" -H "X-Ledger-Id: $FAM_LID" "$BASE/assets")
BOB_ASSET_ID=$(echo "$BOB_ASSETS_LIST" | jq -r '.data.list[] | select(.name == "Bob活期卡") | .id')
test -n "$BOB_ASSET_ID" && test "$BOB_ASSET_ID" -gt 0

# Alice updates Bob back to viewer on Account A
SET_PERM_A_VIEW=$(curl -s -X PUT "$BASE/ledgers/${FAM_LID}/accounts/${ACCT_A_ID}/permissions" \
  -H "Authorization: Bearer $TOKEN_A" \
  -H "Content-Type: application/json" \
  -d "{\"permissions\":[{\"user_id\":$USER_B_ID,\"permission_level\":\"viewer\",\"mask_balance\":1,\"mask_account_no\":1}]}")
test "$(echo "$SET_PERM_A_VIEW" | jq -r '.code | floor')" = "0"

# Verify Bob cannot write daily-expense on Account A (viewer)
BOB_WRITE_FAIL=$(curl -s -X POST "$BASE/daily-expenses" \
  -H "Authorization: Bearer $TOKEN_B" \
  -H "X-Ledger-Id: $FAM_LID" \
  -H "Content-Type: application/json" \
  -d "{\"category_id\":1,\"amount\":100,\"expense_type\":\"expense\",\"asset_id\":$BOB_ASSET_ID,\"expense_date\":\"2026-11-20\",\"note\":\"Blocked expense\"}")
BOB_WRITE_CODE=$(echo "$BOB_WRITE_FAIL" | jq -r '.code | floor')
test "$BOB_WRITE_CODE" = "1004"
echo "✅ Account RBAC: Bob write to viewer Account A rejected with 1004"

# Alice verifies member permissions API
MEMBER_PERMS=$(curl -s -H "Authorization: Bearer $TOKEN_A" "$BASE/ledgers/${FAM_LID}/members/${USER_B_ID}/account-permissions")
test "$(echo "$MEMBER_PERMS" | jq -r '.code | floor')" = "0"
test "$(echo "$MEMBER_PERMS" | jq '.data | length')" = "2"
echo "✅ Account RBAC: Member account permissions query OK"

# Alice resets Bob's permission on Account A (deletes override, restoring inherit/editor)
DEL_PERM_A=$(curl -s -X DELETE "$BASE/ledgers/${FAM_LID}/accounts/${ACCT_A_ID}/permissions/${USER_B_ID}" \
  -H "Authorization: Bearer $TOKEN_A")
test "$(echo "$DEL_PERM_A" | jq -r '.code | floor')" = "0"

# Now Bob can write daily expense on Bob's asset under Account A
BOB_WRITE_OK=$(curl -s -X POST "$BASE/daily-expenses" \
  -H "Authorization: Bearer $TOKEN_B" \
  -H "X-Ledger-Id: $FAM_LID" \
  -H "Content-Type: application/json" \
  -d "{\"category_id\":1,\"amount\":50,\"expense_type\":\"expense\",\"asset_id\":$BOB_ASSET_ID,\"expense_date\":\"2026-11-20\",\"note\":\"Restored expense\"}")
test "$(echo "$BOB_WRITE_OK" | jq -r '.code | floor')" = "0"
echo "✅ Account RBAC: Resetting permission restores ledger editor role, write succeeds"

# 11. Alice removes Bob from family ledger
REM_RES=$(curl -s -X DELETE "$BASE/ledgers/${FAM_LID}/members/${USER_B_ID}" \
  -H "Authorization: Bearer $TOKEN_A")
REM_CODE=$(echo "$REM_RES" | jq -r '.code | floor')
test "$REM_CODE" = "0"
echo "Alice removed Bob from family ledger"

# 12. Verify Bob is no longer a member
MEMBERS_3=$(curl -s -X GET "$BASE/ledgers/${FAM_LID}/members" \
  -H "Authorization: Bearer $TOKEN_A")
BOB_IN_LIST=$(echo "$MEMBERS_3" | jq -r ".data[] | select((.user_id|floor|tostring) == \"$USER_B_ID\") | .user_id")
test -z "$BOB_IN_LIST"
echo "Verified Bob is no longer in member list"

# 13. Alice adds Bob back by username directly
ADD_DIRECT=$(curl -s -X POST "$BASE/ledgers/${FAM_LID}/members" \
  -H "Authorization: Bearer $TOKEN_A" \
  -H "Content-Type: application/json" \
  -d '{"username":"bob","role":"editor"}')
ADD_CODE=$(echo "$ADD_DIRECT" | jq -r '.code | floor')
test "$ADD_CODE" = "0"
echo "Alice added Bob directly by username OK"

# 14. Bob leaves the ledger
BOB_LEAVE=$(curl -s -X DELETE "$BASE/ledgers/${FAM_LID}/members/${USER_B_ID}" \
  -H "Authorization: Bearer $TOKEN_B")
LEAVE_CODE=$(echo "$BOB_LEAVE" | jq -r '.code | floor')
test "$LEAVE_CODE" = "0"
echo "Bob successfully left the ledger"

# 15. Alice updates and then deletes the family ledger
UPD_FAM=$(curl -s -X PUT "$BASE/ledgers/${FAM_LID}" \
  -H "Authorization: Bearer $TOKEN_A" \
  -H "Content-Type: application/json" \
  -d '{"name":"家庭公共账本(改)","description":"修改后的描述","currency":"CNY","color":"#ef4444","icon":"ph:wallet"}')
echo "UPD_FAM: $UPD_FAM"
test "$(echo "$UPD_FAM" | jq -r '.code | floor')" = "0"

DEL_FAM=$(curl -s -X DELETE "$BASE/ledgers/${FAM_LID}" \
  -H "Authorization: Bearer $TOKEN_A")
echo "DEL_FAM: $DEL_FAM"
grep "DEBUG DEL_LEDGER" "${TMP_DIR}/server.log" || true
test "$(echo "$DEL_FAM" | jq -r '.code | floor')" = "0"
echo "Alice deleted the family ledger successfully"

echo "🎉 ALL MULTI-LEDGER & SPACES TESTS PASSED SUCCESSFULLY!"
