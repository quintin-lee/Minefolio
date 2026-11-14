/**
 * @file loop.c
 * @brief AI Runtime Agent 循环 — 薄层 re-export / 兼容入口。
 *
 * 核心实现已拆分为三个职责单一模块：
 *   - loop_env.c        环境准备/清理、流式桥接、安全包装、技能 prompt、trace 结算
 *   - loop_dispatch.c   并行/串行工具调度、policy 评估、去重、结果裁剪
 *   - loop_convergence.c 主循环状态机、预算收敛、部分文本持久化、同步入口
 *
 * 本文件仅保留公共 API 的 re-export 声明（由 loop.h 对外暴露），
 * 以及 CMake GLOB_RECURSE 需要至少一个 .c 文件编译入 runtime 目录的约束。
 */
#include "services/ai/runtime/loop.h"

/*
 * 所有函数实现已移至上述三个模块。
 * CMake GLOB_RECURSE "src/*.c" 会自动编译 loop_env.c / loop_dispatch.c / loop_convergence.c。
 * 此文件作为编译单元占位与 re-export 保障（确保 loop.h 中的符号可被链接器解析）。
 */
