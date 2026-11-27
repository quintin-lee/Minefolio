#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "domain/mcp/entity.h"
#include "services/ai/tools/mcp/mcp_client.h"
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

    /* evict_idle 在空池上必须安全返回 0（调度心跳每 30s 调用一次） */
    assert(mf_mcp_stdio_pool_evict_idle() == 0);
    printf("PASS: test_mcp_pool_evict_idle_empty\n");

    /* env_json 黑名单拦截校验 (LD_* / PATH / DYLD_* / 敏感关键字 / 控制字符) */
    assert(mf_mcp_env_key_is_blocked("LD_PRELOAD") == true);
    assert(mf_mcp_env_key_is_blocked("ld_library_path") == true);
    assert(mf_mcp_env_key_is_blocked("PATH") == true);
    assert(mf_mcp_env_key_is_blocked("path") == true);
    assert(mf_mcp_env_key_is_blocked("DYLD_INSERT_LIBRARIES") == true);
    assert(mf_mcp_env_key_is_blocked("PYTHONPATH") == true);
    assert(mf_mcp_env_key_is_blocked("NODE_OPTIONS") == true);
    assert(mf_mcp_env_key_is_blocked("MY_API_SECRET") == true);
    assert(mf_mcp_env_key_is_blocked("BAD=KEY") == true);
    assert(mf_mcp_env_key_is_blocked("MCP_LOG_LEVEL") == false);
    assert(mf_mcp_env_key_is_blocked("TZ") == false);
    printf("PASS: test_mcp_env_blacklist\n");

    /* Shutdown while one client is in_use, then release: zero UAF / double-free under ASan. */
    setenv("MINEFOLIO_MCP_STDIO_MAX_PROCS", "4", 1);
    assert(mf_mcp_config_stdio_max_procs() == 4);

    mf_mcp_server_t live_srv = {0};
    live_srv.user_id         = 1;
    live_srv.transport       = MCP_TRANSPORT_STDIO;
    live_srv.timeout_ms      = 2000;
    strcpy(live_srv.name, "mock_mcp");
    strcpy(live_srv.command, "bash");
    strcpy(live_srv.args_json,
           "[\"-c\", \"while IFS= read -r line; do printf "
           "'{\\\"jsonrpc\\\":\\\"2.0\\\",\\\"id\\\":1,\\\"result\\\":{}}\\n'; done\"]");

    live_srv.id = 101;
    err[0] = '\0';
    mf_mcp_client_t* idle_c = mf_mcp_stdio_pool_acquire(1, 101, &live_srv, err, sizeof(err));
    assert(idle_c != NULL);
    mf_mcp_stdio_pool_release(idle_c); /* now !in_use in pool */

    live_srv.id = 102;
    err[0] = '\0';
    mf_mcp_client_t* inuse_c = mf_mcp_stdio_pool_acquire(1, 102, &live_srv, err, sizeof(err));
    assert(inuse_c != NULL);

    /* Shutdown must only tear down idle entries; in_use client stays valid until released. */
    int detached = mf_mcp_stdio_pool_shutdown();
    assert(detached == 1);

    err[0] = '\0';
    char* resp = mf_mcp_client_call_tool(inuse_c, "ping", "{}", err, sizeof(err));
    assert(resp != NULL);
    free(resp);

    mf_mcp_stdio_pool_release(inuse_c);
    assert(mf_mcp_stdio_pool_shutdown() == 0);
    printf("PASS: test_mcp_pool_shutdown_while_in_use\n");
    return 0;
}
