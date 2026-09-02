/**
 * @file ai_trace_controller.c
 * @brief AI 链路追踪与指标统计控制器实现文件
 *
 * 遵循三层 C 架构规范，本控制器负责将 HTTP 请求路由映射并分发至
 * services/ai_trace_service.c 中实现的 AI 观测性与指标统计业务层。
 */

#include "controllers/ai_trace_controller.h"
#include "services/ai_trace_service.h"

/**
 * @brief 注册 AI 链路追踪模块的所有 HTTP 路由
 *
 * @details 详细端点定义与参数说明：
 *
 * 1. GET /api/ai/traces
 *    - 功能: 分页查询当前用户的 AI 对话链路追踪记录
 *    - 鉴权: JWT (Bearer Token)
 *    - 查询参数: page (int, 默认 1), page_size (int, 默认 20), session_id (int64, 可选), status (string, 可选)
 *    - 响应: 200 OK {"code": 0, "data": {"items": [{"id": 1, "session_id": 10, "provider": "openai", "model": "gpt-4o", "prompt_tokens": 120, "completion_tokens": 45, "total_tokens": 165, "latency_ms": 1250, "status": "success", "created_at": "..."}, ...], "total": 30}}
 *
 * 2. GET /api/ai/traces/stats
 *    - 功能: 获取当前用户的 AI Token 消耗与性能统计指标
 *    - 鉴权: JWT (Bearer Token)
 *    - 响应: 200 OK {"code": 0, "data": {"total_calls": 128, "total_prompt_tokens": 54200, "total_completion_tokens": 18900, "total_tokens": 73100, "avg_latency_ms": 1420.5}}
 *
 * 3. GET /api/ai/traces/:id
 *    - 功能: 获取指定 Trace 的完整详细信息（包含用户输入、模型回复与所有工具调用 Spans）
 *    - 鉴权: JWT (Bearer Token)
 *    - 路径参数: id (int64)
 *    - 响应: 200 OK {"code": 0, "data": {"id": 1, "prompt": "分析本月支出", "response": "...", "spans": [{"tool_name": "query_monthly_expenses", "duration_ms": 80, "output": "..."}, ...]}}
 *
 * @param[in,out] app Csilk 应用实例指针 (csilk_app_t*)
 */
void
register_ai_trace_routes(csilk_app_t* app)

{
    csilk_app_get_ext(app,
                      "/api/ai/traces",
                      ai_trace_service_list,
                      NULL,
                      NULL,
                      "List AI traces",
                      "Returns paginated AI conversation traces");
    csilk_app_get_ext(app,
                      "/api/ai/traces/stats",
                      ai_trace_service_stats,
                      NULL,
                      NULL,
                      "AI trace stats",
                      "Returns aggregate trace statistics");
    csilk_app_get_ext(app,
                      "/api/ai/traces/:id",
                      ai_trace_service_get,
                      NULL,
                      NULL,
                      "Get AI trace",
                      "Returns full trace detail including messages");
}
