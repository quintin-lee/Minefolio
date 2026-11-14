#!/usr/bin/env bash
# Static check to prevent accidental usage of csilk_json_get_number() on DB results.
# csilk DB driver serializes ALL column values as JSON string nodes, where
# csilk_json_get_number() silently returns 0.0! Code querying database results MUST
# use db_get_num(), db_get_int(), db_get_decimal(), or db_get_money() from common/db.h.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

TARGET_DIRS=(
  "$ROOT_DIR/backend/src/infrastructure/repositories"
  "$ROOT_DIR/backend/src/services"
  "$ROOT_DIR/backend/src/core/ledger"
)

VIOLATIONS=0

for target in "${TARGET_DIRS[@]}"; do
  if [ -e "$target" ]; then
    MATCHES=$(grep -rn "csilk_json_get_number(" "$target" 2>/dev/null || true)
    if [ -n "$MATCHES" ]; then
      echo "❌ Forbidden call to csilk_json_get_number() detected in DB-result code:"
      echo "$MATCHES"
      VIOLATIONS=$((VIOLATIONS + 1))
    fi
  fi
done

if [ "$VIOLATIONS" -gt 0 ]; then
  echo ""
  echo "Error: DB columns are serialized as string nodes. csilk_json_get_number() returns 0.0 on string nodes."
  echo "Please use db_get_num(), db_get_int(), or db_get_decimal() from common/db.h instead."
  exit 1
fi

echo "✅ DB JSON stringification contract check passed: zero raw csilk_json_get_number() calls in repositories & reporting services."
exit 0
