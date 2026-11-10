#!/usr/bin/env bash
# Unit test for db_helper.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HELPER="${SCRIPT_DIR}/db_helper.sh"

if [ ! -f "$HELPER" ]; then
  echo "❌ FAIL: Helper not found at $HELPER"
  exit 1
fi

source "$HELPER"

PASS=0
FAIL=0

assert_eq() {
  local desc="$1"
  local expected="$2"
  local actual="$3"
  if [ "$expected" = "$actual" ]; then
    echo "  ✅ $desc"
    PASS=$((PASS + 1))
  else
    echo "  ❌ $desc (expected: '$expected', actual: '$actual')"
    FAIL=$((FAIL + 1))
  fi
}

echo "=== 1. Test SQLite mode in db_helper ==="
export MINEFOLIO_DB_DRIVER=sqlite
export MINEFOLIO_PORT=18099
db_init_context 18099 "helper_test"

assert_eq "Driver is sqlite" "sqlite" "$DB_DRIVER"
assert_eq "Port matches" "18099" "$PORT"

# Clean before start
db_clean
if [ -f "$DB_FILE" ]; then
  echo "  ❌ DB_FILE exists after db_clean"
  FAIL=$((FAIL + 1))
else
  echo "  ✅ DB_FILE cleaned up"
  PASS=$((PASS + 1))
fi

# DDL & DML
db_exec "CREATE TABLE test_items (id INTEGER PRIMARY KEY, name TEXT, amount REAL);"
db_exec "INSERT INTO test_items (name, amount) VALUES ('apple', 12.5);"

VAL=$(db_query "SELECT name FROM test_items WHERE id=1;")
assert_eq "Query returns inserted string" "apple" "$VAL"

NUM=$(db_query "SELECT amount FROM test_items WHERE id=1;")
assert_eq "Query returns inserted number" "12.5" "$NUM"

# Test sqlite3 compatibility wrapper
WRAP_VAL=$(sqlite3 "$DB" "SELECT name FROM test_items WHERE id=1;")
assert_eq "sqlite3 wrapper returns string" "apple" "$WRAP_VAL"

sqlite3 "$DB" "UPDATE test_items SET name='banana' WHERE id=1;"
WRAP_MOD=$(sqlite3 "$DB" "SELECT name FROM test_items WHERE id=1;")
assert_eq "sqlite3 wrapper update works" "banana" "$WRAP_MOD"

# Clean after test
db_clean
if [ -f "$DB_FILE" ]; then
  echo "  ❌ DB_FILE not removed on exit"
  FAIL=$((FAIL + 1))
else
  echo "  ✅ DB_FILE cleaned up successfully"
  PASS=$((PASS + 1))
fi

echo "=== Result: PASS=$PASS FAIL=$FAIL ==="
if [ "$FAIL" -gt 0 ]; then
  exit 1
fi
