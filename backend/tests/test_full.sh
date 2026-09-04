#!/usr/bin/env bash
# Minefolio 完整功能测试
# 覆盖：认证、分类、标签、资产、记账(交易)、持仓交易、转账、日常收支、现金流、DCA、报表、导入导出、授权与边界。
# 用法：./test_full.sh   (需已在 backend/build 构建过 minefolio，依赖 node + jq + sqlite3)
set -o pipefail

BASE="http://localhost:8181/api"
DB="/tmp/mf_full_test.db"
BUILD_DIR="$(cd "$(dirname "$0")/../build" && pwd)"
export TEST_PORT=8181
PASS=0; FAIL=0; FAILED_CASES=()

cleanup() {
  [ -n "${SERVER_PID:-}" ] && kill "$SERVER_PID" 2>/dev/null || true
  rm -f "$DB" "$DB-wal" "$DB-shm"
}
trap cleanup EXIT

check() {
  if [ "$2" = "$3" ]; then PASS=$((PASS+1)); echo "  ✅ $1"
  else FAIL=$((FAIL+1)); FAILED_CASES+=("$1 (期望 $2 实际 $3)")
    echo "  ❌ $1 (期望 $2 实际 $3)"; fi
}

check_num() {
  local desc="$1" expected="$2" actual="$3"
  local diff=$(awk -v e="$expected" -v a="$actual" 'BEGIN { d = e - a; if (d < 0) d = -d; print (d < 0.001) ? "1" : "0" }' 2>/dev/null || echo "0")
  if [ "$diff" = "1" ]; then PASS=$((PASS+1)); echo "  ✅ $desc"
  else FAIL=$((FAIL+1)); FAILED_CASES+=("$desc (期望 $expected 实际 $actual)")
    echo "  ❌ $desc (期望 $expected 实际 $actual)"; fi
}

check_nz() {
  if [ -n "$2" ] && [ "$2" != "0" ] && [ "$2" != "null" ]; then PASS=$((PASS+1)); echo "  ✅ $1"
  else FAIL=$((FAIL+1)); FAILED_CASES+=("$1 (got: $2)"); echo "  ❌ $1 (got: $2)"; fi
}

check_reject() { # check_reject DESC CODE  — pass if code is not "0" (empty/null 也算拒绝)
  if [ "$2" != "0" ] && [ "$2" != "null" ]; then PASS=$((PASS+1)); echo "  ✅ $1 (code=$2)"
  else FAIL=$((FAIL+1)); FAILED_CASES+=("$1 (got code=$2)"); echo "  ❌ $1 (got code=$2)"; fi
}

req() {
  local method="$1" path="$2" data="${3:-}"
  if [ -n "$data" ]; then
    curl -s -X "$method" -H "Content-Type: application/json" "$BASE$path" -d "$data"
  else
    curl -s -X "$method" "$BASE$path"
  fi
}

req_auth() {
  local method="$1" path="$2" data="${3:-}" token="$4"
  if [ -n "$data" ]; then
    curl -s -X "$method" -H "Authorization: Bearer $token" -H "Content-Type: application/json" "$BASE$path" -d "$data"
  else
    curl -s -X "$method" -H "Authorization: Bearer $token" "$BASE$path"
  fi
}

extract_code() { { echo "$1" | jq -r '.code | floor // empty' 2>/dev/null; } || echo ""; }
extract_data() {
  if [ -n "${1:-}" ] && [ "${1:0:1}" = "." ]; then
    { jq -r "if .data$1 != null then .data$1 else empty end" 2>/dev/null; } || echo ""
  else
    { echo "${1:-}" | jq -r "if .data${2:-} != null then .data${2:-} else empty end" 2>/dev/null; } || echo ""
  fi
}
# id of a category/asset/tag by name (within a user)
id_by_name() { local table="$1" name="$2" uid="$3"
  sqlite3 "$DB" "SELECT id FROM $table WHERE name='$name' AND user_id=$uid LIMIT 1"
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
      .replace(/\+/g,'-').replace(/\//g,'_').replace(/=+\$/,''));
  });
});"
}

export TEST_JWT_SECRET=$(node -e "console.log(require('crypto').randomBytes(32).toString('hex'))")
rm -f "$DB" "$DB-wal" "$DB-shm"
cd "$BUILD_DIR"
MINEFOLIO_DB_DSN="$DB" MINEFOLIO_JWT_SECRET="$TEST_JWT_SECRET" PORT="$TEST_PORT" ./minefolio >/dev/null 2>&1 &
SERVER_PID=$!
for _i in $(seq 1 30); do
  curl -sf http://localhost:$TEST_PORT/healthz >/dev/null 2>&1 && break
  sleep 0.5
done

# ============================================================
echo "== A. 系统初始化与认证 =="
INIT=$(req GET /system/status | extract_data .initialized)
check "系统未初始化" "false" "$INIT"

SETUP_ENC=$(rsa_encrypt "pass1234")
SETUP_RES=$(req POST /system/setup "{\"username\":\"fulltest\",\"password_enc\":\"$SETUP_ENC\"}")
TOKEN=$(extract_data "$SETUP_RES" .token)
check_nz "初始化后返回 token" "$TOKEN"
UID1=$(sqlite3 "$DB" "SELECT id FROM users WHERE username='fulltest'")
check_nz "用户 fulltest 已建" "$UID1"

DUP=$(extract_code "$(req POST /system/setup "{\"username\":\"fulltest2\",\"password_enc\":\"$(rsa_encrypt "pass1234")\"}")")
check_reject "重复初始化被拒" "$DUP"

REG_ENC=$(rsa_encrypt "pass123456")
REG2_RES=$(req POST /auth/register "{\"username\":\"fulltest2\",\"password_enc\":\"$REG_ENC\"}")
TOKEN2=$(extract_data "$REG2_RES" .token)
check "注册第二个用户 code=0" "0" "$(extract_code "$REG2_RES")"
check_nz "第二个用户获得 token" "$TOKEN2"
UID2=$(sqlite3 "$DB" "SELECT id FROM users WHERE username='fulltest2'")
check_nz "用户 fulltest2 已建" "$UID2"

DUP_REG=$(extract_code "$(req POST /auth/register "{\"username\":\"fulltest2\",\"password_enc\":\"$REG_ENC\"}")")
check "重复用户名注册 code=1004" "1004" "$DUP_REG"

LOGIN_RES=$(req POST /auth/login "{\"username\":\"fulltest2\",\"password_enc\":\"$REG_ENC\"}")
LOGIN_TOKEN=$(extract_data "$LOGIN_RES" .token)
check "登录 code=0" "0" "$(extract_code "$LOGIN_RES")"

ME=$(extract_data "$(req_auth GET /auth/me "" "$LOGIN_TOKEN")" .username)
check "/auth/me 返回正确用户名" "fulltest2" "$ME"

BAD=$(extract_code "$(req POST /auth/login "{\"username\":\"fulltest2\",\"password_enc\":\"$(rsa_encrypt "wrongpass1")\"}")")
check "错误密码登录 code=1001" "1001" "$BAD"

# 未认证访问：应被拒（任何非 0 响应或 HTTP 失败）
NO_AUTH_CODE=$(extract_code "$(req GET /categories)")
check_reject "未认证访问被拒" "$NO_AUTH_CODE"

# ============================================================
echo "== B. 分类管理 =="
# 分类 POST 不返回 data.id，用 sqlite3 按名称取
for nm in "FT-餐饮:expense" "FT-工资:income" "FT-现金账户:asset" "FT-银行卡:asset" "FT-信用卡:asset" "FT-股票:asset" "FT-基金:asset" "FT-债券:asset" "FT-加密货币:asset" "FT-贷款:asset" "FT-其他负债:asset"; do
  name="${nm%%:*}"; type="${nm##*:}"
  asset_type=""
  case "$name" in FT-现金账户) asset_type="\"asset_type\":\"cash\"";; FT-银行卡) asset_type="\"asset_type\":\"cash\"";; FT-信用卡) asset_type="\"asset_type\":\"credit_card\"";; FT-股票) asset_type="\"asset_type\":\"stock\"";; FT-基金) asset_type="\"asset_type\":\"fund\"";; FT-债券) asset_type="\"asset_type\":\"bond\"";; FT-加密货币) asset_type="\"asset_type\":\"crypto\"";; FT-贷款) asset_type="\"asset_type\":\"loan\"";; FT-其他负债) asset_type="\"asset_type\":\"other_liability\"";; esac
  body="{\"name\":\"$name\",\"type\":\"$type\",\"currency\":\"CNY\""
  [ -n "$asset_type" ] && body="$body,$asset_type"
  body="$body}"
  res=$(req_auth POST /categories "$body" "$TOKEN")
  code=$(extract_code "$res")
  check "创建分类 $name code=0" "0" "$code"
done

EXPENSE_CAT=$(id_by_name categories FT-餐饮 $UID1)
INCOME_CAT=$(id_by_name categories FT-工资 $UID1)
CASH_CAT=$(id_by_name categories FT-现金账户 $UID1)
BANK_CAT=$(id_by_name categories FT-银行卡 $UID1)
CC_CAT=$(id_by_name categories FT-信用卡 $UID1)
STOCK_CAT=$(id_by_name categories FT-股票 $UID1)
FUND_CAT=$(id_by_name categories FT-基金 $UID1)
BOND_CAT=$(id_by_name categories FT-债券 $UID1)
CRYPTO_CAT=$(id_by_name categories FT-加密货币 $UID1)
LOAN_CAT=$(id_by_name categories FT-贷款 $UID1)
OTHER_CAT=$(id_by_name categories FT-其他负债 $UID1)
check_nz "EXPENSE_CAT" "$EXPENSE_CAT"
check_nz "INCOME_CAT" "$INCOME_CAT"
check_nz "CASH_CAT" "$CASH_CAT"
check_nz "BANK_CAT" "$BANK_CAT"

# 子分类
res=$(req_auth POST /categories "{\"name\":\"FT-餐饮-午餐\",\"type\":\"expense\",\"parent_id\":$EXPENSE_CAT,\"currency\":\"CNY\"}" "$TOKEN")
check "创建子分类 code=0" "0" "$(extract_code "$res")"
SUBCAT=$(id_by_name categories FT-餐饮-午餐 $UID1)
check_nz "子分类 FT-餐饮-午餐" "$SUBCAT"

TREE=$(req_auth GET /categories "" "$TOKEN")
TOTAL_CAT=$(echo "$TREE" | jq '.data | if type=="array" then length else 0 end')
[ "$TOTAL_CAT" = "0" ] && TOTAL_CAT=$(echo "$TREE" | jq '.data | length // 0')
check_nz "分类总数>0" "$TOTAL_CAT"

UPD=$(extract_code "$(req_auth PUT /categories/$EXPENSE_CAT "{\"name\":\"FT-餐饮美食\",\"type\":\"expense\",\"currency\":\"CNY\",\"color\":\"#ff0000\"}" "$TOKEN")")
check "更新分类 code=0" "0" "$UPD"

CHILD=$(req_auth GET /categories/$EXPENSE_CAT/children "" "$TOKEN")
CHILD_CNT=$(echo "$CHILD" | jq '.data | length')
check "子分类数量=1" "1" "$CHILD_CNT"

DEL_PARENT=$(extract_code "$(req_auth DELETE /categories/$EXPENSE_CAT "" "$TOKEN")")
check_reject "删除有子的分类被拒" "$DEL_PARENT"

# ============================================================
echo "== C. 标签管理 =="
T1=$(extract_data "$(req_auth POST /tags '{"name":"出差","color":"#3b82f6"}' "$TOKEN")" .id)
check_nz "创建标签 出差" "$T1"
T2=$(extract_data "$(req_auth POST /tags '{"name":"家庭","color":"#10b981"}' "$TOKEN")" .id)
check_nz "创建标签 家庭" "$T2"
TAG_DUP=$(extract_code "$(req_auth POST /tags '{"name":"出差","color":"#3b82f6"}' "$TOKEN")")
check_reject "重复标签名被拒" "$TAG_DUP"
UPD_TAG=$(extract_code "$(req_auth PUT /tags/$T1 '{"name":"商务出差","color":"#ef4444"}' "$TOKEN")")
check "更新标签 code=0" "0" "$UPD_TAG"

# ============================================================
echo "== D. 资产管理 =="
for spec in "FT钱包:$CASH_CAT:10000" "FT招行卡:$BANK_CAT:50000" "FT招行信用卡:$CC_CAT:0" "AAPL:$STOCK_CAT:0" "沪深300ETF:$FUND_CAT:0" "FT房贷:$LOAN_CAT:0"; do
  name="${spec%%:*}"; rest="${spec#*:}"; catid="${rest%%:*}"; val="${rest##*:}"
  if [ "$name" = "AAPL" ]; then
    body="{\"name\":\"$name\",\"category_id\":$catid,\"current_value\":$val,\"quantity\":0,\"cost_basis\":0,\"net_value\":0,\"currency\":\"USD\"}"
  elif [ "$name" = "沪深300ETF" ]; then
    body="{\"name\":\"$name\",\"category_id\":$catid,\"current_value\":$val,\"quantity\":0,\"cost_basis\":0,\"net_value\":0,\"currency\":\"CNY\"}"
  else
    body="{\"name\":\"$name\",\"category_id\":$catid,\"current_value\":$val,\"currency\":\"CNY\"}"
  fi
  res=$(req_auth POST /assets "$body" "$TOKEN")
  check "创建资产 $name code=0" "0" "$(extract_code "$res")"
done

WALLET=$(id_by_name assets FT钱包 $UID1)
BANK=$(id_by_name assets FT招行卡 $UID1)
CC=$(id_by_name assets FT招行信用卡 $UID1)
STOCK=$(id_by_name assets AAPL $UID1)
FUND=$(id_by_name assets 沪深300ETF $UID1)
LOAN=$(id_by_name assets FT房贷 $UID1)
check_nz "WALLET id" "$WALLET"
check_nz "BANK id" "$BANK"
check_nz "STOCK id" "$STOCK"
check_nz "FUND id" "$FUND"

ASSET_LIST=$(req_auth GET /assets "" "$TOKEN")
ASSET_CNT=$(echo "$ASSET_LIST" | jq '.data | if type=="array" then length else (.items // .data | length) end')
# 资产列表可能为数组或 {items:[]} 形式
if [ "$ASSET_CNT" = "null" ] || [ "$ASSET_CNT" = "0" ]; then
  ASSET_CNT=$(echo "$ASSET_LIST" | jq '.data | if type=="array" then length else (if .items then .items|length else (.data|length) end) end')
fi
check "资产列表数量=6" "6" "$ASSET_CNT"

GET_WALLET=$(extract_data "$(req_auth GET /assets/$WALLET "" "$TOKEN")" .current_value)
check "获取资产 钱包余额=10000" "10000" "$GET_WALLET"

UPD_ASSET=$(extract_code "$(req_auth PUT /assets/$WALLET "{\"name\":\"我的钱包\",\"category_id\":$CASH_CAT,\"current_value\":12000,\"currency\":\"CNY\"}" "$TOKEN")")
check "更新资产 code=0" "0" "$UPD_ASSET"
check "更新后余额=12000" "12000.0" "$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$WALLET")"

OTHER_ASSET=$(extract_code "$(req_auth GET /assets/$WALLET "" "$TOKEN2")")
check "另一用户访问资产 code=1003" "1003" "$OTHER_ASSET"

LOGS=$(req_auth GET /asset-balance-logs?asset_id=$WALLET "" "$TOKEN")
LOGS_CNT=$(echo "$LOGS" | jq '.data | if type=="array" then length else (.items|length) end')
[ "$LOGS_CNT" = "null" ] && LOGS_CNT=0
check_nz "钱包余额日志存在" "$LOGS_CNT"

# ============================================================
echo "== E. 记账（交易）基础流程 =="
# 钱包当前 12000
T_DEP=$(extract_code "$(req_auth POST /transactions "{\"asset_id\":$WALLET,\"category_id\":$CASH_CAT,\"transaction_type\":\"deposit\",\"amount\":2000,\"currency\":\"CNY\",\"transaction_date\":\"2026-08-01\"}" "$TOKEN")")
check "存款 code=0" "0" "$T_DEP"
check "存款后余额=14000" "14000.0" "$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$WALLET")"

# /transactions 支持的类型为 deposit/withdrawal/income/buy/sell/transfer 等，支出走 /daily-expenses
T_WD=$(extract_code "$(req_auth POST /transactions "{\"asset_id\":$WALLET,\"category_id\":$EXPENSE_CAT,\"transaction_type\":\"withdrawal\",\"amount\":300,\"currency\":\"CNY\",\"transaction_date\":\"2026-08-02\"}" "$TOKEN")")
check "取款(支出) code=0" "0" "$T_WD"
check "取款后余额=13700" "13700.0" "$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$WALLET")"

T_INC=$(extract_code "$(req_auth POST /transactions "{\"asset_id\":$WALLET,\"category_id\":$INCOME_CAT,\"transaction_type\":\"income\",\"amount\":8000,\"currency\":\"CNY\",\"transaction_date\":\"2026-08-04\"}" "$TOKEN")")
check "收入 code=0" "0" "$T_INC"
check "收入后余额=21700" "21700.0" "$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$WALLET")"

# 列表
T_LIST=$(req_auth GET /transactions?page=1\&page_size=10 "" "$TOKEN")
T_TOTAL=$(echo "$T_LIST" | jq -r '.data.total // (.data | if type=="array" then length else (.items|length) end)')
check "交易列表 total=4" "4" "$T_TOTAL"

T_BY_TYPE=$(req_auth GET /transactions?transaction_type=withdrawal "" "$TOKEN" | jq '.data | if type=="array" then length else (.items|length) end')
check "按类型过滤 withdrawal=1" "1" "$T_BY_TYPE"

# 单条 — 用 sqlite3 找 deposit 行的 id（POST 不返 data.id）
DEP_ID=$(sqlite3 "$DB" "SELECT id FROM transactions WHERE transaction_type='deposit' AND user_id=$UID1 LIMIT 1")
T_ONE=$(extract_data "$(req_auth GET /transactions/$DEP_ID "" "$TOKEN")" .amount)
check "单条交易获取" "2000" "$T_ONE"

UPD_TX=$(extract_code "$(req_auth PUT /transactions/$DEP_ID "{\"asset_id\":$WALLET,\"category_id\":$CASH_CAT,\"transaction_type\":\"deposit\",\"amount\":2500,\"currency\":\"CNY\",\"transaction_date\":\"2026-08-01\"}" "$TOKEN")")
check "更新交易 code=0" "0" "$UPD_TX"
check "更新差量+500 余额=22200" "22200.0" "$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$WALLET")"

T_DEL=$(extract_code "$(req_auth DELETE /transactions/$DEP_ID "" "$TOKEN")")
check "删除交易 code=0" "0" "$T_DEL"
check "删除反转-2500 余额=19700" "19700.0" "$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$WALLET")"

BEFORE_TX=$(sqlite3 "$DB" "SELECT COUNT(*) FROM transactions WHERE user_id=$UID1")
BAD_TX=$(extract_code "$(req_auth POST /transactions "{\"asset_id\":99999,\"category_id\":$CASH_CAT,\"transaction_type\":\"deposit\",\"amount\":100,\"currency\":\"CNY\",\"transaction_date\":\"2026-08-10\"}" "$TOKEN")")
AFTER_TX=$(sqlite3 "$DB" "SELECT COUNT(*) FROM transactions WHERE user_id=$UID1")
check "非法资产交易被拒" "1002" "$BAD_TX"
check "非法交易主记录未落库" "$BEFORE_TX" "$AFTER_TX"
check "非法交易余额不变" "19700.0" "$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$WALLET")"

MONTHLY=$(req_auth GET /transactions/monthly?year=2026\&month=8 "" "$TOKEN")
M_CNT=$(echo "$MONTHLY" | jq '.data | if type=="array" then length else (.items|length) end')
check_nz "月度聚合存在数据" "$M_CNT"

# ============================================================
echo "== F. 转账 =="
TR1=$(extract_code "$(req_auth POST /transfers "{\"from_asset_id\":$WALLET,\"to_asset_id\":$BANK,\"amount\":3000,\"transfer_date\":\"2026-08-05\",\"note\":\"转账到银行卡\"}" "$TOKEN")")
check "转账 code=0" "0" "$TR1"
check "转出钱包 余额=16700" "16700.0" "$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$WALLET")"
check "转入银行卡 余额=53000" "53000.0" "$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$BANK")"

TR_OUT_CNT=$(sqlite3 "$DB" "SELECT COUNT(*) FROM transactions WHERE transaction_type='transfer_out' AND user_id=$UID1")
TR_IN_CNT=$(sqlite3 "$DB" "SELECT COUNT(*) FROM transactions WHERE transaction_type='transfer_in' AND user_id=$UID1")
check "转账生成 transfer_out 行=1" "1" "$TR_OUT_CNT"
check "转账生成 transfer_in 行=1" "1" "$TR_IN_CNT"

SELF=$(extract_code "$(req_auth POST /transfers "{\"from_asset_id\":$WALLET,\"to_asset_id\":$WALLET,\"amount\":100,\"transfer_date\":\"2026-08-06\"}" "$TOKEN")")
check "自转账 code=1002" "1002" "$SELF"

NEG=$(extract_code "$(req_auth POST /transfers "{\"from_asset_id\":$WALLET,\"to_asset_id\":$BANK,\"amount\":-100,\"transfer_date\":\"2026-08-07\"}" "$TOKEN")")
check "负数转账 code=1002" "1002" "$NEG"

# ============================================================
echo "== G. 持仓交易：买入建仓 =="
BUY1=$(extract_code "$(req_auth POST /transactions "{\"asset_id\":$STOCK,\"linked_asset_id\":$WALLET,\"category_id\":$STOCK_CAT,\"transaction_type\":\"buy\",\"amount\":1500,\"price_per_unit\":150,\"quantity\":10,\"currency\":\"USD\",\"transaction_date\":\"2026-08-10\",\"note\":\"首笔买入\"}" "$TOKEN")")
check "买入股票 code=0" "0" "$BUY1"
check "买入后 quantity=10" "10" "$(sqlite3 "$DB" "SELECT printf('%.0f', quantity) FROM assets WHERE id=$STOCK")"
check "买入后 cost_basis=1500" "1500.0000" "$(sqlite3 "$DB" "SELECT printf('%.4f', cost_basis) FROM assets WHERE id=$STOCK")"
check "买入后 net_value=150" "150.0000" "$(sqlite3 "$DB" "SELECT printf('%.4f', net_value) FROM assets WHERE id=$STOCK")"
check "买入后 current_value=1500" "1500.0" "$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$STOCK")"

NV=$(extract_code "$(req_auth PUT /assets/$STOCK '{"net_value":180}' "$TOKEN")")
check "更新净值 code=0" "0" "$NV"
check "净值=180 后 current_value=1800" "1800.0" "$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$STOCK")"

BUY2=$(extract_code "$(req_auth POST /transactions "{\"asset_id\":$STOCK,\"linked_asset_id\":$WALLET,\"category_id\":$STOCK_CAT,\"transaction_type\":\"buy\",\"amount\":850,\"price_per_unit\":170,\"quantity\":5,\"fee\":10,\"currency\":\"USD\",\"transaction_date\":\"2026-08-12\"}" "$TOKEN")")
check "加仓 code=0" "0" "$BUY2"
check "加仓后 quantity=15" "15" "$(sqlite3 "$DB" "SELECT printf('%.0f', quantity) FROM assets WHERE id=$STOCK")"
check "加仓后 cost_basis=2360" "2360.0000" "$(sqlite3 "$DB" "SELECT printf('%.4f', cost_basis) FROM assets WHERE id=$STOCK")"

SELL1=$(extract_code "$(req_auth POST /transactions "{\"asset_id\":$STOCK,\"linked_asset_id\":$WALLET,\"category_id\":$STOCK_CAT,\"transaction_type\":\"sell\",\"amount\":1000,\"price_per_unit\":200,\"quantity\":5,\"currency\":\"USD\",\"transaction_date\":\"2026-08-15\"}" "$TOKEN")")
check "卖出 code=0" "0" "$SELL1"
check "卖出后 quantity=10" "10" "$(sqlite3 "$DB" "SELECT printf('%.0f', quantity) FROM assets WHERE id=$STOCK")"

SELL_OVER=$(extract_code "$(req_auth POST /transactions "{\"asset_id\":$STOCK,\"linked_asset_id\":$WALLET,\"category_id\":$STOCK_CAT,\"transaction_type\":\"sell\",\"amount\":1000,\"price_per_unit\":200,\"quantity\":9999,\"currency\":\"USD\",\"transaction_date\":\"2026-08-16\"}" "$TOKEN")")
check "超量卖出 code=1002" "1002" "$SELL_OVER"

FBUY=$(extract_code "$(req_auth POST /transactions "{\"asset_id\":$FUND,\"linked_asset_id\":$WALLET,\"category_id\":$FUND_CAT,\"transaction_type\":\"buy\",\"amount\":1200,\"price_per_unit\":1.2,\"quantity\":1000,\"currency\":\"CNY\",\"transaction_date\":\"2026-08-11\"}" "$TOKEN")")
check "基金买入 code=0" "0" "$FBUY"
check "基金 quantity=1000" "1000.0" "$(sqlite3 "$DB" "SELECT printf('%.0f', quantity) FROM assets WHERE id=$FUND")"

HOLDINGS=$(req_auth GET /reports/holdings "" "$TOKEN")
H_CNT=$(echo "$HOLDINGS" | jq '.data.holdings | length')
check "持仓行数=2" "2" "$H_CNT"
TOTAL_CV=$(echo "$HOLDINGS" | jq '.data.summary.total_market_value')
TOTAL_CB=$(echo "$HOLDINGS" | jq '.data.summary.total_cost_basis')
check_nz "持仓总市值>0" "$TOTAL_CV"
check_nz "持仓总成本>0" "$TOTAL_CB"

req_auth PUT /assets/$STOCK '{"net_value":190}' "$TOKEN" >/dev/null
HOLDINGS2=$(req_auth GET /reports/holdings "" "$TOKEN")
STOCK_CV=$(echo "$HOLDINGS2" | jq -r '.data.holdings[] | select(.asset_id=='$STOCK') | .current_value')
check "净值190后 AAPL current_value=1900" "1900" "$STOCK_CV"
STOCK_FPL=$(echo "$HOLDINGS2" | jq -r '.data.holdings[] | select(.asset_id=='$STOCK') | .floating_pnl')
check_nz "AAPL 浮动盈亏>0" "$STOCK_FPL"

# ============================================================
echo "== H. 日常收支（含 tag） =="
DE_RES=$(req_auth POST /daily-expenses "{\"asset_id\":$WALLET,\"category_id\":$SUBCAT,\"expense_type\":\"expense\",\"amount\":45,\"currency\":\"CNY\",\"expense_date\":\"2026-08-08\",\"note\":\"午餐\",\"tags\":[{\"name\":\"工作日\"}]}" "$TOKEN")
DE_CODE=$(extract_code "$DE_RES")
check "日常支出带 tag code=0" "0" "$DE_CODE"
DE_ID=$(sqlite3 "$DB" "SELECT id FROM daily_expenses WHERE user_id=$UID1 AND amount=45 LIMIT 1")
check_nz "日常支出获得 id" "$DE_ID"

DE_UPDATE=$(extract_code "$(req_auth PUT /daily-expenses/$DE_ID "{\"asset_id\":$BANK,\"category_id\":$SUBCAT,\"expense_type\":\"expense\",\"amount\":45,\"currency\":\"CNY\",\"expense_date\":\"2026-08-08\",\"note\":\"午餐\"}" "$TOKEN")")
check "跨资产更新 code=0" "0" "$DE_UPDATE"
WALLET_NOW=$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$WALLET")
BANK_NOW=$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$BANK")
echo "  [info] 跨资产后 钱包=$WALLET_NOW 银行卡=$BANK_NOW"

BAD_TYPE=$(extract_code "$(req_auth POST /daily-expenses "{\"asset_id\":$WALLET,\"category_id\":$SUBCAT,\"expense_type\":\"transfer\",\"amount\":1,\"currency\":\"CNY\",\"expense_date\":\"2026-08-08\"}" "$TOKEN")")
check "非法 expense_type code=1002" "1002" "$BAD_TYPE"

DE_DEL=$(extract_code "$(req_auth DELETE /daily-expenses/$DE_ID "" "$TOKEN")")
check "删除日常收支 code=0" "0" "$DE_DEL"
TAG_ORPHAN=$(sqlite3 "$DB" "SELECT COUNT(*) FROM expense_tags WHERE expense_id=$DE_ID")
check "删除后 tag 行清理" "0" "$TAG_ORPHAN"

DE_MONTHLY=$(req_auth GET /daily-expenses/monthly?year=2026\&month=8 "" "$TOKEN")
DE_M_CNT=$(echo "$DE_MONTHLY" | jq '.data | if type=="array" then length else (.items|length) end')
check_nz "日常月度聚合存在" "$DE_M_CNT"

# ============================================================
echo "== I. 负债联动（信用卡/贷款） =="
CC_SPEND=$(extract_code "$(req_auth POST /daily-expenses "{\"asset_id\":$CC,\"category_id\":$EXPENSE_CAT,\"expense_type\":\"expense\",\"amount\":800,\"currency\":\"CNY\",\"expense_date\":\"2026-08-09\"}" "$TOKEN")")
check "信用卡刷卡 code=0" "0" "$CC_SPEND"
check "信用卡欠款=800" "800.0" "$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$CC")"

CC_REPAY=$(extract_code "$(req_auth POST /daily-expenses "{\"asset_id\":$CC,\"category_id\":$INCOME_CAT,\"expense_type\":\"income\",\"amount\":300,\"currency\":\"CNY\",\"expense_date\":\"2026-08-10\"}" "$TOKEN")")
check "信用卡还款 code=0" "0" "$CC_REPAY"
check "信用卡欠款=500" "500.0" "$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$CC")"

LOAN_BORROW=$(extract_code "$(req_auth POST /daily-expenses "{\"asset_id\":$LOAN,\"category_id\":$INCOME_CAT,\"expense_type\":\"income\",\"amount\":10000,\"currency\":\"CNY\",\"expense_date\":\"2026-08-11\"}" "$TOKEN")")
check "贷款借入 code=0" "0" "$LOAN_BORROW"
check "贷款余额=-10000" "-10000.0" "$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$LOAN")"

LOAN_PAY=$(extract_code "$(req_auth POST /daily-expenses "{\"asset_id\":$LOAN,\"category_id\":$EXPENSE_CAT,\"expense_type\":\"expense\",\"amount\":1000,\"currency\":\"CNY\",\"expense_date\":\"2026-08-12\"}" "$TOKEN")")
check "贷款还款 code=0" "0" "$LOAN_PAY"
check "贷款余额=-9000" "-9000.0" "$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$LOAN")"

# ============================================================
echo "== J. 报表 =="
EXP_MONTHLY=$(req_auth GET /reports/expense/monthly?year=2026 "" "$TOKEN")
check_nz "月度支出报表数据" "$(echo "$EXP_MONTHLY" | jq '.data | if type=="array" then length else (.items|length) end')"
EXP_TREND=$(req_auth GET /reports/expense/trend?months=3 "" "$TOKEN")
check_nz "支出趋势数据" "$(echo "$EXP_TREND" | jq '.data | if type=="array" then length else (.items|length) end')"
EXP_CAT=$(req_auth GET /reports/expense/category?start_date=2026-08-01\&end_date=2026-08-31 "" "$TOKEN")
check_nz "按分类支出聚合" "$(echo "$EXP_CAT" | jq '.data | if type=="array" then length else (.items|length) end')"
EXP_TAG=$(req_auth GET /reports/expense/tag?start_date=2026-08-01\&end_date=2026-08-31 "" "$TOKEN")
check_nz "按 tag 支出聚合" "$(echo "$EXP_TAG" | jq '.data | if type=="array" then length else (.items|length) end')"
EXP_YEARLY=$(req_auth GET /reports/expense/yearly?year=2026 "" "$TOKEN")
check_nz "年度支出数据" "$(echo "$EXP_YEARLY" | jq '.data | if type=="array" then length else (.items|length) end')"

ASSET_TREND=$(req_auth GET /reports/asset/trend?period=month\&months=6 "" "$TOKEN")
check_nz "资产趋势数据" "$(echo "$ASSET_TREND" | jq '.data | if type=="array" then length else (.items|length) end')"
ASSET_BREAKDOWN=$(req_auth GET /reports/asset/breakdown "" "$TOKEN")
check_nz "资产分布数据" "$(echo "$ASSET_BREAKDOWN" | jq '.data | if type=="array" then length else (.items|length) end')"
ASSET_SUMMARY=$(req_auth GET /reports/asset/summary "" "$TOKEN")
check_nz "资产汇总" "$(echo "$ASSET_SUMMARY" | jq '.data | if type=="array" then length else (.items|length) end')"

SUMMARY=$(req_auth GET /summary "" "$TOKEN")
check_nz "总览数据" "$(echo "$SUMMARY" | jq '.data | length')"

TX_PERF=$(req_auth GET /reports/transaction/performance?start_date=2026-08-01\&end_date=2026-08-31 "" "$TOKEN")
check_nz "交易表现数据" "$(echo "$TX_PERF" | jq '.data | length')"

# ============================================================
echo "== K. 现金流（cashflow） =="
CF1=$(extract_code "$(req_auth POST /cashflow/schedule "{\"name\":\"月度工资\",\"amount\":15000,\"type\":\"income\",\"category_id\":$INCOME_CAT,\"asset_id\":$BANK,\"frequency\":\"monthly\",\"start_date\":\"2026-09-01\"}" "$TOKEN")")
check "创建现金流计划 code=0" "0" "$CF1"

CF_LIST=$(req_auth GET /cashflow/schedule "" "$TOKEN")
check_nz "现金流计划列表" "$(echo "$CF_LIST" | jq '.data | if type=="array" then length else (.items|length) end')"

CF_ID=$(sqlite3 "$DB" "SELECT id FROM cashflow_schedules WHERE user_id=$UID1 LIMIT 1")
check_nz "现金流计划 id" "$CF_ID"

CF_UPD=$(extract_code "$(req_auth PUT /cashflow/schedule/$CF_ID "{\"name\":\"月度工资（更新）\",\"amount\":16000}" "$TOKEN")")
check "更新现金流计划 code=0" "0" "$CF_UPD"

CF_ACTUAL=$(extract_code "$(req_auth POST /cashflow/actual "{\"schedule_id\":$CF_ID,\"amount\":16000,\"date\":\"2026-09-01\"}" "$TOKEN")")
check "记录现金流实际 code=0" "0" "$CF_ACTUAL"

CF_DEL=$(extract_code "$(req_auth DELETE /cashflow/schedule/$CF_ID "" "$TOKEN")")
check "删除现金流计划 code=0" "0" "$CF_DEL"

# ============================================================
echo "== L. DCA 定投 =="
DCA1=$(extract_code "$(req_auth POST /dca/plans "{\"name\":\"AAPL月定投\",\"asset_id\":$STOCK,\"linked_asset_id\":$WALLET,\"amount\":300,\"frequency\":\"monthly\",\"day_of_month\":15,\"start_date\":\"2026-09-15\"}" "$TOKEN")")
check "创建 DCA 计划 code=0" "0" "$DCA1"
DCA_ID=$(sqlite3 "$DB" "SELECT id FROM dca_plans WHERE user_id=$UID1 LIMIT 1")
check_nz "DCA 计划 id" "$DCA_ID"

DCA_LIST=$(req_auth GET /dca/plans "" "$TOKEN")
check_nz "DCA 列表" "$(echo "$DCA_LIST" | jq '.data | if type=="array" then length else (.items|length) end')"

DCA_GET=$(extract_data "$(req_auth GET /dca/plans/$DCA_ID "" "$TOKEN")" .name)
check "DCA 详情 name=AAPL月定投" "AAPL月定投" "$DCA_GET"

DCA_UPD=$(extract_code "$(req_auth PUT /dca/plans/$DCA_ID "{\"name\":\"AAPL周定投\",\"amount\":100,\"frequency\":\"weekly\"}" "$TOKEN")")
check "更新 DCA 计划 code=0" "0" "$DCA_UPD"

DCA_EXEC=$(extract_code "$(req_auth POST /dca/plans/$DCA_ID/execute "{\"execution_date\":\"2026-09-15\",\"price_per_unit\":175}" "$TOKEN")")
check "DCA 执行 code=0" "0" "$DCA_EXEC"
DCA_TX=$(sqlite3 "$DB" "SELECT COUNT(*) FROM transactions WHERE user_id=$UID1 AND note LIKE '%DCA%'")
check "DCA 执行产生交易行" "1" "$DCA_TX"

DCA_DEL=$(extract_code "$(req_auth DELETE /dca/plans/$DCA_ID "" "$TOKEN")")
check "删除 DCA 计划 code=0" "0" "$DCA_DEL"

# ============================================================
echo "== M. 导入导出 =="
# 导出交易 CSV
EXP_TX=$(curl -s -H "Authorization: Bearer $TOKEN" "$BASE/transactions/export")
check_nz "交易 CSV 导出 header" "$(echo "$EXP_TX" | head -1 | grep -c ',')"

EXP_DE=$(curl -s -H "Authorization: Bearer $TOKEN" "$BASE/daily-expenses/export")
check_nz "日常收支 CSV 导出 header" "$(echo "$EXP_DE" | head -1 | grep -c ',')"

# 导入测试
cat > /tmp/import_test.csv <<'EOF'
transaction_date,amount,type,category,asset,currency,note
2026-08-20,100,expense,餐饮美食,我的钱包,CNY,导入测试
EOF
IMP=$(extract_code "$(curl -s -X POST -H "Authorization: Bearer $TOKEN" -F "file=@/tmp/import_test.csv" "$BASE/transactions/import")")
check "导入交易 code=0" "0" "$IMP"
check "导入后交易行+1" "1" "$(sqlite3 "$DB" "SELECT COUNT(*) FROM transactions WHERE user_id=$UID1 AND note='导入测试'")"

# ============================================================
echo "== N. 授权与隔离 =="
NO_AUTH_ASSET=$(extract_code "$(req_auth GET /assets/$WALLET "" "$TOKEN2")")
check "另一用户 GET 资产 code=1003" "1003" "$NO_AUTH_ASSET"

NO_AUTH_CAT=$(extract_code "$(req_auth GET /categories/$EXPENSE_CAT "" "$TOKEN2")")
check "另一用户 GET 分类 code=1003" "1003" "$NO_AUTH_CAT"

NO_AUTH_PUT=$(extract_code "$(req_auth PUT /assets/$WALLET '{"name":"hacked"}' "$TOKEN2")")
check "另一用户 PUT 资产 code=1003" "1003" "$NO_AUTH_PUT"

C_OTHER=$(extract_code "$(req_auth POST /categories '{"name":"其他支出","type":"expense","currency":"CNY"}' "$TOKEN2")")
A_OTHER=$(extract_code "$(req_auth POST /assets "{\"name\":\"其他钱包\",\"category_id\":$CASH_CAT,\"current_value\":1000,\"currency\":\"CNY\"}" "$TOKEN2")")
check "另一用户建资产 code=0" "0" "$A_OTHER"
A_OTHER_ID=$(id_by_name assets 其他钱包 $UID2)
check_nz "另一用户资产 id" "$A_OTHER_ID"

SEE_OTHER=$(extract_code "$(req_auth GET /assets/$A_OTHER_ID "" "$TOKEN")")
check "看不到另一用户的资产 code=1003" "1003" "$SEE_OTHER"

# ============================================================
echo "== O. 边界与错误处理 =="
MISS=$(extract_code "$(req_auth POST /transactions "{\"asset_id\":$WALLET,\"transaction_type\":\"deposit\",\"amount\":100,\"currency\":\"CNY\",\"transaction_date\":\"2026-08-10\"}" "$TOKEN")")
check "缺 category_id code=1002" "1002" "$MISS"

ZERO=$(extract_code "$(req_auth POST /transactions "{\"asset_id\":$WALLET,\"category_id\":$CASH_CAT,\"transaction_type\":\"deposit\",\"amount\":0,\"currency\":\"CNY\",\"transaction_date\":\"2026-08-10\"}" "$TOKEN")")
check "amount=0 code=1002" "1002" "$ZERO"

NEG_AMT=$(extract_code "$(req_auth POST /transactions "{\"asset_id\":$WALLET,\"category_id\":$CASH_CAT,\"transaction_type\":\"deposit\",\"amount\":-100,\"currency\":\"CNY\",\"transaction_date\":\"2026-08-10\"}" "$TOKEN")")
check "amount=-100 code=1002" "1002" "$NEG_AMT"

UNK=$(extract_code "$(req_auth POST /transactions "{\"asset_id\":$WALLET,\"category_id\":$CASH_CAT,\"transaction_type\":\"unknown_type_xyz\",\"amount\":100,\"currency\":\"CNY\",\"transaction_date\":\"2026-08-10\"}" "$TOKEN")")
check "未知交易类型 code=1002" "1002" "$UNK"

BAD_ID=$(extract_code "$(req_auth GET /assets/abc "" "$TOKEN")")
check "非法 ID 格式 code=1003" "1003" "$BAD_ID"

NF_ID=$(extract_code "$(req_auth GET /assets/999999 "" "$TOKEN")")
check "不存在 ID code=1003" "1003" "$NF_ID"

UNI=$(extract_code "$(req_auth POST /categories '{"name":"🎉娱乐","type":"expense","currency":"CNY"}' "$TOKEN")")
check "Unicode 分类 code=0" "0" "$UNI"

BIG=$(extract_code "$(req_auth POST /transactions "{\"asset_id\":$BANK,\"category_id\":$INCOME_CAT,\"transaction_type\":\"income\",\"amount\":1000000,\"currency\":\"CNY\",\"transaction_date\":\"2026-08-13\"}" "$TOKEN")")
check "百万级交易 code=0" "0" "$BIG"

# ============================================================
echo ""
echo "================================================================"
echo "  完整功能测试结果"
echo "  PASS=$PASS  FAIL=$FAIL"
echo "================================================================"
if [ $FAIL -gt 0 ]; then
  echo ""
  echo "失败用例："
  for c in "${FAILED_CASES[@]}"; do echo "  - $c"; done
  exit 1
fi
exit 0
