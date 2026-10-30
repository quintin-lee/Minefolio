#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "services/ai/memory/summary.h"

int main(void) {
    printf("Running test_summary_worker_pool...\n");
    /* 1. Test idempotent initialization and shutdown */
    ai_summary_pool_init();
    ai_summary_pool_init(); /* duplicate no-op */

    /* 2. Enqueue 20 dummy test tasks and verify queue processing */
    for (int i = 1; i <= 20; i++) {
        int rc = ai_summary_pool_enqueue_test_task((int64_t)i, (int64_t)(100 + i), "Test prompt");
        assert(rc == 0);
    }

    /* Wait for 2 persistent workers to drain queue */
    int retries = 50;
    while (retries-- > 0 && ai_summary_pool_queue_count() > 0) {
        usleep(10000); /* 10ms */
    }
    assert(ai_summary_pool_queue_count() == 0);

    /* 3. Verify LRU replacement when all 64 slots are full */
    for (int i = 1; i <= 65; i++) {
        ai_summary_pool_enqueue_test_task((int64_t)(1000 + i), (int64_t)i, "LRU test prompt");
    }
    retries = 50;
    while (retries-- > 0 && ai_summary_pool_queue_count() > 0) {
        usleep(10000);
    }
    /* Slots capped at 64 */
    assert(ai_summary_pending_slots_count() == 64);

    /* 4. Shutdown and verify queue draining and slot clearing */
    ai_summary_pool_shutdown();
    assert(ai_summary_pending_slots_count() == 0);
    ai_summary_pool_shutdown(); /* duplicate no-op */

    /* 5. Re-init to verify clean state reset */
    ai_summary_pool_init();
    ai_summary_pool_shutdown();

    printf("PASS: test_summary_worker_pool completed successfully\n");
    return 0;
}
