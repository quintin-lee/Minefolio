#include "services/ai_trace_service.h"
#include "repositories/ai_trace_repo.h"
#include "common/db.h"
#include "common/response.h"
#include "common/ctx.h"
#include "csilk/core/server.h"

void ai_trace_service_list(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;
    int64_t page = 1, page_size = 20;
    parse_page_params(c, &page, &page_size);
    const char* provider = csilk_get_query(c, "provider");
    const char* model = csilk_get_query(c, "model");
    int64_t total = 0;
    CSILK_LOG_I("ai_trace_service_list: user_id=%lld page=%lld page_size=%lld provider='%s' model='%s'",
        (long long)user_id, (long long)page, (long long)page_size,
        provider ? provider : "", model ? model : "");
    csilk_json_t* list = ai_trace_list(db_get_pool(), user_id, page, page_size,
                                        provider, model, &total);
    if (!list) { respond_error(c, 500, "查询失败"); return; }
    respond_page_ok(c, list, total, page, page_size);
}

void ai_trace_service_get(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;
    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }
    int64_t id = atoll(id_str);
    CSILK_LOG_I("ai_trace_service_get: user_id=%lld id=%lld", (long long)user_id, (long long)id);
    csilk_json_t* r = ai_trace_get(db_get_pool(), user_id, id);
    if (!r || csilk_json_array_size(r) == 0) {
        csilk_json_free(r);
        respond_not_found(c);
        return;
    }
    csilk_json_t* row = csilk_json_array_get(r, 0);
    csilk_json_t* out = csilk_json_object();
    csilk_json_add_number(out, "id", db_get_num(row, "id"));
    csilk_json_add_number(out, "user_id", db_get_num(row, "user_id"));
    csilk_json_add_number(out, "session_id", db_get_num(row, "session_id"));
    csilk_json_add_string(out, "provider", csilk_json_get_string(row, "provider"));
    csilk_json_add_string(out, "model", csilk_json_get_string(row, "model"));
    csilk_json_add_string(out, "input_messages", csilk_json_get_string(row, "input_messages"));
    csilk_json_add_string(out, "output_content", csilk_json_get_string(row, "output_content"));
    csilk_json_add_string(out, "system_prompt", csilk_json_get_string(row, "system_prompt"));
    csilk_json_add_number(out, "prompt_tokens", db_get_num(row, "prompt_tokens"));
    csilk_json_add_number(out, "completion_tokens", db_get_num(row, "completion_tokens"));
    csilk_json_add_number(out, "total_tokens", db_get_num(row, "total_tokens"));
    csilk_json_add_number(out, "latency_ms", db_get_num(row, "latency_ms"));
    csilk_json_add_number(out, "first_token_ms", db_get_num(row, "first_token_ms"));
    csilk_json_add_number(out, "tokens_per_sec", db_get_num(row, "tokens_per_sec"));
    csilk_json_add_number(out, "cost_usd", db_get_num(row, "cost_usd"));
    csilk_json_add_number(out, "temperature", db_get_num(row, "temperature"));
    csilk_json_add_number(out, "max_tokens", db_get_num(row, "max_tokens"));
    csilk_json_add_number(out, "top_p", db_get_num(row, "top_p"));
    csilk_json_add_string(out, "status", csilk_json_get_string(row, "status"));
    csilk_json_add_string(out, "error_message", csilk_json_get_string(row, "error_message"));
    csilk_json_add_string(out, "metadata", csilk_json_get_string(row, "metadata"));
    csilk_json_add_string(out, "created_at", csilk_json_get_string(row, "created_at"));
    csilk_json_free(r);
    respond_ok(c, out);
}

void ai_trace_service_stats(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;
    CSILK_LOG_I("ai_trace_service_stats: user_id=%lld", (long long)user_id);
    csilk_json_t* r = ai_trace_stats(db_get_pool(), user_id);
    if (!r) { respond_error(c, 500, "查询失败"); return; }
    csilk_json_t* stats = csilk_json_array_get(r, 0);
    csilk_json_t* out = csilk_json_object();
    csilk_json_add_number(out, "total_traces", db_get_int(stats, "total_traces"));
    csilk_json_add_number(out, "total_tokens", db_get_int(stats, "total_tokens"));
    csilk_json_add_number(out, "avg_latency_ms", db_get_num(stats, "avg_latency_ms"));
    csilk_json_add_number(out, "avg_first_token_ms", db_get_num(stats, "avg_first_token_ms"));
    csilk_json_add_number(out, "avg_tokens_per_sec", db_get_num(stats, "avg_tokens_per_sec"));
    csilk_json_add_number(out, "total_cost_usd", db_get_num(stats, "total_cost_usd"));
    csilk_json_free(r);
    respond_ok(c, out);
}
