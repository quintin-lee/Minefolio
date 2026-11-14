#!/usr/bin/env bash
# Shared Database & Server Lifecycle Helper for Minefolio Integration Tests
# Supports SQLite and PostgreSQL with automated environment switching and dialect compatibility.

_DB_HELPER_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$_DB_HELPER_DIR/cgroup_helper.sh"

if [ -n "${MINEFOLIO_BUILD_DIR:-}" ] && [ "${MINEFOLIO_BUILD_DIR#/}" = "$MINEFOLIO_BUILD_DIR" ]; then
  if [ -d "$MINEFOLIO_BUILD_DIR" ]; then
    MINEFOLIO_BUILD_DIR="$(cd "$MINEFOLIO_BUILD_DIR" && pwd)"
  elif [ -d "$_DB_HELPER_DIR/../$MINEFOLIO_BUILD_DIR" ]; then
    MINEFOLIO_BUILD_DIR="$(cd "$_DB_HELPER_DIR/../$MINEFOLIO_BUILD_DIR" && pwd)"
  fi
  export MINEFOLIO_BUILD_DIR
fi

DB_DRIVER="${MINEFOLIO_DB_DRIVER:-sqlite}"
SERVER_PID=""

db_init_context() {
  local default_port="${1:-18080}"
  local suite_name="${2:-test}"

  PORT="${MINEFOLIO_PORT:-${PORT:-$default_port}}"
  MINEFOLIO_PORT="$PORT"
  DB_DRIVER="${MINEFOLIO_DB_DRIVER:-sqlite}"

  export PORT MINEFOLIO_PORT MINEFOLIO_DB_DRIVER DB_DRIVER

  if [ "$DB_DRIVER" = "postgres" ]; then
    PGHOST="${PGHOST:-localhost}"
    PGPORT="${PGPORT:-5432}"
    PGUSER="${PGUSER:-postgres}"
    PGPASSWORD="${PGPASSWORD:-postgres_password}"
    PGDATABASE="${PGDATABASE:-minefolio_test}"

    MINEFOLIO_DB_DSN="${MINEFOLIO_DB_DSN:-host=${PGHOST} port=${PGPORT} user=${PGUSER} password=${PGPASSWORD} dbname=${PGDATABASE} sslmode=disable}"
    export PGHOST PGPORT PGUSER PGPASSWORD PGDATABASE MINEFOLIO_DB_DSN
    DB="$MINEFOLIO_DB_DSN"
    DB_FILE="$DB"
  else
    DB_FILE="${MINEFOLIO_DB_DSN:-/tmp/mf_${suite_name}_$$.db}"
    MINEFOLIO_DB_DSN="$DB_FILE"
    DB="$DB_FILE"
    export MINEFOLIO_DB_DSN DB_FILE DB
  fi
}

db_clean() {
  if [ "${DB_DRIVER:-sqlite}" = "postgres" ]; then
    PGPASSWORD="${PGPASSWORD:-postgres_password}" psql -h "${PGHOST:-localhost}" -p "${PGPORT:-5432}" -U "${PGUSER:-postgres}" -d "${PGDATABASE:-minefolio_test}" -q -c "DROP SCHEMA IF EXISTS public CASCADE; CREATE SCHEMA public;" 2>/dev/null || true
    # Inject lightweight SQL compatibility functions into PostgreSQL public schema
    PGPASSWORD="${PGPASSWORD:-postgres_password}" psql -h "${PGHOST:-localhost}" -p "${PGPORT:-5432}" -U "${PGUSER:-postgres}" -d "${PGDATABASE:-minefolio_test}" -q << 'EOF' 2>/dev/null || true
CREATE OR REPLACE FUNCTION printf(fmt text, val anyelement) RETURNS text AS $$
BEGIN
  IF fmt = '%.1f' THEN RETURN to_char(val::numeric, 'FM999999999990.0');
  ELSIF fmt = '%.2f' THEN RETURN to_char(val::numeric, 'FM999999999990.00');
  ELSIF fmt = '%.4f' THEN RETURN to_char(val::numeric, 'FM999999999990.0000');
  ELSE RETURN to_char(val::numeric, 'FM999999999990.00');
  END IF;
END;
$$ LANGUAGE plpgsql IMMUTABLE;

CREATE OR REPLACE FUNCTION last_insert_rowid() RETURNS bigint AS $$
BEGIN
  RETURN lastval();
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION json_extract(doc text, path text) RETURNS text AS $$
DECLARE
  clean_path text;
  parts text[];
BEGIN
  clean_path := regexp_replace(path, '^\$\.?', '');
  clean_path := regexp_replace(clean_path, '\[([0-9]+)\]', '.\1', 'g');
  parts := string_to_array(clean_path, '.');
  RETURN doc::json #>> parts;
EXCEPTION WHEN OTHERS THEN
  RETURN NULL;
END;
$$ LANGUAGE plpgsql IMMUTABLE;
EOF
  else
    if [ -n "${DB_FILE:-}" ]; then
      rm -f "$DB_FILE" "$DB_FILE-wal" "$DB_FILE-shm"
    fi
    if [ -n "${DB:-}" ] && [ "${DB:-}" != "${DB_FILE:-}" ]; then
      rm -f "$DB" "$DB-wal" "$DB-shm"
    fi
  fi
}

db_query() {
  local sql="$1"
  if [ "${DB_DRIVER:-sqlite}" = "postgres" ]; then
    PGPASSWORD="${PGPASSWORD:-postgres_password}" psql -h "${PGHOST:-localhost}" -p "${PGPORT:-5432}" -U "${PGUSER:-postgres}" -d "${PGDATABASE:-minefolio_test}" -t -A -c "$sql" | tail -n 1
  else
    command sqlite3 "$DB_FILE" "$sql"
  fi
}

db_exec() {
  local sql="$1"
  if [ "${DB_DRIVER:-sqlite}" = "postgres" ]; then
    PGPASSWORD="${PGPASSWORD:-postgres_password}" psql -h "${PGHOST:-localhost}" -p "${PGPORT:-5432}" -U "${PGUSER:-postgres}" -d "${PGDATABASE:-minefolio_test}" -q -v ON_ERROR_STOP=1 -c "$sql" >/dev/null
  else
    command sqlite3 "$DB_FILE" "$sql" >/dev/null
  fi
}

# Universal sqlite3 CLI interception wrapper to support legacy tests on both SQLite and PostgreSQL
sqlite3() {
  if [ "$#" -ge 2 ]; then
    shift
  fi
  local sql="$1"
  if [[ "$sql" =~ ^[[:space:]]*([Ii][Nn][Ss][Ee][Rr][Tt]|[Uu][Pp][Dd][Aa][Tt][Ee]|[Dd][Ee][Ll][Ee][Tt][Ee]|[Cc][Rr][Ee][Aa][Tt][Ee]|[Dd][Rr][Oo][Pp]) ]] && [[ ! "$sql" =~ last_insert_rowid ]]; then
    db_exec "$sql"
  else
    db_query "$sql"
  fi
}

db_start_server() {
  local port="${1:-18080}"
  local build_dir="${2:-${MINEFOLIO_BUILD_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../build" && pwd)}}"
  local log_file="${3:-/dev/null}"
  # Memory ceiling for the long-lived server. A leak or an ASan build (~2x every
  # allocation) can otherwise exhaust host memory and trigger an OOM reboot.
  # This is a *virtual-address* cap (ulimit -v); csilk + ASan reserve large
  # virtual ranges, so keep it generous. Override via MINEFOLIO_SERVER_MEM_LIMIT.
  local mem_limit="${MINEFOLIO_SERVER_MEM_LIMIT:-4096M}"
  local mem_kb
  # Validate the limit is a clean `<int><K|M|G|T|B>` before arithmetic. A
  # malformed value (e.g. a literal `${VAR:-default}` leaked from a YAML env:
  # block, which GitHub Actions does NOT shell-expand) must fall back to a
  # safe default instead of crashing the test under `set -e`.
  case "$mem_limit" in
    "" | *[!0-9KMGTkmgtBb]*) mem_limit="4096M" ;;
  esac
  mem_kb="$(
    spec="$mem_limit"; num="${spec%%[KMGTkmgtBb]*}"; suffix="${spec#"$num"}"
    [ -z "$num" ] && num=0; [ -z "$suffix" ] && suffix="K"
    case "$suffix" in
      K|k) factor=1 ;; M|m) factor=1024 ;; G|g) factor=$((1024 * 1024)) ;;
      T|t) factor=$((1024 * 1024 * 1024)) ;; B|b) factor=0 ;; *) factor=1 ;;
    esac
    if [ "$factor" -eq 0 ]; then printf '%s' "$(( (num + 1023) / 1024 ))"; else printf '%s' "$(( num * factor ))"; fi
  )"

  # Launch the server in a backgrounded subshell. A virtual-memory ceiling
  # (ulimit -v) protects a *normal* build from exhausting host memory. Under
  # ASan/UBSan we must NOT set a virtual cap: ASan reserves enormous virtual
  # address ranges (shadow memory ~1/8 of the address space + redzones), so
  # any finite `ulimit -v` aborts the binary at startup (core dump). The hard
  # ceiling under ASan is the cgroup RSS cap applied by mf_exec_isolated
  # (MINEFOLIO_TEST_MEM_MAX), which is RSS-based and safe for ASan.
  # ASAN_OPTIONS is exported at source time by mf_setup_sanitizer_env, so its
  # presence reliably flags an ASan build.
  local apply_vlimit=1
  [ -n "${ASAN_OPTIONS:-}" ] && apply_vlimit=0
  (
    cd "$build_dir"
    if [ "$apply_vlimit" -eq 1 ]; then
      ulimit -v "$mem_kb" 2>/dev/null || true
    fi
    export MINEFOLIO_DB_DRIVER="$DB_DRIVER"
    export MINEFOLIO_DB_DSN="$MINEFOLIO_DB_DSN"
    export MINEFOLIO_PORT="$port"
    export PORT="$port"
    export MINEFOLIO_JWT_SECRET="${TEST_JWT_SECRET:-test_jwt_secret_12345678901234567890}"
    mf_exec_isolated ./minefolio > "$log_file" 2>&1
  ) &
  SERVER_PID=$!

  # Poll health endpoint until server is ready
  local ready=0
  for _i in $(seq 1 40); do
    if curl -sf "http://127.0.0.1:${port}/healthz" >/dev/null 2>&1; then
      ready=1
      break
    fi
    sleep 0.2
  done

  if [ "$ready" -ne 1 ]; then
    echo "❌ Server failed to start on port $port (PID: $SERVER_PID)"
    if [ -f "$log_file" ] && [ "$log_file" != "/dev/null" ]; then
      tail -n 20 "$log_file"
    fi
    kill "$SERVER_PID" 2>/dev/null || true
    return 1
  fi

  return 0
}

db_stop_server() {
  local pid="${1:-${SERVER_PID:-}}"
  if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
  fi
}
