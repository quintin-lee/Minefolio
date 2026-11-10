#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include "application/ai/config_provider.h"

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
    test_lifecycle_and_scope();
    test_concurrent_acquire_release();
    test_rcu_hot_reload_isolation();
    printf("ALL AI CONFIG PROVIDER TESTS PASSED!\n");
    return 0;
}
