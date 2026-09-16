#include "services/ai/tools/mcp/mcp_config.h"
#include "config/secret.h"
#include <stdlib.h>
#include <string.h>

/* 读 MINEFOLIO_MCP_CACHE_TTL，默认 1800 秒（30 分钟）。负值 → 0（禁用 TTL）。 */
int
mf_mcp_config_cache_ttl_sec(void)
{
    char        buf[32];
    const char* v = config_env_get("MINEFOLIO_MCP_CACHE_TTL", buf, sizeof(buf), "1800");
    long        n = strtol(v, NULL, 10);
    return n < 0 ? 0 : (int)n;
}

/* 读 MINEFOLIO_MCP_STDIO_MAX_PROCS，默认 16。负值 → 0（禁用池）。 */
int
mf_mcp_config_stdio_max_procs(void)
{
    char        buf[32];
    const char* v = config_env_get("MINEFOLIO_MCP_STDIO_MAX_PROCS", buf, sizeof(buf), "16");
    long        n = strtol(v, NULL, 10);
    return n < 0 ? 0 : (int)n;
}

/* 读 MINEFOLIO_MCP_RATE_PER_MIN，默认 30。负值 → 0（禁用 MCP 专属限频）。 */
int
mf_mcp_config_rate_per_min(void)
{
    char        buf[32];
    const char* v = config_env_get("MINEFOLIO_MCP_RATE_PER_MIN", buf, sizeof(buf), "30");
    long        n = strtol(v, NULL, 10);
    return n < 0 ? 0 : (int)n;
}

/* MINEFOLIO_MCP_ALLOW_LOCAL=1 放行内网/回环地址。与 usecases.c 现有 mcp_allow_local 等价，集中此供全局复用。 */
bool
mf_mcp_config_allow_local(void)
{
    const char* v = config_env_get("MINEFOLIO_MCP_ALLOW_LOCAL", NULL, 0, "0");
    return v != NULL && strcmp(v, "1") == 0;
}
