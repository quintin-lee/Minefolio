#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "services/ai/workflow/graph.h"
#include "services/ai/workflow/executor.h"

static int s_terminal_called = 0;

static char* mock_fail_node_null(csilk_db_pool_t* pool, int64_t user_id, const csilk_json_t* params, const char* ctx_json) {
    (void)pool; (void)user_id; (void)params; (void)ctx_json;
    return NULL;
}

static char* mock_fail_node_malformed_json(csilk_db_pool_t* pool, int64_t user_id, const csilk_json_t* params, const char* ctx_json) {
    (void)pool; (void)user_id; (void)params; (void)ctx_json;
    return strdup("{invalid json syntax...");
}

static char* mock_terminal_node(csilk_db_pool_t* pool, int64_t user_id, const csilk_json_t* params, const char* ctx_json) {
    (void)pool; (void)user_id; (void)params; (void)ctx_json;
    s_terminal_called++;
    return strdup("{\"status\":\"ok\"}");
}

int main(void) {
    printf("Running test_workflow_step_error...\n");

    /* Test Case 1: Node returns NULL -> halts before terminal step */
    ai_workflow_graph_t g1 = {
        .id = "wf_err1",
        .title = "WF1",
        .node_count = 2,
        .nodes = {
            { .node_id = "step1", .title = "Null Step", .execute = mock_fail_node_null },
            { .node_id = "step_term", .title = "Terminal Step", .execute = mock_terminal_node }
        }
    };
    ai_wf_context_t ctx1 = { .user_id = 1, .session_id = 1, .params = csilk_json_object() };

    void* dummy_http_ctx = calloc(1, 4096);
    s_terminal_called = 0;
    int rc1 = ai_workflow_execute_stream((csilk_ctx_t*)dummy_http_ctx, &g1, &ctx1);
    assert(rc1 == -1);
    assert(s_terminal_called == 0); /* Terminal step was NOT invoked */
    csilk_json_free((csilk_json_t*)ctx1.params);

    /* Test Case 2: Node returns malformed JSON -> halts before terminal step */
    ai_workflow_graph_t g2 = {
        .id = "wf_err2",
        .title = "WF2",
        .node_count = 2,
        .nodes = {
            { .node_id = "step2", .title = "Bad JSON Step", .execute = mock_fail_node_malformed_json },
            { .node_id = "step_term", .title = "Terminal Step", .execute = mock_terminal_node }
        }
    };
    ai_wf_context_t ctx2 = { .user_id = 1, .session_id = 1, .params = csilk_json_object() };

    s_terminal_called = 0;
    int rc2 = ai_workflow_execute_stream((csilk_ctx_t*)dummy_http_ctx, &g2, &ctx2);
    assert(rc2 == -1);
    assert(s_terminal_called == 0);
    csilk_json_free((csilk_json_t*)ctx2.params);

    free(dummy_http_ctx);
    printf("PASS: test_workflow_step_error completed successfully\n");
    return 0;
}
