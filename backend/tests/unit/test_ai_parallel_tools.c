#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "services/ai/tools/registry.h"
#include "services/ai/runtime/loop.h"

static char* mock_sleep_200ms(const ai_tool_t* t, const ai_tool_context_t* ctx, const csilk_json_t* args) {
    (void)t; (void)ctx; (void)args;
    usleep(200000); /* 200ms sleep */
    return strdup("{\"status\":\"slept_200ms\"}");
}

int main(void) {
    printf("Running test_ai_parallel_tools...\n");
    ai_tool_registry_init();

    /* Register mock sleep tool override for web_search */
    ai_tool_t t_search = {
        .name = "web_search",
        .permission = AI_PERM_READ,
        .is_mutation = false,
        .execute = mock_sleep_200ms
    };
    ai_tool_register(&t_search);

    /* 1. Test stateless classification */
    assert(is_stateless_network_tool("web_search") == true);
    assert(is_stateless_network_tool("fetch_webpage") == true);
    assert(is_stateless_network_tool("get_assets") == false);
    assert(is_stateless_network_tool("mcp:1:tool") == false);

    /* 2. Test batch eligibility */
    const char* valid_batch[] = {"web_search", "fetch_webpage"};
    assert(ai_parallel_batch_eligible(valid_batch, 2) == true);
    const char* mixed_batch[] = {"web_search", "get_assets"};
    assert(ai_parallel_batch_eligible(mixed_batch, 2) == false);

    /* 3. Benchmark parallel execution of 2 stateless calls (both 200ms) */
    struct timespec start_par, end_par;
    clock_gettime(CLOCK_MONOTONIC, &start_par);

    csilk_json_t* a1 = csilk_json_object();
    csilk_json_t* a2 = csilk_json_object();
    char* r1 = NULL;
    char* r2 = NULL;

    int rc_par = ai_tools_execute_parallel_batch(1, 1, "web_search", a1, "web_search", a2, &r1, &r2);
    clock_gettime(CLOCK_MONOTONIC, &end_par);
    long elapsed_par_ms = (end_par.tv_sec - start_par.tv_sec) * 1000 + (end_par.tv_nsec - start_par.tv_nsec) / 1000000;

    assert(rc_par == 0);
    assert(r1 != NULL && r2 != NULL);
    assert(elapsed_par_ms < 350); /* Parallel execution takes ~200ms, well below serial 400ms */

    free(r1);
    free(r2);

    /* 4. Benchmark fallback sequential execution of mixed batch (1 web_search + 1 get_assets) */
    struct timespec start_seq, end_seq;
    clock_gettime(CLOCK_MONOTONIC, &start_seq);

    char* r_mix1 = NULL;
    char* r_mix2 = NULL;
    int rc_seq = ai_tools_execute_parallel_batch(1, 1, "web_search", a1, "get_assets", a2, &r_mix1, &r_mix2);
    clock_gettime(CLOCK_MONOTONIC, &end_seq);
    long elapsed_seq_ms = (end_seq.tv_sec - start_seq.tv_sec) * 1000 + (end_seq.tv_nsec - start_seq.tv_nsec) / 1000000;

    assert(rc_seq == 0);
    assert(r_mix1 != NULL);
    /* Ineligible batch executes sequentially */
    assert(elapsed_seq_ms >= 180);

    free(r_mix1);
    if (r_mix2) free(r_mix2);
    csilk_json_free(a1);
    csilk_json_free(a2);

    printf("PASS: test_ai_parallel_tools completed successfully\n");
    return 0;
}
