#!/usr/bin/env bash
# scripts/rollback.sh — 回滚部署到历史版本（在部署目标机上运行）
#
# 版本历史由 .gitea/workflows/deploy.yml 在每次成功部署后写入
#   .deploy-history（每行一个版本，最新在末行）与 .deployed-version
# 回滚只切换镜像版本，不改动数据库；如涉及破坏性 schema 变更，请先备份。
#
# 用法:
#   ./rollback.sh                 # 回滚到上一个版本
#   ./rollback.sh <version>       # 回滚到指定版本
#   ./rollback.sh --list          # 列出历史版本
#   ./rollback.sh --dry-run       # 只打印计划

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEPLOY_DIR="${MINEFOLIO_DEPLOY_DIR:-$PWD}"
COMPOSE_FILE="${MINEFOLIO_COMPOSE_FILE:-$DEPLOY_DIR/docker-compose.yml}"
ENV_FILE="${MINEFOLIO_ENV_FILE:-$DEPLOY_DIR/.env}"
HISTORY_FILE="${MINEFOLIO_DEPLOY_HISTORY:-$DEPLOY_DIR/.deploy-history}"
CONTAINER="minefolio"

usage() {
  cat <<'EOF'
用法: rollback.sh [版本|--list] [选项]

选项:
  --list      列出已部署版本历史
  --yes       跳过交互确认
  --dry-run   只打印计划，不执行
  -h, --help  显示帮助
EOF
}

log()  { printf '[rollback] %s\n' "$*"; }
warn() { printf '[rollback] WARN: %s\n' "$*" >&2; }
die()  { printf '[rollback] ERROR: %s\n' "$*" >&2; exit 1; }

TARGET=""
LIST=0
ASSUME_YES=0
DRY_RUN=0
while [ "$#" -gt 0 ]; do
  case "$1" in
    --list)    LIST=1; shift ;;
    --yes)     ASSUME_YES=1; shift ;;
    --dry-run) DRY_RUN=1; shift ;;
    -h|--help) usage; exit 0 ;;
    -*)        die "未知参数: $1" ;;
    *)         [ -z "$TARGET" ] || die "只能指定一个版本"; TARGET="$1"; shift ;;
  esac
done

[ -f "$HISTORY_FILE" ] || die "版本历史不存在: $HISTORY_FILE（该部署可能不是由新 workflow 部署的）"
mapfile -t HISTORY < <(grep -v '^[[:space:]]*$' "$HISTORY_FILE" || true)
[ "${#HISTORY[@]}" -ge 1 ] || die "版本历史为空"

if [ "$LIST" -eq 1 ]; then
  i=0
  for v in "${HISTORY[@]}"; do
    i=$((i + 1))
    printf '%3d  %s\n' "$i" "$v"
  done
  exit 0
fi

CURRENT="${HISTORY[$(( ${#HISTORY[@]} - 1 ))]}"
if [ -z "$TARGET" ]; then
  [ "${#HISTORY[@]}" -ge 2 ] || die "只有一个历史版本，无法自动回滚；请显式指定版本"
  TARGET="${HISTORY[$(( ${#HISTORY[@]} - 2 ))]}"
fi

found=0
for v in "${HISTORY[@]}"; do
  [ "$v" = "$TARGET" ] && found=1 && break
done
[ "$found" -eq 1 ] || die "版本 $TARGET 不在历史记录中（用 --list 查看）"
[ "$TARGET" != "$CURRENT" ] || die "目标版本与当前版本相同: $TARGET"

log "当前版本: $CURRENT"
log "目标版本: $TARGET"
[ -f "$COMPOSE_FILE" ] || die "compose 文件不存在: $COMPOSE_FILE"
[ -f "$ENV_FILE" ] || die "环境文件不存在: $ENV_FILE"

if [ "$ASSUME_YES" -ne 1 ]; then
  printf '确认回滚到 %s？输入 YES 继续: ' "$TARGET"
  read -r ans
  [ "$ans" = "YES" ] || die "已取消"
fi

if [ "$DRY_RUN" -eq 1 ]; then
  log "[dry-run] 将 $ENV_FILE 中 MINEFOLIO_VERSION 设为 $TARGET，然后 pull + up -d 并校验 /healthz"
  exit 0
fi

# 更新版本变量（保留其他环境变量不动）
if grep -q '^MINEFOLIO_VERSION=' "$ENV_FILE"; then
  sed -i.bak "s|^MINEFOLIO_VERSION=.*|MINEFOLIO_VERSION=${TARGET}|" "$ENV_FILE"
else
  printf 'MINEFOLIO_VERSION=%s\n' "$TARGET" >> "$ENV_FILE"
fi
rm -f "$ENV_FILE.bak"
chmod 600 "$ENV_FILE"

log "拉取镜像并切换服务..."
docker compose -f "$COMPOSE_FILE" --env-file "$ENV_FILE" pull
docker compose -f "$COMPOSE_FILE" --env-file "$ENV_FILE" up -d

log "校验健康状态..."
ok=0
for _ in $(seq 1 30); do
  if docker exec "$CONTAINER" curl -fsS --max-time 3 http://127.0.0.1:8080/healthz >/dev/null 2>&1; then
    ok=1; break
  fi
  sleep 2
done
if [ "$ok" -ne 1 ]; then
  warn "/healthz 未通过；可再次执行 rollback.sh 回滚到更早版本，或检查日志"
  exit 1
fi

# 记录回滚后的版本（与部署成功路径一致）
printf '%s\n' "$TARGET" >> "$HISTORY_FILE"
printf '%s\n' "$TARGET" > "$(dirname "$HISTORY_FILE")/.deployed-version"
log "回滚完成: $CURRENT → $TARGET"
