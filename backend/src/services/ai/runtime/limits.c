#include "services/ai/runtime/limits.h"
#include <stdio.h>

ai_runtime_limits_t
ai_runtime_limits_default(void)
{
    return (ai_runtime_limits_t){
        .max_iterations = 10,
        .timeout_ms = 60000,
        .token_budget = 32768,
        .tool_budget = 15,
        .cost_budget = 0.50,
    };
}

bool
ai_runtime_limits_check_pre_turn(const ai_runtime_limits_t* limits,
                                 const ai_runtime_stats_t*  stats,
                                 const volatile bool*       cancel_token,
                                 int64_t                    current_elapsed_ms,
                                 ai_runtime_status_t*       out_status)
{
    if (cancel_token && *cancel_token) {
        ai_runtime_status_set(out_status,
                              AI_RUNTIME_ERR_CANCELLED,
                              "Execution cancelled by client",
                              "Cancellation token signaled");
        return false;
    }

    if (!limits || !stats) {
        return true;
    }

    if (limits->max_iterations > 0 && stats->iterations_done >= limits->max_iterations) {
        char detail[128];
        snprintf(detail, sizeof(detail), "Reached max_iterations limit (%d)", limits->max_iterations);
        ai_runtime_status_set(out_status, AI_RUNTIME_ERR_TIMEOUT, "Max iterations exceeded", detail);
        return false;
    }

    if (limits->timeout_ms > 0 && current_elapsed_ms >= limits->timeout_ms) {
        char detail[128];
        snprintf(detail, sizeof(detail), "Elapsed %lld ms exceeded timeout %lld ms",
                 (long long)current_elapsed_ms, (long long)limits->timeout_ms);
        ai_runtime_status_set(out_status, AI_RUNTIME_ERR_TIMEOUT, "Execution timeout", detail);
        return false;
    }

    if (limits->token_budget > 0 && stats->total_tokens >= limits->token_budget) {
        char detail[128];
        snprintf(detail, sizeof(detail), "Total tokens %d exceeded budget %d",
                 stats->total_tokens, limits->token_budget);
        ai_runtime_status_set(out_status, AI_RUNTIME_ERR_CONTEXT_OVERFLOW, "Token budget exceeded", detail);
        return false;
    }

    if (limits->cost_budget > 0.0 && stats->total_cost >= limits->cost_budget) {
        char detail[128];
        snprintf(detail, sizeof(detail), "Total cost $%.4f exceeded budget $%.4f",
                 stats->total_cost, limits->cost_budget);
        ai_runtime_status_set(out_status, AI_RUNTIME_ERR_CONTEXT_OVERFLOW, "Cost budget exceeded", detail);
        return false;
    }

    if (out_status) {
        ai_runtime_status_set(out_status, AI_RUNTIME_ERR_OK, "OK", NULL);
    }
    return true;
}

bool
ai_runtime_limits_check_tool_budget(const ai_runtime_limits_t* limits,
                                    const ai_runtime_stats_t*  stats,
                                    int                        requested_tools,
                                    ai_runtime_status_t*       out_status)
{
    if (!limits || !stats || limits->tool_budget <= 0) {
        return true;
    }

    if (stats->tool_calls_count + requested_tools > limits->tool_budget) {
        char detail[128];
        snprintf(detail, sizeof(detail), "Tool calls count %d + %d would exceed budget %d",
                 stats->tool_calls_count, requested_tools, limits->tool_budget);
        ai_runtime_status_set(out_status, AI_RUNTIME_ERR_TOOL, "Tool budget exceeded", detail);
        return false;
    }
    return true;
}

void
ai_runtime_limits_record_tokens(ai_runtime_stats_t* stats,
                                int                 prompt_tokens,
                                int                 completion_tokens,
                                double              cost)
{
    if (!stats) {
        return;
    }
    if (prompt_tokens > 0) stats->prompt_tokens += prompt_tokens;
    if (completion_tokens > 0) stats->completion_tokens += completion_tokens;
    stats->total_tokens = stats->prompt_tokens + stats->completion_tokens;
    if (cost > 0.0) stats->total_cost += cost;
}

void
ai_runtime_limits_record_tool_call(ai_runtime_stats_t* stats, int count)
{
    if (!stats || count <= 0) {
        return;
    }
    stats->tool_calls_count += count;
}
