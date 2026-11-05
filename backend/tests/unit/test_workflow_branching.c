#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "services/ai/workflow/graph.h"
#include "services/ai/workflow/executor.h"

/* ------------------------------------------------------------------ */
/* Test State Tracking                                                */
/* ------------------------------------------------------------------ */
static int s_step1_exec_count = 0;
static int s_step2_exec_count = 0;
static int s_step3_exec_count = 0;

static int s_comp1_called = 0;
static int s_comp2_called = 0;
static int s_comp_order[10];
static int s_comp_order_idx = 0;

static void
reset_tracking(void)
{
    s_step1_exec_count = 0;
    s_step2_exec_count = 0;
    s_step3_exec_count = 0;
    s_comp1_called = 0;
    s_comp2_called = 0;
    s_comp_order_idx = 0;
    memset(s_comp_order, 0, sizeof(s_comp_order));
}

/* ------------------------------------------------------------------ */
/* Mock Condition Functions                                           */
/* ------------------------------------------------------------------ */
static bool
mock_condition_false(csilk_db_pool_t* pool, int64_t user_id, const csilk_json_t* shared_state)
{
    (void)pool; (void)user_id; (void)shared_state;
    return false;
}

static bool
mock_condition_true(csilk_db_pool_t* pool, int64_t user_id, const csilk_json_t* shared_state)
{
    (void)pool; (void)user_id; (void)shared_state;
    return true;
}

/* ------------------------------------------------------------------ */
/* Mock Compensation Functions                                        */
/* ------------------------------------------------------------------ */
static int
mock_compensate_step1(csilk_db_pool_t* pool, int64_t user_id, const csilk_json_t* shared_state)
{
    (void)pool; (void)user_id; (void)shared_state;
    s_comp1_called++;
    s_comp_order[s_comp_order_idx++] = 1;
    return 0;
}

static int
mock_compensate_step2(csilk_db_pool_t* pool, int64_t user_id, const csilk_json_t* shared_state)
{
    (void)pool; (void)user_id; (void)shared_state;
    s_comp2_called++;
    s_comp_order[s_comp_order_idx++] = 2;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Mock Execute Functions                                             */
/* ------------------------------------------------------------------ */
static char*
mock_exec_step1_ok(csilk_db_pool_t* pool, int64_t user_id, const csilk_json_t* params, const char* ctx_json)
{
    (void)pool; (void)user_id; (void)params; (void)ctx_json;
    s_step1_exec_count++;
    return strdup("{\"step1\":\"ok\"}");
}

static char*
mock_exec_step2_ok(csilk_db_pool_t* pool, int64_t user_id, const csilk_json_t* params, const char* ctx_json)
{
    (void)pool; (void)user_id; (void)params; (void)ctx_json;
    s_step2_exec_count++;
    return strdup("{\"step2\":\"ok\"}");
}

static char*
mock_exec_terminal_ok(csilk_db_pool_t* pool, int64_t user_id, const csilk_json_t* params, const char* ctx_json)
{
    (void)pool; (void)user_id; (void)params; (void)ctx_json;
    s_step3_exec_count++;
    return strdup("{\"terminal\":\"done\"}");
}

static char*
mock_exec_fail_always(csilk_db_pool_t* pool, int64_t user_id, const csilk_json_t* params, const char* ctx_json)
{
    (void)pool; (void)user_id; (void)params; (void)ctx_json;
    s_step3_exec_count++;
    return NULL;
}

static char*
mock_exec_fail_twice_then_ok(csilk_db_pool_t* pool, int64_t user_id, const csilk_json_t* params, const char* ctx_json)
{
    (void)pool; (void)user_id; (void)params; (void)ctx_json;
    s_step1_exec_count++;
    if (s_step1_exec_count < 3) {
        return NULL; /* Fail on attempt 1 and 2 */
    }
    return strdup("{\"retry\":\"recovered\"}");
}

/* ------------------------------------------------------------------ */
/* Main Test Runner                                                   */
/* ------------------------------------------------------------------ */
int
main(void)
{
    printf("============================================================\n");
    printf("   TEST SUITE: AI Workflow Dynamic Branching & Saga Engine    \n");
    printf("============================================================\n");

    void* dummy_http_ctx = calloc(1, 4096);

    /* ------------------------------------------------------------- */
    /* Scenario 1: Dynamic Condition Branching (Step Skipped)       */
    /* ------------------------------------------------------------- */
    printf("[Test 1] Dynamic Condition Branching (Step Skipped)...\n");
    {
        reset_tracking();
        ai_workflow_graph_t g = {
            .id = "wf_branching",
            .title = "Branching Workflow",
            .node_count = 3,
            .nodes = {
                {
                    .node_id = "step1",
                    .title = "Skipped Step",
                    .execute = mock_exec_step1_ok,
                    .condition = mock_condition_false, /* Returns false -> MUST be skipped */
                },
                {
                    .node_id = "step2",
                    .title = "Active Step",
                    .execute = mock_exec_step2_ok,
                    .condition = mock_condition_true,  /* Returns true -> MUST execute */
                },
                {
                    .node_id = "terminal",
                    .title = "Terminal Step",
                    .execute = mock_exec_terminal_ok,
                }
            }
        };

        ai_wf_context_t ctx = { .user_id = 100, .session_id = 1, .params = csilk_json_object() };
        int rc = ai_workflow_execute_stream((csilk_ctx_t*)dummy_http_ctx, &g, &ctx);
        assert(rc == 0);
        assert(s_step1_exec_count == 0); /* Skipped! */
        assert(s_step2_exec_count == 1); /* Executed! */
        assert(s_step3_exec_count == 1); /* Executed! */
        csilk_json_free((csilk_json_t*)ctx.params);
        printf("  ✅ Step 1 correctly skipped; Steps 2 and 3 executed successfully\n");
    }

    /* ------------------------------------------------------------- */
    /* Scenario 2: Transient Step Failure & Exponential Retry       */
    /* ------------------------------------------------------------- */
    printf("[Test 2] Transient Step Failure & Exponential Retry Recovery...\n");
    {
        reset_tracking();
        ai_workflow_graph_t g = {
            .id = "wf_retry",
            .title = "Retry Workflow",
            .node_count = 2,
            .nodes = {
                {
                    .node_id = "step1",
                    .title = "Transient Step",
                    .execute = mock_exec_fail_twice_then_ok,
                    .max_retries = 3, /* Allows up to 3 retries (4 attempts total) */
                },
                {
                    .node_id = "terminal",
                    .title = "Terminal Step",
                    .execute = mock_exec_terminal_ok,
                }
            }
        };

        ai_wf_context_t ctx = { .user_id = 100, .session_id = 1, .params = csilk_json_object() };
        int rc = ai_workflow_execute_stream((csilk_ctx_t*)dummy_http_ctx, &g, &ctx);
        assert(rc == 0);
        assert(s_step1_exec_count == 3); /* Failed twice, succeeded on attempt 3 */
        assert(s_step3_exec_count == 1); /* Terminal executed */
        csilk_json_free((csilk_json_t*)ctx.params);
        printf("  ✅ Recovered after %d attempts; Workflow completed successfully\n", s_step1_exec_count);
    }

    /* ------------------------------------------------------------- */
    /* Scenario 3: Reverse Saga Compensation on Step Failure        */
    /* ------------------------------------------------------------- */
    printf("[Test 3] Reverse Saga Compensation on Step Failure...\n");
    {
        reset_tracking();
        ai_workflow_graph_t g = {
            .id = "wf_saga",
            .title = "Saga Workflow",
            .node_count = 3,
            .nodes = {
                {
                    .node_id = "step1",
                    .title = "Step 1 (Saga)",
                    .execute = mock_exec_step1_ok,
                    .compensate = mock_compensate_step1,
                },
                {
                    .node_id = "step2",
                    .title = "Step 2 (Saga)",
                    .execute = mock_exec_step2_ok,
                    .compensate = mock_compensate_step2,
                },
                {
                    .node_id = "step3_fail",
                    .title = "Step 3 (Fails)",
                    .execute = mock_exec_fail_always,
                    .max_retries = 1,
                }
            }
        };

        ai_wf_context_t ctx = { .user_id = 100, .session_id = 1, .params = csilk_json_object() };
        int rc = ai_workflow_execute_stream((csilk_ctx_t*)dummy_http_ctx, &g, &ctx);
        assert(rc == -1); /* Workflow halted with error */
        assert(s_step1_exec_count == 1);
        assert(s_step2_exec_count == 1);
        assert(s_step3_exec_count == 2); /* 1 initial attempt + 1 retry */

        /* Verify reverse compensation: step 2 compensated first, then step 1 */
        assert(s_comp2_called == 1);
        assert(s_comp1_called == 1);
        assert(s_comp_order_idx == 2);
        assert(s_comp_order[0] == 2); /* First step 2 */
        assert(s_comp_order[1] == 1); /* Then step 1 */
        csilk_json_free((csilk_json_t*)ctx.params);
        printf("  ✅ Saga rollback executed in exact reverse order: Step 2 -> Step 1\n");
    }

    /* ------------------------------------------------------------- */
    /* Scenario 4: Step with NULL Compensate Handled Gracefully     */
    /* ------------------------------------------------------------- */
    printf("[Test 4] Step with NULL Compensate Handled Gracefully...\n");
    {
        reset_tracking();
        ai_workflow_graph_t g = {
            .id = "wf_null_comp",
            .title = "Mixed Compensation",
            .node_count = 3,
            .nodes = {
                {
                    .node_id = "step1",
                    .title = "Step 1 (Has comp)",
                    .execute = mock_exec_step1_ok,
                    .compensate = mock_compensate_step1,
                },
                {
                    .node_id = "step2",
                    .title = "Step 2 (No comp)",
                    .execute = mock_exec_step2_ok,
                    .compensate = NULL, /* No cleanup needed */
                },
                {
                    .node_id = "step3_fail",
                    .title = "Step 3 (Fails)",
                    .execute = mock_exec_fail_always,
                }
            }
        };

        ai_wf_context_t ctx = { .user_id = 100, .session_id = 1, .params = csilk_json_object() };
        int rc = ai_workflow_execute_stream((csilk_ctx_t*)dummy_http_ctx, &g, &ctx);
        assert(rc == -1);
        assert(s_comp1_called == 1);
        assert(s_comp2_called == 0);
        assert(s_comp_order_idx == 1);
        assert(s_comp_order[0] == 1);
        csilk_json_free((csilk_json_t*)ctx.params);
        printf("  ✅ Step 2 with NULL compensate skipped safely; Step 1 compensated\n");
    }

    /* ------------------------------------------------------------- */
    /* Scenario 5: Terminal Step Failure Triggers Saga Rollback      */
    /* ------------------------------------------------------------- */
    printf("[Test 5] Terminal Step Failure Triggers Saga Rollback...\n");
    {
        reset_tracking();
        ai_workflow_graph_t g = {
            .id = "wf_terminal_fail",
            .title = "Terminal Fail Workflow",
            .node_count = 2,
            .nodes = {
                {
                    .node_id = "step1",
                    .title = "Step 1 (Has comp)",
                    .execute = mock_exec_step1_ok,
                    .compensate = mock_compensate_step1,
                },
                {
                    .node_id = "terminal_fail",
                    .title = "Terminal Step (Fails)",
                    .execute = mock_exec_fail_always,
                }
            }
        };

        ai_wf_context_t ctx = { .user_id = 100, .session_id = 1, .params = csilk_json_object() };
        int rc = ai_workflow_execute_stream((csilk_ctx_t*)dummy_http_ctx, &g, &ctx);
        assert(rc == -1);
        assert(s_comp1_called == 1);
        csilk_json_free((csilk_json_t*)ctx.params);
        printf("  ✅ Terminal step failure triggered compensation of preceding step 1\n");
    }

    free(dummy_http_ctx);

    printf("============================================================\n");
    printf("   ALL 5 WORKFLOW BRANCHING & SAGA TESTS PASSED!            \n");
    printf("============================================================\n");
    return 0;
}
