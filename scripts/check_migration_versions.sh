#!/usr/bin/env bash
# Static guard: migration version numbers must be unique per dialect directory.
#
# csilk's migration discovery (deps/csilk/src/drivers/db/migration/migration_engine.c)
# rejects the WHOLE discovery step when two scripts share the same V<num>, because it
# cannot order them deterministically. The failure surfaces far away from the cause:
# every DB-touching unit test aborts in setup, and integration servers fail to boot
# ("[migration] Failed to discover migration scripts").
#
# Two feature branches picking the same next version number (e.g. both taking V037)
# is invisible in review of either branch alone, so enforce it statically here.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

MIGRATION_ROOT="$ROOT_DIR/backend/sql/migrations"
DIALECTS=(sqlite postgres)

# Keep the dialect sets in lockstep: a migration applied to one engine but not the
# other makes the dual-engine CI matrix diverge silently.
declare -A DIALECT_VERSIONS=()
VIOLATIONS=0

for dialect in "${DIALECTS[@]}"; do
    dir="$MIGRATION_ROOT/$dialect"
    if [ ! -d "$dir" ]; then
        echo "❌ Missing migration directory: $dir"
        VIOLATIONS=$((VIOLATIONS + 1))
        continue
    fi

    # "--" marker protects against filenames starting with a dash.
    dup_versions=$(ls -1 "$dir" 2>/dev/null \
        | sed -n 's/^V\([0-9][0-9]*\)__.*\.sql$/\1/p' \
        | sort -n \
        | uniq -d)

    if [ -n "$dup_versions" ]; then
        echo "❌ Duplicate migration version(s) in $dialect: $(echo "$dup_versions" | tr '\n' ' ')"
        while IFS= read -r v; do
            [ -z "$v" ] && continue
            ls -1 "$dir"/V"$v"__*.sql | sed 's/^/     /'
        done <<< "$dup_versions"
        VIOLATIONS=$((VIOLATIONS + 1))
    fi

    versions=$(ls -1 "$dir" 2>/dev/null | sed -n 's/^V\([0-9][0-9]*\)__.*\.sql$/\1/p' | sort -n | uniq)
    DIALECT_VERSIONS["$dialect"]="$versions"

    # Numeric ascending order with no gaps is not required, but a non-monotonic
    # filename order usually means a renumbering half-finished.
    unsorted=$(ls -1 "$dir" 2>/dev/null | sed -n 's/^V\([0-9][0-9]*\)__.*\.sql$/\1/p')
    if [ -n "$unsorted" ] && [ "$unsorted" != "$(printf '%s\n' "$unsorted" | sort -n)" ]; then
        echo "❌ $dialect migration versions are not in ascending numeric order"
        VIOLATIONS=$((VIOLATIONS + 1))
    fi
done

sqlite_versions="${DIALECT_VERSIONS[sqlite]:-}"
pg_versions="${DIALECT_VERSIONS[postgres]:-}"
if [ -n "$sqlite_versions" ] && [ -n "$pg_versions" ] && [ "$sqlite_versions" != "$pg_versions" ]; then
    echo "❌ sqlite and postgres migration version sets diverge:"
    diff <(printf '%s\n' "$sqlite_versions") <(printf '%s\n' "$pg_versions") \
        | sed 's/^/     /' || true
    VIOLATIONS=$((VIOLATIONS + 1))
fi

if [ "$VIOLATIONS" -gt 0 ]; then
    echo ""
    echo "Error: duplicate or divergent migration versions make csilk's discovery fail,"
    echo "breaking every DB-backed unit test and all integration servers."
    echo "Pick the next free V<num> (highest existing + 1) for BOTH dialects."
    exit 1
fi

echo "✅ Migration version contract check passed: unique & aligned versions in sqlite/ and postgres/."
exit 0