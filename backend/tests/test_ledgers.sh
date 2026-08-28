#!/usr/bin/env bash
set -euo pipefail

PORT=8080
BASE="http://127.0.0.1:${PORT}/api"
TMP_DIR=$(mktemp -d)
DB_PATH="${TMP_DIR}/test_ledgers.db"
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

# Kill any lingering process on PORT
kill $(lsof -t -i:${PORT} 2>/dev/null) 2>/dev/null || true
sleep 0.5

echo "=== Starting Minefolio Test Server on port ${PORT} ==="
export MINEFOLIO_JWT_SECRET="test_secret_for_ledgers_suite"
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
DEF_LID_A=$(echo "$LEDGERS_A" | jq -r '.data[0].id')
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
BOB_MEM=$(echo "$MEMBERS" | jq -r ".data[] | select((.user_id|tostring) == \"$USER_B_ID\")")
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
BOB_ROLE_2=$(echo "$MEMBERS_2" | jq -r ".data[] | select((.user_id|tostring) == \"$USER_B_ID\") | .role")
test "$BOB_ROLE_2" = "viewer"
echo "Bob role verified as: $BOB_ROLE_2"

# 11. Alice removes Bob from family ledger
REM_RES=$(curl -s -X DELETE "$BASE/ledgers/${FAM_LID}/members/${USER_B_ID}" \
  -H "Authorization: Bearer $TOKEN_A")
REM_CODE=$(echo "$REM_RES" | jq -r '.code | floor')
test "$REM_CODE" = "0"
echo "Alice removed Bob from family ledger"

# 12. Verify Bob is no longer a member
MEMBERS_3=$(curl -s -X GET "$BASE/ledgers/${FAM_LID}/members" \
  -H "Authorization: Bearer $TOKEN_A")
BOB_IN_LIST=$(echo "$MEMBERS_3" | jq -r ".data[] | select((.user_id|tostring) == \"$USER_B_ID\") | .user_id")
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
test "$(echo "$UPD_FAM" | jq -r '.code | floor')" = "0"

DEL_FAM=$(curl -s -X DELETE "$BASE/ledgers/${FAM_LID}" \
  -H "Authorization: Bearer $TOKEN_A")
test "$(echo "$DEL_FAM" | jq -r '.code | floor')" = "0"
echo "Alice deleted the family ledger successfully"

echo "🎉 ALL MULTI-LEDGER & SPACES TESTS PASSED SUCCESSFULLY!"
