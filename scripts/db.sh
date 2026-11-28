#!/usr/bin/env bash
# scripts/db.sh — 数据库运维（在部署目标机上运行）
#
# 子命令:
#   status          显示数据库驱动、已应用迁移版本与磁盘迁移文件数
#   consistency     对比磁盘迁移版本集合与库中已应用版本，报告差异
#   optimize        执行 VACUUM / ANALYZE（SQLite 与 Postgres 分别处理）
#   heal-checksums  独立调用后端的迁移 checksum 自愈子命令
#
# 用法: ./db.sh <subcommand> [--dry-run|--help]

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONTAINER="minefolio"
PG_CONTAINER="minefolio-postgres"

usage() {
  cat <<'EOF'
用法: db.sh <subcommand> [选项]

子命令:
  status           显示迁移状态
  consistency      校验磁盘与数据库迁移版本集合一致
  optimize         执行 VACUUM/ANALYZE
  heal-checksums   修复已应用迁移的 checksum 漂移

选项:
  --dry-run        只打印计划
  -h, --help       显示帮助
EOF
}

log()  { printf '[db] %s\n' "$*"; }
die()  { printf '[db] ERROR: %s\n' "$*" >&2; exit 1; }

[ "$#" -ge 1 ] || { usage; exit 1; }
case "$1" in
  -h|--help) usage; exit 0 ;;
esac
CMD="$1"; shift
DRY_RUN=0
while [ "$#" -gt 0 ]; do
  case "$1" in
    --dry-run) DRY_RUN=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "未知参数: $1" ;;
  esac
done

command -v docker >/dev/null 2>&1 || die "未找到 docker"
docker info >/dev/null 2>&1 || die "Docker daemon 不可达"
docker ps --format '{{.Names}}' | grep -qx "$CONTAINER" || die "容器 $CONTAINER 未运行"

DRIVER="$(docker exec "$CONTAINER" printenv MINEFOLIO_DB_DRIVER 2>/dev/null || echo sqlite)"
[ "$DRIVER" = "sqlite" ] || [ "$DRIVER" = "postgres" ] || die "未知驱动: $DRIVER"

# 从容器内 /app/sql/migrations/<driver> 读取磁盘迁移版本（与镜像内实际文件一致）
disk_versions() {
  docker exec "$CONTAINER" sh -c \
    "ls /app/sql/migrations/${DRIVER} 2>/dev/null | sed -n 's/^V\\([0-9][0-9]*\\)__.*/\\1/p' | sort -n"
}

applied_versions() {
  if [ "$DRIVER" = "sqlite" ]; then
    docker exec "$CONTAINER" python3 -c \
      "import sqlite3;c=sqlite3.connect('file:/app/data/minefolio.db?mode=ro',uri=True);[print(r[0]) for r in c.execute('select version from schema_migrations order by version asc')]" 2>/dev/null
  else
    PG_USER="$(docker exec "$CONTAINER" printenv MINEFOLIO_DB_USER 2>/dev/null || echo minefolio)"
    PG_NAME="$(docker exec "$CONTAINER" printenv MINEFOLIO_DB_NAME 2>/dev/null || echo minefolio)"
    docker exec "$PG_CONTAINER" psql -U "$PG_USER" -d "$PG_NAME" -tAc \
      'select version from schema_migrations order by version asc' 2>/dev/null | tr -d ' '
  fi
}

case "$CMD" in
  status)
    log "驱动: $DRIVER"
    printf '磁盘迁移文件: %s 个\n' "$(disk_versions | grep -c . || true)"
    printf '库中已应用:   %s 个\n' "$(applied_versions | grep -c . || true)"
    log "最新已应用版本: $(applied_versions | tail -1 || echo unknown)"
    ;;

  consistency)
    # 磁盘文件名为 V001 补零形式，库里是数值 1；统一去前导零并按字典序排序供 comm 比较
    DISK="$(disk_versions | sed 's/^0*//' | LC_ALL=C sort)"
    APPLIED="$(applied_versions | sed 's/^0*//' | LC_ALL=C sort)"
    MISSING="$(comm -23 <(printf '%s\n' "$DISK") <(printf '%s\n' "$APPLIED"))"
    EXTRA="$(comm -13 <(printf '%s\n' "$DISK") <(printf '%s\n' "$APPLIED"))"
    if [ -z "$MISSING" ] && [ -z "$EXTRA" ]; then
      log "一致：磁盘与数据库迁移版本集合相同"
      exit 0
    fi
    [ -z "$MISSING" ] || { printf '未在库中应用: %s\n' "$(printf '%s ' "$MISSING")"; }
    [ -z "$EXTRA" ]   || { printf '库中有但磁盘缺失: %s\n' "$(printf '%s ' "$EXTRA")"; }
    die "迁移版本集合不一致"
    ;;

  optimize)
    if [ "$DRY_RUN" -eq 1 ]; then
      log "[dry-run] 将执行 $DRIVER VACUUM/ANALYZE"
      exit 0
    fi
    if [ "$DRIVER" = "sqlite" ]; then
      docker exec "$CONTAINER" python3 -c \
        "import sqlite3;c=sqlite3.connect('/app/data/minefolio.db',isolation_level=None);c.execute('VACUUM');c.execute('ANALYZE');c.close()" \
        || die "SQLite VACUUM/ANALYZE 失败"
    else
      PG_USER="$(docker exec "$CONTAINER" printenv MINEFOLIO_DB_USER 2>/dev/null || echo minefolio)"
      PG_NAME="$(docker exec "$CONTAINER" printenv MINEFOLIO_DB_NAME 2>/dev/null || echo minefolio)"
      docker exec "$PG_CONTAINER" psql -U "$PG_USER" -d "$PG_NAME" -c 'VACUUM ANALYZE;' \
        || die "Postgres VACUUM ANALYZE 失败"
    fi
    log "优化完成"
    ;;

  heal-checksums)
    if [ "$DRY_RUN" -eq 1 ]; then
      log "[dry-run] 将在容器内执行 /app/minefolio --heal-migration-checksums"
      exit 0
    fi
    docker exec "$CONTAINER" /app/minefolio --heal-migration-checksums \
      || die "checksum 自愈失败（迁移文件缺失时拒绝修正）"
    log "checksum 自愈完成"
    ;;

  *) usage; die "未知子命令: $CMD" ;;
esac
