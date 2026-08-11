#!/bin/bash
# scripts/build.sh — 构建生产版本

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "Building backend..."
mkdir -p "${PROJECT_DIR}/backend/build"
cd "${PROJECT_DIR}/backend/build"
cmake .. -DCMAKE_BUILD_TYPE=Release > /dev/null
make -j"$(nproc)"

echo "Building frontend..."
cd "${PROJECT_DIR}/frontend"
npm run build

echo "Build complete."
echo "  Backend: ${PROJECT_DIR}/backend/build/minefolio"
echo "  Frontend: ${PROJECT_DIR}/frontend/dist/"
