#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "services/ai/tools/mcp/mcp_client.h"

int main(void) {
    printf("Running test_mcp_child_sanitation...\n");

    /* Create dummy file descriptors that parent process holds open */
    int dummy_pipe1[2];
    int dummy_pipe2[2];
    assert(pipe(dummy_pipe1) == 0);
    assert(pipe(dummy_pipe2) == 0);

    /* Use bash script checking that only descriptors 0, 1, 2 remain open */
    mf_mcp_server_t s = {
        .transport = MCP_TRANSPORT_STDIO,
        .command = "bash",
        .args_json = "[\"-c\", \"if ls /proc/self/fd | grep -E '^([4-9]|[1-9][0-9]+)$' >/dev/null; then echo '{\\\"error\\\":\\\"leaked_fds\\\"}'; else echo '{\\\"jsonrpc\\\":\\\"2.0\\\",\\\"id\\\":1,\\\"result\\\":{\\\"clean\\\":true}}'; fi\"]",
        .timeout_ms = 4000
    };

    mf_mcp_client_t* c = mf_mcp_client_new(&s);
    assert(c != NULL);

    char err[256] = {0};
    char* resp = mf_mcp_client_call_tool(c, "dummy", "{}", err, sizeof(err));
    assert(resp != NULL);
    assert(strstr(resp, "\"clean\":true") != NULL);
    free(resp);

    close(dummy_pipe1[0]);
    close(dummy_pipe1[1]);
    close(dummy_pipe2[0]);
    close(dummy_pipe2[1]);
    mf_mcp_client_free(c);
    printf("PASS: test_mcp_child_sanitation completed successfully\n");
    return 0;
}
