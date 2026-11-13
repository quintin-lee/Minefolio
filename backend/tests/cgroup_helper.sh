#!/usr/bin/env bash
# Shared cgroups v2 (systemd-run) memory isolation and ASan/UBSan/LSan defaults.
# Sourced by db_helper.sh, integration scripts, and run_isolated.sh.

_MF_CGROUP_HELPER_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
_MF_SYSTEMD_SCOPE_MODE="${_MF_SYSTEMD_SCOPE_MODE:-}"

if [ -n "${MINEFOLIO_BUILD_DIR:-}" ] && [ -d "$MINEFOLIO_BUILD_DIR" ]; then
  MINEFOLIO_BUILD_DIR="$(cd "$MINEFOLIO_BUILD_DIR" && pwd)"
  export MINEFOLIO_BUILD_DIR
fi

mf_setup_sanitizer_env() {
  local tests_dir="${1:-$_MF_CGROUP_HELPER_DIR}"
  local rss_limit="${MINEFOLIO_ASAN_RSS_LIMIT_MB:-2048}"

  if [ -z "${ASAN_OPTIONS:-}" ]; then
    ASAN_OPTIONS="hard_rss_limit_mb=${rss_limit}:detect_leaks=1:abort_on_error=1"
  elif [[ ":${ASAN_OPTIONS}:" != *":hard_rss_limit_mb="* ]]; then
    ASAN_OPTIONS="${ASAN_OPTIONS}:hard_rss_limit_mb=${rss_limit}"
  fi

  # Hosts with /etc/ld.so.preload inject hooks before libasan.so; bypass link-order abort
  # so ASan binaries run cleanly without requiring manual LD_PRELOAD.
  if [ -f /etc/ld.so.preload ] && [[ ":${ASAN_OPTIONS}:" != *":verify_asan_link_order="* ]]; then
    ASAN_OPTIONS="${ASAN_OPTIONS}:verify_asan_link_order=0"
  fi
  export ASAN_OPTIONS

  if [ -z "${UBSAN_OPTIONS:-}" ]; then
    export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"
  fi

  if [ -z "${LSAN_OPTIONS:-}" ] && [ -f "${tests_dir}/.lsan-suppressions" ]; then
    export LSAN_OPTIONS="suppressions=${tests_dir}/.lsan-suppressions"
  fi
}

mf_detect_systemd_scope() {
  if [ -n "$_MF_SYSTEMD_SCOPE_MODE" ]; then
    return 0
  fi

  if [ "${MINEFOLIO_ISOLATE_CGROUP:-1}" = "0" ]; then
    _MF_SYSTEMD_SCOPE_MODE="none"
    return 0
  fi

  if ! command -v systemd-run >/dev/null 2>&1; then
    _MF_SYSTEMD_SCOPE_MODE="none"
    return 0
  fi

  if [ "$(stat -fc %T /sys/fs/cgroup/ 2>/dev/null || true)" != "cgroup2fs" ]; then
    _MF_SYSTEMD_SCOPE_MODE="none"
    return 0
  fi

  local probe_mem="${MINEFOLIO_TEST_MEM_MAX:-2G}"
  local probe_swap="${MINEFOLIO_TEST_SWAP_MAX:-0}"

  if [ "$(id -u)" -eq 0 ]; then
    if systemd-run --scope -q -p "MemoryMax=${probe_mem}" -p "MemorySwapMax=${probe_swap}" -- true >/dev/null 2>&1; then
      _MF_SYSTEMD_SCOPE_MODE="system"
      return 0
    fi
  else
    if systemd-run --user --scope -q -p "MemoryMax=${probe_mem}" -p "MemorySwapMax=${probe_swap}" -- true >/dev/null 2>&1; then
      _MF_SYSTEMD_SCOPE_MODE="user"
      return 0
    fi
  fi

  _MF_SYSTEMD_SCOPE_MODE="none"
  return 0
}

# Replace current shell/subshell with the target command under cgroups v2 memory limits.
# Since `systemd-run --scope` execs into the command, `$!` in backgrounded `( mf_exec_isolated ... ) &`
# remains the exact child PID for kill/wait lifecycle management.
mf_exec_isolated() {
  mf_setup_sanitizer_env
  mf_detect_systemd_scope

  local mem_max="${MINEFOLIO_TEST_MEM_MAX:-2G}"
  local swap_max="${MINEFOLIO_TEST_SWAP_MAX:-0}"

  case "$_MF_SYSTEMD_SCOPE_MODE" in
    user)
      exec systemd-run --user --scope -q -p "MemoryMax=${mem_max}" -p "MemorySwapMax=${swap_max}" -- "$@"
      ;;
    system)
      exec systemd-run --scope -q -p "MemoryMax=${mem_max}" -p "MemorySwapMax=${swap_max}" -- "$@"
      ;;
    *)
      exec "$@"
      ;;
  esac
}

mf_run_isolated() {
  ( mf_exec_isolated "$@" )
}

# Ensure sanitizer defaults are exported as soon as helper is sourced.
mf_setup_sanitizer_env
