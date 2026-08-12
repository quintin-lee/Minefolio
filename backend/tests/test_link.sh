#!/usr/bin/env bash
# 收支-资产联动集成测试
# 用法: ./test_link.sh   (需已在 backend/build 构建过 minefolio)
set -euo pipefail

BASE="http://localhost:8080/api"   # 后端硬编码 8080（main.c:234），运行前确认 8080 空闲
DB="/tmp/mf_link_test.db"
BUILD_DIR="$(cd "$(dirname "$0")/../build" && pwd)"
PASS=0; FAIL=0

cleanup() {
  [ -n "${SERVER_PID:-}" ] && kill "$SERVER_PID" 2>/dev/null || true
  rm -f "$DB"
}
trap cleanup EXIT

# --- 启动服务器 ---
rm -f "$DB"
cd "$BUILD_DIR"
MINEFOLIO_DB_DSN="$DB" ./minefolio &
SERVER_PID=$!
sleep 1

req() { # req METHOD PATH JSON
  local method="$1" path="$2" data="${3:-}"
  if [ -n "$data" ]; then
    curl -s -X "$method" -H "Content-Type: application/json" "$BASE$path" -d "$data"
  else
    curl -s -X "$method" "$BASE$path"
  fi
}

check() { # check DESC EXPECTED ACTUAL
  if [ "$2" = "$3" ]; then PASS=$((PASS+1)); echo "  ✅ $1"; else FAIL=$((FAIL+1)); echo "  ❌ $1 (期望 $2 实际 $3)"; fi
}

# rsa_encrypt PLAINTEXT → base64url-encoded RSA-OAEP(SHA-256) ciphertext
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


echo "== 1. 首次系统初始化（/api/system/setup）=="
INIT_STATUS=$(req GET /system/status | jq -r '.data.initialized')
check "系统初始状态 initialized=false" "false" "$INIT_STATUS"

SETUP_PASS=$(rsa_encrypt "pass1234")
SETUP_RES=$(req POST /system/setup "{\"username\":\"linktest\",\"password_enc\":\"$SETUP_PASS\"}")
TOKEN=$(echo "$SETUP_RES" | jq -r '.data.token')
AUTH="Authorization: Bearer $TOKEN"

INIT_AFTER=$(req GET /system/status | jq -r '.data.initialized')
check "初始化后 status initialized=true" "true" "$INIT_AFTER"

REG_CODE=$(req POST /auth/register '{"username":"linktest2","password":"pass1234"}' | jq -r '.code | floor')
check "初始化后公开注册被封禁 code=1004" "1004" "$REG_CODE"
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/categories" -d '{"name":"日常消费","type":"expense","currency":"CNY"}' >/dev/null
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/categories" -d '{"name":"工资","type":"income","currency":"CNY"}' >/dev/null
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/categories" -d '{"name":"现金","type":"asset","currency":"CNY"}' >/dev/null
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/categories" -d '{"name":"信用卡","type":"asset","asset_type":"credit_card","currency":"CNY"}' >/dev/null
# 读取真实分类 id（避免依赖插入顺序）
EXPENSE_CAT=$(sqlite3 "$DB" "SELECT id FROM categories WHERE name='日常消费' LIMIT 1")
INCOME_CAT=$(sqlite3 "$DB" "SELECT id FROM categories WHERE name='工资' LIMIT 1")
ASSET_CAT=$(sqlite3 "$DB" "SELECT id FROM categories WHERE name='现金' LIMIT 1")
CC_CAT=$(sqlite3 "$DB" "SELECT id FROM categories WHERE name='信用卡' LIMIT 1")

echo "== 2. 建资产 =="
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/assets" -d "{\"name\":\"钱包\",\"category_id\":$ASSET_CAT,\"current_value\":10000,\"currency\":\"CNY\"}" >/dev/null
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/assets" -d "{\"name\":\"信用卡\",\"category_id\":$CC_CAT,\"current_value\":0,\"currency\":\"CNY\"}" >/dev/null
# 用 sqlite3 直接取真实 id（避免依赖 API 返回）
WALLET_ID=$(sqlite3 "$DB" "SELECT id FROM assets WHERE name='钱包' AND category_id=$ASSET_CAT LIMIT 1")
CC_ID=$(sqlite3 "$DB" "SELECT id FROM assets WHERE name='信用卡' AND category_id=$CC_CAT LIMIT 1")

echo "== 3. 记收入 500 → 余额 10500 =="
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/daily-expenses" -d "{\"asset_id\":$WALLET_ID,\"category_id\":$INCOME_CAT,\"expense_type\":\"income\",\"amount\":500,\"currency\":\"CNY\",\"expense_date\":\"2026-08-01\"}" >/dev/null
BAL=$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$WALLET_ID")
check "普通资产收入+500" "10500.0" "$BAL"
LOG=$(sqlite3 "$DB" "SELECT printf('%.1f', delta) FROM asset_balance_logs WHERE source_type='daily_expense' ORDER BY id DESC LIMIT 1")
check "审计 delta=+500" "500.0" "$LOG"

echo "== 4. 记支出 300 → 余额 10200 =="
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/daily-expenses" -d "{\"asset_id\":$WALLET_ID,\"category_id\":$EXPENSE_CAT,\"expense_type\":\"expense\",\"amount\":300,\"currency\":\"CNY\",\"expense_date\":\"2026-08-02\"}" >/dev/null
BAL=$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$WALLET_ID")
check "普通资产支出-300" "10200.0" "$BAL"

echo "== 5. 更新收入 500→800 → 余额 10500 =="
EXP_ID=$(sqlite3 "$DB" "SELECT id FROM daily_expenses WHERE expense_type='income' LIMIT 1")
curl -s -X PUT -H "$AUTH" -H "Content-Type: application/json" "$BASE/daily-expenses/$EXP_ID" -d "{\"asset_id\":$WALLET_ID,\"category_id\":$INCOME_CAT,\"expense_type\":\"income\",\"amount\":800,\"currency\":\"CNY\",\"expense_date\":\"2026-08-01\"}" >/dev/null
BAL=$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$WALLET_ID")
check "更新同资产合并差量+300" "10500.0" "$BAL"

echo "== 6. 删除支出 300 → 余额 10800 =="
DEL_ID=$(sqlite3 "$DB" "SELECT id FROM daily_expenses WHERE expense_type='expense' LIMIT 1")
curl -s -X DELETE -H "$AUTH" "$BASE/daily-expenses/$DEL_ID" >/dev/null
BAL=$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$WALLET_ID")
check "删除支出反转+300" "10800.0" "$BAL"

echo "== 7. 信用卡：刷卡支出 500 → 欠款 +500 =="
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/daily-expenses" -d "{\"asset_id\":$CC_ID,\"category_id\":$EXPENSE_CAT,\"expense_type\":\"expense\",\"amount\":500,\"currency\":\"CNY\",\"expense_date\":\"2026-08-03\"}" >/dev/null
BAL=$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$CC_ID")
check "负债支出→余额+500" "500.0" "$BAL"

echo "== 8. 信用卡还款 500 → 欠款 0 =="
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/daily-expenses" -d "{\"asset_id\":$CC_ID,\"category_id\":$INCOME_CAT,\"expense_type\":\"income\",\"amount\":500,\"currency\":\"CNY\",\"expense_date\":\"2026-08-04\"}" >/dev/null
BAL=$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$CC_ID")
check "负债还款→余额-500" "0.0" "$BAL"

echo "== 9. 余额不足允许负数 =="
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/daily-expenses" -d "{\"asset_id\":$WALLET_ID,\"category_id\":$EXPENSE_CAT,\"expense_type\":\"expense\",\"amount\":20000,\"currency\":\"CNY\",\"expense_date\":\"2026-08-05\"}" >/dev/null
BAL=$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$WALLET_ID")
check "允许负数" "-9200.0" "$BAL"

echo "== 10. 非法资产 → code 1002 且原子回滚 =="
BEFORE=$(sqlite3 "$DB" "SELECT COUNT(*) FROM daily_expenses")
CODE=$(curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/daily-expenses" -d '{"asset_id":99999,"category_id":1,"expense_type":"expense","amount":100,"currency":"CNY","expense_date":"2026-08-06"}' | jq -r '.code | floor')
AFTER=$(sqlite3 "$DB" "SELECT COUNT(*) FROM daily_expenses")
check "非法资产 code=1002" "1002" "$CODE"
check "非法资产主记录不落库" "$BEFORE" "$AFTER"
BAL=$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$WALLET_ID")
check "非法资产余额不变" "-9200.0" "$BAL"

echo "== 11. 交易联动：存款 +1000 =="
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/transactions" -d "{\"asset_id\":$WALLET_ID,\"category_id\":$ASSET_CAT,\"transaction_type\":\"deposit\",\"amount\":1000,\"currency\":\"CNY\",\"transaction_date\":\"2026-08-07\"}" >/dev/null
BAL=$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$WALLET_ID")
check "存款+1000" "-8200.0" "$BAL"

echo "== 12. 转账不联动 =="
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/transactions" -d "{\"asset_id\":$WALLET_ID,\"category_id\":$ASSET_CAT,\"transaction_type\":\"transfer_out\",\"amount\":100,\"currency\":\"CNY\",\"transaction_date\":\"2026-08-08\"}" >/dev/null
BAL=$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$WALLET_ID")
check "转账不联动" "-8200.0" "$BAL"

echo "== 13. 更新时切换关联资产 A→B（钱包→信用卡）=="
# 将步骤 5 的收入记录（800，原资产=钱包）切到信用卡；信用卡是负债（direction=-1），收入 → 欠款减少 → 期望 -800
LOG_CNT_BEFORE=$(sqlite3 "$DB" "SELECT COUNT(*) FROM asset_balance_logs")
curl -s -X PUT -H "$AUTH" -H "Content-Type: application/json" "$BASE/daily-expenses/$EXP_ID" -d "{\"asset_id\":$CC_ID,\"category_id\":$INCOME_CAT,\"expense_type\":\"income\",\"amount\":800,\"currency\":\"CNY\",\"expense_date\":\"2026-08-01\"}" >/dev/null
BAL_A=$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$WALLET_ID")
BAL_B=$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$CC_ID")
LOG_CNT_AFTER=$(sqlite3 "$DB" "SELECT COUNT(*) FROM asset_balance_logs")
check "A 钱包回退 -800" "-9000.0" "$BAL_A"
check "B 信用卡应用 -800（负债反转）" "-800.0" "$BAL_B"
check "切换产生 2 条审计" "$((LOG_CNT_BEFORE + 2))" "$LOG_CNT_AFTER"

echo "== 14. 交易买入联动扣除关联资金账户 =="
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/transactions" -d "{\"asset_id\":$CC_ID,\"linked_asset_id\":$WALLET_ID,\"category_id\":$ASSET_CAT,\"transaction_type\":\"buy\",\"amount\":500,\"currency\":\"CNY\",\"transaction_date\":\"2026-08-09\"}" >/dev/null
BAL_WALLET=$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$WALLET_ID")
LINKED_NAME=$(sqlite3 "$DB" "SELECT la.name FROM transactions t JOIN assets la ON t.linked_asset_id=la.id WHERE t.asset_id=$CC_ID AND t.transaction_type='buy' LIMIT 1")
check "买入从钱包扣款 -500" "-9500.0" "$BAL_WALLET"
check "关联资金账户名称查出" "钱包" "$LINKED_NAME"

echo ""
echo "== 15. 修改密码：原密码错误 == "
OLD_ENC=$(rsa_encrypt "wrongpass")
NEW_ENC=$(rsa_encrypt "newpass123")
CODE=$(curl -s -X PUT -H "$AUTH" -H "Content-Type: application/json" "$BASE/auth/password" -d "{\"old_password_enc\":\"$OLD_ENC\",\"new_password_enc\":\"$NEW_ENC\"}" | jq -r '.code | floor')
check "原密码错误 code=1002" "1002" "$CODE"

echo "== 16. 修改密码：成功 == "
OLD_ENC=$(rsa_encrypt "pass1234")
NEW_ENC=$(rsa_encrypt "newpass123")
CODE=$(curl -s -X PUT -H "$AUTH" -H "Content-Type: application/json" "$BASE/auth/password" -d "{\"old_password_enc\":\"$OLD_ENC\",\"new_password_enc\":\"$NEW_ENC\"}" | jq -r '.code | floor')
check "修改密码成功 code=0" "0" "$CODE"

echo "== 17. 用新密码登录 == "
LOGIN_PASS=$(rsa_encrypt "newpass123")
LOGIN_RES=$(req POST /auth/login "{\"username\":\"linktest\",\"password_enc\":\"$LOGIN_PASS\"}")
LOGIN_CODE=$(echo "$LOGIN_RES" | jq -r '.code | floor')
check "新密码登录成功" "0" "$LOGIN_CODE"

echo ""
echo "结果: PASS=$PASS FAIL=$FAIL"
[ "$FAIL" -eq 0 ] || exit 1
