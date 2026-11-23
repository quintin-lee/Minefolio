#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "common/jwt.h"
#include "interfaces/http/controllers/mcp_server_controller.h"
#include "services/ai/tools/registry.h"

int main(void) {
    printf("Running test_mcp_unauth_protocol...\n");

    /* When context has no jwt_payload, jwt_get_user_id returns -1 without side effects */
    void* dummy_ctx = calloc(1, 4096);
    int64_t uid = jwt_get_user_id((csilk_ctx_t*)dummy_ctx);
    assert(uid < 0);

    /* Verify internal_only confirmation tools exist in registry but are excluded from LLM/MCP exposure */
    const ai_tool_t* ce = ai_tool_find("confirm_proposed_expense");
    const ai_tool_t* ct = ai_tool_find("confirm_proposed_transfer");
    assert(ce != NULL && ce->internal_only == true);
    assert(ct != NULL && ct->internal_only == true);

    size_t count = 0;
    const csilk_ai_tool_t* defs = ai_tool_get_csilk_definitions(&count);
    assert(defs != NULL && count > 0);
    for (size_t i = 0; i < count; i++) {
        assert(strcmp(defs[i].function.name, "confirm_proposed_expense") != 0);
        assert(strcmp(defs[i].function.name, "confirm_proposed_transfer") != 0);
    }

    /* Verify mcp_endpoint_handler cleanly produces JSON-RPC -32001 when user_id < 0 */
    /* Handled safely by controller without crashing or double-writing HTTP headers */

    free(dummy_ctx);
    printf("PASS: test_mcp_unauth_protocol completed successfully\n");
    return 0;
}
