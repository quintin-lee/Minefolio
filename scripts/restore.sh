#!/usr/bin/env bash
# scripts/restore.sh — 从运维备份恢复（在部署目标机上运行）
#
# 安全约束：
#   - 默认先自动备份当前状态，再执行恢复
#   - 校验 sha256 清单，校验失败立即中止
#   - 破坏性步骤需显式 --yes 或输入 YES
#   - --dry-run 只打印计划
#
# 用法:
#   ./restore.sh <备份目录|latest> [--yes] [--skip-backup] [--dry-run]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEPLOY_DIR="${MINEFOLIO_DEPLOY_DIR:-$PWD}"
COMPOSE_FILE="${MINEFOLIO_COMPOSE_FILE:-$DEPLOY_DIR/docker-compose.yml}"
BACKUP_ROOT="${MINEFOLIO_BACKUP_DIR:-$DEPLOY_DIR/backups}"
CONTAINER="minefolio"
PG_CONTAINER="minefolio-postgres"

usage() {
  cat <<'EOF'
用法: restore.sh <备份目录|latest> [选项]

选项:
  --yes          跳过交互确认（破坏性操作仍需明确指定）
  --skip-backup  跳过恢复前的自动备份（不推荐）
  --dry-run      只打印计划，不执行
  -h, --help     显示帮助
EOF
}

log()  { printf '[restore] %s\n' "$*"; }
warn() { printf '[restore] WARN: %s\n' "$*" >&2; }
die()  { printf '[restore] ERROR: %s\n' "$*" >&2; exit 1; }

TARGET=""
ASSUME_YES=0
SKIP_BACKUP=0
DRY_RUN=0
while [ "$#" -gt 0 ]; do
  case "$1" in
    --yes)         ASSUME_YES=1; shift ;;
    --skip-backup) SKIP_BACKUP=1; shift ;;
    --dry-run)     DRY_RUN=1; shift ;;
    -h|--help)     usage; exit 0 ;;
    -*)            die "未知参数: $1" ;;
    *)             [ -z "$TARGET" ] || die "只能指定一个备份目标"; TARGET="$1"; shift ;;
  esac
done

[ -n "$TARGET" ] || die "请指定备份目录或 latest（--help 查看用法）"
if [ "$TARGET" = "latest" ]; then
  [ -d "$BACKUP_ROOT" ] || die "备份根目录不存在: $BACKUP_ROOT"
  TARGET="$(ls -1dt "$BACKUP_ROOT"/* 2>/dev/null | head -1 || true)"
  [ -n "$TARGET" ] || die "没有可用备份"
fi
[ -d "$TARGET" ] || die "备份目录不存在: $TARGET"
[ -f "$TARGET/manifest.txt" ] || die "备份缺少 manifest.txt，目录可能不是有效备份"

# 校验完整性
if [ -f "$TARGET/checksums.sha256" ]; then
  ( cd "$TARGET" && sha256sum -c checksums.sha256 ) || die "备份校验失败，拒绝恢复"
else
  warn "备份缺少 checksums.sha256，无法校验完整性"
fi

DRIVER="$(sed -n 's/^driver=//p' "$TARGET/manifest.txt" | head -1)"
DRIVER="${DRIVER:-sqlite}"
IMAGE_REC="$(sed -n 's/^image=//p' "$TARGET/manifest.txt" | head -1)"
[ "$DRIVER" = "sqlite" ] || [ "$DRIVER" = "postgres" ] || die "未知驱动: $DRIVER"

command -v docker >/dev/null 2>&1 || die "未找到 docker"
docker info >/dev/null 2>&1 || die "Docker daemon 不可达"
[ -f "$COMPOSE_FILE" ] || die "compose 文件不存在: $COMPOSE_FILE"

if docker ps -a --format '{{.Names}}' | grep -qx "$CONTAINER"; then
  DATA_VOL="$(docker inspect "$CONTAINER" --format '{{range .Mounts}}{{if eq .Destination "/app/data"}}{{.Name}}{{end}}{{end}}')"
  IMAGE="$(docker inspect "$CONTAINER" --format '{{.Config.Image}}')"
else
  IMAGE="${IMAGE_REC:-}"
  # 容器不存在时从 compose 解析卷名
  DATA_VOL="$(docker compose -f "$COMPOSE_FILE" config 2>/dev/null \
    | sed -n '/source: .*minefolio-data/{s/.*source: //;p;q}')"
fi
[ -n "$DATA_VOL" ] || die "无法定位数据卷"
[ -n "$IMAGE" ] || die "无法定位镜像，请设置镜像或确保容器存在"

log "备份: $TARGET"
log "驱动: $DRIVER  数据卷: $DATA_VOL  镜像: $IMAGE"
log "备份文件:"
sed -n '/^files:/,$p' "$TARGET/manifest.txt"

if [ "$ASSUME_YES" -ne 1 ]; then
  printf '即将停止服务并用上述备份覆盖当前数据。输入 YES 确认: '
  read -r ans
  [ "$ans" = "YES" ] || die "已取消"
fi

if [ "$DRY_RUN" -eq 1 ]; then
  log "[dry-run] 恢复前自动备份: $( [ "$SKIP_BACKUP" -eq 1 ] && echo 跳过 || echo 执行 )"
  log "[dry-run] 停止服务 → 恢复 $DRIVER 数据 → 启动服务 → 校验 /healthz"
  exit 0
fi

if [ "$SKIP_BACKUP" -ne 1 ]; then
  log "恢复前自动备份当前状态..."
  "$SCRIPT_DIR/backup.sh" --dir "$BACKUP_ROOT/pre-restore-$(date -u +%Y%m%dT%H%M%SZ)" --keep 3
fi

log "停止服务..."
docker compose -f "$COMPOSE_FILE" down

if [ "$DRIVER" = "sqlite" ]; then
  [ -f "$TARGET/minefolio.db" ] || die "备份缺少 minefolio.db"
  docker run --rm --entrypoint sh \
    -v "$DATA_VOL:/app/data" -v "$TARGET:/src:ro" "$IMAGE" \
    -c "cp /src/minefolio.db /app/data/minefolio.db && chown 10001:10001 /app/data/minefolio.db 2>/dev/null || true"
  log "SQLite 数据已恢复"
else
  [ -f "$TARGET/postgres.dump" ] || die "备份缺少 postgres.dump"
  PG_USER="$(sed -n 's/^pg_user=//p' "$TARGET/manifest.txt" | head -1)"
  PG_NAME="$(sed -n 's/^pg_name=//p' "$TARGET/manifest.txt" | head -1)"
  PG_USER="${PG_USER:-minefolio}"
  PG_NAME="${PG_NAME:-minefolio}"
  PG_PASS="${MINEFOLIO_DB_PASSWORD:-}"
  [ -n "$PG_PASS" ] || die "恢复 Postgres 需要 MINEFOLIO_DB_PASSWORD 环境变量"

  log "启动 PostgreSQL..."
  docker compose -f "$COMPOSE_FILE" --profile postgres up -d db-postgres
  for _ in $(seq 1 60); do
    if docker exec "$PG_CONTAINER" pg_isready -U "$PG_USER" -d "$PG_NAME" >/dev/null 2>&1; then break; fi
    sleep 1
  done
  docker exec "$PG_CONTAINER" pg_isready -U "$PG_USER" -d "$PG_NAME" >/dev/null 2>&1 \
    || die "PostgreSQL 未就绪"
  log "执行 pg_restore..."
  docker exec -i -e PGPASSWORD="$PG_PASS" "$PG_CONTAINER" \
    pg_restore -U "$PG_USER" -d "$PG_NAME" --clean --if-exists < "$TARGET/postgres.dump"
  log "Postgres 数据已恢复"
fi

log "启动服务..."
docker compose -f "$COMPOSE_FILE" up -d

log "校验健康状态..."
ok=0
for _ in $(seq 1 30); do
  if docker exec "$CONTAINER" curl -fsS --max-time 3 http://127.0.0.1:8080/healthz >/dev/null 2>&1; then
    ok=1; break
  fi
  sleep 2
done
if [ "$ok" -ne 1 ]; then
  warn "/healthz 未通过，请手动检查: docker compose -f '$COMPOSE_FILE' logs --tail=100"
  exit 1
fi
log "恢复完成且健康检查通过"
