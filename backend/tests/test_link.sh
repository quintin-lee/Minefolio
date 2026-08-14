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

echo "== 12. 转账联动：转出 100 到信用卡 =="
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/transactions" -d "{\"asset_id\":$WALLET_ID,\"linked_asset_id\":$CC_ID,\"category_id\":$ASSET_CAT,\"transaction_type\":\"transfer_out\",\"amount\":100,\"currency\":\"CNY\",\"transaction_date\":\"2026-08-08\"}" >/dev/null
BAL=$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$WALLET_ID")
BAL_CC=$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$CC_ID")
check "转账转出钱包 -100" "-8300.0" "$BAL"
check "转账转入信用卡（负债方向） -100" "-100.0" "$BAL_CC"

echo "== 13. 更新时切换关联资产 A→B（钱包→信用卡）=="
# 将步骤 5 的收入记录（800，原资产=钱包）切到信用卡；信用卡是负债（direction=-1），收入 → 欠款减少 → 期望 -800
LOG_CNT_BEFORE=$(sqlite3 "$DB" "SELECT COUNT(*) FROM asset_balance_logs")
curl -s -X PUT -H "$AUTH" -H "Content-Type: application/json" "$BASE/daily-expenses/$EXP_ID" -d "{\"asset_id\":$CC_ID,\"category_id\":$INCOME_CAT,\"expense_type\":\"income\",\"amount\":800,\"currency\":\"CNY\",\"expense_date\":\"2026-08-01\"}" >/dev/null
BAL_A=$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$WALLET_ID")
BAL_B=$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$CC_ID")
LOG_CNT_AFTER=$(sqlite3 "$DB" "SELECT COUNT(*) FROM asset_balance_logs")
check "A 钱包回退 -800" "-9100.0" "$BAL_A"
check "B 信用卡应用 -800（负债反转）" "-900.0" "$BAL_B"
check "切换产生 2 条审计" "$((LOG_CNT_BEFORE + 2))" "$LOG_CNT_AFTER"

echo "== 14. 交易买入联动扣除关联资金账户 =="
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/transactions" -d "{\"asset_id\":$CC_ID,\"linked_asset_id\":$WALLET_ID,\"category_id\":$ASSET_CAT,\"transaction_type\":\"buy\",\"amount\":500,\"currency\":\"CNY\",\"transaction_date\":\"2026-08-09\"}" >/dev/null
BAL_WALLET=$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$WALLET_ID")
LINKED_NAME=$(sqlite3 "$DB" "SELECT la.name FROM transactions t JOIN assets la ON t.linked_asset_id=la.id WHERE t.asset_id=$CC_ID AND t.transaction_type='buy' LIMIT 1")
check "买入从钱包扣款 -500" "-9600.0" "$BAL_WALLET"
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

echo "== 18. 分页查询 =="
TX_TOTAL=$(sqlite3 "$DB" "SELECT COUNT(*) FROM transactions")
TX_PAGE=$(curl -s -H "$AUTH" "$BASE/transactions?page=1&page_size=1")
check "transactions total 正确" "$TX_TOTAL" "$(echo "$TX_PAGE" | jq -r '.data.total | floor')"
check "transactions page_size=1 返回 1 条" "1" "$(echo "$TX_PAGE" | jq -r '.data.list | length')"
check "transactions page=1" "1" "$(echo "$TX_PAGE" | jq -r '.data.page | floor')"
check "transactions page_size=1" "1" "$(echo "$TX_PAGE" | jq -r '.data.page_size | floor')"

EXP_TOTAL=$(sqlite3 "$DB" "SELECT COUNT(*) FROM daily_expenses")
EXP_PAGE=$(curl -s -H "$AUTH" "$BASE/daily-expenses?page=1&page_size=1")
check "daily-expenses total 正确" "$EXP_TOTAL" "$(echo "$EXP_PAGE" | jq -r '.data.total | floor')"
check "daily-expenses page_size=1 返回 1 条" "1" "$(echo "$EXP_PAGE" | jq -r '.data.list | length')"

ASSET_TOTAL=$(sqlite3 "$DB" "SELECT COUNT(*) FROM assets")
ASSET_PAGE=$(curl -s -H "$AUTH" "$BASE/assets?page_size=500")
check "assets total 正确" "$ASSET_TOTAL" "$(echo "$ASSET_PAGE" | jq -r '.data.total | floor')"
check "assets page_size=500 全量返回" "$ASSET_TOTAL" "$(echo "$ASSET_PAGE" | jq -r '.data.list | length')"

LOG_TOTAL=$(sqlite3 "$DB" "SELECT COUNT(*) FROM asset_balance_logs")
LOG_PAGE=$(curl -s -H "$AUTH" "$BASE/asset-balance-logs?page=1&page_size=3")
check "asset-balance-logs total 正确" "$LOG_TOTAL" "$(echo "$LOG_PAGE" | jq -r '.data.total | floor')"
check "asset-balance-logs page_size=3 返回 3 条" "3" "$(echo "$LOG_PAGE" | jq -r '.data.list | length')"

MONTH_RES=$(curl -s -H "$AUTH" "$BASE/transactions/monthly?month=2026-08")
check "transactions/monthly total_volume=1600" "1600" "$(echo "$MONTH_RES" | jq -r '.data.total_volume | floor')"
check "transactions/monthly inflows=1000" "1000" "$(echo "$MONTH_RES" | jq -r '.data.inflows | floor')"
check "transactions/monthly outflows=600" "600" "$(echo "$MONTH_RES" | jq -r '.data.outflows | floor')"
check "transactions/monthly count=3" "3" "$(echo "$MONTH_RES" | jq -r '.data.count | floor')"

echo "== 19. 分类树形结构 =="
PARENT_ID=$(sqlite3 "$DB" "INSERT INTO categories (user_id, name, type, asset_type, currency, icon, sort_order) SELECT id, '测试父分类', 'expense', 'cash', 'CNY', '', 99 FROM users WHERE username='linktest'; SELECT last_insert_rowid();")
sqlite3 "$DB" "INSERT INTO categories (user_id, name, parent_id, type, asset_type, currency, icon, sort_order) SELECT id, '测试子分类', $PARENT_ID, 'expense', 'cash', 'CNY', '', 1 FROM users WHERE username='linktest';"
CATS=$(curl -s -H "$AUTH" "$BASE/categories")
check "categories 返回数组" "array" "$(echo "$CATS" | jq -r '.data | type')"
check "categories 非空" "1" "$(echo "$CATS" | jq -r 'if (.data | length) > 0 then 1 else 0 end')"
check "categories 含测试父分类" "1" "$(echo "$CATS" | jq -r '[.data[] | select(.name == "测试父分类")] | if length > 0 then 1 else 0 end')"
check "父分类 children 含测试子分类" "1" "$(echo "$CATS" | jq -r '[.data[] | select(.name == "测试父分类") | .children[]? | select(.name == "测试子分类")] | if length > 0 then 1 else 0 end')"
check "子分类 parent_id 指向父分类" "$PARENT_ID" "$(echo "$CATS" | jq -r '[.data[] | select(.name == "测试父分类") | .children[]? | select(.name == "测试子分类") | .parent_id | floor] | .[0]')"

echo "== 20. 导出日常收支 CSV =="
EXP_CSV=$(curl -s -H "$AUTH" "$BASE/export/daily-expenses")
check "export daily-expenses 含 BOM" "1" "$(echo -n "$EXP_CSV" | head -c 3 | xxd -p | grep -c 'efbbbf' || echo 0)"
check "导出含日期列名" "1" "$(echo "$EXP_CSV" | grep -c 'date,asset_name,category_name')"
check "导出含数据行" "1" "$(echo "$EXP_CSV" | wc -l | awk '{print ($1 > 1) ? 1 : 0}')"
check "导出 Content-Type 正确" "1" "$(curl -s -o /dev/null -w '%{content_type}' -H "$AUTH" "$BASE/export/daily-expenses" | grep -c 'text/csv')"

echo "== 21. 导入日常收支 CSV =="
BEFORE_EXP=$(sqlite3 "$DB" "SELECT COUNT(*) FROM daily_expenses")
# Use a temp file with real newlines (not \n escape)
cat > /tmp/mf_import_test.csv << 'CSVEOF'
date,asset_name,category_name,expense_type,amount,currency,note
2026-08-10,钱包,日常消费,expense,100,CNY,测试导入
2026-08-11,钱包,工资,income,200,CNY,测试收入
CSVEOF
IMPORT_RES=$(curl -s -H "$AUTH" -H 'Content-Type: text/csv; charset=utf-8' --data-binary @/tmp/mf_import_test.csv "$BASE/import/daily-expenses")
rm -f /tmp/mf_import_test.csv
IMPORTED=$(echo "$IMPORT_RES" | jq -r '.data.imported | floor')
ERRS=$(echo "$IMPORT_RES" | jq -r '.data.errors | floor')
check "导入成功 imported=2" "2" "$IMPORTED"
check "导入成功 errors=0" "0" "$ERRS"
AFTER_EXP=$(sqlite3 "$DB" "SELECT COUNT(*) FROM daily_expenses")
check "数据库新增 2 条记录" "$((BEFORE_EXP + 2))" "$AFTER_EXP"

echo "== 22. 导入失败行不中断整体 =="
cat > /tmp/mf_import_fail.csv << 'CSVEOF'
date,asset_name,category_name,expense_type,amount,currency,note
2026-08-12,钱包,日常消费,expense,50,CNY,有效行
2026-08-13,不存在的资产,日常消费,expense,50,CNY,无效行
CSVEOF
IMPORT_RES2=$(curl -s -H "$AUTH" -H 'Content-Type: text/csv; charset=utf-8' --data-binary @/tmp/mf_import_fail.csv "$BASE/import/daily-expenses")
rm -f /tmp/mf_import_fail.csv
IMPORTED2=$(echo "$IMPORT_RES2" | jq -r '.data.imported | floor')
ERRS2=$(echo "$IMPORT_RES2" | jq -r '.data.errors | floor')
check "部分失败 imported=1" "1" "$IMPORTED2"
check "部分失败 errors=1" "1" "$ERRS2"
check "部分失败含错误详情" "1" "$(echo "$IMPORT_RES2" | jq -r 'if .data.errors_detail then 1 else 0 end')"

echo "== 23. 导出交易 CSV =="
TX_EXPORT=$(curl -s -H "$AUTH" "$BASE/export/transactions")
check "export transactions 含日期列名" "1" "$(echo "$TX_EXPORT" | grep -c 'date,asset_name,category_name')"

echo "== 24. 方向数据化：新类型 interest 驱动统计、余额、列表 =="
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/transactions" -d "{\"asset_id\":$WALLET_ID,\"category_id\":$ASSET_CAT,\"transaction_type\":\"interest\",\"amount\":200,\"currency\":\"CNY\",\"transaction_date\":\"2026-09-01\"}" >/dev/null
# 钱包余额在测试 14 后为 -9600.0，interest(+200, balance_dir=in) → -9400.0
BAL_AFTER=$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$WALLET_ID")
check "interest 余额联动 +200" "-9400.0" "$BAL_AFTER"
DIR_VAL=$(sqlite3 "$DB" "SELECT direction FROM transactions WHERE transaction_type='interest' ORDER BY id DESC LIMIT 1")
check "interest direction 已持久化" "in" "$DIR_VAL"
MONTH9=$(curl -s -H "$AUTH" "$BASE/transactions/monthly?month=2026-09")
check "monthly 2026-09 inflows=200" "200" "$(echo "$MONTH9" | jq -r '.data.inflows | floor')"
check "monthly 2026-09 outflows=0" "0" "$(echo "$MONTH9" | jq -r '.data.outflows | floor')"
check "monthly 2026-09 count=1" "1" "$(echo "$MONTH9" | jq -r '.data.count | floor')"

echo "== 25. 未知交易类型 → 1002 且原子回滚 =="
TX_BEFORE=$(sqlite3 "$DB" "SELECT COUNT(*) FROM transactions")
CODE=$(curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/transactions" -d "{\"asset_id\":$WALLET_ID,\"category_id\":$ASSET_CAT,\"transaction_type\":\"mystery\",\"amount\":100,\"currency\":\"CNY\",\"transaction_date\":\"2026-09-02\"}" | jq -r '.code | floor')
TX_AFTER=$(sqlite3 "$DB" "SELECT COUNT(*) FROM transactions")
check "未知类型 code=1002" "1002" "$CODE"
check "未知类型不落库" "$TX_BEFORE" "$TX_AFTER"
check "未知类型余额不变" "$BAL_AFTER" "$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$WALLET_ID")"

echo "== 26. 导入 CSV 含新类型 interest =="
cat > /tmp/mf_import_tx.csv << 'CSVEOF'
date,asset_name,category_name,transaction_type,source_type,amount,price_per_unit,quantity,currency,linked_asset_name,note
2026-09-03,钱包,现金,interest,income,50,0,0,CNY,,imp-int
CSVEOF
IMP_RES=$(curl -s -H "$AUTH" -H 'Content-Type: text/csv; charset=utf-8' --data-binary @/tmp/mf_import_tx.csv "$BASE/import/transactions")
rm -f /tmp/mf_import_tx.csv
check "导入 interest imported=1" "1" "$(echo "$IMP_RES" | jq -r '.data.imported | floor')"
check "导入 interest errors=0" "0" "$(echo "$IMP_RES" | jq -r '.data.errors | floor')"
check "导入行 direction='in'" "in" "$(sqlite3 "$DB" "SELECT direction FROM transactions WHERE note='imp-int' LIMIT 1")"

echo "== 27. 存量数据 direction 持久化 =="
check "存量 deposit 行 direction='in'" "in" "$(sqlite3 "$DB" "SELECT direction FROM transactions WHERE transaction_type='deposit' ORDER BY id DESC LIMIT 1")"
check "存量 transfer_out 行 linked_direction='in'" "in" "$(sqlite3 "$DB" "SELECT linked_direction FROM transactions WHERE transaction_type='transfer_out' ORDER BY id DESC LIMIT 1")"

echo "== 28. 股票基金交易：买入建仓 =="
# 创建基金类资产
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/categories" -d '{"name":"基金","type":"asset","asset_type":"fund","currency":"CNY"}' >/dev/null
FUND_CAT=$(sqlite3 "$DB" "SELECT id FROM categories WHERE name='基金' AND type='asset' LIMIT 1")
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/assets" -d "{\"name\":\"XX基金\",\"category_id\":$FUND_CAT,\"current_value\":0,\"currency\":\"CNY\"}" >/dev/null
FUND_ID=$(sqlite3 "$DB" "SELECT id FROM assets WHERE name='XX基金' LIMIT 1")
# 买 1000 份 × 2 元
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/transactions" -d "{\"asset_id\":$FUND_ID,\"linked_asset_id\":$WALLET_ID,\"category_id\":$FUND_CAT,\"transaction_type\":\"buy\",\"amount\":2000,\"quantity\":1000,\"price_per_unit\":2,\"currency\":\"CNY\",\"transaction_date\":\"2026-08-14\"}" >/dev/null
POS=$(sqlite3 "$DB" "SELECT printf('%.4f',quantity),printf('%.4f',cost_basis),printf('%.4f',net_value),printf('%.4f',current_value) FROM assets WHERE id=$FUND_ID")
check "T1 买入后 quantity=1000" "1000.0000" "$(echo "$POS" | cut -d'|' -f1)"
check "T1 买入后 cost_basis=2000" "2000.0000" "$(echo "$POS" | cut -d'|' -f2)"
check "T1 买入后 net_value=2" "2.0000" "$(echo "$POS" | cut -d'|' -f3)"
check "T1 买入后 current_value=2000" "2000.0000" "$(echo "$POS" | cut -d'|' -f4)"

echo "== 29. 净值更新 == "
curl -s -X PUT -H "$AUTH" -H "Content-Type: application/json" "$BASE/assets/$FUND_ID" -d '{"net_value":2.5}' >/dev/null
CUR_VAL=$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$FUND_ID")
check "T2 净值更新 current_value=2500" "2500.0" "$CUR_VAL"

echo "== 30. 卖出+已实现盈亏 =="
# 卖出 400 份 × 3 元 = 1200
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/transactions" -d "{\"asset_id\":$FUND_ID,\"linked_asset_id\":$WALLET_ID,\"category_id\":$FUND_CAT,\"transaction_type\":\"sell\",\"amount\":1200,\"quantity\":400,\"price_per_unit\":3,\"currency\":\"CNY\",\"transaction_date\":\"2026-08-15\"}" >/dev/null
POS2=$(sqlite3 "$DB" "SELECT printf('%.4f',quantity),printf('%.4f',cost_basis) FROM assets WHERE id=$FUND_ID")
check "T3 卖出后 quantity=600" "600.0000" "$(echo "$POS2" | cut -d'|' -f1)"
check "T3 卖出后 cost_basis=1200" "1200.0000" "$(echo "$POS2" | cut -d'|' -f2)"
PERF=$(curl -s -H "$AUTH" "$BASE/reports/transaction/performance")
check "T3 已实现盈亏=400" "400" "$(echo "$PERF" | jq -r '.data.realized_pnl | floor')"

echo "== 31. 浮动盈亏 =="
FLOATING=$(echo "$PERF" | jq -r '.data.floating_pnl | floor')
check "T4 浮动盈亏=300" "300" "$FLOATING"

echo "== 32. 手续费 =="
TX_BUY1=$(sqlite3 "$DB" "SELECT id FROM transactions WHERE asset_id=$FUND_ID AND transaction_type='buy' AND transaction_date='2026-08-14' LIMIT 1")
# 买 100 份 × 2 元 + fee 5 元
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/transactions" -d "{\"asset_id\":$FUND_ID,\"linked_asset_id\":$WALLET_ID,\"category_id\":$FUND_CAT,\"transaction_type\":\"buy\",\"amount\":200,\"quantity\":100,\"price_per_unit\":2,\"fee\":5,\"currency\":\"CNY\",\"transaction_date\":\"2026-08-16\"}" >/dev/null
CB=$(sqlite3 "$DB" "SELECT printf('%.4f', cost_basis) FROM assets WHERE id=$FUND_ID")
check "T5 cost_basis 含 fee" "1405.0000" "$(echo "$CB")"
FEE_ROWS=$(sqlite3 "$DB" "SELECT COUNT(*) FROM transactions WHERE transaction_type='fee' AND note LIKE '%fee%'")
check "T5 fee 行落库" "1" "$FEE_ROWS"

echo ""
echo "== 33. 持仓报表 =="

# H1: 空态 — 新空用户持仓为空，summary 全 0
# JWT 直接用开发默认密钥伪造（服务器由本脚本以 MINEFOLIO_DB_DSN 启动，未设置 MINEFOLIO_JWT_SECRET）
EMPTY_UID=$(sqlite3 "$DB" "INSERT INTO users (username, password) VALUES ('holdings_empty','x'); SELECT last_insert_rowid();")
EMPTY_TOKEN=$(node -e "
const crypto = require('crypto');
const secret = 'minefolio-dev-secret-change-in-production';
const h = Buffer.from(JSON.stringify({alg:'HS256',typ:'JWT'})).toString('base64url');
const p = Buffer.from(JSON.stringify({sub:$EMPTY_UID,iat:Math.floor(Date.now()/1000)})).toString('base64url');
const s = crypto.createHmac('sha256', secret).update(h+'.'+p).digest('base64url');
process.stdout.write(h+'.'+p+'.'+s);
")
H1=$(curl -s -H "Authorization: Bearer $EMPTY_TOKEN" "$BASE/reports/holdings")
H1_EMPTY=$(echo "$H1" | jq -r '.data.holdings | length')
H1_MARKET=$(echo "$H1" | jq -r '.data.summary.total_market_value | floor')
H1_PCT=$(echo "$H1" | jq -r '.data.summary.floating_pct | floor')
check "H1 空用户 holdings 为空数组" "0" "$H1_EMPTY"
check "H1 空用户 summary 总市值 0" "0" "$H1_MARKET"
check "H1 空用户 floating_pct 0（无 NaN）" "0" "$H1_PCT"

# 新建基金分类与资产供 H2-H4 使用（不复用 T 段的 XX基金，保证数值独立）
# 载荷与 ID 读取严格对齐 T 段现有测试：category POST 带 currency，asset POST 带 current_value:0 无 type 字段，ID 一律用 sqlite3 读取
curl -s -H "$AUTH" -H "Content-Type: application/json" -X POST "$BASE/categories" \
  -d '{"name":"基金二号类","type":"asset","asset_type":"fund","currency":"CNY"}' > /dev/null
H2_CAT=$(sqlite3 "$DB" "SELECT id FROM categories WHERE name='基金二号类' AND type='asset' LIMIT 1")
curl -s -H "$AUTH" -H "Content-Type: application/json" -X POST "$BASE/assets" \
  -d "{\"name\":\"基金二号\",\"category_id\":$H2_CAT,\"current_value\":0,\"currency\":\"CNY\"}" > /dev/null
H2_ASSET=$(sqlite3 "$DB" "SELECT id FROM assets WHERE name='基金二号' LIMIT 1")

# H2: 建仓浮动盈亏 — 买 1000×2，PUT net_value=2.5 → floating=500, pct=25
curl -s -H "$AUTH" -H "Content-Type: application/json" -X POST "$BASE/transactions" \
  -d "{\"asset_id\":$H2_ASSET,\"linked_asset_id\":$WALLET_ID,\"category_id\":$H2_CAT,\"transaction_type\":\"buy\",\"amount\":2000,\"quantity\":1000,\"price_per_unit\":2,\"currency\":\"CNY\",\"transaction_date\":\"2026-01-01\"}" > /dev/null
curl -s -H "$AUTH" -H "Content-Type: application/json" -X PUT "$BASE/assets/$H2_ASSET" \
  -d '{"net_value":2.5}' > /dev/null
H2=$(curl -s -H "$AUTH" "$BASE/reports/holdings")
H2_ROW=$(echo "$H2" | jq -c '.data.holdings[] | select(.name=="基金二号")')
check "H2 建仓 floating_pnl=500" "500" "$(echo "$H2_ROW" | jq -r '.floating_pnl | floor')"
check "H2 建仓 floating_pct=25" "25" "$(echo "$H2_ROW" | jq -r '.floating_pct | floor')"
check "H2 建仓 current_value=2500" "2500" "$(echo "$H2_ROW" | jq -r '.current_value | floor')"
check "H2 建仓 realized_pnl=0" "0" "$(echo "$H2_ROW" | jq -r '.realized_pnl | floor')"

# H2b: fee 不含入盈利口径 — 买 1000×2 fee=1，卖 100×2.5 → realized = 250-100*2.0 = 50
curl -s -H "$AUTH" -H "Content-Type: application/json" -X POST "$BASE/assets" \
  -d "{\"name\":\"基金三号\",\"category_id\":$H2_CAT,\"current_value\":0,\"currency\":\"CNY\"}" > /dev/null
H2B_ASSET=$(sqlite3 "$DB" "SELECT id FROM assets WHERE name='基金三号' LIMIT 1")
curl -s -H "$AUTH" -H "Content-Type: application/json" -X POST "$BASE/transactions" \
  -d "{\"asset_id\":$H2B_ASSET,\"linked_asset_id\":$WALLET_ID,\"category_id\":$H2_CAT,\"transaction_type\":\"buy\",\"amount\":2000,\"quantity\":1000,\"price_per_unit\":2,\"fee\":1,\"currency\":\"CNY\",\"transaction_date\":\"2026-01-02\"}" > /dev/null
curl -s -H "$AUTH" -H "Content-Type: application/json" -X POST "$BASE/transactions" \
  -d "{\"asset_id\":$H2B_ASSET,\"linked_asset_id\":$WALLET_ID,\"category_id\":$H2_CAT,\"transaction_type\":\"sell\",\"amount\":250,\"quantity\":100,\"price_per_unit\":2.5,\"currency\":\"CNY\",\"transaction_date\":\"2026-01-03\"}" > /dev/null
H2B=$(curl -s -H "$AUTH" "$BASE/reports/holdings")
H2B_ROW=$(echo "$H2B" | jq -c '.data.holdings[] | select(.name=="基金三号")')
check "H2b 带 fee 买入后 realized=50（avg_cost 不含 fee）" "50" "$(echo "$H2B_ROW" | jq -r '.realized_pnl | floor')"

# H3: 卖出已实现盈亏 — 买 1000×2，卖 400×3 → realized=400, quantity=600
curl -s -H "$AUTH" -H "Content-Type: application/json" -X POST "$BASE/assets" \
  -d "{\"name\":\"基金四号\",\"category_id\":$H2_CAT,\"current_value\":0,\"currency\":\"CNY\"}" > /dev/null
H3_ASSET=$(sqlite3 "$DB" "SELECT id FROM assets WHERE name='基金四号' LIMIT 1")
curl -s -H "$AUTH" -H "Content-Type: application/json" -X POST "$BASE/transactions" \
  -d "{\"asset_id\":$H3_ASSET,\"linked_asset_id\":$WALLET_ID,\"category_id\":$H2_CAT,\"transaction_type\":\"buy\",\"amount\":2000,\"quantity\":1000,\"price_per_unit\":2,\"currency\":\"CNY\",\"transaction_date\":\"2026-02-01\"}" > /dev/null
curl -s -H "$AUTH" -H "Content-Type: application/json" -X POST "$BASE/transactions" \
  -d "{\"asset_id\":$H3_ASSET,\"linked_asset_id\":$WALLET_ID,\"category_id\":$H2_CAT,\"transaction_type\":\"sell\",\"amount\":1200,\"quantity\":400,\"price_per_unit\":3,\"currency\":\"CNY\",\"transaction_date\":\"2026-02-02\"}" > /dev/null
H3=$(curl -s -H "$AUTH" "$BASE/reports/holdings")
H3_ROW=$(echo "$H3" | jq -c '.data.holdings[] | select(.name=="基金四号")')
check "H3 卖出 realized=400" "400" "$(echo "$H3_ROW" | jq -r '.realized_pnl | floor')"
check "H3 卖出后 quantity=600" "600" "$(echo "$H3_ROW" | jq -r '.quantity | floor')"

# H4: 多资产聚合 — 各行字段求和 == summary 各总数；持仓行数
H4=$(curl -s -H "$AUTH" "$BASE/reports/holdings")
H4_COUNT=$(echo "$H4" | jq -r '.data.holdings | length')
H4_SUM_MARKET=$(echo "$H4" | jq -r '[.data.holdings[].current_value] | add | floor')
H4_SUM_COST=$(echo "$H4" | jq -r '[.data.holdings[].cost_basis] | add | floor')
H4_SUM_FLOAT=$(echo "$H4" | jq -r '[.data.holdings[].floating_pnl] | add | floor')
H4_SUM_REAL=$(echo "$H4" | jq -r '[.data.holdings[].realized_pnl] | add | floor')
H4_MARKET=$(echo "$H4" | jq -r '.data.summary.total_market_value | floor')
H4_COST=$(echo "$H4" | jq -r '.data.summary.total_cost_basis | floor')
H4_FLOAT=$(echo "$H4" | jq -r '.data.summary.total_floating_pnl | floor')
H4_REAL=$(echo "$H4" | jq -r '.data.summary.total_realized_pnl | floor')
check "H4 持仓行数=4（基金二号/三号/四号/XX基金）" "4" "$H4_COUNT"
check "H4 summary.total_market_value == Σcurrent_value" "$H4_SUM_MARKET" "$H4_MARKET"
check "H4 summary.total_cost_basis == Σcost_basis" "$H4_SUM_COST" "$H4_COST"
check "H4 summary.total_floating_pnl == Σfloating_pnl" "$H4_SUM_FLOAT" "$H4_FLOAT"
check "H4 summary.total_realized_pnl == Σrealized_pnl" "$H4_SUM_REAL" "$H4_REAL"

# H5: 零持仓 — 全卖光 XX基金（T 段资产）：quantity=0 行仍返回，floating_pnl=0, floating_pct=0
# XX基金 状态: T1 买1000×2(08-14), T3 卖400×3(08-15), T5 买100×2 fee5(08-16) → qty=700, cost_for_pnl=2200, realized=-50(400-450)
# 注意: H5 卖出日期必须晚于 T5(08-16)，否则时序颠倒导致 avg_cost 计算错误
curl -s -H "$AUTH" -H "Content-Type: application/json" -X POST "$BASE/transactions" \
  -d "{\"asset_id\":$FUND_ID,\"linked_asset_id\":$WALLET_ID,\"category_id\":$FUND_CAT,\"transaction_type\":\"sell\",\"amount\":1750,\"quantity\":700,\"price_per_unit\":2.5,\"currency\":\"CNY\",\"transaction_date\":\"2026-08-20\"}" > /dev/null
H5=$(curl -s -H "$AUTH" "$BASE/reports/holdings")
H5_ROW=$(echo "$H5" | jq -c --argjson aid "$FUND_ID" '.data.holdings[] | select(.asset_id==$aid)')
check "H5 零持仓行仍返回 quantity=0" "0" "$(echo "$H5_ROW" | jq -r '.quantity | floor')"
check "H5 零持仓 floating_pnl=0" "0" "$(echo "$H5_ROW" | jq -r '.floating_pnl | floor')"
check "H5 零持仓 floating_pct=0（无 NaN）" "0" "$(echo "$H5_ROW" | jq -r '.floating_pct | floor')"
check "H5 零持仓 realized=-50（复用 performance 口径）" "-50" "$(echo "$H5_ROW" | jq -r '.realized_pnl | floor')"

# H6: 分红 — 对基金四号做 income 100 → realized 由 400 变为 500
# income 类型 qty_independent，载荷含 linked/category/currency 以对齐其他交易载荷
curl -s -H "$AUTH" -H "Content-Type: application/json" -X POST "$BASE/transactions" \
  -d "{\"asset_id\":$H3_ASSET,\"linked_asset_id\":$WALLET_ID,\"category_id\":$H2_CAT,\"transaction_type\":\"income\",\"amount\":100,\"currency\":\"CNY\",\"transaction_date\":\"2026-02-03\"}" > /dev/null
H6=$(curl -s -H "$AUTH" "$BASE/reports/holdings")
H6_ROW=$(echo "$H6" | jq -c --argjson aid "$H3_ASSET" '.data.holdings[] | select(.asset_id==$aid)')
check "H6 分红后 realized=500（400+100）" "500" "$(echo "$H6_ROW" | jq -r '.realized_pnl | floor')"

# I1: 投资类资产创建 — 只传份额/净值，不传成本/市值 → 自动推导 cost_basis=份额×净值、current_value=份额×净值
I1_CAT=$(curl -s -H "$AUTH" -H "Content-Type: application/json" -X POST "$BASE/categories" \
  -d '{"name":"直接建仓类","type":"asset","asset_type":"fund","currency":"CNY"}' > /dev/null; sqlite3 "$DB" "SELECT id FROM categories WHERE name='直接建仓类' ORDER BY id DESC LIMIT 1")
curl -s -H "$AUTH" -H "Content-Type: application/json" -X POST "$BASE/assets" \
  -d "{\"name\":\"直接建仓基金\",\"category_id\":$I1_CAT,\"quantity\":500,\"net_value\":2.5,\"currency\":\"CNY\"}" > /dev/null
I1_ASSET=$(sqlite3 "$DB" "SELECT id FROM assets WHERE name='直接建仓基金' ORDER BY id DESC LIMIT 1")
I1_POS=$(sqlite3 "$DB" "SELECT printf('%.2f',quantity)||'|'||printf('%.2f',cost_basis)||'|'||printf('%.2f',net_value)||'|'||printf('%.2f',current_value) FROM assets WHERE id=$I1_ASSET")
check "I1 自动推导 cost_basis=500×2.5=1250" "500.00|1250.00|2.50|1250.00" "$I1_POS"

# I1b: 投资类资产 PUT 改净值 → current_value 跟随重算（quantity/cost_basis 保留）
curl -s -H "$AUTH" -H "Content-Type: application/json" -X PUT "$BASE/assets/$I1_ASSET" \
  -d '{"net_value":3.0}' > /dev/null
I1_POS2=$(sqlite3 "$DB" "SELECT printf('%.2f',quantity)||'|'||printf('%.2f',cost_basis)||'|'||printf('%.2f',net_value)||'|'||printf('%.2f',current_value) FROM assets WHERE id=$I1_ASSET")
check "I1b PUT 净值后 current_value=500×3.0=1500" "500.00|1250.00|3.00|1500.00" "$I1_POS2"

# I2: 投资类资产 PUT 改份额+成本 → 一并持久化（不再丢弃）
curl -s -H "$AUTH" -H "Content-Type: application/json" -X PUT "$BASE/assets/$I1_ASSET" \
  -d '{"quantity":600,"cost_basis":1500}' > /dev/null
I1_POS3=$(sqlite3 "$DB" "SELECT printf('%.2f',quantity)||'|'||printf('%.2f',cost_basis)||'|'||printf('%.2f',net_value)||'|'||printf('%.2f',current_value) FROM assets WHERE id=$I1_ASSET")
check "I2 PUT 份额+成本 → quantity=600 cost=1500 current=600×3.0=1800" "600.00|1500.00|3.00|1800.00" "$I1_POS3"

# I3: 非投资类资产创建不受影响 — current_value 透传
I3_CAT=$(sqlite3 "$DB" "SELECT id FROM categories WHERE name='流动资产' ORDER BY id DESC LIMIT 1")
curl -s -H "$AUTH" -H "Content-Type: application/json" -X POST "$BASE/assets" \
  -d "{\"name\":\"普通钱包测试\",\"category_id\":$I3_CAT,\"current_value\":8888,\"currency\":\"CNY\"}" > /dev/null
I3_POS=$(sqlite3 "$DB" "SELECT printf('%.2f',current_value) FROM assets WHERE name='普通钱包测试' ORDER BY id DESC LIMIT 1")
check "I3 非投资资产 current_value 透传=8888" "8888.00" "$I3_POS"

echo ""
echo "结果: PASS=$PASS FAIL=$FAIL"
[ "$FAIL" -eq 0 ] || exit 1
