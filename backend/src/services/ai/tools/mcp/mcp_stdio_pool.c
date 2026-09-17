#include "services/ai/tools/mcp/mcp_stdio_pool.h"

#include "services/ai/tools/mcp/mcp_config.h"

#include <pthread.h>
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
    bool             in_use;      /* 已被某 dispatch 持有，evict 跳过 */
    bool             pool;        /* 池条目（vs short-lived） */
    bool             in_progress; /* client==NULL 占位：initialize 进行中 */
} pool_entry_t;

#define MF_MCP_STDIO_POOL_CAP 64
static pool_entry_t
    s_pool[MF_MCP_STDIO_POOL_CAP]; /* 固定容量，上限由 MINEFOLIO_MCP_STDIO_MAX_PROCS 控制逻辑 */
static size_t          s_pool_count = 0;
static pthread_mutex_t s_pool_lock = PTHREAD_MUTEX_INITIALIZER;

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
       所有 spawn + 握手 I/O（initialize）在锁外执行，避免串行化并发 dispatch。 */
    pthread_mutex_lock(&s_pool_lock);

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
       在锁内安全地 free 其子进程（mf_mcp_client_free 只触碰子进程，不碰池） */
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
        /* 淘汰：先取出末位将被覆盖的条目客户端指针，再移动末位到 victim，
           最后 free 被驱逐的旧子进程（lock-safe：仅触碰子进程，不碰池）。 */
        mf_mcp_client_t* dead = s_pool[victim].client;
        s_pool[victim] = s_pool[--s_pool_count];
        mf_mcp_client_free(dead);
    }

    /* 预留占位槽：in_progress=true / client=NULL，稍后锁外 initialize */
    if (s_pool_count >= MF_MCP_STDIO_POOL_CAP) {
        pthread_mutex_unlock(&s_pool_lock);
        goto short_lived;
    }
    size_t reserve = s_pool_count++;
    s_pool[reserve].user_id = user_id;
    s_pool[reserve].server_id = server_id;
    s_pool[reserve].client = NULL;
    s_pool[reserve].last_used = time(NULL);
    s_pool[reserve].in_use = true;
    s_pool[reserve].pool = true;
    s_pool[reserve].in_progress = true;
    pthread_mutex_unlock(&s_pool_lock);

    /* 阶段 2：锁外 spawn + 握手（子进程 I/O，秒级）。
       占位槽已预留，并发同键线程在阶段 1 会看到 in_progress 而转 short-lived。 */
    {
        mf_mcp_client_t* c = mf_mcp_client_new(server);
        if (!c) {
            pthread_mutex_lock(&s_pool_lock);
            s_pool[reserve] = s_pool[--s_pool_count]; /* 回收占位槽 */
            pthread_mutex_unlock(&s_pool_lock);
            if (err && err_sz) {
                snprintf(err, err_sz, "mcp stdio pool: OOM");
            }
            return NULL;
        }
        if (mf_mcp_client_initialize(c, err, err_sz) != 0) {
            mf_mcp_client_free(c);
            pthread_mutex_lock(&s_pool_lock);
            s_pool[reserve] = s_pool[--s_pool_count]; /* 回收占位槽 */
            pthread_mutex_unlock(&s_pool_lock);
            return NULL;
        }

        /* 阶段 3：锁内回填就绪条目 */
        pthread_mutex_lock(&s_pool_lock);
        s_pool[reserve].client = c;
        s_pool[reserve].in_progress = false;
        s_pool[reserve].last_used = time(NULL);
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
        /* 跳过进行中的占位槽：initialize 由持有线程负责清理，evict 不得触碰
           client==NULL 的槽位（mf_mcp_client_free(NULL) 虽是 no-op，但
           末位覆盖会把 in_progress 标记一并搬走，破坏持有线程的回填）。 */
        if (s_pool[i].in_progress) {
            i++;
            continue;
        }
        if (!s_pool[i].in_use && (now - s_pool[i].last_used) > (time_t)ttl) {
            mf_mcp_client_t* dead = s_pool[i].client;
            s_pool[i] = s_pool[--s_pool_count]; /* 末位覆盖 */
            mf_mcp_client_free(dead);
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
        /* 进行中的占位槽 client==NULL：mf_mcp_client_free(NULL) 为 no-op，安全。 */
        mf_mcp_client_t* dead = s_pool[s_pool_count].client;
        mf_mcp_client_free(dead);
        count++;
    }
    pthread_mutex_unlock(&s_pool_lock);
    return count;
}
