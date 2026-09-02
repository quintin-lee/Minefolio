#pragma once
#include "services/ai/workflow/graph.h"

#ifdef __cplusplus
extern "C" {
#endif

const ai_workflow_graph_t* ai_workflow_cashflow_forecast_get_graph(void);
const ai_workflow_graph_t* ai_workflow_bill_calendar_get_graph(void);
const ai_workflow_graph_t* ai_workflow_payday_split_get_graph(void);
const ai_workflow_graph_t* ai_workflow_debt_payoff_get_graph(void);
const ai_workflow_graph_t* ai_workflow_expense_decision_get_graph(void);
const ai_workflow_graph_t* ai_workflow_budget_guard_get_graph(void);
const ai_workflow_graph_t* ai_workflow_anomaly_detect_get_graph(void);
const ai_workflow_graph_t* ai_workflow_subscription_audit_get_graph(void);
const ai_workflow_graph_t* ai_workflow_goal_tracker_get_graph(void);

#ifdef __cplusplus
}
#endif
