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

typedef struct {
    int64_t          user_id;
    int64_t          session_id;
    char             provider_id[64];
    char             model_name[128];
    csilk_db_pool_t* pool;
    size_t           inflight_idx;
} summary_task_t;

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
    csilk_db_pool_t* pool = task->pool;

    csilk_json_t* hist = mf_ai_message_recent(pool, task->session_id, SUMMARY_HISTORY_LIMIT);
    char*         prev = mf_ai_summary_get(pool, task->session_id);
    char*         transcript = build_history_transcript(hist);

    if (!transcript) {
        free(prev);
        if (hist) {
            csilk_json_free(hist);
        }
        free(task);
        return;
    }

    size_t prompt_cap = strlen(transcript) + (prev && prev[0] ? strlen(prev) : 0) + 512;
    char*  user_prompt = (char*)malloc(prompt_cap);
    if (!user_prompt) {
        free(transcript);
        free(prev);
        if (hist) {
            csilk_json_free(hist);
        }
        free(task);
        return;
    }
    if (prev && prev[0]) {
        snprintf(user_prompt,
                 prompt_cap,
                 "以下是历史对话摘要：\n%s\n\n以下是最新的对话记录：\n%s\n\n"
                 "请综合以上信息，生成一份简洁连贯的整体对话摘要，"
                 "保留关键财务事实、用户偏好与决策要点，不超过 500 字。",
                 prev,
                 transcript);
    } else {
        snprintf(
            user_prompt,
            prompt_cap,
            "以下是对话记录：\n%s\n\n"
            "请生成一份简洁连贯的对话摘要，保留关键财务事实、用户偏好与决策要点，不超过 500 字。",
            transcript);
    }

    ai_config_t*   cfg = ai_get_config();
    ai_provider_t* prov = NULL;
    if (cfg) {
        prov = ai_config_find_provider(cfg, task->provider_id[0] ? task->provider_id : NULL);
        if (!prov) {
            prov = ai_config_default_provider(cfg);
        }
    }
    if (!prov || (!prov->api_key[0] && strcmp(prov->id, "ollama") != 0)) {
        free(user_prompt);
        free(transcript);
        free(prev);
        if (hist) {
            csilk_json_free(hist);
        }
        free(task);
        return;
    }

    const char* dname = (strcmp(prov->id, "ollama") == 0) ? "ollama" : "openai";
    const char* key = (prov->api_key[0] != '\0') ? prov->api_key : "dummy";
    const char* model = task->model_name[0]                       ? task->model_name
                        : (prov->models[0] && prov->models[0][0]) ? prov->models[0]
                                                                  : "gpt-4o-mini";

    csilk_ai_t* inst = csilk_ai_new(dname, key, prov->base_url[0] ? prov->base_url : NULL);
    if (!inst) {
        free(user_prompt);
        free(transcript);
        free(prev);
        if (hist) {
            csilk_json_free(hist);
        }
        free(task);
        return;
    }

    csilk_ai_message_t messages[2] = {
        {.role = "system", .content = "你是对话摘要助手，负责将长对话浓缩为简洁摘要。"},
        {.role = "user",   .content = user_prompt                                     },
    };
    csilk_ai_chat_request_t req = {
        .model = model,
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
        mf_ai_summary_upsert(
            pool, task->session_id, task->user_id, res.content, res.completion_tokens);
    } else if (res.error_message && res.error_message[0]) {
        fprintf(stderr,
                "[ai-summary] session=%ld summary failed: %s\n",
                (long)task->session_id,
                res.error_message);
    }

    csilk_ai_chat_response_free(&res);
    csilk_ai_free(inst);
    free(user_prompt);
    free(transcript);
    free(prev);
    if (hist) {
        csilk_json_free(hist);
    }
    /* 释放 in-flight 槽位后再销毁 task */
    inflight_release(task->inflight_idx);
    free(task);
}

static void*
summary_worker_entry(void* arg)
{
    summary_worker((summary_task_t*)arg);
    return NULL;
}

void
ai_summary_maybe_trigger(csilk_db_pool_t* pool, ai_runtime_context_t* ctx)
{
    if (!pool || !ctx || ctx->session_id <= 0 || ctx->limits.token_budget <= 0) {
        return;
    }
    if (ctx->stats.total_tokens < (int)(ctx->limits.token_budget * SUMMARY_TRIGGER_RATIO)) {
        return;
    }

    int    found = 0;
    size_t idx = inflight_acquire(ctx->session_id, &found);
    if (found) {
        return;
    }

    summary_task_t* task = (summary_task_t*)calloc(1, sizeof(summary_task_t));
    if (!task) {
        inflight_release(idx);
        return;
    }
    task->user_id = ctx->user_id;
    task->session_id = ctx->session_id;
    snprintf(task->provider_id, sizeof(task->provider_id), "%s", ctx->provider_id);
    snprintf(task->model_name, sizeof(task->model_name), "%s", ctx->model_name);
    task->pool = pool;
    task->inflight_idx = idx;

    pthread_t tid;
    if (pthread_create(&tid, NULL, summary_worker_entry, task) != 0) {
        free(task);
        inflight_release(idx);
        return;
    }
    pthread_detach(tid);
}
