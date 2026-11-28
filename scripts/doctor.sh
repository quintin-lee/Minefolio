#!/usr/bin/env bash
# scripts/doctor.sh — 一键健康诊断（在部署目标机上运行）
#
# 聚合输出：Docker 状态、容器健康、资源占用、磁盘、数据库迁移版本、
# /healthz 探测、日志错误、部署版本与最近备份。
# 单项失败不影响其他检查，最后汇总 OK/WARN/FAIL。
#
# 用法: ./doctor.sh [--short] [--help]

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEPLOY_DIR="${MINEFOLIO_DEPLOY_DIR:-$PWD}"
BACKUP_ROOT="${MINEFOLIO_BACKUP_DIR:-$DEPLOY_DIR/backups}"
CONTAINER="minefolio"
PG_CONTAINER="minefolio-postgres"

usage() {
  cat <<'EOF'
用法: doctor.sh [选项]

选项:
  --short     只输出非 OK 项
  -h, --help  显示帮助
EOF
}

SHORT=0
while [ "$#" -gt 0 ]; do
  case "$1" in
    --short) SHORT=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) printf '[doctor] ERROR: 未知参数: %s\n' "$1" >&2; exit 1 ;;
  esac
done

OK=0; WARN=0; FAIL=0
section() { printf '\n== %s ==\n' "$*"; }
ok()   { OK=$((OK+1));   [ "$SHORT" -eq 1 ] || printf '  [OK]   %s\n' "$*"; }
warn() { WARN=$((WARN+1)); printf '  [WARN] %s\n' "$*"; }
fail() { FAIL=$((FAIL+1)); printf '  [FAIL] %s\n' "$*"; }
info() { [ "$SHORT" -eq 1 ] || printf '         %s\n' "$*"; }

printf 'Minefolio 健康诊断  %s\n' "$(date -u +%FT%TZ)"

section "Docker"
if command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
  ok "Docker daemon 可用"
  info "版本: $(docker version --format '{{.Server.Version}}' 2>/dev/null || echo unknown)"
else
  fail "Docker daemon 不可用（后续容器检查将跳过）"
fi

section "容器状态"
if docker info >/dev/null 2>&1; then
  for c in minefolio minefolio-nginx "$PG_CONTAINER" minefolio-napcat minefolio-im-gateway minefolio-ilink-gateway; do
    state="$(docker inspect "$c" --format '{{.State.Status}}' 2>/dev/null || true)"
    if [ -z "$state" ]; then
      warn "容器 $c 不存在（未启用对应 profile 属正常）"
    elif [ "$state" = "running" ]; then
      health="$(docker inspect "$c" --format '{{if .State.Health}}{{.State.Health.Status}}{{else}}none{{end}}' 2>/dev/null || echo none)"
      if [ "$health" = "healthy" ] || [ "$health" = "none" ]; then
        ok "容器 $c running (health=$health)"
      else
        warn "容器 $c running 但 health=$health"
      fi
    else
      fail "容器 $c 状态为 $state"
    fi
  done

  section "资源占用"
  docker stats --no-stream --format 'table {{.Name}}\t{{.CPUPerc}}\t{{.MemUsage}}' 2>/dev/null | head -10 || true

  section "磁盘"
  df -h "$DEPLOY_DIR" 2>/dev/null | tail -1 || true
  docker system df 2>/dev/null || true
else
  warn "跳过容器/资源/磁盘检查"
fi

section "数据库与迁移"
DRIVER="$(docker exec "$CONTAINER" printenv MINEFOLIO_DB_DRIVER 2>/dev/null || echo sqlite)"
if docker ps --format '{{.Names}}' | grep -qx "$CONTAINER"; then
  case "$DRIVER" in
    sqlite)
      MIG="$(docker exec "$CONTAINER" python3 -c "import sqlite3;c=sqlite3.connect('file:/app/data/minefolio.db?mode=ro',uri=True);print(c.execute('select max(version) from schema_migrations').fetchone()[0])" 2>/dev/null || true)"
      if [ -n "$MIG" ]; then
        ok "SQLite 可访问，最新迁移版本: $MIG"
      else
        fail "SQLite 数据库不可访问或 schema_migrations 缺失"
      fi
      ;;
    postgres)
      PG_USER="$(docker exec "$CONTAINER" printenv MINEFOLIO_DB_USER 2>/dev/null || echo minefolio)"
      PG_NAME="$(docker exec "$CONTAINER" printenv MINEFOLIO_DB_NAME 2>/dev/null || echo minefolio)"
      MIG="$(docker exec "$PG_CONTAINER" psql -U "$PG_USER" -d "$PG_NAME" -tAc 'select max(version) from schema_migrations' 2>/dev/null | tr -d ' ' || true)"
      if [ -n "$MIG" ]; then
        ok "Postgres 可访问，最新迁移版本: $MIG"
      else
        fail "Postgres 不可访问（容器未运行或凭据错误）"
      fi
      ;;
    *) fail "未知 MINEFOLIO_DB_DRIVER: $DRIVER" ;;
  esac
else
  warn "容器 $CONTAINER 未运行，跳过数据库检查"
fi

section "服务健康"
if docker ps --format '{{.Names}}' | grep -qx "$CONTAINER"; then
  if docker exec "$CONTAINER" curl -fsS --max-time 5 http://127.0.0.1:8080/healthz >/dev/null 2>&1; then
    ok "/healthz 返回 200"
  else
    fail "/healthz 探测失败"
  fi
else
  warn "跳过 /healthz（容器未运行）"
fi

section "日志错误（最近 100 行）"
if docker ps -a --format '{{.Names}}' | grep -qx "$CONTAINER"; then
  ERRORS="$(docker logs --tail=100 "$CONTAINER" 2>&1 | grep -iE 'error|fatal|panic' | tail -5 || true)"
  if [ -n "$ERRORS" ]; then
    warn "发现疑似错误日志:"
    printf '%s\n' "$ERRORS" | sed 's/^/         /'
  else
    ok "最近日志无明显错误"
  fi
else
  warn "跳过日志检查"
fi

section "部署与备份"
if [ -f "$DEPLOY_DIR/.deployed-version" ]; then
  ok "当前版本: $(cat "$DEPLOY_DIR/.deployed-version")"
else
  warn "无 .deployed-version（可能由旧 workflow 部署）"
fi
LATEST="$(ls -1dt "$BACKUP_ROOT"/* 2>/dev/null | head -1 || true)"
if [ -n "$LATEST" ]; then
  ok "最近备份: $(basename "$LATEST")"
else
  warn "尚无备份，建议执行 minefolio.sh backup"
fi

printf '\n汇总: %d OK, %d WARN, %d FAIL\n' "$OK" "$WARN" "$FAIL"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
