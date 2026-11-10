#!/usr/bin/env bash
# Shared Database & Server Lifecycle Helper for Minefolio Integration Tests
# Supports SQLite and PostgreSQL with automated environment switching and dialect compatibility.

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
  local build_dir="${2:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../build" && pwd)}"
  local log_file="${3:-/dev/null}"

  (
    cd "$build_dir"
    MINEFOLIO_DB_DRIVER="$DB_DRIVER" \
    MINEFOLIO_DB_DSN="$MINEFOLIO_DB_DSN" \
    MINEFOLIO_PORT="$port" \
    PORT="$port" \
    MINEFOLIO_JWT_SECRET="${TEST_JWT_SECRET:-test_jwt_secret_12345678901234567890}" \
    ./minefolio > "$log_file" 2>&1
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
