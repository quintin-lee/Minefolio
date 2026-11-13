#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include "application/ai/config_provider.h"
#include "config/secret.h"

/**
 * @brief 测试用最小 AI 配置（写在临时文件里）
 *
 * ai_config_provider_init(NULL) 会先查 DB（此处传 NULL，无库），再回退到
 * 环境变量 AI_CONFIG 指定的路径，最后才用 "config/ai.json" 这个相对路径。
 * config/ai.json 被 .gitignore 排除，CI 干净克隆里没有它；且 ctest 从构建
 * 目录运行时相对路径也不成立。所以测试必须自带配置，不能依赖仓库里的
 * config/ 目录（copy_assets 也复制不出未被跟踪的文件）。
 */
static const char* TEST_AI_CONFIG_JSON =
    "{\"providers\":[{\"id\":\"openai\",\"name\":\"Test OpenAI\",\"api_key\":\"\","
    "\"base_url\":\"\",\"models\":[\"gpt-test\"]}],\"default_provider\":\"openai\","
    "\"default_model\":\"gpt-test\",\"context_size\":20,"
    "\"system_prompt\":\"test system prompt\"}";

static char s_cfg_path[256];

/** @brief 写出临时配置文件并让 AI_CONFIG 指向它；返回 0 成功 */
static int
setup_temp_config(void)
{
    const char* tmpdir = getenv("TMPDIR");
    if (!tmpdir || !tmpdir[0]) {
        tmpdir = "/tmp";
    }
    snprintf(s_cfg_path, sizeof(s_cfg_path), "%s/mf_test_ai_config_%d.json", tmpdir, (int)getpid());

    FILE* f = fopen(s_cfg_path, "w");
    if (!f) {
        return -1;
    }
    size_t n = strlen(TEST_AI_CONFIG_JSON);
    int    ok = (fwrite(TEST_AI_CONFIG_JSON, 1, n, f) == n);
    ok = (fclose(f) == 0) && ok;
    if (!ok) {
        unlink(s_cfg_path);
        return -1;
    }

    /* config_env_get("AI_CONFIG", ...) 会先查 Secret Provider，再查环境变量，
     * 因此覆盖点必须落在 Secret Provider 上（test override 优先级最高）。 */
    config_secret_set_test_override("AI_CONFIG", s_cfg_path);
    return 0;
}

static void
teardown_temp_config(void)
{
    config_secret_set_test_override("AI_CONFIG", NULL);
    if (s_cfg_path[0]) {
        unlink(s_cfg_path);
        s_cfg_path[0] = '\0';
    }
}

static void test_lifecycle_and_scope(void) {
    ai_config_provider_init(NULL);

    ai_config_t* cfg = ai_config_acquire();
    assert(cfg != NULL);
    assert(cfg->default_provider != NULL);
    ai_config_release(cfg);

    int scope_entered = 0;
    AI_CONFIG_SCOPE(s_cfg) {
        assert(s_cfg != NULL);
        scope_entered = 1;
    }
    assert(scope_entered == 1);

    ai_config_provider_shutdown();
    assert(ai_config_acquire() == NULL);

    printf("PASS: test_lifecycle_and_scope\n");
}

#define NUM_THREADS 8
#define ITERS_PER_THREAD 2000

static void* reader_worker(void* arg) {
    (void)arg;
    for (int i = 0; i < ITERS_PER_THREAD; i++) {
        ai_config_t* cfg = ai_config_acquire();
        if (cfg) {
            assert(cfg->default_provider != NULL);
            ai_config_release(cfg);
        }
    }
    return NULL;
}

static void test_concurrent_acquire_release(void) {
    ai_config_provider_init(NULL);

    pthread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        assert(pthread_create(&threads[i], NULL, reader_worker, NULL) == 0);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    ai_config_provider_shutdown();
    printf("PASS: test_concurrent_acquire_release (8 threads x 2000 iters)\n");
}

typedef struct {
    pthread_barrier_t barrier;
    volatile int hold_done;
} rcu_sync_t;

static void* rcu_reader_hold(void* arg) {
    rcu_sync_t* sync = (rcu_sync_t*)arg;

    ai_config_t* cfg = ai_config_acquire();
    assert(cfg != NULL);
    char original_provider[128];
    snprintf(original_provider, sizeof(original_provider), "%s", cfg->default_provider);

    /* 同步点 1：通知主线程已持有旧快照 */
    pthread_barrier_wait(&sync->barrier);

    /* 等待主线程执行 reload */
    while (!sync->hold_done) {
        usleep(1000);
    }

    /* 旧快照内容必须依然有效且未被释放 */
    assert(strcmp(cfg->default_provider, original_provider) == 0);

    /* 释放旧快照引用 */
    ai_config_release(cfg);
    return NULL;
}

static void test_rcu_hot_reload_isolation(void) {
    ai_config_provider_init(NULL);

    rcu_sync_t sync;
    pthread_barrier_init(&sync.barrier, NULL, 2);
    sync.hold_done = 0;

    pthread_t reader_tid;
    assert(pthread_create(&reader_tid, NULL, rcu_reader_hold, &sync) == 0);

    /* 等待读取线程成功拿到旧快照 */
    pthread_barrier_wait(&sync.barrier);

    /* 主线程执行热重载（原子替换新快照） */
    int rc = ai_config_provider_reload(NULL);
    assert(rc == 0);

    /* 新读者应拿到新快照 */
    ai_config_t* new_cfg = ai_config_acquire();
    assert(new_cfg != NULL);
    ai_config_release(new_cfg);

    /* 允许持有旧快照的线程检查并退出 */
    sync.hold_done = 1;
    pthread_join(reader_tid, NULL);
    pthread_barrier_destroy(&sync.barrier);

    ai_config_provider_shutdown();
    printf("PASS: test_rcu_hot_reload_isolation\n");
}

int main(void) {
    printf("Running AI config provider unit tests...\n");
    /* config 必须由测试自己提供：config/ai.json 未被 git 跟踪（.gitignore
     * 第 6 行），CI 干净克隆里不存在；ctest 也从构建目录运行，相对路径不成立。 */
    if (setup_temp_config() != 0) {
        fprintf(stderr, "FAIL: could not create temporary AI config in %s\n",
                (getenv("TMPDIR") && getenv("TMPDIR")[0]) ? getenv("TMPDIR") : "/tmp");
        return 1;
    }

    test_lifecycle_and_scope();
    test_concurrent_acquire_release();
    test_rcu_hot_reload_isolation();

    teardown_temp_config();
    printf("ALL AI CONFIG PROVIDER TESTS PASSED!\n");
    return 0;
}
