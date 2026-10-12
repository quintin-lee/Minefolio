#!/bin/bash
# 部署 PostgreSQL 驱动时的启动前检查 + DSN 引导 (由 docker-compose.yml bind-mount 进容器):
#   1. 当 MINEFOLIO_DB_DRIVER=postgres 且未显式指定 MINEFOLIO_DB_DSN 时,
#      从标准变量推导 DSN (指向 compose 内的 db-postgres 服务), 只需在 .env 配密码。
#   2. DSN 指向 db-postgres / postgres 时, 等待端口可达 (最多 120s)。
#      外部 PG 不等待: 应用启动失败即退出, restart: unless-stopped 会重试。
# 最后 exec /app/minefolio —— 镜像原有的 ENTRYPOINT 不变 (tini 作为 PID 1 接管)。
set -e

if [ "${MINEFOLIO_DB_DRIVER:-sqlite}" = "postgres" ]; then
  # 镜像默认 DSN 是 sqlite 文件路径; 切换到 postgres 时若仍为原值或空则按变量推导
  case "${MINEFOLIO_DB_DSN:-}" in
    *.db)
      echo "[db-wait] NOTICE: MINEFOLIO_DB_DRIVER=postgres but MINEFOLIO_DB_DSN is a sqlite path; overriding with derived Postgres DSN."
      export MINEFOLIO_DB_DSN=""
      ;;
  esac
  if [ -z "${MINEFOLIO_DB_DSN:-}" ]; then
    export MINEFOLIO_DB_DSN="host=${MINEFOLIO_DB_HOST:-db-postgres} port=${MINEFOLIO_DB_PORT:-5432} user=${MINEFOLIO_DB_USER:-minefolio} password=${MINEFOLIO_DB_PASSWORD:?MINEFOLIO_DB_PASSWORD is required for MINEFOLIO_DB_DRIVER=postgres} dbname=${MINEFOLIO_DB_NAME:-minefolio} sslmode=disable"
  fi

  pg_host=$(printf '%s' "$MINEFOLIO_DB_DSN" | sed -n 's/.*\bhost=\([A-Za-z0-9_.-]*\).*/\1/p')
  pg_port=$(printf '%s' "$MINEFOLIO_DB_DSN" | sed -n 's/.*\bport=\([0-9]*\).*/\1/p')
  if [ "$pg_host" = "db-postgres" ] || [ "$pg_host" = "postgres" ]; then
    pg_port=${pg_port:-5432}
    ready=0
    for i in $(seq 1 120); do
      if timeout 1 bash -c "exec 3<>/dev/tcp/${pg_host}/${pg_port}" 2>/dev/null; then
        ready=1
        break
      fi
      sleep 1
    done
    if [ "$ready" -ne 1 ]; then
      echo "[db-wait] ERROR: PostgreSQL ${pg_host}:${pg_port} not ready after 120s, aborting start." >&2
      exit 1
    fi
    echo "[db-wait] PostgreSQL ${pg_host}:${pg_port} is ready."
  fi
fi

exec /app/minefolio "$@"
