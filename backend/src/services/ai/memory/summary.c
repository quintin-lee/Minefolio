#include "services/ai/memory/summary.h"
#include "common/ai_config.h"
#include "infrastructure/repositories/ai_session_repo_impl.h"
#include "services/ai_service.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 摘要生成参数 */
#define SUMMARY_HISTORY_LIMIT 50
#define SUMMARY_MAX_OUTPUT_TOKENS 2048
#define SUMMARY_INFLIGHT_SLOTS 256
#define SUMMARY_TRIGGER_RATIO 0.8
/** 待落库摘要槽位上限：worker 只做 LLM 调用，结果暂存于此，
    由主线程在下次交互（触发或读取摘要）时串行落库 */
#define SUMMARY_PENDING_SLOTS 64
#define SUMMARY_QUEUE_CAP 64

typedef struct {
    int64_t user_id;
    int64_t session_id;
    /* Provider 配置快照：在 trigger 线程（g_config 有效时）深拷贝，detached worker 只读本地副本，
     * 与 g_config 生命周期解耦，消除 UAF。 */
    char   dname[16];
    char   api_key[512];
    char   base_url[512];
    char   model[128];
    int    has_key;
    size_t inflight_idx;
    char*  user_prompt; /**< 触发线程构建好的摘要 prompt；worker 全程不触库 */
} summary_task_t;

typedef struct {
    int64_t session_id;
    int64_t user_id;
    char*   text;
    int     completion_tokens;
    time_t  created_at; /* 用于 LRU 替换最旧的一项 */
} summary_pending_t;

static summary_pending_t s_pending[SUMMARY_PENDING_SLOTS];
static pthread_mutex_t   s_pending_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    summary_task_t* queue[SUMMARY_QUEUE_CAP];
    size_t          head;
    size_t          tail;
    size_t          count;
    pthread_mutex_t lock;
    pthread_cond_t  not_empty;
    bool            shutdown;
    bool            initialized;
    pthread_t       workers[2];
} summary_worker_pool_t;

static summary_worker_pool_t s_pool = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .not_empty = PTHREAD_COND_INITIALIZER,
    .initialized = false,
    .shutdown = false
};

typedef struct {
    int64_t session_id;
    int     in_flight;
} summary_inflight_t;

static summary_inflight_t s_inflight[SUMMARY_INFLIGHT_SLOTS];
static size_t             s_inflight_head = 0;
static pthread_mutex_t    s_inflight_lock = PTHREAD_MUTEX_INITIALIZER;

/* 取 slot：命中同 session 则标记 found_existing；否则占用空闲或最旧槽位。 */
static size_t
inflight_acquire(int64_t session_id, int* found_existing)
{
    pthread_mutex_lock(&s_inflight_lock);
    *found_existing = 0;
    for (size_t i = 0; i < SUMMARY_INFLIGHT_SLOTS; i++) {
        if (s_inflight[i].in_flight && s_inflight[i].session_id == session_id) {
            pthread_mutex_unlock(&s_inflight_lock);
            *found_existing = 1;
            return i;
        }
    }
    size_t idx = s_inflight_head;
    s_inflight[idx].session_id = session_id;
    s_inflight[idx].in_flight = 1;
    s_inflight_head = (s_inflight_head + 1) % SUMMARY_INFLIGHT_SLOTS;
    pthread_mutex_unlock(&s_inflight_lock);
    return idx;
}

static void
inflight_release(size_t idx)
{
    pthread_mutex_lock(&s_inflight_lock);
    s_inflight[idx].in_flight = 0;
    s_inflight[idx].session_id = 0;
    pthread_mutex_unlock(&s_inflight_lock);
}

/* worker 线程：暂存生成的摘要。不触库——与请求线程共享的 csilk 单连接 SQLite
 * 池无法跨线程安全读写，落库职责统一交回主线程（ai_summary_flush_pending）。 */
static void
summary_stage_result(int64_t session_id, int64_t user_id, const char* text, int completion_tokens)
{
    if (!text || !text[0]) {
        return;
    }
    char* copy = strdup(text);
    if (!copy) {
        return;
    }
    pthread_mutex_lock(&s_pending_lock);
    size_t slot = SUMMARY_PENDING_SLOTS;
    for (size_t i = 0; i < SUMMARY_PENDING_SLOTS; i++) {
        if (s_pending[i].text && s_pending[i].session_id == session_id) {
            free(s_pending[i].text); /* 同会话旧暂存被新结果覆盖 */
            slot = i;
            break;
        }
    }
    if (slot == SUMMARY_PENDING_SLOTS) {
        for (size_t i = 0; i < SUMMARY_PENDING_SLOTS; i++) {
            if (!s_pending[i].text) {
                slot = i;
                break;
            }
        }
    }
    if (slot == SUMMARY_PENDING_SLOTS) {
        /* 槽位全满：LRU 替换最旧的一项 */
        size_t oldest = 0;
        time_t oldest_time = s_pending[0].created_at;
        for (size_t i = 1; i < SUMMARY_PENDING_SLOTS; i++) {
            if (s_pending[i].created_at < oldest_time) {
                oldest_time = s_pending[i].created_at;
                oldest = i;
            }
        }
        slot = oldest;
        free(s_pending[slot].text);
    }
    s_pending[slot].session_id = session_id;
    s_pending[slot].user_id = user_id;
    s_pending[slot].text = copy;
    s_pending[slot].completion_tokens = completion_tokens;
    s_pending[slot].created_at = time(NULL);
    pthread_mutex_unlock(&s_pending_lock);
}

/* 主线程：把该会话暂存的摘要串行落库（应在每次读取摘要/触发摘要前调用）。 */
void
ai_summary_flush_pending(csilk_db_pool_t* pool, int64_t session_id)
{
    if (!pool || session_id <= 0) {
        return;
    }
    char*   text = NULL;
    int64_t user_id = 0;
    int     tokens = 0;
    pthread_mutex_lock(&s_pending_lock);
    for (size_t i = 0; i < SUMMARY_PENDING_SLOTS; i++) {
        if (s_pending[i].text && s_pending[i].session_id == session_id) {
            text = s_pending[i].text;
            user_id = s_pending[i].user_id;
            tokens = s_pending[i].completion_tokens;
            s_pending[i].text = NULL;
            s_pending[i].session_id = 0;
            break;
        }
    }
    pthread_mutex_unlock(&s_pending_lock);
    if (text) {
        mf_ai_summary_upsert(pool, session_id, user_id, text, tokens);
        free(text);
    }
}

/* 把 csilk_json_t 消息数组拼成 "role: content\n" 行，供摘要 LLM 阅读。 */
static char*
build_history_transcript(const csilk_json_t* msgs)
{
    if (!msgs) {
        return NULL;
    }
    size_t count = csilk_json_array_size(msgs);
    if (count == 0) {
        return NULL;
    }
    size_t total = 0;
    for (size_t i = 0; i < count; i++) {
        csilk_json_t* item = csilk_json_array_get(msgs, i);
        if (!item) {
            continue;
        }
        const char* role = csilk_json_get_string(item, "role");
        const char* content = csilk_json_get_string(item, "content");
        total += (role ? strlen(role) : 1) + 2 + (content ? strlen(content) : 0) + 1;
    }
    if (total == 0) {
        return NULL;
    }
    char* buf = (char*)malloc(total + 1);
    if (!buf) {
        return NULL;
    }
    size_t off = 0;
    for (size_t i = 0; i < count; i++) {
        csilk_json_t* item = csilk_json_array_get(msgs, i);
        if (!item) {
            continue;
        }
        const char* role = csilk_json_get_string(item, "role");
        const char* content = csilk_json_get_string(item, "content");
        off += (size_t)snprintf(
            buf + off, total + 1 - off, "%s: %s\n", role ? role : "?", content ? content : "");
    }
    buf[off] = '\0';
    return buf;
}

static void
summary_worker(summary_task_t* task)
{
    /* worker 全程不触库：历史读取与 prompt 构建已由触发线程完成
       （task->user_prompt），结果经 summary_stage_result 暂存、由主线程落库。 */
    csilk_ai_t* inst =
        csilk_ai_new(task->dname, task->api_key, task->base_url[0] ? task->base_url : NULL);
    if (!inst) {
        free(task->user_prompt);
        inflight_release(task->inflight_idx);
        free(task);
        return;
    }

    csilk_ai_message_t messages[2] = {
        {.role = "system", .content = "你是对话摘要助手，负责将长对话浓缩为简洁摘要。"},
        {.role = "user",   .content = task->user_prompt                               },
    };
    csilk_ai_chat_request_t req = {
        .model = task->model,
        .messages = messages,
        .message_count = 2,
        .temperature = 0.2,
        .max_tokens = SUMMARY_MAX_OUTPUT_TOKENS,
        .stream = 0,
        .timeout_ms = 120000,
    };
    csilk_ai_chat_response_t res = {0};
    int                      rc = csilk_ai_chat(inst, &req, &res);

    if (rc == 0 && res.content && res.content[0]) {
        /* 不在此触库（与请求线程共享 csilk 单连接）：暂存结果待主线程落库 */
        summary_stage_result(task->session_id, task->user_id, res.content, res.completion_tokens);
    } else if (res.error_message && res.error_message[0]) {
        fprintf(stderr,
                "[ai-summary] session=%ld summary failed: %s\n",
                (long)task->session_id,
                res.error_message);
    }

    csilk_ai_chat_response_free(&res);
    csilk_ai_free(inst);
    free(task->user_prompt);
    /* 释放 in-flight 槽位后再销毁 task */
    inflight_release(task->inflight_idx);
    free(task);
}

static void*
summary_pool_worker_entry(void* arg)
{
    (void)arg;
    while (1) {
        pthread_mutex_lock(&s_pool.lock);
        while (!s_pool.shutdown && s_pool.count == 0) {
            pthread_cond_wait(&s_pool.not_empty, &s_pool.lock);
        }
        if (s_pool.shutdown) {
            pthread_mutex_unlock(&s_pool.lock);
            break;
        }
        summary_task_t* task = s_pool.queue[s_pool.head];
        s_pool.head = (s_pool.head + 1) % SUMMARY_QUEUE_CAP;
        s_pool.count--;
        pthread_mutex_unlock(&s_pool.lock);

        summary_worker(task);
    }
    return NULL;
}

void
ai_summary_pool_init(void)
{
    pthread_mutex_lock(&s_pool.lock);
    if (s_pool.initialized) {
        pthread_mutex_unlock(&s_pool.lock);
        return;
    }
    s_pool.shutdown = false;
    s_pool.head = 0;
    s_pool.tail = 0;
    s_pool.count = 0;
    pthread_create(&s_pool.workers[0], NULL, summary_pool_worker_entry, NULL);
    pthread_create(&s_pool.workers[1], NULL, summary_pool_worker_entry, NULL);
    s_pool.initialized = true;
    pthread_mutex_unlock(&s_pool.lock);
}

void
ai_summary_pool_shutdown(void)
{
    pthread_mutex_lock(&s_pool.lock);
    if (!s_pool.initialized) {
        pthread_mutex_unlock(&s_pool.lock);
        return;
    }
    s_pool.shutdown = true;
    pthread_cond_broadcast(&s_pool.not_empty);
    pthread_mutex_unlock(&s_pool.lock);

    pthread_join(s_pool.workers[0], NULL);
    pthread_join(s_pool.workers[1], NULL);

    pthread_mutex_lock(&s_pool.lock);
    /* 清空任务队列中的任务 */
    while (s_pool.count > 0) {
        summary_task_t* task = s_pool.queue[s_pool.head];
        s_pool.head = (s_pool.head + 1) % SUMMARY_QUEUE_CAP;
        s_pool.count--;
        if (task) {
            free(task->user_prompt);
            inflight_release(task->inflight_idx);
            free(task);
        }
    }
    s_pool.initialized = false;
    pthread_mutex_unlock(&s_pool.lock);

    /* 清空 pending 暂存槽中的未落库文本 */
    pthread_mutex_lock(&s_pending_lock);
    for (size_t i = 0; i < SUMMARY_PENDING_SLOTS; i++) {
        if (s_pending[i].text) {
            free(s_pending[i].text);
            s_pending[i].text = NULL;
        }
    }
    pthread_mutex_unlock(&s_pending_lock);
}

size_t
ai_summary_pool_queue_count(void)
{
    pthread_mutex_lock(&s_pool.lock);
    size_t c = s_pool.count;
    pthread_mutex_unlock(&s_pool.lock);
    return c;
}

size_t
ai_summary_pending_slots_count(void)
{
    pthread_mutex_lock(&s_pending_lock);
    size_t cnt = 0;
    for (size_t i = 0; i < SUMMARY_PENDING_SLOTS; i++) {
        if (s_pending[i].text) {
            cnt++;
        }
    }
    pthread_mutex_unlock(&s_pending_lock);
    return cnt;
}

int
ai_summary_pool_enqueue_test_task(int64_t session_id, int64_t user_id, const char* prompt)
{
    pthread_mutex_lock(&s_pool.lock);
    if (!s_pool.initialized || s_pool.shutdown) {
        pthread_mutex_unlock(&s_pool.lock);
        return -1;
    }
    if (s_pool.count >= SUMMARY_QUEUE_CAP) {
        pthread_mutex_unlock(&s_pool.lock);
        summary_stage_result(session_id, user_id, prompt, 10);
        return 0;
    }
    int found = 0;
    size_t idx = inflight_acquire(session_id, &found);

    summary_task_t* task = (summary_task_t*)calloc(1, sizeof(summary_task_t));
    if (!task) {
        inflight_release(idx);
        pthread_mutex_unlock(&s_pool.lock);
        return -1;
    }
    task->user_id = user_id;
    task->session_id = session_id;
    task->inflight_idx = idx;
    task->user_prompt = strdup(prompt ? prompt : "");

    s_pool.queue[s_pool.tail] = task;
    s_pool.tail = (s_pool.tail + 1) % SUMMARY_QUEUE_CAP;
    s_pool.count++;
    pthread_cond_signal(&s_pool.not_empty);
    pthread_mutex_unlock(&s_pool.lock);

    summary_stage_result(session_id, user_id, prompt, 10);
    return 0;
}

void
ai_summary_maybe_trigger(csilk_db_pool_t* pool, ai_runtime_context_t* ctx)
{
    if (!pool || !ctx || ctx->session_id <= 0 || ctx->limits.token_budget <= 0) {
        return;
    }
    /* 先把上一轮 worker 暂存的摘要落库（主线程串行触库），再评估是否触发新摘要 */
    ai_summary_flush_pending(pool, ctx->session_id);

    if (ctx->stats.total_tokens < (int)(ctx->limits.token_budget * SUMMARY_TRIGGER_RATIO)) {
        return;
    }

    int    found = 0;
    size_t idx = inflight_acquire(ctx->session_id, &found);
    if (found) {
        return;
    }

    /* 历史与旧摘要在本线程读取、prompt 在本线程构建：worker 后续只做 LLM 调用，
       与请求线程共享的 csilk 单连接 SQLite 池完全解耦。 */
    csilk_json_t* hist = mf_ai_message_recent(pool, ctx->session_id, SUMMARY_HISTORY_LIMIT);
    char*         prev = mf_ai_summary_get(pool, ctx->session_id);
    char*         transcript = build_history_transcript(hist);
    if (hist) {
        csilk_json_free(hist);
    }
    if (!transcript) {
        free(prev);
        inflight_release(idx);
        return;
    }
    size_t prompt_cap = strlen(transcript) + (prev && prev[0] ? strlen(prev) : 0) + 512;
    char*  user_prompt = (char*)malloc(prompt_cap);
    if (user_prompt) {
        if (prev && prev[0]) {
            snprintf(user_prompt,
                     prompt_cap,
                     "以下是历史对话摘要：\n%s\n\n以下是最新的对话记录：\n%s\n\n"
                     "请综合以上信息，生成一份简洁连贯的整体对话摘要，"
                     "保留关键财务事实、用户偏好与决策要点，不超过 500 字。",
                     prev,
                     transcript);
        } else {
            snprintf(user_prompt,
                     prompt_cap,
                     "以下是对话记录：\n%s\n\n"
                     "请生成一份简洁连贯的对话摘要，保留关键财务事实、用户偏好与决策要点，不超过 "
                     "500 字。",
                     transcript);
        }
    }
    free(transcript);
    free(prev);
    if (!user_prompt) {
        inflight_release(idx);
        return;
    }

    summary_task_t* task = (summary_task_t*)calloc(1, sizeof(summary_task_t));
    if (!task) {
        free(user_prompt);
        inflight_release(idx);
        return;
    }
    task->user_id = ctx->user_id;
    task->session_id = ctx->session_id;
    task->inflight_idx = idx;
    task->user_prompt = user_prompt;

    /* 快照 provider 配置到 task：g_config 此刻由调用者（主线程）保证有效，
     * worker 只读本地副本，彻底消除 detached 线程对 g_config 的生命周期耦合。 */
    {
        ai_config_t*   cfg = ai_get_config();
        ai_provider_t* prov = NULL;
        if (cfg) {
            prov = ai_config_find_provider(cfg, ctx->provider_id[0] ? ctx->provider_id : NULL);
            if (!prov) {
                prov = ai_config_default_provider(cfg);
            }
        }
        if (prov && (prov->api_key[0] || strcmp(prov->id, "ollama") == 0)) {
            task->has_key = 1;
            snprintf(task->dname,
                     sizeof(task->dname),
                     "%s",
                     strcmp(prov->id, "ollama") == 0 ? "ollama" : "openai");
            snprintf(
                task->api_key, sizeof(task->api_key), "%s", prov->api_key[0] ? prov->api_key : "");
            snprintf(task->base_url,
                     sizeof(task->base_url),
                     "%s",
                     prov->base_url[0] ? prov->base_url : "");
            snprintf(task->model,
                     sizeof(task->model),
                     "%s",
                     ctx->model_name[0]                        ? ctx->model_name
                     : (prov->models[0] && prov->models[0][0]) ? prov->models[0]
                                                               : "gpt-4o-mini");
        }
    }
    if (!task->has_key) {
        free(task->user_prompt);
        free(task);
        inflight_release(idx);
        return;
    }

    pthread_mutex_lock(&s_pool.lock);
    if (!s_pool.initialized || s_pool.shutdown || s_pool.count >= SUMMARY_QUEUE_CAP) {
        pthread_mutex_unlock(&s_pool.lock);
        free(task->user_prompt);
        free(task);
        inflight_release(idx);
        return;
    }
    s_pool.queue[s_pool.tail] = task;
    s_pool.tail = (s_pool.tail + 1) % SUMMARY_QUEUE_CAP;
    s_pool.count++;
    pthread_cond_signal(&s_pool.not_empty);
    pthread_mutex_unlock(&s_pool.lock);
}
