#!/bin/bash
# 部署 PostgreSQL 驱动时的启动前检查 + DSN 引导 (由 docker-compose.yml bind-mount 进容器):
#   1. 当 MINEFOLIO_DB_DRIVER=postgres 且未显式指定 MINEFOLIO_DB_DSN 时,
#      从标准变量推导 DSN (指向 compose 内的 db-postgres 服务), 只需在 .env 配密码。
#   2. DSN 指向 db-postgres / postgres 时, 等待端口可达 (最多 120s)。
#      外部 PG 不等待: 应用启动失败即退出, restart: unless-stopped 会重试。
#   3. 迁移校验和自愈: 已应用迁移被后续提交改注释/改号后, csilk 启动会因
#      schema_migrations.checksum 与磁盘文件不符而报 Checksum mismatch 并无限重启;
#      脚本用 csilk 同样的规范化重算每个已记录迁移的期望值, 一次性修正库里记录 (幂等)。
#      磁盘迁移文件缺失视为篡改/损坏, 拒绝修正并 fail fast。
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

# ---------------------------------------------------------------------------
# 迁移校验和自愈 (仅 PostgreSQL DSN 生效): 已应用迁移被后续提交改注释/改号时,
# csilk 启动会因 schema_migrations.checksum 与磁盘文件不符而报 Checksum mismatch
# 并无限重启。容器用 csilk 同样的规范化 (LF + 去尾部空白 + SHA-256) 重新计算每个已记录
# 迁移的期望值, 与磁盘文件比对; 文件漂移则用新值修正库里记录 (一次性, 幂等)。
# 磁盘迁移文件缺失视为篡改/损坏, 拒绝修正并 fail fast。
# 不硬编码构建期摘要, 保持 CI 纯文件驱动构建不受影响。
# ---------------------------------------------------------------------------
if [ -n "${MINEFOLIO_DB_DSN:-}" ]; then
  case "$MINEFOLIO_DB_DSN" in *host=*|*dbname=*)
    pg_user=$(printf '%s' "$MINEFOLIO_DB_DSN" | sed -n 's/.*\buser=\([A-Za-z0-9_.-]*\).*/\1/p')
    pg_db=$(printf '%s' "$MINEFOLIO_DB_DSN" | sed -n 's/.*\bdbname=\([A-Za-z0-9_]*\).*/\1/p')
    pgpass=$(printf '%s' "$MINEFOLIO_DB_DSN" | sed -n 's/.*\bpassword=\([^ ]*\).*/\1/p')
    export PGPASSWORD="$pgpass"
    migrate_dir=/app/sql/migrations/postgres
    if [ -n "$pg_user" ] && [ -n "$pg_db" ] && [ -d "$migrate_dir" ]; then
      rows=$(psql -U "$pg_user" -d "$pg_db" -Atc \
          "SELECT version, name FROM schema_migrations WHERE version >= 1 ORDER BY version" 2>/dev/null)
      if [ -n "$rows" ]; then
        fixed=0
        while IFS='|' read -r ver name; do
          [ -z "$ver" ] && continue
          # 按 VNNN__<name>.sql 约定定位文件; 兼容 V0NN 三位与 VNNNN 四位编号
          file=""
          for v in "V$(printf '%03d' "$ver")" "V$(printf '%04d' "$ver")"; do
            cand="$migrate_dir/${v}__${name}.sql"
            [ -f "$cand" ] && file="$cand" && break
          done
          if [ -z "$file" ]; then
            echo "[db-wait] ERROR: schema_migrations records V${ver} (${name}) but no matching migration file under $migrate_dir; refusing to heal (possible tamper/corrupt DB)." >&2
            exit 1
          fi
          expected=$(tr -d '\r' < "$file" | sed 's/[[:space:]]*$//' | sha256sum | cut -d' ' -f1)
          recorded=$(psql -U "$pg_user" -d "$pg_db" -Atc \
              "SELECT checksum FROM schema_migrations WHERE version=${ver} AND name='${name}'" 2>/dev/null | tr -d '\n')
          if [ "$recorded" != "$expected" ]; then
            echo "[db-wait] NOTICE: schema_migrations checksum for V${ver} (${name}) drifted; re-seeding recorded value."
            psql -U "$pg_user" -d "$pg_db" -qc \
                "UPDATE schema_migrations SET checksum='${expected}' WHERE version=${ver} AND name='${name}';" \
                || { echo "[db-wait] ERROR: failed to re-seed V${ver} checksum" >&2; exit 1; }
            fixed=$((fixed+1))
          fi
        done <<< "$rows"
        if [ "$fixed" -gt 0 ]; then
          echo "[db-wait] re-seeded $fixed schema_migrations checksum record(s)."
        fi
      else
        echo "[db-wait] NOTICE: schema_migrations empty or unreadable; skipping checksum self-heal."
      fi
    fi
  ;; esac
fi

# 确保 Node.js / npx / npm 拥有可写的缓存目录 (持久化于 minefolio-data 卷中)
if [ -d "/app/data" ] && [ -w "/app/data" ]; then
  mkdir -p /app/data/.npm 2>/dev/null || true
  export npm_config_cache="${npm_config_cache:-/app/data/.npm}"
else
  mkdir -p /tmp/.npm 2>/dev/null || true
  export npm_config_cache="${npm_config_cache:-/tmp/.npm}"
fi

exec /app/minefolio "$@"
