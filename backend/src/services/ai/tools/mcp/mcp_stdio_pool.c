#include "services/ai/tools/mcp/mcp_stdio_pool.h"

#include "services/ai/tools/mcp/mcp_config.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* 池条目。client->last_used 不存于 mf_mcp_client_t，池自维护时间戳。
   pool 为 true 表示该条目由池管理（子进程存活供复用）；false 表示池外 short-lived
   （调用方 release 时直接 free）。in_progress 表示 client 尚在 initialize 中
   （client==NULL 占位），其他线程不得复用该槽位。 */
typedef struct {
    int64_t          user_id;
    int64_t          server_id;
    mf_mcp_client_t* client;
    time_t           last_used;
    uint64_t         reserve_id;  /* 占位槽唯一令牌：防止解锁后下标漂移/越界 */
    bool             in_use;      /* 已被某 dispatch 持有，evict 跳过 */
    bool             pool;        /* 池条目（vs short-lived） */
    bool             in_progress; /* client==NULL 占位：initialize 进行中 */
} pool_entry_t;

#define MF_MCP_STDIO_POOL_CAP 64
static pool_entry_t
    s_pool[MF_MCP_STDIO_POOL_CAP]; /* 固定容量，上限由 MINEFOLIO_MCP_STDIO_MAX_PROCS 控制逻辑 */
static size_t          s_pool_count = 0;
static uint64_t        s_next_reserve_id = 0;
static bool            s_shutting_down = false;
static pthread_mutex_t s_pool_lock = PTHREAD_MUTEX_INITIALIZER;

/* 解锁执行 I/O 后必须按 reserve_id 重新定位占位槽，禁止复用解锁前的数组下标。 */
static size_t
find_reserved_slot(uint64_t reserve_id)
{
    if (reserve_id == 0) {
        return SIZE_MAX;
    }
    for (size_t i = 0; i < s_pool_count; i++) {
        if (s_pool[i].in_progress && s_pool[i].reserve_id == reserve_id) {
            return i;
        }
    }
    return SIZE_MAX;
}

/* 查找 (user, server) 对应的"就绪"闲置槽位：!in_use 且 !in_progress。
   in_progress 槽位（client==NULL 占位、initialize 进行中）不可复用 —— 其他线程
   必须走 short-lived 路径而非等待其握手完成。 */
static size_t
find_idle_slot(int64_t user_id, int64_t server_id)
{
    size_t scan = s_pool_count;
    for (size_t i = 0; i < scan; i++) {
        if (s_pool[i].user_id == user_id && s_pool[i].server_id == server_id && !s_pool[i].in_use &&
            !s_pool[i].in_progress) {
            return i;
        }
    }
    return SIZE_MAX;
}

/* 查找 (user, server) 是否已存在"进行中"槽位（in_progress）。 */
static size_t
find_in_progress_slot(int64_t user_id, int64_t server_id)
{
    size_t scan = s_pool_count;
    for (size_t i = 0; i < scan; i++) {
        if (s_pool[i].user_id == user_id && s_pool[i].server_id == server_id &&
            s_pool[i].in_progress) {
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

    int max = mf_mcp_config_stdio_max_procs();
    if (max <= 0 || max > MF_MCP_STDIO_POOL_CAP) {
        max = MF_MCP_STDIO_POOL_CAP;
    }

    /* 阶段 1：锁内做纯簿记 —— 命中就绪闲置 / 抢占 LRU / 预留占位槽。
       所有 spawn + 握手 I/O（initialize）与淘汰回收（mf_mcp_client_free）在锁外执行，
       避免持锁阻塞或串行化并发 dispatch。 */
    mf_mcp_client_t* evicted_dead = NULL;
    pthread_mutex_lock(&s_pool_lock);

    if (s_shutting_down) {
        pthread_mutex_unlock(&s_pool_lock);
        if (err && err_sz) {
            snprintf(err, err_sz, "mcp stdio pool: shut down");
        }
        return NULL;
    }

    size_t slot = find_idle_slot(user_id, server_id);
    if (slot != SIZE_MAX) {
        /* 命中就绪闲置条目：标记在用，更新 LRU 时间戳 */
        s_pool[slot].in_use = true;
        s_pool[slot].last_used = time(NULL);
        mf_mcp_client_t* got = s_pool[slot].client;
        pthread_mutex_unlock(&s_pool_lock);
        return got; /* 调用方用完后 release；命中不重新 initialize（会话保持） */
    }

    /* 另一线程正在为同一 (user, server) 做握手：不等待、不抢占，走 short-lived */
    if (find_in_progress_slot(user_id, server_id) != SIZE_MAX) {
        pthread_mutex_unlock(&s_pool_lock);
        goto short_lived;
    }

    /* 池满时先 LRU 淘汰一个"就绪且闲置"的条目（仅淘汰非 in_progress 的），
       先在锁内从池中摘除指针，解锁后再 free 其子进程，避免持锁阻塞 waitpid。 */
    if (s_pool_count >= (size_t)max || s_pool_count >= MF_MCP_STDIO_POOL_CAP) {
        size_t victim = SIZE_MAX;
        time_t oldest = 0;
        for (size_t i = 0; i < s_pool_count; i++) {
            if (s_pool[i].in_use || s_pool[i].in_progress) {
                continue;
            }
            if (victim == SIZE_MAX || s_pool[i].last_used < oldest) {
                oldest = s_pool[i].last_used;
                victim = i;
            }
        }
        if (victim == SIZE_MAX) {
            /* 全部被占用/进行中且池满：走 short-lived（并发请求安全隔离） */
            pthread_mutex_unlock(&s_pool_lock);
            goto short_lived;
        }
        evicted_dead = s_pool[victim].client;
        s_pool[victim] = s_pool[--s_pool_count];
    }

    /* 预留占位槽：in_progress=true / client=NULL，稍后锁外 initialize */
    if (s_pool_count >= MF_MCP_STDIO_POOL_CAP) {
        pthread_mutex_unlock(&s_pool_lock);
        if (evicted_dead) {
            mf_mcp_client_free(evicted_dead);
        }
        goto short_lived;
    }
    size_t   reserve = s_pool_count++;
    uint64_t my_reserve_id = ++s_next_reserve_id;
    if (my_reserve_id == 0) {
        my_reserve_id = ++s_next_reserve_id;
    }
    s_pool[reserve].user_id = user_id;
    s_pool[reserve].server_id = server_id;
    s_pool[reserve].client = NULL;
    s_pool[reserve].last_used = time(NULL);
    s_pool[reserve].reserve_id = my_reserve_id;
    s_pool[reserve].in_use = true;
    s_pool[reserve].pool = true;
    s_pool[reserve].in_progress = true;
    pthread_mutex_unlock(&s_pool_lock);

    if (evicted_dead) {
        mf_mcp_client_free(evicted_dead);
    }

    /* 阶段 2：锁外 spawn + 握手（子进程 I/O，秒级）。
       占位槽已预留，并发同键线程在阶段 1 会看到 in_progress 而转 short-lived。
       注意：解锁期间 s_pool 可能被其他线程淘汰压缩或 shutdown 清空，
       因此重新加锁后必须通过 find_reserved_slot(my_reserve_id) 重查下标。 */
    {
        mf_mcp_client_t* c = mf_mcp_client_new(server);
        if (!c) {
            pthread_mutex_lock(&s_pool_lock);
            size_t idx = find_reserved_slot(my_reserve_id);
            if (idx != SIZE_MAX && s_pool_count > 0) {
                s_pool[idx] = s_pool[--s_pool_count]; /* 回收占位槽 */
            }
            if (s_shutting_down && s_pool_count == 0) {
                s_shutting_down = false;
            }
            pthread_mutex_unlock(&s_pool_lock);
            if (err && err_sz) {
                snprintf(err, err_sz, "mcp stdio pool: OOM");
            }
            return NULL;
        }
        if (mf_mcp_client_initialize(c, err, err_sz) != 0) {
            mf_mcp_client_free(c);
            pthread_mutex_lock(&s_pool_lock);
            size_t idx = find_reserved_slot(my_reserve_id);
            if (idx != SIZE_MAX && s_pool_count > 0) {
                s_pool[idx] = s_pool[--s_pool_count]; /* 回收占位槽 */
            }
            if (s_shutting_down && s_pool_count == 0) {
                s_shutting_down = false;
            }
            pthread_mutex_unlock(&s_pool_lock);
            return NULL;
        }

        /* 阶段 3：锁内重定位占位槽并回填就绪条目 */
        pthread_mutex_lock(&s_pool_lock);
        size_t idx = find_reserved_slot(my_reserve_id);
        if (idx == SIZE_MAX || s_shutting_down) {
            /* 握手期间池被 shutdown，摘除占位槽并释放客户端后失败返回 */
            if (idx != SIZE_MAX && s_pool_count > 0) {
                s_pool[idx] = s_pool[--s_pool_count];
            }
            if (s_shutting_down && s_pool_count == 0) {
                s_shutting_down = false;
            }
            pthread_mutex_unlock(&s_pool_lock);
            mf_mcp_client_free(c);
            if (err && err_sz) {
                snprintf(err, err_sz, "mcp stdio pool: shut down during initialize");
            }
            return NULL;
        }
        s_pool[idx].client = c;
        s_pool[idx].in_progress = false;
        s_pool[idx].reserve_id = 0;
        s_pool[idx].last_used = time(NULL);
        pthread_mutex_unlock(&s_pool_lock);
        return c; /* 池条目：调用方用完后 MUST release */
    }

short_lived:
    /* 池外 short-lived：spawn + 握手全程锁外；release 时直接 free（pool=false） */
    mf_mcp_client_t* c = mf_mcp_client_new(server);
    if (c && mf_mcp_client_initialize(c, err, err_sz) != 0) {
        mf_mcp_client_free(c);
        return NULL;
    }
    return c; /* 调用方 release（池外条目直接 free） */
}

void
mf_mcp_stdio_pool_release(mf_mcp_client_t* client)
{
    if (!client) {
        return;
    }
    mf_mcp_client_t* to_free = NULL;
    pthread_mutex_lock(&s_pool_lock);
    for (size_t i = 0; i < s_pool_count; i++) {
        if (s_pool[i].client == client) {
            if (s_shutting_down) {
                to_free = s_pool[i].client;
                s_pool[i] = s_pool[--s_pool_count];
                if (s_pool_count == 0) {
                    s_shutting_down = false;
                }
            } else {
                s_pool[i].in_use = false;
                s_pool[i].last_used = time(NULL);
            }
            pthread_mutex_unlock(&s_pool_lock);
            if (to_free) {
                mf_mcp_client_free(to_free);
            }
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
    time_t           now = time(NULL);
    mf_mcp_client_t* dead[MF_MCP_STDIO_POOL_CAP];
    size_t           dead_count = 0;
    int              evicted = 0;

    pthread_mutex_lock(&s_pool_lock);
    size_t i = 0;
    while (i < s_pool_count) {
        /* 跳过进行中的占位槽：initialize 由持有线程负责清理，evict 不得触碰
           client==NULL 的槽位（mf_mcp_client_free(NULL) 虽是 no-op，但
           末位覆盖会把 in_progress 标记一并搬走，破坏持有线程的回填）。 */
        if (s_pool[i].in_progress) {
            i++;
            continue;
        }
        if (!s_pool[i].in_use && (now - s_pool[i].last_used) > (time_t)ttl) {
            if (s_pool[i].client && dead_count < MF_MCP_STDIO_POOL_CAP) {
                dead[dead_count++] = s_pool[i].client;
            }
            s_pool[i] = s_pool[--s_pool_count]; /* 末位覆盖 */
            evicted++;
            /* 不 i++，检查被覆盖的末位 */
        } else {
            i++;
        }
    }
    pthread_mutex_unlock(&s_pool_lock);

    for (size_t k = 0; k < dead_count; k++) {
        mf_mcp_client_free(dead[k]);
    }
    return evicted;
}

int
mf_mcp_stdio_pool_shutdown(void)
{
    mf_mcp_client_t* dead[MF_MCP_STDIO_POOL_CAP];
    size_t           dead_count = 0;
    int              count = 0;

    pthread_mutex_lock(&s_pool_lock);
    s_shutting_down = true;
    size_t i = 0;
    while (i < s_pool_count) {
        if (!s_pool[i].in_use && !s_pool[i].in_progress) {
            if (s_pool[i].client && dead_count < MF_MCP_STDIO_POOL_CAP) {
                dead[dead_count++] = s_pool[i].client;
            }
            s_pool[i] = s_pool[--s_pool_count];
            count++;
        } else {
            i++;
        }
    }
    if (s_pool_count == 0) {
        s_shutting_down = false;
    }
    pthread_mutex_unlock(&s_pool_lock);

    for (size_t k = 0; k < dead_count; k++) {
        mf_mcp_client_free(dead[k]);
    }
    return count;
}
