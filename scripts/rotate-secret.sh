#!/usr/bin/env bash
# scripts/rotate-secret.sh — JWT secret 轮换（在部署目标机上运行）
#
# ⚠️ 影响：轮换后所有已签发的 JWT 立即失效，全部用户需重新登录。
# 因此默认只展示计划，必须显式 --yes 才执行；secret 不明文回显。
#
# 用法:
#   ./rotate-secret.sh --show-current       # 只查看当前 secret 指纹
#   ./rotate-secret.sh --dry-run            # 查看轮换计划
#   ./rotate-secret.sh --yes                # 真正执行轮换
#   ./rotate-secret.sh --secret VALUE --yes # 使用指定的新 secret

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEPLOY_DIR="${MINEFOLIO_DEPLOY_DIR:-$PWD}"
ENV_FILE="${MINEFOLIO_ENV_FILE:-$DEPLOY_DIR/.env}"
COMPOSE_FILE="${MINEFOLIO_COMPOSE_FILE:-$DEPLOY_DIR/docker-compose.yml}"
CONTAINER="minefolio"

usage() {
  cat <<'EOF'
用法: rotate-secret.sh [选项]

选项:
  --show-current   显示当前 secret 指纹（不明文）
  --secret VALUE   指定新 secret（默认用 openssl 生成 32 字节随机值）
  --dry-run        只打印计划，不修改
  --yes            确认执行轮换（会使所有会话失效）
  -h, --help       显示帮助
EOF
}

log()  { printf '[rotate] %s\n' "$*"; }
die()  { printf '[rotate] ERROR: %s\n' "$*" >&2; exit 1; }

SHOW_CURRENT=0
NEW_SECRET=""
DRY_RUN=0
ASSUME_YES=0
while [ "$#" -gt 0 ]; do
  case "$1" in
    --show-current) SHOW_CURRENT=1; shift ;;
    --secret) [ "$#" -ge 2 ] || die "--secret 缺少参数"; NEW_SECRET="$2"; shift 2 ;;
    --dry-run) DRY_RUN=1; shift ;;
    --yes) ASSUME_YES=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "未知参数: $1" ;;
  esac
done

[ -f "$ENV_FILE" ] || die "环境文件不存在: $ENV_FILE"
[ -f "$COMPOSE_FILE" ] || die "compose 文件不存在: $COMPOSE_FILE"

current="$(sed -n 's/^MINEFOLIO_JWT_SECRET=//p' "$ENV_FILE" | head -1 || true)"
fingerprint() {
  local s="$1"
  printf '%s… (sha256=%s)' "${s:0:4}" "$(printf '%s' "$s" | sha256sum | cut -c1-8)"
}

if [ "$SHOW_CURRENT" -eq 1 ]; then
  if [ -z "$current" ]; then
    log "当前未设置 MINEFOLIO_JWT_SECRET"
  else
    log "当前 secret: $(fingerprint "$current")"
  fi
  exit 0
fi

if [ -z "$NEW_SECRET" ]; then
  command -v openssl >/dev/null 2>&1 || die "未找到 openssl，请用 --secret 显式提供"
  NEW_SECRET="$(openssl rand -hex 32)"
fi
[ "${#NEW_SECRET}" -ge 32 ] || die "secret 长度不足 32 字符，拒绝使用弱密钥"

log "当前 secret: $( [ -n "$current" ] && fingerprint "$current" || echo '(未设置)' )"
log "新 secret:   $(fingerprint "$NEW_SECRET")"
log "⚠️  轮换会使所有已登录会话失效，用户需重新登录。"

if [ "$DRY_RUN" -eq 1 ]; then
  log "[dry-run] 将更新 $ENV_FILE 并强制重建容器 $CONTAINER"
  exit 0
fi

if [ "$ASSUME_YES" -ne 1 ]; then
  printf '确认轮换 JWT secret？输入 YES 继续: '
  read -r ans
  [ "$ans" = "YES" ] || die "已取消"
fi

command -v docker >/dev/null 2>&1 || die "未找到 docker"
docker info >/dev/null 2>&1 || die "Docker daemon 不可达"

# 备份环境文件（含旧 secret，权限收紧）
cp -p "$ENV_FILE" "$ENV_FILE.bak"
chmod 600 "$ENV_FILE.bak"

if grep -q '^MINEFOLIO_JWT_SECRET=' "$ENV_FILE"; then
  sed -i "s|^MINEFOLIO_JWT_SECRET=.*|MINEFOLIO_JWT_SECRET=${NEW_SECRET}|" "$ENV_FILE"
else
  printf 'MINEFOLIO_JWT_SECRET=%s\n' "$NEW_SECRET" >> "$ENV_FILE"
fi
chmod 600 "$ENV_FILE"
log "环境文件已更新（备份: $ENV_FILE.bak）"

# restart 不会重新读取 .env 中的环境变量，必须 force-recreate
docker compose -f "$COMPOSE_FILE" --env-file "$ENV_FILE" up -d --force-recreate "$CONTAINER"

log "校验健康状态..."
ok=0
for _ in $(seq 1 30); do
  if docker exec "$CONTAINER" curl -fsS --max-time 3 http://127.0.0.1:8080/healthz >/dev/null 2>&1; then
    ok=1; break
  fi
  sleep 2
done
if [ "$ok" -ne 1 ]; then
  die "/healthz 未通过：请检查 $ENV_FILE.bak 并考虑回滚 secret"
fi

log "轮换完成: $( [ -n "$current" ] && fingerprint "$current" || echo '(未设置)' ) → $(fingerprint "$NEW_SECRET")"
log "请通知所有用户重新登录；确认无误后可删除 $ENV_FILE.bak"
