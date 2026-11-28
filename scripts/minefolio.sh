#!/usr/bin/env bash
# scripts/minefolio.sh — 统一运维入口
#
# 将散落的运维脚本收拢到单一 CLI；支持在本地或远程部署机执行同一套命令。
# 远程执行要求目标机的部署目录已包含 scripts/（由 deploy workflow 自动上传）。
#
# 用法:
#   ./minefolio.sh <command> [args...]
#   ./minefolio.sh --host user@nas.example.com doctor
#   ./minefolio.sh --help

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
  cat <<'EOF'
Minefolio 运维工具集

用法:
  minefolio.sh [--host USER@HOST] [--dry-run] <command> [args...]

命令:
  dev             启动前后端开发服务器
  build           构建生产版本
  test            运行测试（支持 fast/slow/integration 分类）
  check           运行所有静态契约检查
  backup          数据备份（--help 查看备份选项）
  restore         从备份恢复
  rollback        回滚部署到历史版本
  doctor          一键健康诊断
  db              数据库运维（迁移状态/一致性/优化）
  rotate-secret   JWT secret 轮换（需显式确认）
  release         版本与 changelog 管理
  changelog       生成变更日志

全局选项:
  --host USER@HOST  在远程部署机执行（需配 SSH_PASS 或 SSH_KEY）
  --dry-run         透传给支持该选项的子命令
  -h, --help        显示帮助

示例:
  minefolio.sh build
  minefolio.sh --host root@nas doctor
  minefolio.sh backup --include-im --keep 14
EOF
}

log()  { printf '[minefolio] %s\n' "$*"; }
die()  { printf '[minefolio] ERROR: %s\n' "$*" >&2; exit 1; }

HOST=""
DRY_RUN=0
while [ "$#" -gt 0 ]; do
  case "$1" in
    --host)  [ "$#" -ge 2 ] || die "--host 缺少参数"; HOST="$2"; shift 2 ;;
    --dry-run) DRY_RUN=1; shift ;;
    -h|--help|help) usage; exit 0 ;;
    -*)      die "未知全局选项: $1（子命令选项请放在命令之后）" ;;
    *)       break ;;
  esac
done

[ "$#" -ge 1 ] || { usage; exit 1; }
CMD="$1"; shift

# 命令到脚本的映射
case "$CMD" in
  dev)           SCRIPT="dev.sh";;
  build)         SCRIPT="build.sh";;
  test)          SCRIPT="test.sh";;
  backup)        SCRIPT="backup.sh";;
  restore)       SCRIPT="restore.sh";;
  rollback)      SCRIPT="rollback.sh";;
  doctor)        SCRIPT="doctor.sh";;
  db)            SCRIPT="db.sh";;
  rotate-secret) SCRIPT="rotate-secret.sh";;
  release)       SCRIPT="release.sh";;
  changelog)     SCRIPT="release.sh"; set -- changelog "$@";;
  check)         SCRIPT="";;
  *)             die "未知命令: $CMD（用 --help 查看）";;
esac

# 子命令选项转义，供远程 shell 安全重建
quoted_args() {
  local out=""
  local a
  for a in "$@"; do
    printf -v a '%q' "$a"
    out="$out $a"
  done
  printf '%s' "$out"
}

if [ -n "$HOST" ]; then
  REMOTE_DIR="${MINEFOLIO_REMOTE_DIR:-/volume3/Synology/docker/minefolio}"
  SSH_PORT="${MINEFOLIO_SSH_PORT:-22}"
  SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=30"
  if [ -n "${MINEFOLIO_SSH_KEY:-}" ]; then
    printf '%s\n' "$MINEFOLIO_SSH_KEY" > /tmp/mf_ops_key
    chmod 600 /tmp/mf_ops_key
    trap 'rm -f /tmp/mf_ops_key' EXIT
    SSH_OPTS="-i /tmp/mf_ops_key $SSH_OPTS"
  fi
  ssh_run() {
    if [ -n "${MINEFOLIO_SSH_PASS:-}" ]; then
      sshpass -p "$MINEFOLIO_SSH_PASS" ssh $SSH_OPTS -p "$SSH_PORT" "$HOST" "$@"
    else
      ssh $SSH_OPTS -p "$SSH_PORT" "$HOST" "$@"
    fi
  }

  if [ "$CMD" = "check" ]; then
    REMOTE_CMD="cd '$REMOTE_DIR' && bash scripts/check_db_contract.sh && bash scripts/check_migration_versions.sh"
  else
    [ -f "$SCRIPT_DIR/$SCRIPT" ] || die "本地缺少 $SCRIPT，无法确认远程版本"
    ARGS="$(quoted_args "$@")"
    if [ "$DRY_RUN" -eq 1 ]; then ARGS="--dry-run $ARGS"; fi
    REMOTE_CMD="cd '$REMOTE_DIR' && bash 'scripts/$SCRIPT'$ARGS"
  fi

  log "远程执行: $HOST → $CMD"
  ssh_run "$REMOTE_CMD"
  rm -f /tmp/mf_ops_key
  exit $?
fi

# 本地执行
if [ "$CMD" = "check" ]; then
  bash "$SCRIPT_DIR/check_db_contract.sh"
  bash "$SCRIPT_DIR/check_migration_versions.sh"
  exit 0
fi

[ -f "$SCRIPT_DIR/$SCRIPT" ] || die "脚本不存在: $SCRIPT_DIR/$SCRIPT"
ARGS=("$@")
if [ "$DRY_RUN" -eq 1 ]; then ARGS=(--dry-run "${ARGS[@]}"); fi
log "本地执行: $SCRIPT ${ARGS[*]:-}"
exec bash "$SCRIPT_DIR/$SCRIPT" "${ARGS[@]}"
