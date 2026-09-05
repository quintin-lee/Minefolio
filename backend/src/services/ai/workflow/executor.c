#include "services/ai/workflow/executor.h"
#include "services/ai_service.h"
#include "repositories/ai_session_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void
exec_get_datetime_str(char* out, size_t sz)
{
    time_t    now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    snprintf(out,
             sz,
             "%04d-%02d-%02d %02d:%02d:%02d",
             tm_buf.tm_year + 1900,
             tm_buf.tm_mon + 1,
             tm_buf.tm_mday,
             tm_buf.tm_hour,
             tm_buf.tm_min,
             tm_buf.tm_sec);
}

static void
exec_get_current_month_str(char* out, size_t sz)
{
    time_t    now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    snprintf(out, sz, "%04d-%02d", tm_buf.tm_year + 1900, tm_buf.tm_mon + 1);
}

int
ai_workflow_execute_stream(csilk_ctx_t* c, const ai_workflow_graph_t* graph, ai_wf_context_t* ctx)
{
    if (!c || !graph || !ctx || ctx->user_id <= 0) {
        return -1;
    }

    csilk_db_pool_t*    pool = ctx->pool;
    int64_t             user_id = ctx->user_id;
    int64_t             session_id = ctx->session_id;
    const csilk_json_t* params = ctx->params;

    if (session_id <= 0 && pool) {
        session_id = ai_session_insert(pool, user_id, graph->title, "workflow-agent", "system");
        ctx->session_id = session_id;
    } else if (session_id > 0 && pool) {
        csilk_json_t* sess = ai_session_get(pool, user_id, session_id);
        if (!sess) {
            return -1;
        }
        csilk_json_free(sess);
    }

    char exec_time_str[64];
    exec_get_datetime_str(exec_time_str, sizeof(exec_time_str));
    time_t exec_ts = time(NULL);

    csilk_sse_init(c);

    /* 1. workflow_start event */
    csilk_json_t* ev_start = csilk_json_object();
    csilk_json_add_string(ev_start, "workflow_id", graph->id);
    csilk_json_add_string(ev_start, "title", graph->title);
    csilk_json_add_number(ev_start, "total_steps", (double)graph->node_count);
    csilk_json_add_number(ev_start, "session_id", (double)session_id);
    csilk_json_add_string(ev_start, "execution_time", exec_time_str);
    csilk_json_add_number(ev_start, "execution_timestamp", (double)exec_ts);
    size_t slen = 0;
    char*  str_start = csilk_json_serialize(ev_start, &slen);
    csilk_json_free(ev_start);
    csilk_sse_send(c, "workflow_start", str_start ? str_start : "{}");
    free(str_start);

    /* Cumulative context across steps */
    csilk_json_t* ctx_obj = csilk_json_object();
    csilk_json_add_string(ctx_obj, "execution_time", exec_time_str);
    csilk_json_add_number(ctx_obj, "execution_timestamp", (double)exec_ts);
    {
        char cur_month_tmp[32];
        exec_get_current_month_str(cur_month_tmp, sizeof(cur_month_tmp));
        csilk_json_add_string(ctx_obj, "current_month", cur_month_tmp);
    }

    for (int i = 0; i < graph->node_count; i++) {
        const ai_workflow_node_t* node = &graph->nodes[i];

        char step_time_str[64];
        exec_get_datetime_str(step_time_str, sizeof(step_time_str));

        /* step_start event */
        csilk_json_t* ev_st_start = csilk_json_object();
        csilk_json_add_number(ev_st_start, "step_index", (double)i);
        csilk_json_add_string(ev_st_start, "step_id", node->node_id);
        csilk_json_add_string(ev_st_start, "title", node->title);
        csilk_json_add_string(ev_st_start, "step_time", step_time_str);
        csilk_json_add_number(ev_st_start, "step_timestamp", (double)time(NULL));
        char* str_st_start = csilk_json_serialize(ev_st_start, &slen);
        csilk_json_free(ev_st_start);
        csilk_sse_send(c, "step_start", str_st_start ? str_st_start : "{}");
        free(str_st_start);

        char summary_buf[256] = {0};

        if (i == graph->node_count - 1) {
            char* cur_ctx_str = csilk_json_serialize(ctx_obj, &slen);
            int   streamed_by_model =
                ai_service_stream_report(c, user_id, session_id, graph->title, cur_ctx_str);

            if (!streamed_by_model) {
                char* step_out = node->execute(pool, user_id, params, cur_ctx_str);
                if (step_out && step_out[0]) {
                    size_t rlen = strlen(step_out);
                    size_t offset = 0;
                    size_t chunk_sz = 64;
                    while (offset < rlen) {
                        size_t take = (offset + chunk_sz < rlen) ? chunk_sz : (rlen - offset);
                        char   chunk[128];
                        memcpy(chunk, step_out + offset, take);
                        chunk[take] = '\0';
                        offset += take;

                        csilk_json_t* ev_chunk = csilk_json_object();
                        csilk_json_add_string(ev_chunk, "content", chunk);
                        char* str_chunk = csilk_json_serialize(ev_chunk, &slen);
                        csilk_json_free(ev_chunk);
                        csilk_sse_send(c, "delta", str_chunk ? str_chunk : "{}");
                        free(str_chunk);
                    }

                    if (pool && session_id > 0) {
                        ai_message_insert(
                            pool, session_id, "assistant", step_out, "workflow-agent");
                    }
                    free(step_out);
                }
                snprintf(summary_buf, sizeof(summary_buf), "财务复盘与图表渲染完毕");
            } else {
                snprintf(summary_buf, sizeof(summary_buf), "AI 大模型已完成深度诊断与图表生成");
            }
            free(cur_ctx_str);
        } else {
            char* cur_ctx_str = csilk_json_serialize(ctx_obj, &slen);
            char* step_out = node->execute(pool, user_id, params, cur_ctx_str);
            free(cur_ctx_str);

            if (step_out) {
                csilk_json_t* parsed_out = csilk_json_parse(step_out);
                if (parsed_out) {
                    snprintf(summary_buf, sizeof(summary_buf), "完成数据计算与分析");
                    csilk_json_add_object(ctx_obj, node->node_id, parsed_out);
                }
                free(step_out);
            }
        }

        /* step_progress event */
        double        progress = (double)(i + 1) / (double)graph->node_count;
        csilk_json_t* ev_prog = csilk_json_object();
        csilk_json_add_number(ev_prog, "step_index", (double)i);
        csilk_json_add_number(ev_prog, "progress", progress);
        char* str_prog = csilk_json_serialize(ev_prog, &slen);
        csilk_json_free(ev_prog);
        csilk_sse_send(c, "step_progress", str_prog ? str_prog : "{}");
        free(str_prog);

        /* step_complete event */
        char step_fin_time_str[64];
        exec_get_datetime_str(step_fin_time_str, sizeof(step_fin_time_str));
        csilk_json_t* ev_st_cmp = csilk_json_object();
        csilk_json_add_number(ev_st_cmp, "step_index", (double)i);
        csilk_json_add_string(ev_st_cmp, "step_id", node->node_id);
        csilk_json_add_string(ev_st_cmp, "summary", summary_buf[0] ? summary_buf : "步骤执行完成");
        csilk_json_add_string(ev_st_cmp, "step_time", step_fin_time_str);
        csilk_json_add_number(ev_st_cmp, "step_timestamp", (double)time(NULL));
        char* str_st_cmp = csilk_json_serialize(ev_st_cmp, &slen);
        csilk_json_free(ev_st_cmp);
        csilk_sse_send(c, "step_complete", str_st_cmp ? str_st_cmp : "{}");
        free(str_st_cmp);
    }

    /* workflow_complete event */
    char fin_time_str[64];
    exec_get_datetime_str(fin_time_str, sizeof(fin_time_str));
    csilk_json_t* ev_cmp = csilk_json_object();
    csilk_json_add_string(ev_cmp, "workflow_id", graph->id);
    csilk_json_add_number(ev_cmp, "total_steps", (double)graph->node_count);
    csilk_json_add_string(ev_cmp, "finish_time", fin_time_str);
    csilk_json_add_number(ev_cmp, "finish_timestamp", (double)time(NULL));
    char* str_cmp = csilk_json_serialize(ev_cmp, &slen);
    csilk_json_free(ev_cmp);
    csilk_sse_send(c, "workflow_complete", str_cmp ? str_cmp : "{}");
    free(str_cmp);

    csilk_json_free(ctx_obj);
    csilk_sse_close(c);
    return 0;
}
