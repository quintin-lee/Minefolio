#include "services/ai/tools/mcp/mcp_stdio_pool.h"

#include "services/ai/tools/mcp/mcp_config.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* 池条目。client->last_used 不存于 mf_mcp_client_t，池自维护时间戳。 */
typedef struct {
    int64_t          user_id;
    int64_t          server_id;
    mf_mcp_client_t* client;
    time_t           last_used;
    bool             in_use; /* 已被某 dispatch 持有，evict 跳过 */
} pool_entry_t;

#define MF_MCP_STDIO_POOL_CAP 64
static pool_entry_t
    s_pool[MF_MCP_STDIO_POOL_CAP]; /* 固定容量，上限由 MINEFOLIO_MCP_STDIO_MAX_PROCS 控制逻辑 */
static size_t          s_pool_count = 0;
static pthread_mutex_t s_pool_lock = PTHREAD_MUTEX_INITIALIZER;

static size_t
find_idle_slot(int64_t user_id, int64_t server_id)
{
    size_t scan = s_pool_count;
    for (size_t i = 0; i < scan; i++) {
        if (s_pool[i].user_id == user_id && s_pool[i].server_id == server_id && !s_pool[i].in_use) {
            return i;
        }
    }
    return SIZE_MAX;
}

mf_mcp_client_t*
mf_mcp_stdio_pool_acquire(
    int64_t user_id, int64_t server_id, const mf_mcp_server_t* server, char* err, size_t err_sz)
{
    /* 池禁用（_STDIO_MAX_PROCS=0）或非 stdio：走 short-lived new/free，不入池 */
    if (mf_mcp_config_stdio_max_procs() <= 0 || !server ||
        server->transport != MCP_TRANSPORT_STDIO) {
        mf_mcp_client_t* c = mf_mcp_client_new(server);
        if (c && mf_mcp_client_initialize(c, err, err_sz) != 0) {
            mf_mcp_client_free(c);
            return NULL;
        }
        return c; /* 调用方负责 mf_mcp_client_free（池外） */
    }

    pthread_mutex_lock(&s_pool_lock);

    size_t slot = find_idle_slot(user_id, server_id);
    if (slot != SIZE_MAX) {
        /* 命中闲置条目：标记在用，更新 LRU 时间戳 */
        s_pool[slot].in_use = true;
        s_pool[slot].last_used = time(NULL);
        mf_mcp_client_t* got = s_pool[slot].client;
        pthread_mutex_unlock(&s_pool_lock);
        return got; /* 调用方用完后 release；命中不重新 initialize（会话保持） */
    }

    /* 未命中闲置条目：需要新分配 */
    int max = mf_mcp_config_stdio_max_procs();
    if (max <= 0 || max > MF_MCP_STDIO_POOL_CAP) {
        max = MF_MCP_STDIO_POOL_CAP;
    }
    if (s_pool_count >= (size_t)max || s_pool_count >= MF_MCP_STDIO_POOL_CAP) {
        /* 池满：LRU 淘汰最久未用（仅淘汰不在用的条目） */
        size_t victim = SIZE_MAX;
        time_t oldest = 0;
        for (size_t i = 0; i < s_pool_count; i++) {
            if (s_pool[i].in_use) {
                continue;
            }
            if (victim == SIZE_MAX || s_pool[i].last_used < oldest) {
                oldest = s_pool[i].last_used;
                victim = i;
            }
        }
        if (victim != SIZE_MAX) {
            mf_mcp_client_free(s_pool[victim].client); /* SIGTERM→SIGKILL 双段 */
            s_pool[victim] = s_pool[--s_pool_count];   /* 末位覆盖回收空出一个槽位 */
        } else {
            /* 全被占用且池满：走 short-lived（不抢占在用会话，并发请求安全隔离） */
            pthread_mutex_unlock(&s_pool_lock);
            mf_mcp_client_t* c = mf_mcp_client_new(server);
            if (c && mf_mcp_client_initialize(c, err, err_sz) != 0) {
                mf_mcp_client_free(c);
                return NULL;
            }
            return c;
        }
    }

    /* 新条目 */
    mf_mcp_client_t* c = mf_mcp_client_new(server);
    if (!c) {
        pthread_mutex_unlock(&s_pool_lock);
        if (err && err_sz) {
            snprintf(err, err_sz, "mcp stdio pool: OOM");
        }
        return NULL;
    }
    if (mf_mcp_client_initialize(c, err, err_sz) != 0) {
        mf_mcp_client_free(c);
        pthread_mutex_unlock(&s_pool_lock);
        return NULL;
    }

    if (s_pool_count < MF_MCP_STDIO_POOL_CAP) {
        s_pool[s_pool_count].user_id = user_id;
        s_pool[s_pool_count].server_id = server_id;
        s_pool[s_pool_count].client = c;
        s_pool[s_pool_count].last_used = time(NULL);
        s_pool[s_pool_count].in_use = true;
        s_pool_count++;
    }
    pthread_mutex_unlock(&s_pool_lock);
    return c; /* 调用方用完后 MUST release */
}

void
mf_mcp_stdio_pool_release(mf_mcp_client_t* client)
{
    if (!client) {
        return;
    }
    pthread_mutex_lock(&s_pool_lock);
    for (size_t i = 0; i < s_pool_count; i++) {
        if (s_pool[i].client == client) {
            s_pool[i].in_use = false;
            s_pool[i].last_used = time(NULL);
            pthread_mutex_unlock(&s_pool_lock);
            return; /* 池条目：保持子进程存活供下次 acquire 复用 */
        }
    }
    pthread_mutex_unlock(&s_pool_lock);
    /* 池外条目（池禁用/池满退化路径）：直接关闭子进程 */
    mf_mcp_client_free(client);
}

int
mf_mcp_stdio_pool_evict_idle(void)
{
    int ttl = mf_mcp_config_cache_ttl_sec();
    if (ttl <= 0) {
        return 0;
    }
    time_t now = time(NULL);
    int    evicted = 0;
    pthread_mutex_lock(&s_pool_lock);
    size_t i = 0;
    while (i < s_pool_count) {
        if (!s_pool[i].in_use && (now - s_pool[i].last_used) > (time_t)ttl) {
            mf_mcp_client_free(s_pool[i].client); /* SIGTERM→SIGKILL 双段 */
            s_pool[i] = s_pool[--s_pool_count];   /* 末位覆盖 */
            evicted++;
            /* 不 i++，检查被覆盖的末位 */
        } else {
            i++;
        }
    }
    pthread_mutex_unlock(&s_pool_lock);
    return evicted;
}

int
mf_mcp_stdio_pool_shutdown(void)
{
    int count = 0;
    pthread_mutex_lock(&s_pool_lock);
    while (s_pool_count > 0) {
        s_pool_count--;
        mf_mcp_client_free(s_pool[s_pool_count].client);
        count++;
    }
    pthread_mutex_unlock(&s_pool_lock);
    return count;
}
