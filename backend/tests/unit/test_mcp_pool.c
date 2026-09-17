#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "domain/mcp/entity.h"
#include "services/ai/tools/mcp/mcp_config.h"
#include "services/ai/tools/mcp/mcp_stdio_pool.h"

int
main(void)
{
    /* Force the pool to be disabled so acquire takes the short-lived path. */
    setenv("MINEFOLIO_MCP_STDIO_MAX_PROCS", "0", 1);
    assert(mf_mcp_config_stdio_max_procs() == 0);

    mf_mcp_server_t srv = {0};
    srv.user_id   = 1;
    srv.id        = 99;
    srv.transport = MCP_TRANSPORT_STDIO;
    strcpy(srv.name, "probe");
    srv.command[0] = '\0'; /* no real spawn — exercise pool bookkeeping only */

    char err[256];
    /* Each acquire here either yields a short-lived client or NULL+err (spawn
     * fails because command is empty). Neither must corrupt the pool or crash. */
    for (int i = 0; i < 4; i++) {
        err[0] = '\0';
        mf_mcp_client_t* c = mf_mcp_stdio_pool_acquire(1, 99, &srv, err, sizeof(err));
        if (c) {
            mf_mcp_stdio_pool_release(c); /* short-lived: release == free */
        }
    }

    int shutdowns = mf_mcp_stdio_pool_shutdown();
    printf("PASS: test_mcp_pool (shutdowns=%d)\n", shutdowns);
    (void)shutdowns;
    return 0;
}
