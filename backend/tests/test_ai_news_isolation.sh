#!/usr/bin/env bash
# Integration test for AI News isolation by user_id + ledger_id and data masking.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/db_helper.sh"

db_init_context 8183 "ai_news_isolation"
BASE="http://127.0.0.1:${PORT}/api"
TMP_DIR=$(mktemp -d)
DB_PATH="$DB_FILE"
export MINEFOLIO_JWT_SECRET="ai_news_isolation_secret_32_bytes_min"
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

echo "== [AI News Isolation Test] Starting server on port $PORT =="
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

# 1. 注册 Alice (User A)
PASS_A=$(rsa_encrypt "PasswordAlice123!")
SETUP_A=$(curl -s -X POST "$BASE/system/setup" -H 'Content-Type: application/json' \
  -d "{\"username\":\"alice\",\"password_enc\":\"$PASS_A\"}")
CODE_SETUP=$(echo "$SETUP_A" | jq -r '.code')
if [ "$CODE_SETUP" != "0" ]; then
  echo "Setup Alice failed: $SETUP_A"
  exit 1
fi
TOKEN_A=$(echo "$SETUP_A" | jq -r '.data.token')

# 获取 Alice 默认账本
LEDGERS_A=$(curl -s "$BASE/ledgers" -H "Authorization: Bearer $TOKEN_A")
LEDGER_A1=$(echo "$LEDGERS_A" | jq -r '.data.list[0].id')

# Alice 创建第二个账本
NEW_LEDGER_A=$(curl -s -X POST "$BASE/ledgers" -H "Authorization: Bearer $TOKEN_A" \
  -H 'Content-Type: application/json' -d '{"name":"Alice美股账本","currency":"CNY"}')
LEDGER_A2=$(echo "$NEW_LEDGER_A" | jq -r '.data.id')

# 2. 注册 Bob (User B)
sqlite3 "$DB_PATH" "INSERT INTO users (username, password) VALUES ('bob', 'dummyhash')"
UID_B=$(sqlite3 "$DB_PATH" "SELECT id FROM users WHERE username='bob'")
TOKEN_B=$(node -e "
const crypto = require('crypto');
const header = Buffer.from(JSON.stringify({alg:'HS256',typ:'JWT'})).toString('base64url');
const now = Math.floor(Date.now()/1000);
const payload = Buffer.from(JSON.stringify({sub: $UID_B, user_id: $UID_B, v: 0, exp: now + 604800, iat: now})).toString('base64url');
const sig = crypto.createHmac('sha256', process.env.MINEFOLIO_JWT_SECRET).update(header + '.' + payload).digest('base64url');
process.stdout.write(header + '.' + payload + '.' + sig);
")
sqlite3 "$DB_PATH" "INSERT INTO ledgers (name, owner_id, is_default, currency) VALUES ('Bob默认账本', $UID_B, 1, 'CNY')"
LEDGER_B1=$(sqlite3 "$DB_PATH" "SELECT id FROM ledgers WHERE owner_id=$UID_B AND is_default=1")


echo "Alice default ledger: $LEDGER_A1, second ledger: $LEDGER_A2; Bob ledger: $LEDGER_B1"

# 3. Alice 在账本 1 创建持仓资产（含金额与数量）
# 获取分类
CATS_A=$(curl -s "$BASE/categories?page_size=100" -H "Authorization: Bearer $TOKEN_A" -H "X-Ledger-Id: $LEDGER_A1")
STOCK_CAT=$(echo "$CATS_A" | jq -r '.data.list[] | select(.asset_type=="stock") | .id' | head -n 1)

# 创建资产
curl -s -X POST "$BASE/assets" -H "Authorization: Bearer $TOKEN_A" -H "X-Ledger-Id: $LEDGER_A1" \
  -H 'Content-Type: application/json' \
  -d "{\"name\":\"贵州茅台\",\"category_id\":$STOCK_CAT,\"currency\":\"CNY\",\"current_value\":180000}" >/dev/null
ASSET_ID=$(sqlite3 "$DB_PATH" "SELECT id FROM assets WHERE name='贵州茅台' AND ledger_id=$LEDGER_A1 LIMIT 1")
sqlite3 "$DB_PATH" "UPDATE assets SET current_value=180000 WHERE id=$ASSET_ID;"
echo "Alice created asset $ASSET_ID in ledger $LEDGER_A1"

# 4. 触发 Alice 账本 1 的 AI 早报分析
TRIGGER_A1=$(curl -s -X POST "$BASE/ai/news/trigger" -H "Authorization: Bearer $TOKEN_A" -H "X-Ledger-Id: $LEDGER_A1")
echo "Trigger response: $TRIGGER_A1"
CODE_TRG=$(echo "$TRIGGER_A1" | jq -r '.code')
REPORT_ID=$(echo "$TRIGGER_A1" | jq -r '.data.report_id')
if [ "$CODE_TRG" != "0" ] || [ -z "$REPORT_ID" ] || [ "$REPORT_ID" = "null" ]; then
  echo "FAIL: Trigger failed: $TRIGGER_A1"
  exit 1
fi

# 5. 等待后台 worker 执行完毕
echo "Waiting for worker to complete analysis..."
for _i in $(seq 1 40); do
  REPORT_STATUS=$(sqlite3 "$DB_PATH" "SELECT status FROM ai_news_reports WHERE id=$REPORT_ID;")
  if [ "$REPORT_STATUS" = "done" ] || [ "$REPORT_STATUS" = "error" ]; then
    break
  fi
  sleep 0.5
done
echo "Report status: $REPORT_STATUS, error: $(sqlite3 "$DB_PATH" "SELECT error_msg FROM ai_news_reports WHERE id=$REPORT_ID;")"

# 6. 安全与隔离验证：Bob 访问 Alice 的账本 1 早报，必须被拦截 (1004)
BOB_ACCESS_A=$(curl -s "$BASE/ai/news?ledger_id=$LEDGER_A1" -H "Authorization: Bearer $TOKEN_B")
echo "Bob access Alice ledger response: $BOB_ACCESS_A"
BOB_CODE=$(echo "$BOB_ACCESS_A" | jq -r '.code')
if [ "$BOB_CODE" != "1004" ]; then
  echo "FAIL: Expected code 1004 for unauthorized ledger access, got $BOB_CODE"
  exit 1
fi
echo "PASS: Bob unauthorized access blocked with 1004"

# 7. 账本隔离验证：Alice 查询账本 2，由于未在该账本触发生成，列表应为空
ALICE_L2=$(curl -s "$BASE/ai/news?ledger_id=$LEDGER_A2" -H "Authorization: Bearer $TOKEN_A")
L2_CNT=$(echo "$ALICE_L2" | jq -r '.data.list | length')
if [ "$L2_CNT" != "0" ]; then
  echo "FAIL: Expected 0 reports for Ledger A2, got $L2_CNT"
  exit 1
fi
echo "PASS: Ledger A2 has 0 reports (isolated from Ledger A1)"

# 8. 持仓数据隐私脱敏验证 (Zero-Knowledge Data Masking)
HOLDINGS_SNAPSHOT=$(sqlite3 "$DB_PATH" "SELECT holdings_snapshot FROM ai_news_reports WHERE id=$REPORT_ID;")
echo "Holdings snapshot in DB: $HOLDINGS_SNAPSHOT"

# 必须包含脱敏后的 name 和 weight
HAS_NAME=$(echo "$HOLDINGS_SNAPSHOT" | grep -c "贵州茅台" || true)
HAS_WEIGHT=$(echo "$HOLDINGS_SNAPSHOT" | grep -c "100.0%" || true)
# 绝不能包含真实金额与数量！
HAS_CURRENT_VAL=$(echo "$HOLDINGS_SNAPSHOT" | grep -c "current_value" || true)
HAS_COST_BASIS=$(echo "$HOLDINGS_SNAPSHOT" | grep -c "cost_basis" || true)
HAS_QUANTITY=$(echo "$HOLDINGS_SNAPSHOT" | grep -c "quantity" || true)

if [ "$HAS_NAME" -eq 0 ] || [ "$HAS_WEIGHT" -eq 0 ]; then
  echo "FAIL: Masked snapshot missing asset name or weight percentage!"
  exit 1
fi
if [ "$HAS_CURRENT_VAL" -gt 0 ] || [ "$HAS_COST_BASIS" -gt 0 ] || [ "$HAS_QUANTITY" -gt 0 ]; then
  echo "FAIL: Privacy leak detected! Holdings snapshot contains raw monetary values!"
  exit 1
fi
echo "PASS: Holdings snapshot successfully masked with 0 raw monetary leakage"

# 9. 阶段一公共摘要表验证
DIGEST_CNT=$(sqlite3 "$DB_PATH" "SELECT count(*) FROM market_news_digests;")
if [ "$DIGEST_CNT" -lt 1 ]; then
  echo "FAIL: market_news_digests was not populated in Stage 1!"
  exit 1
fi
echo "PASS: Stage 1 global market_news_digests populated ($DIGEST_CNT entries)"

echo "=== ALL AI NEWS ISOLATION AND PRIVACY TESTS PASSED ==="
