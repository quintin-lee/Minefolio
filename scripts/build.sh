#!/bin/bash
# scripts/build.sh — 构建生产版本

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Memory-isolation guard: cap build parallelism + wrap the compile in a cgroup/
# ulimit ceiling so a csilk + ASan compile peak cannot OOM the host.
MG_DIR="${PROJECT_DIR}/backend/tests"
if [ -f "${MG_DIR}/memory_guard.sh" ]; then
  source "${MG_DIR}/memory_guard.sh"
fi

# -j flag: throttle to half of nproc (overridable via MINEFOLIO_BUILD_JOBS).
BUILD_JOBS="$(capped_parallel_flag 2>/dev/null || echo -j"$(nproc)")"
# Default build ceiling; raise via MINEFOLIO_BUILD_MEM_LIMIT for ASan builds.
BUILD_MEM="${MINEFOLIO_BUILD_MEM_LIMIT:-6G}"

echo "Building backend... (parallelism ${BUILD_JOBS}, memory cap ${BUILD_MEM})"
mkdir -p "${PROJECT_DIR}/backend/build"
cd "${PROJECT_DIR}/backend/build"
cmake .. -DCMAKE_BUILD_TYPE=Release > /dev/null
# Run the compile under a hard memory ceiling. On failure the child's exit
# code propagates (set -e still catches it).
if command -v run_mem_capped >/dev/null 2>&1; then
  run_mem_capped "$BUILD_MEM" make ${BUILD_JOBS}
else
  make ${BUILD_JOBS}
fi

echo "Building frontend..."
cd "${PROJECT_DIR}/frontend"
npm run build

echo "Build complete."
echo "  Backend: ${PROJECT_DIR}/backend/build/minefolio"
echo "  Frontend: ${PROJECT_DIR}/frontend/dist/"
