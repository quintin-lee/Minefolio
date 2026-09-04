#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include "services/ai/runtime/error.h"

static void test_runtime_error_taxonomy(void) {
    assert(strcmp(ai_runtime_error_name(AI_RUNTIME_ERR_OK), "OK") == 0);
    assert(strcmp(ai_runtime_error_name(AI_RUNTIME_ERR_MODEL), "MODEL_ERROR") == 0);
    assert(strcmp(ai_runtime_error_name(AI_RUNTIME_ERR_TOOL), "TOOL_ERROR") == 0);
    assert(strcmp(ai_runtime_error_name(AI_RUNTIME_ERR_POLICY), "POLICY_ERROR") == 0);
    assert(strcmp(ai_runtime_error_name(AI_RUNTIME_ERR_TIMEOUT), "TIMEOUT") == 0);
    assert(strcmp(ai_runtime_error_name(AI_RUNTIME_ERR_CONTEXT_OVERFLOW), "CONTEXT_OVERFLOW") == 0);
    assert(strcmp(ai_runtime_error_name(AI_RUNTIME_ERR_VALIDATION), "VALIDATION_ERROR") == 0);
    assert(strcmp(ai_runtime_error_name(AI_RUNTIME_ERR_CANCELLED), "CANCELLED") == 0);

    ai_runtime_status_t st = {0};
    ai_runtime_status_set(&st, AI_RUNTIME_ERR_POLICY, "Permission denied for tool", "asset_delete requires ADMIN");
    assert(st.code == AI_RUNTIME_ERR_POLICY);
    assert(strcmp(st.message, "Permission denied for tool") == 0);
    assert(strcmp(st.detail, "asset_delete requires ADMIN") == 0);

    printf("PASS: test_runtime_error_taxonomy\n");
}

int main(void) {
    test_runtime_error_taxonomy();
    printf("All runtime initial tests passed!\n");
    return 0;
}
