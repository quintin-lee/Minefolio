#!/usr/bin/env bash
# scripts/backup.sh — 运维级数据备份（在部署目标机上运行）
#
# 覆盖范围：
#   - SQLite:   通过容器内 python3 的 sqlite3.backup() 生成一致性快照（避免直接 cp 热库）
#   - Postgres: pg_dump -Fc 逻辑备份
#   - 可选 IM 卷（napcat / gateway / ilink），需 --include-im
# 每个备份目录含 manifest 与 sha256 清单，并自动按 --keep 轮转。
#
# 用法:
#   ./backup.sh [--dir DIR] [--keep N] [--include-im] [--dry-run]
#
# 环境变量:
#   MINEFOLIO_DEPLOY_DIR   部署目录（默认 $PWD，需含 docker-compose.yml）
#   MINEFOLIO_COMPOSE_FILE compose 文件路径（默认 $DEPLOY_DIR/docker-compose.yml）
#   MINEFOLIO_BACKUP_DIR   备份根目录（默认 $DEPLOY_DIR/backups）
#   MINEFOLIO_BACKUP_KEEP  保留份数（默认 7）

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEPLOY_DIR="${MINEFOLIO_DEPLOY_DIR:-$PWD}"
COMPOSE_FILE="${MINEFOLIO_COMPOSE_FILE:-$DEPLOY_DIR/docker-compose.yml}"
BACKUP_ROOT="${MINEFOLIO_BACKUP_DIR:-$DEPLOY_DIR/backups}"
KEEP="${MINEFOLIO_BACKUP_KEEP:-7}"
DRY_RUN=0
INCLUDE_IM=0
CONTAINER="minefolio"
PG_CONTAINER="minefolio-postgres"

usage() {
  cat <<'EOF'
用法: backup.sh [选项]

选项:
  --dir DIR        备份根目录（覆盖 MINEFOLIO_BACKUP_DIR）
  --keep N         保留最近 N 份（默认 7）
  --include-im     同时备份 napcat / gateway / ilink 卷
  --dry-run        只打印将要执行的操作，不落盘
  -h, --help       显示帮助
EOF
}

log()  { printf '[backup] %s\n' "$*"; }
warn() { printf '[backup] WARN: %s\n' "$*" >&2; }
die()  { printf '[backup] ERROR: %s\n' "$*" >&2; exit 1; }

while [ "$#" -gt 0 ]; do
  case "$1" in
    --dir)        [ "$#" -ge 2 ] || die "--dir 缺少参数"; BACKUP_ROOT="$2"; shift 2 ;;
    --keep)       [ "$#" -ge 2 ] || die "--keep 缺少参数"; KEEP="$2"; shift 2 ;;
    --include-im) INCLUDE_IM=1; shift ;;
    --dry-run)    DRY_RUN=1; shift ;;
    -h|--help)    usage; exit 0 ;;
    *)            die "未知参数: $1" ;;
  esac
done

case "$KEEP" in
  ''|*[!0-9]*) die "--keep 必须为非负整数" ;;
esac

command -v docker >/dev/null 2>&1 || die "未找到 docker"
if ! docker info >/dev/null 2>&1; then
  die "Docker daemon 不可达（当前用户是否有 docker 权限？）"
fi
[ -f "$COMPOSE_FILE" ] || warn "compose 文件不存在: $COMPOSE_FILE（备份仍可继续，但恢复时需自行准备）"

if ! docker ps -a --format '{{.Names}}' | grep -qx "$CONTAINER"; then
  die "容器 $CONTAINER 不存在；请先完成部署"
fi

# 通过容器挂载定位真实卷名，避免 compose 项目名差异（群晖目录 minefolio vs 本地 Minefolio）
DATA_VOL="$(docker inspect "$CONTAINER" --format '{{range .Mounts}}{{if eq .Destination "/app/data"}}{{.Name}}{{end}}{{end}}')"
[ -n "$DATA_VOL" ] || die "无法定位容器 $CONTAINER 的 /app/data 卷"
IMAGE="$(docker inspect "$CONTAINER" --format '{{.Config.Image}}')"

DRIVER="$(docker exec "$CONTAINER" printenv MINEFOLIO_DB_DRIVER 2>/dev/null || true)"
DRIVER="${DRIVER:-sqlite}"
[ "$DRIVER" = "sqlite" ] || [ "$DRIVER" = "postgres" ] || die "不支持的 MINEFOLIO_DB_DRIVER: $DRIVER"

TS="$(date -u +%Y%m%dT%H%M%SZ)"
DEST="$BACKUP_ROOT/$TS"
log "目标: $DEST"
log "驱动: $DRIVER  数据卷: $DATA_VOL  镜像: $IMAGE"

if [ "$DRY_RUN" -eq 1 ]; then
  log "[dry-run] 将创建 $DEST 并备份 $DRIVER 数据"$( [ "$INCLUDE_IM" -eq 1 ] && echo " + IM 卷" )
  exit 0
fi

mkdir -p "$DEST"
STARTS="$(date -u +%FT%TZ)"

if [ "$DRIVER" = "sqlite" ]; then
  # 用 sqlite3 backup API 生成一致性快照；容器内 python3 自带 sqlite3 标准库。
  TMP_DB=".mf-backup-${TS}.db"
  PyScript="import sqlite3; s=sqlite3.connect('file:/app/data/minefolio.db?mode=ro',uri=True); d=sqlite3.connect('/app/data/${TMP_DB}'); s.backup(d); d.close(); s.close()"
  if docker exec "$CONTAINER" python3 -c "$PyScript" 2>/dev/null; then
    docker cp "$CONTAINER:/app/data/$TMP_DB" "$DEST/minefolio.db"
    docker exec "$CONTAINER" rm -f "/app/data/$TMP_DB"
    log "SQLite 一致性快照完成"
  else
    warn "python3 sqlite3 backup 不可用，回退为直接复制数据库文件（服务运行中可能不完整）"
    docker cp "$CONTAINER:/app/data/minefolio.db" "$DEST/minefolio.db"
  fi
else
  if ! docker ps --format '{{.Names}}' | grep -qx "$PG_CONTAINER"; then
    die "驱动为 postgres 但容器 $PG_CONTAINER 未运行"
  fi
  PG_USER="$(docker exec "$CONTAINER" printenv MINEFOLIO_DB_USER 2>/dev/null || echo minefolio)"
  PG_NAME="$(docker exec "$CONTAINER" printenv MINEFOLIO_DB_NAME 2>/dev/null || echo minefolio)"
  docker exec "$PG_CONTAINER" pg_dump -U "$PG_USER" -d "$PG_NAME" -Fc > "$DEST/postgres.dump"
  log "Postgres 逻辑备份完成 (user=$PG_USER db=$PG_NAME)"
fi

if [ "$INCLUDE_IM" -eq 1 ]; then
  for spec in "napcat-qq:napcat-qq" "napcat-config:napcat-config" "napcat-plugins:napcat-plugins" \
              "im_gateway_data:im-gateway" "ilink_state:ilink-state"; do
    vol="${spec%%:*}"
    name="${spec##*:}"
    if docker volume inspect "$vol" >/dev/null 2>&1; then
      docker run --rm --entrypoint sh \
        -v "$vol:/src:ro" -v "$DEST:/dst" "$IMAGE" \
        -c "tar czf /dst/${name}.tar.gz -C /src ." || warn "卷 $vol 备份失败"
      log "卷 $vol 已备份"
    else
      warn "卷 $vol 不存在，跳过"
    fi
  done
fi

# 清单与校验和
{
  echo "backup_ts=$TS"
  echo "started_at=$STARTS"
  echo "finished_at=$(date -u +%FT%TZ)"
  echo "driver=$DRIVER"
  echo "data_volume=$DATA_VOL"
  echo "image=$IMAGE"
  echo "include_im=$INCLUDE_IM"
  if [ "$DRIVER" = "postgres" ]; then
    echo "pg_user=${PG_USER:-minefolio}"
    echo "pg_name=${PG_NAME:-minefolio}"
  fi
  echo "files:"
  for f in "$DEST"/*; do
    [ -f "$f" ] || continue
    printf '  %s\n' "$(basename "$f")"
  done
} > "$DEST/manifest.txt"

# 校验和排除 manifest 与 checksums 自身
( cd "$DEST" && sha256sum -- * 2>/dev/null \
    | grep -v -E ' (manifest\.txt|checksums\.sha256)$' \
    > checksums.sha256 ) || true

# 轮转（KEEP=0 表示不清理）
if [ "$KEEP" -gt 0 ]; then
  OLD="$(ls -1dt "$BACKUP_ROOT"/* 2>/dev/null | tail -n +$((KEEP + 1)) || true)"
  if [ -n "$OLD" ]; then
    printf '%s\n' "$OLD" | while IFS= read -r d; do
      [ -n "$d" ] && rm -rf "$d" && log "已清理旧备份: $d"
    done
  fi
fi

log "完成: $DEST"
printf '%s\n' "$DEST"
