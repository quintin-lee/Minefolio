#!/usr/bin/env bash
# Shared memory-isolation guard for build & test infrastructure.
#
# Rationale: a memory leak (or an ASan build that ~doubles every allocation)
# running unbounded can exhaust host memory and trigger an OOM reboot. This
# helper wraps a command in a hard memory ceiling so a single runaway suite
# or build can never take the host down.
#
# Strategy (in order of preference, first available wins):
#   1. cgroup via `systemd-run --scope -p MemoryMax=<LIMIT>` — a real hard
#      cap; the OOM killer inside the cgroup kills the child, not the host.
#   2. `ulimit -v <LIMIT>` — virtual-memory soft cap; portable everywhere.
#   3. no-op (no tooling) — log a warning and run the command unbounded.
#
# Usage:
#   source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/memory_guard.sh"
#   run_mem_capped 4G make -j"$(nproc)"          # build under a 4G cap
#   run_mem_capped 1536M ./minefolio            # run server under a 1.5G cap
#   capped_parallel_flag                          # echoes a throttled -j flag
#
# Env overrides:
#   MINEFOLIO_MEM_LIMIT   — default cap when none is passed (default 4G)
#   MINEFOLIO_BUILD_JOBS  — default parallelism when none is passed (default nproc)

# Echo a throttled parallelism flag for `make`/`cmake --build`.
# Halves nproc (clamped to >=1) to halve the concurrent-compiler memory peak.
capped_parallel_flag() {
  local jobs="${1:-${MINEFOLIO_BUILD_JOBS:-$(nproc)}}"
  jobs=$((jobs / 2))
  [ "$jobs" -lt 1 ] && jobs=1
  echo "-j$jobs"
}

# _limit_to_kb <limit>  →  print the limit in KiB (kernel-style suffixes).
# Accepts a trailing K/k, M/m, G/g, T/t (or B for bytes). No suffix = KiB.
_limit_to_kb() {
  local spec="$1"
  local num suffix
  # Split into a leading number and an optional unit suffix.
  num="${spec%%[KMGTkmgtBb]*}"
  suffix="${spec#"$num"}"
  [ -z "$num" ] && num=0
  [ -z "$suffix" ] && suffix="K"
  # Normalize suffix to an uppercase class.
  local factor
  case "$suffix" in
    K|k) factor=1 ;;
    M|m) factor=$((1024)) ;;
    G|g) factor=$((1024 * 1024)) ;;
    T|t) factor=$((1024 * 1024 * 1024)) ;;
    B|b) factor=0 ;;            # bytes handled below
    *) factor=1 ;;
  esac
  if [ -n "$(printf '%s' "$suffix" | tr -d 'KMGTkmgtBb')" ]; then
    # Unrecognized suffix: treat the whole string as raw KiB.
    printf '%s\n' "${spec//[KMGTkmgtBb]/}"
    return
  fi
  if [ "$factor" -eq 0 ]; then
    # Bytes → KiB (round up so we never under-estimate the ceiling).
    printf '%s\n' "$(( (num + 1023) / 1024 ))"
  else
    printf '%s\n' "$(( num * factor ))"
  fi
}

# run_mem_capped <limit> <command> [args...]
# Runs the command under a memory ceiling. Returns the child's exit code.
run_mem_capped() {
  local limit="${1:-${MINEFOLIO_MEM_LIMIT:-4G}}"
  shift

  local bytes_kb
  bytes_kb="$(_limit_to_kb "$limit")"

  # 1) cgroup hard cap via systemd-run (running as the invoking user; on
  #    GitHub-hosted ubuntu runners systemd is available for the job user).
  if command -v systemd-run >/dev/null 2>&1; then
    if systemd-run --user --quiet --scope \
        -p "MemoryMax=${bytes_kb}K" \
        -p "MemorySwapMax=0" \
        -p "TasksMax=4096" \
        "$@" 2>/dev/null; then
      return 0
    fi
    # systemd-run may have failed because no user cgroup is delegated; fall
    # through to ulimit rather than treating the command as run.
  fi

  # 2) virtual-memory soft cap via ulimit (works in any POSIX shell without
  #    cgroup delegation). Runs in a subshell so it does not leak into the
  #    caller's limits.
  if [[ "$bytes_kb" =~ ^[0-9]+$ ]] && [ "$bytes_kb" -gt 0 ]; then
    ( ulimit -v "$bytes_kb" 2>/dev/null && exec "$@" )
    return $?
  fi

  # 3) No tooling — run unbounded but warn loudly.
  echo "⚠️  memory_guard: no cgroup/ulimit tooling; running unbounded: $*" >&2
  "$@"
}
