#include "services/ai/workflow/engine.h"
#include "services/ai/workflows/monthly_review.h"
#include "services/ai/workflows/portfolio_analysis.h"
#include "services/ai/workflows/financial_health.h"
#include "services/ai/workflows/cashflow_forecast.h"
#include <string.h>

#define MAX_WORKFLOWS 32
static ai_workflow_graph_t g_workflows[MAX_WORKFLOWS];
static size_t              g_workflow_count = 0;
static bool                g_initialized = false;

static void
ensure_init_builtin_workflows(void)
{
    if (g_initialized) {
        return;
    }
    g_workflow_count = 0;

    ai_workflow_register(ai_workflow_monthly_review_get_graph());
    ai_workflow_register(ai_workflow_portfolio_rebalance_get_graph());
    ai_workflow_register(ai_workflow_expense_decision_get_graph());
    ai_workflow_register(ai_workflow_payday_split_get_graph());
    ai_workflow_register(ai_workflow_budget_guard_get_graph());
    ai_workflow_register(ai_workflow_anomaly_detect_get_graph());
    ai_workflow_register(ai_workflow_subscription_audit_get_graph());
    ai_workflow_register(ai_workflow_emergency_fund_get_graph());
    ai_workflow_register(ai_workflow_goal_tracker_get_graph());
    ai_workflow_register(ai_workflow_debt_payoff_get_graph());
    ai_workflow_register(ai_workflow_cashflow_forecast_get_graph());
    ai_workflow_register(ai_workflow_bill_calendar_get_graph());
    ai_workflow_register(ai_workflow_health_score_get_graph());

    g_initialized = true;
}

void
ai_workflow_engine_init(void)
{
    g_initialized = false;
    ensure_init_builtin_workflows();
}

int
ai_workflow_register(const ai_workflow_graph_t* graph)
{
    if (!graph || !graph->id || g_workflow_count >= MAX_WORKFLOWS) {
        return -1;
    }
    for (size_t i = 0; i < g_workflow_count; i++) {
        if (strcmp(g_workflows[i].id, graph->id) == 0) {
            g_workflows[i] = *graph;
            return 0;
        }
    }
    g_workflows[g_workflow_count++] = *graph;
    return 0;
}

const ai_workflow_graph_t*
ai_workflow_find(const char* workflow_id)
{
    if (!workflow_id) {
        return NULL;
    }
    ensure_init_builtin_workflows();
    for (size_t i = 0; i < g_workflow_count; i++) {
        if (strcmp(g_workflows[i].id, workflow_id) == 0) {
            return &g_workflows[i];
        }
    }
    return NULL;
}

csilk_json_t*
ai_workflow_get_all_definitions_json(void)
{
    ensure_init_builtin_workflows();
    csilk_json_t* arr = csilk_json_array();
    for (size_t i = 0; i < g_workflow_count; i++) {
        const ai_workflow_graph_t* wf = &g_workflows[i];
        csilk_json_t*              w = csilk_json_object();
        csilk_json_add_string(w, "id", wf->id);
        csilk_json_add_string(w, "title", wf->title);
        csilk_json_add_string(w, "description", wf->description);
        csilk_json_add_string(w, "icon", wf->icon);
        csilk_json_add_number(w, "step_count", (double)wf->node_count);

        csilk_json_t* steps_arr = csilk_json_array();
        for (int j = 0; j < wf->node_count; j++) {
            csilk_json_t* st = csilk_json_object();
            csilk_json_add_string(st, "step_id", wf->nodes[j].node_id);
            csilk_json_add_string(st, "title", wf->nodes[j].title);
            csilk_json_add_string(st, "description", wf->nodes[j].description);
            csilk_json_array_append(steps_arr, st);
        }
        csilk_json_add_array(w, "steps", steps_arr);
        csilk_json_array_append(arr, w);
    }
    return arr;
}
