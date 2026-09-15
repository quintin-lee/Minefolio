#include "services/ai/memory/summary.h"
#include "services/ai/memory/memory.h"
#include "services/ai/runtime/context.h"
#include "common/db.h"
#include "common/config.h"
#include "services/ai_service.h"
#include "infrastructure/repositories/ai_session_repo_impl.h"
#include "infrastructure/repositories/ai_settings_repo_impl.h"
#include "csilk/csilk.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MOCK_BASE_URL "http://127.0.0.1:18081/v1"

/* 初始化临时 SQLite 库：建 ai_sessions/ai_messages/ai_session_summaries + users。 */
static csilk_db_pool_t*
init_test_db(const char* db_path)
{
    unlink(db_path);
    setenv("MINEFOLIO_DB_DRIVER", "sqlite", 1);
    setenv("MINEFOLIO_DB_DSN", db_path, 1);
    setenv("MINEFOLIO_JWT_SECRET", "test_ai_summary_jwt_secret_32bytes_len", 1);

    csilk_db_pool_t* pool = NULL;
    assert(db_init(&pool) == 0);
    assert(pool != NULL);

    FILE* f = fopen("sql/migration.sql", "r");
    if (!f) {
        f = fopen("../sql/migration.sql", "r");
    }
    if (!f) {
        f = fopen("backend/sql/migration.sql", "r");
    }
    assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* sql = (char*)malloc((size_t)sz + 1);
    assert(sql != NULL);
    assert(fread(sql, 1, (size_t)sz, f) == (size_t)sz);
    sql[sz] = '\0';
    fclose(f);
    assert(csilk_db_exec(pool, sql) == 0);
    free(sql);

    /* V010: ai_session_summaries */
    FILE* f2 = fopen("sql/migrations/sqlite/V010__ai_session_summaries.sql", "r");
    if (!f2) {
        f2 = fopen("../sql/migrations/sqlite/V010__ai_session_summaries.sql", "r");
    }
    if (!f2) {
        f2 = fopen("backend/sql/migrations/sqlite/V010__ai_session_summaries.sql", "r");
    }
    assert(f2 != NULL);
    fseek(f2, 0, SEEK_END);
    long sz2 = ftell(f2);
    fseek(f2, 0, SEEK_SET);
    char* sql2 = (char*)malloc((size_t)sz2 + 1);
    assert(sql2 != NULL);
    assert(fread(sql2, 1, (size_t)sz2, f2) == (size_t)sz2);
    sql2[sz2] = '\0';
    fclose(f2);
    assert(csilk_db_exec(pool, sql2) == 0);
    free(sql2);

    /* 用户 + 会话 */
    assert(csilk_db_exec(
               pool,
               "INSERT INTO users (id, username, password) VALUES (1, 'tester', 'hash123')")
           == 0);
    assert(csilk_db_exec(pool,
                         "INSERT INTO ai_sessions (id, user_id, title, model, provider) "
                         "VALUES (1, 1, '测试会话', 'mock-model', 'mockai')")
           == 0);
    assert(csilk_db_exec(pool,
                         "INSERT INTO ai_messages (id, session_id, role, content, model) "
                         "VALUES (1, 1, 'user', '我想了解稳健型理财建议', 'mock-model')")
           == 0);

    /* 把 mock provider 配置存进 ai_settings，ai_init 会读它填充 g_config。 */
    const char* cfg_json =
        "{\"providers\":[{\"id\":\"mockai\",\"name\":\"Mock\",\"api_key\":\"dummy\","
        "\"base_url\":\"" MOCK_BASE_URL "\",\"models\":[\"mock-model\"]}],"
        "\"default_provider\":\"mockai\",\"default_model\":\"mock-model\","
        "\"context_size\":20}";
    assert(mf_ai_settings_repo_save(pool, cfg_json) == 0);

    return pool;
}

/* 检测 127.0.0.1:18081 是否已被外部 mock 占用（可连即复用） */
static int
mock_port_busy(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return 0;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(18081);
    int connected = (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0);
    close(fd);
    return connected;
}

int
main(void)
{
    printf("=== Running E2E Test: AI Summary Pipeline (non-streaming mock) ===\n");

    /* 自包含：启动本地 mock summary server（若外部已有则复用），测试结束清理。 */
    int external_mock = mock_port_busy();
    pid_t spawned_pid = 0;
    if (!external_mock) {
        spawned_pid = fork();
        if (spawned_pid == 0) {
            /* 子进程：启动 node mock server（绝对路径，避免 ctest CWD 差异） */
            const char* mock_js = "../tests/mock_summary_server.js";
            execlp("node", "node", mock_js, (char*)NULL);
            _exit(127);
        }
        /* 等待 mock 监听，最多 ~5s */
        for (int i = 0; i < 50; i++) {
            usleep(100000);
            if (mock_port_busy()) {
                break;
            }
        }
        if (!mock_port_busy()) {
            kill(spawned_pid, SIGTERM);
            waitpid(spawned_pid, NULL, 0);
            fprintf(stderr,
                    "FATAL: failed to start mock summary server at 127.0.0.1:18081\n");
            return 2;
        }
    }

    const char* db_file = "/tmp/test_ai_summary.db";
    csilk_db_pool_t* pool = init_test_db(db_file);

    /* 初始化 AI 全局配置（从 ai_settings 表读 mockai provider） */
    ai_init(pool);
    ai_config_t* cfg = ai_get_config();
    assert(cfg != NULL);
    ai_provider_t* prov = ai_config_find_provider(cfg, "mockai");
    assert(prov != NULL);
    printf("mock provider ready: base_url=%s model=%s\n", prov->base_url, prov->models[0]);

    /* 构造 ctx：token 预算 100，已用 90 → 超过 80% 阈值，应触发摘要 */
    ai_runtime_context_t ctx = {0};
    ai_runtime_context_init(&ctx);
    ctx.user_id = 1;
    ctx.session_id = 1;
    snprintf(ctx.provider_id, sizeof(ctx.provider_id), "%s", "mockai");
    snprintf(ctx.model_name, sizeof(ctx.model_name), "%s", "mock-model");
    ctx.limits.token_budget = 100;
    ctx.stats.total_tokens = 90;

    ai_summary_maybe_trigger(pool, &ctx);

    /* detached 线程不可 join → 轮询 ai_session_summaries，最多 15s */
    char* summary = NULL;
    for (int i = 0; i < 150; i++) {
        summary = mf_ai_summary_get(pool, 1);
        if (summary && summary[0]) {
            break;
        }
        free(summary);
        summary = NULL;
        usleep(100000);
    }

    assert(summary != NULL);
    assert(strstr(summary, "对话摘要") != NULL);
    printf("PASS: summary persisted by detached worker -> \"%s\"\n", summary);
    free(summary);

    /* 验证摘要能被 ai_memory_build_messages 注入（第二条 system 消息） */
    csilk_json_t* hist = mf_ai_message_recent(pool, 1, 20);
    assert(hist != NULL);
    csilk_json_t* msgs = ai_memory_build_messages(
        "System Prompt", hist, "新一轮输入", 4, "对话摘要：用户偏好稳健型理财");
    assert(msgs != NULL);
    size_t mcount = csilk_json_array_size(msgs);
    assert(mcount >= 2);
    csilk_json_t* second = csilk_json_array_get(msgs, 1);
    assert(second != NULL);
    assert(strcmp(csilk_json_get_string(second, "role"), "system") == 0);
    assert(strstr(csilk_json_get_string(second, "content"), "对话摘要") != NULL);
    printf("PASS: summary injected as 2nd system message (total %zu msgs)\n", mcount);
    csilk_json_free(msgs);
    if (hist) {
        csilk_json_free(hist);
    }

    ai_runtime_context_free(&ctx);
    ai_shutdown();
    unlink(db_file);

    /* 清理本测试自行启动的 mock server（外部复用的不动） */
    if (!external_mock && spawned_pid > 0) {
        kill(spawned_pid, SIGTERM);
        waitpid(spawned_pid, NULL, 0);
    }

    printf("ALL SUMMARY PIPELINE TESTS PASSED\n");
    return 0;
}
