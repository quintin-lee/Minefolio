#!/usr/bin/env bash
# scripts/test.sh — 测试编排（统一 runner）
#
# 分类：
#   fast  普通集成测试（test_*.sh，排除 slow/helper）
#   slow  长耗时或特殊环境测试（test_full.sh / test_ai_loop_hardening.sh / test_link_nocleanup.sh）
#   all   fast + slow
#
# 用法:
#   ./test.sh list [fast|slow|all]
#   ./test.sh fast [--parallel N]
#   ./test.sh slow
#   ./test.sh test_2fa.sh           # 执行单个脚本
#   ./test.sh fast --dry-run

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BACKEND_DIR="$PROJECT_DIR/backend"
TEST_DIR="$BACKEND_DIR/tests"
LOG_DIR="${MINEFOLIO_TEST_LOG_DIR:-$BACKEND_DIR/build/test-logs}"
TIMEOUT="${MINEFOLIO_TEST_TIMEOUT:-300}"

SLOW_LIST="test_full.sh test_ai_loop_hardening.sh test_link_nocleanup.sh"
EXCLUDE_LIST="test_db_helper.sh"

usage() {
  cat <<'EOF'
用法: test.sh <命令> [选项]

命令:
  list [fast|slow|all]   列出测试脚本
  fast                   运行快速集成测试
  slow                   运行长耗时测试
  all                    运行全部
  <script.sh>            运行指定测试脚本

选项:
  --parallel N           并行度（默认 1）
  --dry-run              只打印计划
  -h, --help             显示帮助

环境变量:
  MINEFOLIO_TEST_TIMEOUT   单脚本超时秒数（默认 300）
  MINEFOLIO_TEST_LOG_DIR   日志目录（默认 backend/build/test-logs）
EOF
}

log()  { printf '[test] %s\n' "$*"; }
die()  { printf '[test] ERROR: %s\n' "$*" >&2; exit 1; }

is_slow() {
  local s
  for s in $SLOW_LIST; do [ "$s" = "$1" ] && return 0; done
  return 1
}
is_excluded() {
  local s
  for s in $EXCLUDE_LIST; do [ "$s" = "$1" ] && return 0; done
  return 1
}

collect_scripts() {
  local group="$1"
  local f name
  for f in "$TEST_DIR"/test_*.sh; do
    [ -f "$f" ] || continue
    name="$(basename "$f")"
    is_excluded "$name" && continue
    case "$group" in
      fast) is_slow "$name" && continue ;;
      slow) is_slow "$name" || continue ;;
      all)  ;;
    esac
    printf '%s\n' "$name"
  done
}

run_one() {
  local name="$1"
  mkdir -p "$LOG_DIR"
  local logfile="$LOG_DIR/${name%.sh}.log"
  local start=$SECONDS
  local rc
  ( cd "$BACKEND_DIR" && timeout "$TIMEOUT" bash "tests/$name" ) > "$logfile" 2>&1
  rc=$?
  local dur=$((SECONDS - start))
  if [ "$rc" -eq 0 ]; then
    printf '[PASS] %-38s %ss\n' "$name" "$dur"
    return 0
  fi
  printf '[FAIL] %-38s %ss (exit=%s, log=%s)\n' "$name" "$dur" "$rc" "$logfile"
  tail -20 "$logfile" | sed 's/^/       /'
  return 1
}

# 供 xargs 并行调用的隐藏入口
if [ "${1:-}" = "_run-one" ]; then
  [ "$#" -ge 2 ] || exit 1
  run_one "$2"
  exit $?
fi

[ "$#" -ge 1 ] || { usage; exit 1; }
case "$1" in -h|--help) usage; exit 0 ;; esac
CMD="$1"; shift

# list 的分组是位置参数，需在选项解析前取出
GROUP="all"
if [ "$CMD" = "list" ] && [ "$#" -gt 0 ]; then
  case "$1" in
    fast|slow|all) GROUP="$1"; shift ;;
  esac
fi

PARALLEL=1
DRY_RUN=0
while [ "$#" -gt 0 ]; do
  case "$1" in
    --parallel) [ "$#" -ge 2 ] || die "--parallel 缺少参数"; PARALLEL="$2"; shift 2 ;;
    --dry-run) DRY_RUN=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "未知参数: $1" ;;
  esac
done
case "$PARALLEL" in ''|*[!0-9]*) die "--parallel 必须为正整数" ;; esac
[ "$PARALLEL" -ge 1 ] || die "--parallel 必须为正整数"

[ -d "$TEST_DIR" ] || die "测试目录不存在: $TEST_DIR"

case "$CMD" in
  list)
    collect_scripts "$GROUP" | nl -ba
    ;;

  fast|slow|all)
    mapfile -t SCRIPTS < <(collect_scripts "$CMD")
    log "待测脚本: ${#SCRIPTS[@]} 个（分组 $CMD，并行度 $PARALLEL）"
    if [ "$DRY_RUN" -eq 1 ]; then
      printf '%s\n' "${SCRIPTS[@]}"
      exit 0
    fi
    if [ "$PARALLEL" -eq 1 ]; then
      PASS=0; FAIL=0
      for s in "${SCRIPTS[@]}"; do
        if run_one "$s"; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi
      done
    else
      OUTPUT="$(printf '%s\n' "${SCRIPTS[@]}" \
        | xargs -P "$PARALLEL" -I{} env MINEFOLIO_TEST_LOG_DIR="$LOG_DIR" MINEFOLIO_TEST_TIMEOUT="$TIMEOUT" \
            bash "$0" _run-one {} 2>&1)"
      printf '%s\n' "$OUTPUT"
      PASS="$(printf '%s\n' "$OUTPUT" | grep -c '^\[PASS\]' || true)"
      FAIL="$(printf '%s\n' "$OUTPUT" | grep -c '^\[FAIL\]' || true)"
    fi
    printf '\n汇总: %d PASS, %d FAIL\n' "$PASS" "$FAIL"
    [ "$FAIL" -eq 0 ] || exit 1
    ;;

  *)
    # 单脚本模式
    NAME="$CMD"
    case "$NAME" in *.sh) ;; *) NAME="$NAME.sh" ;; esac
    [ -f "$TEST_DIR/$NAME" ] || die "测试脚本不存在: $NAME"
    if [ "$DRY_RUN" -eq 1 ]; then
      log "[dry-run] 将执行 $NAME"
      exit 0
    fi
    run_one "$NAME"
    ;;
esac
