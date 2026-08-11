#!/bin/bash
# scripts/dev.sh — 启动前后端开发服务器

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# 设置环境变量
export MINEFOLIO_JWT_SECRET="${MINEFOLIO_JWT_SECRET:-minefolio-dev-secret-change-in-production}"
export MINEFOLIO_DB_DSN="${MINEFOLIO_DB_DSN:-${PROJECT_DIR}/backend/data/minefolio.db}"

# 后端构建
echo "Building backend..."
mkdir -p "${PROJECT_DIR}/backend/build"
cd "${PROJECT_DIR}/backend/build"
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON > /dev/null
make -j"$(nproc)"

# 确保 data 目录存在
mkdir -p "${PROJECT_DIR}/backend/data"

# 启动后端（后台运行）
echo "Starting backend on :8080..."
cd "${PROJECT_DIR}/backend/build"
./minefolio &
BACKEND_PID=$!

# 前端开发服务器
echo "Starting frontend dev server on :5173..."
cd "${PROJECT_DIR}/frontend"
npm run dev &
FRONTEND_PID=$!

echo ""
echo "=========================================="
echo "  Minefolio 开发环境已启动"
echo "  前端: http://localhost:5173"
echo "  后端: http://localhost:8080"
echo "  API:  http://localhost:8080/api"
echo "=========================================="
echo "PID 后端: $BACKEND_PID, 前端: $FRONTEND_PID"
echo "按 Ctrl+C 停止所有服务"

# 捕获退出信号，停止所有子进程
trap "kill $BACKEND_PID $FRONTEND_PID 2>/dev/null; exit" INT TERM
wait
