#!/usr/bin/env bash
# Static guard: migration version numbers must be unique per dialect directory,
# and the DDL object sets in sqlite/ and postgres/ must stay in lockstep.
#
# csilk's migration discovery (deps/csilk/src/drivers/db/migration/migration_engine.c)
# rejects the WHOLE discovery step when two scripts share the same V<num>, because it
# cannot order them deterministically. The failure surfaces far away from the cause:
# every DB-touching unit test aborts in setup, and integration servers fail to boot
# ("[migration] Failed to discover migration scripts").
#
# Two feature branches picking the same next version number (e.g. both taking V037)
# is invisible in review of either branch alone, so enforce it statically here.
#
# The entire value of a static guard is deterministic output: pin the locale so
# sort/uniq collation can never differ between CI runners and developer machines
# (LANG=zh_CN.UTF-8 vs C.UTF-8), which would resurrect ghost pass/fail flips.

export LC_ALL=C
export LANG=C

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

# Content-alignment guard: for each V<num> that exists in BOTH dialects, verify the
# DDL statement count signature (CREATE TABLE / CREATE INDEX / ALTER TABLE) is
# identical. A significant mismatch (e.g. sqlite creates a new table but postgres
# only adds columns) signals a schema divergence that will break dual-engine tests.
# Known exceptions (pre-existing migrations where one dialect intentionally uses a
# different rewrite strategy) are whitelisted.
declare -A DDL_EXCEPTIONS=()
DDL_EXCEPTIONS[012]=1   # sqlite: table rewrite for fx column; postgres: ALTER TABLE ADD
DDL_EXCEPTIONS[014]=1   # sqlite: transfers rewrite for from/to_tx_id; postgres: ALTER TABLE
DDL_EXCEPTIONS[018]=1   # sqlite: partial rewrite; postgres: targeted ALTER TABLE
DDL_EXCEPTIONS[028]=1   # sqlite: partial rewrite; postgres: targeted ALTER TABLE
DDL_EXCEPTIONS[032]=1   # postgres: batch ALTER; sqlite: minimal

sdir="$MIGRATION_ROOT/sqlite"
odir="$MIGRATION_ROOT/postgres"
if [ -d "$sdir" ] && [ -d "$odir" ]; then
    for v in $(printf '%s\n' "${DIALECT_VERSIONS[sqlite]:-}" | LC_ALL=C sort -n); do
        [ -n "${DDL_EXCEPTIONS[$v]:-}" ] && continue
        sfile=$(ls -1 "$sdir"/V"$v"__*.sql 2>/dev/null | head -1)
        ofile=$(ls -1 "$odir"/V"$v"__*.sql 2>/dev/null | head -1)
        [ -n "$sfile" ] && [ -n "$ofile" ] || continue

        s_sig=$(grep -oE 'CREATE TABLE|CREATE INDEX|ALTER TABLE' "$sfile" 2>/dev/null || true)
        s_sig=$(printf '%s\n' "$s_sig" | LC_ALL=C sort | LC_ALL=C uniq -c | awk '{t=$2" "$3; c=$1; print t"="c}' | LC_ALL=C sort | tr '\n' '|')
        o_sig_raw=$(grep -oE 'CREATE TABLE|CREATE INDEX|ALTER TABLE' "$ofile" 2>/dev/null || true)
        o_sig=$(printf '%s\n' "$o_sig_raw" | LC_ALL=C sort | LC_ALL=C uniq -c | awk '{t=$2" "$3; c=$1; print t"="c}' | LC_ALL=C sort | tr '\n' '|')

        if [ "$s_sig" != "$o_sig" ]; then
            echo "❌ V$v: DDL signature mismatch between dialects:"
            echo "   sqlite:   $s_sig"
            echo "   postgres: $o_sig"
            VIOLATIONS=$((VIOLATIONS + 1))
        fi
    done
fi

if [ "$VIOLATIONS" -gt 0 ]; then
    echo ""
    echo "Error: duplicate or divergent migration versions make csilk's discovery fail,"
    echo "breaking every DB-backed unit test and all integration servers."
    echo "Pick the next free V<num> for BOTH dialects."
    exit 1
fi

echo "✅ Migration version contract check passed: unique & aligned versions in sqlite/ and postgres/."
echo "✅ Content-alignment guard passed: DDL object sets match across both dialects."
exit 0
