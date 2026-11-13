#!/usr/bin/env bash
# Memory-isolated build & test runner using cgroups v2 (systemd-run) + ASan RSS limits.
# Prevents runaway test/build allocations from exhausting host RAM or swap.
#
# Examples:
#   ./backend/tests/run_isolated.sh unit [build_dir]
#   ./backend/tests/run_isolated.sh integration [build_dir]
#   ./backend/tests/run_isolated.sh build [build_dir]
#   ./backend/tests/run_isolated.sh -- ./backend/build-asan/test_ai_tools
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BACKEND_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/cgroup_helper.sh"

usage() {
  cat << 'EOF'
用法: run_isolated.sh [选项] <模式|命令>

模式:
  unit [build_dir]         在 cgroups v2 + ASan 上限下运行 CTest 单元测试 (默认 build-asan 若存在，否则 build)
  integration [build_dir]  顺序运行核心后端集成测试套件 (单进程 MemoryMax=2G, MemorySwapMax=0)
  build [build_dir]        限制并行度与内存峰值运行 cmake --build
  -- <command...>          使用隔离参数运行任意命令

选项:
  -m, --mem <limit>        覆盖 cgroups MemoryMax (例如 2G / 4G / 6G)
  -j, --jobs <N>           覆盖构建/CTest 并行度 (默认 4，避免 32 核并发拉爆内存)
  --rss <MB>               覆盖 ASAN hard_rss_limit_mb (默认 2048)
  -h, --help               显示帮助
EOF
}

MEM_OVERRIDE=""
JOBS_OVERRIDE=""
while [ $# -gt 0 ]; do
  case "$1" in
    -m|--mem)
      MEM_OVERRIDE="$2"; shift 2 ;;
    -j|--jobs)
      JOBS_OVERRIDE="$2"; shift 2 ;;
    --rss)
      export MINEFOLIO_ASAN_RSS_LIMIT_MB="$2"
      unset ASAN_OPTIONS
      mf_setup_sanitizer_env
      shift 2 ;;
    -h|--help)
      usage; exit 0 ;;
    --)
      shift
      break ;;
    *)
      break ;;
  esac
done

if [ $# -eq 0 ]; then
  usage
  exit 1
fi

default_build_dir() {
  if [ -d "$BACKEND_DIR/build-asan" ]; then
    echo "$BACKEND_DIR/build-asan"
  else
    echo "$BACKEND_DIR/build"
  fi
}

default_jobs() {
  local cpus
  cpus="$(nproc 2>/dev/null || echo 4)"
  if [ "$cpus" -gt 4 ]; then
    echo 4
  else
    echo "$cpus"
  fi
}

MODE="$1"
case "$MODE" in
  unit)
    BUILD_DIR="${2:-$(default_build_dir)}"
    JOBS="${JOBS_OVERRIDE:-${MINEFOLIO_TEST_JOBS:-$(default_jobs)}}"
    export MINEFOLIO_TEST_MEM_MAX="${MEM_OVERRIDE:-${MINEFOLIO_TEST_SCOPE_MEM:-4G}}"
    export MINEFOLIO_TEST_SWAP_MAX="${MINEFOLIO_TEST_SWAP_MAX:-0}"
    echo "== [Isolated CTest] dir=$BUILD_DIR mem=$MINEFOLIO_TEST_MEM_MAX swap=$MINEFOLIO_TEST_SWAP_MAX jobs=$JOBS rss=${MINEFOLIO_ASAN_RSS_LIMIT_MB:-2048}MB =="
    mf_run_isolated ctest --test-dir "$BUILD_DIR" --output-on-failure -j "$JOBS"
    ;;

  build)
    BUILD_DIR="${2:-$(default_build_dir)}"
    JOBS="${JOBS_OVERRIDE:-${MINEFOLIO_BUILD_JOBS:-$(default_jobs)}}"
    export MINEFOLIO_TEST_MEM_MAX="${MEM_OVERRIDE:-${MINEFOLIO_BUILD_MEM:-6G}}"
    export MINEFOLIO_TEST_SWAP_MAX="${MINEFOLIO_TEST_SWAP_MAX:-0}"
    echo "== [Isolated Build] dir=$BUILD_DIR mem=$MINEFOLIO_TEST_MEM_MAX swap=$MINEFOLIO_TEST_SWAP_MAX jobs=$JOBS =="
    mf_run_isolated cmake --build "$BUILD_DIR" --parallel "$JOBS"
    ;;

  integration)
    BUILD_DIR="${2:-$(default_build_dir)}"
    BUILD_DIR="$(cd "$BUILD_DIR" && pwd)"
    export MINEFOLIO_BUILD_DIR="$BUILD_DIR"
    export MINEFOLIO_TEST_MEM_MAX="${MEM_OVERRIDE:-${MINEFOLIO_TEST_MEM_MAX:-2G}}"
    export MINEFOLIO_TEST_SWAP_MAX="${MINEFOLIO_TEST_SWAP_MAX:-0}"
    echo "== [Isolated Integration] build=$BUILD_DIR mem=$MINEFOLIO_TEST_MEM_MAX swap=$MINEFOLIO_TEST_SWAP_MAX rss=${MINEFOLIO_ASAN_RSS_LIMIT_MB:-2048}MB =="
    SUITES=(
      test_link.sh
      test_2fa.sh
      test_dca_cashflow.sh
      test_ai_trace.sh
      test_ai_tool_call.sh
      test_ai_truncate.sh
      test_ai_loop_hardening.sh
      test_market_sync.sh
      test_ledgers.sh
      test_fx_oauth.sh
      test_mcp.sh
      test_mcp_server.sh
      test_report_isolation.sh
    )
    for s in "${SUITES[@]}"; do
      echo ">>> Running $s (isolated MemoryMax=$MINEFOLIO_TEST_MEM_MAX, MemorySwapMax=$MINEFOLIO_TEST_SWAP_MAX)..."
      mf_run_isolated bash "$SCRIPT_DIR/$s"
    done
    ;;

  *)
    export MINEFOLIO_TEST_MEM_MAX="${MEM_OVERRIDE:-${MINEFOLIO_TEST_MEM_MAX:-2G}}"
    export MINEFOLIO_TEST_SWAP_MAX="${MINEFOLIO_TEST_SWAP_MAX:-0}"
    mf_run_isolated "$@"
    ;;
esac
