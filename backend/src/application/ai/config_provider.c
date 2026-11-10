#include "application/ai/config_provider.h"
#include "infrastructure/repositories/ai_settings_repo_impl.h"
#include "config/secret.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdatomic.h>
#include <pthread.h>

/**
 * 不可变 AI 配置快照 + 引用计数（RCU 风格）：
 * 写者（init/reload）构建新快照后原子交换 g_snapshot；
 * 读者通过 ai_config_acquire()/ai_config_release() 持有引用，
 * 保证流式会话与各用例执行期间 config 与预建 csilk_ai_t 实例不会被提前释放。
 */
typedef struct ai_config_snapshot_s {
    ai_config_t config;      /* 深拷贝，所有权归快照独占 */
    csilk_ai_t* ai_instance; /* 预建实例（可能为 NULL） */
    atomic_int  refcount;    /* 写者初始持 1；读者 acquire 后递增 */
} ai_config_snapshot_t;

static ai_config_snapshot_t* volatile g_snapshot = NULL;
static pthread_mutex_t g_snapshot_lock = PTHREAD_MUTEX_INITIALIZER;

static const char*
get_driver_name(const char* prov_id)
{
    if (prov_id && strcmp(prov_id, "ollama") == 0) {
        return "ollama";
    }
    return "openai";
}

static ai_config_snapshot_t*
make_snapshot(ai_config_t* config_src, csilk_ai_t* instance)
{
    ai_config_snapshot_t* s = calloc(1, sizeof(*s));
    if (!s) {
        return NULL;
    }
    s->config = *config_src;      /* 结构体浅拷贝标量；内部堆内存归快照独占 */
    s->ai_instance = instance;
    atomic_init(&s->refcount, 1); /* 写者持有一份 */
    return s;
}

static void
snapshot_writer_release(ai_config_snapshot_t* s)
{
    if (!s) {
        return;
    }
    int old = atomic_fetch_sub(&s->refcount, 1);
    if (old == 1) {
        ai_config_free(&s->config);
        if (s->ai_instance) {
            csilk_ai_free(s->ai_instance);
        }
        free(s);
    }
}

ai_config_t*
ai_config_acquire(void)
{
    pthread_mutex_lock(&g_snapshot_lock);
    ai_config_snapshot_t* s = g_snapshot;
    if (s) {
        atomic_fetch_add(&s->refcount, 1);
    }
    pthread_mutex_unlock(&g_snapshot_lock);
    return s ? &s->config : NULL;
}

void
ai_config_release(ai_config_t* cfg)
{
    if (!cfg) {
        return;
    }
    ai_config_snapshot_t* s =
        (ai_config_snapshot_t*)((char*)cfg - offsetof(ai_config_snapshot_t, config));
    int old = atomic_fetch_sub(&s->refcount, 1);
    if (old == 1) {
        ai_config_free(&s->config);
        if (s->ai_instance) {
            csilk_ai_free(s->ai_instance);
        }
        free(s);
    }
}

csilk_ai_t*
ai_config_get_ai_instance(const ai_config_t* cfg)
{
    if (!cfg) {
        return NULL;
    }
    const ai_config_snapshot_t* s =
        (const ai_config_snapshot_t*)((const char*)cfg - offsetof(ai_config_snapshot_t, config));
    return s->ai_instance;
}

int
ai_config_provider_reload(void* pool)
{
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    ai_config_t      cfg_copy = {0};
    char*            json = db_pool ? mf_ai_settings_repo_load(db_pool) : NULL;
    int              loaded_from_db = 0;

    if (json) {
        loaded_from_db = (ai_config_load_json(json, &cfg_copy) == 0);
        free(json);
    }
    if (!loaded_from_db) {
        const char* cfg_path = config_env_get("AI_CONFIG", NULL, 0, "config/ai.json");
        if (ai_config_load(cfg_path, &cfg_copy) != 0) {
            fprintf(stderr,
                    "WARN: could not load ai_config from %s; keeping prior snapshot\n",
                    cfg_path);
            return -1;
        }
    }

    ai_provider_t* prov = ai_config_default_provider(&cfg_copy);
    csilk_ai_t*    new_inst = NULL;
    if (prov) {
        char        api_key_buf[512];
        const char* sec_key = config_secret_get("AI_API_KEY", api_key_buf, sizeof(api_key_buf));
        const char* key = (sec_key && sec_key[0])      ? sec_key
                          : (prov->api_key[0] != '\0') ? prov->api_key
                                                       : "";
        const char* dname = get_driver_name(prov->id);
        new_inst = csilk_ai_new(dname, key, prov->base_url[0] ? prov->base_url : NULL);
        if (!new_inst) {
            fprintf(stderr, "WARN: failed to create AI instance for provider '%s'\n", prov->id);
        }
    } else {
        fprintf(stderr,
                "WARN: default provider '%s' not found; AI remains unconfigured\n",
                cfg_copy.default_provider);
    }

    pthread_mutex_lock(&g_snapshot_lock);
    ai_config_snapshot_t* old_snap = g_snapshot;
    ai_config_snapshot_t* new_snap = make_snapshot(&cfg_copy, new_inst);
    if (new_snap) {
        g_snapshot = new_snap;
        if (old_snap) {
            snapshot_writer_release(old_snap);
        }
    } else {
        ai_config_free(&cfg_copy);
        if (new_inst) {
            csilk_ai_free(new_inst);
        }
    }
    pthread_mutex_unlock(&g_snapshot_lock);

    return new_snap ? 0 : -1;
}

void
ai_config_provider_init(void* pool)
{
    ai_config_provider_reload(pool);
}

void
ai_config_provider_shutdown(void)
{
    pthread_mutex_lock(&g_snapshot_lock);
    ai_config_snapshot_t* old_snap = g_snapshot;
    g_snapshot = NULL;
    pthread_mutex_unlock(&g_snapshot_lock);

    if (old_snap) {
        snapshot_writer_release(old_snap);
    }
}
