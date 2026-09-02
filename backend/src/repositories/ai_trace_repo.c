/**
 * @file ai_trace_repo.c
 * @brief AI 调用链路追踪与可观测性数据访问层实现
 *
 * 实现了 AI 请求耗时、Token 统计、模型指标的分页检索、详情获取、总体统计以及
 * 将链路追踪数据与工具调用跨度 (Tool Spans) 结构化合并持久化至 `ai_traces` 表的逻辑。
 */

#include "csilk/core/server.h"
#include "repositories/ai_trace_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief 将用户 ID 格式化为固定宽度的字符串缓冲区
 *
 * @param uid 64 位用户 ID
 * @param out 输出字符串缓冲区（至少 32 字节）
 */
static void
uid_str(int64_t uid, char out[static 32])
{
    snprintf(out, 32, "%lld", (long long)uid);
}

/**
 * @brief 分页多条件查询 AI 追踪记录
 *
 * 1. 构建动态 WHERE 筛选条件（user_id、provider、model）。
 * 2. 查询符合条件的记录总数并写入 `*total`。
 * 3. 组织包含类型转换 (CAST) 与 COALESCE 兜底的安全 SQL 查询分页数据。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param page 页码
 * @param page_size 每页数量
 * @param provider 服务商筛选（可选）
 * @param model 模型筛选（可选）
 * @param[out] total 输出参数，符合条件的总条数
 * @return csilk_json_t* 包含概览记录的 JSON 数组
 */
csilk_json_t*
ai_trace_list(csilk_db_pool_t* pool,
              int64_t          user_id,
              int64_t          page,
              int64_t          page_size,
              const char*      provider,
              const char*      model,
              int64_t*         total)
{
    char uid[32], lim[32], off[32];
    uid_str(user_id, uid);
    snprintf(lim, sizeof(lim), "%lld", (long long)page_size);
    snprintf(off, sizeof(off), "%lld", (long long)((page - 1) * page_size));

    char        where[256] = "WHERE user_id=?";
    const char* cnt_params[8] = {0};
    const char* sql_params[8] = {0};
    int         cnt_pc = 0;
    int         sql_pc = 0;

    cnt_params[cnt_pc++] = uid;
    sql_params[sql_pc++] = uid;

    if (provider && provider[0]) {
        strncat(where, " AND provider=?", sizeof(where) - strlen(where) - 1);
        cnt_params[cnt_pc++] = provider;
        sql_params[sql_pc++] = provider;
    }
    if (model && model[0]) {
        strncat(where, " AND model=?", sizeof(where) - strlen(where) - 1);
        cnt_params[cnt_pc++] = model;
        sql_params[sql_pc++] = model;
    }
    cnt_params[cnt_pc] = NULL;

    /* 1. 执行总数统计查询 */
    char cnt_sql[512];
    snprintf(cnt_sql, sizeof(cnt_sql), "SELECT COUNT(*) as cnt FROM ai_traces %s", where);
    csilk_json_t* cnt = csilk_db_query_param_json(pool, cnt_sql, cnt_params);
    *total = 0;
    if (cnt && csilk_json_array_size(cnt) > 0) {
        *total = db_get_int(csilk_json_array_get(cnt, 0), "cnt");
    }
    csilk_json_free(cnt);

    /* 2. 执行分页查询 */
    char sql[1024];
    snprintf(
        sql,
        sizeof(sql),
        "SELECT id, user_id, session_id, provider, model, "
        "prompt_tokens, completion_tokens, total_tokens, "
        "latency_ms, first_token_ms, "
        "COALESCE(CAST(tokens_per_sec AS REAL), 0.0) as tokens_per_sec, "
        "COALESCE(CAST(cost_usd AS REAL), 0.0) as cost_usd, "
        "COALESCE(CAST(temperature AS REAL), 0.0) as temperature, "
        "COALESCE(CAST(max_tokens AS INTEGER), 0) as max_tokens, "
        "COALESCE(CAST(top_p AS REAL), 0.0) as top_p, "
        "status, "
        "CASE WHEN status = 'ok' THEN '' ELSE COALESCE(error_message, '') END as error_message, "
        "created_at "
        "FROM ai_traces %s ORDER BY created_at DESC, id DESC LIMIT ? OFFSET ?",
        where);

    sql_params[sql_pc++] = lim;
    sql_params[sql_pc++] = off;
    sql_params[sql_pc] = NULL;

    CSILK_LOG_I("ai_trace_list: user_id=%lld page=%lld page_size=%lld total=%lld",
                (long long)user_id,
                (long long)page,
                (long long)page_size,
                (long long)*total);
    return csilk_db_query_param_json(pool, sql, sql_params);
}

/**
 * @brief 获取指定 ID 的 AI 完整追踪详情
 *
 * 包含完整的输入提示词、输出正文、系统提示词与元数据信息。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 追踪记录 ID
 * @return csilk_json_t* 包含详情对象的 JSON 数组；未找到返回 NULL
 */
csilk_json_t*
ai_trace_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    char uid[32], id_s[32];
    uid_str(user_id, uid);
    snprintf(id_s, 32, "%lld", (long long)id);

    CSILK_LOG_I("ai_trace_get: user_id=%lld id=%lld", (long long)user_id, (long long)id);
    csilk_json_t* r = csilk_db_query_param_json(
        pool,
        "SELECT id, user_id, session_id, provider, model, "
        "input_messages, output_content, system_prompt, "
        "prompt_tokens, completion_tokens, total_tokens, "
        "latency_ms, first_token_ms, "
        "COALESCE(CAST(tokens_per_sec AS REAL), 0.0) as tokens_per_sec, "
        "COALESCE(CAST(cost_usd AS REAL), 0.0) as cost_usd, "
        "COALESCE(CAST(temperature AS REAL), 0.0) as temperature, "
        "COALESCE(CAST(max_tokens AS INTEGER), 0) as max_tokens, "
        "COALESCE(CAST(top_p AS REAL), 0.0) as top_p, "
        "status, "
        "CASE WHEN status = 'ok' THEN '' ELSE COALESCE(error_message, '') END as error_message, "
        "metadata, created_at "
        "FROM ai_traces WHERE id=? AND user_id=?",
        (const char*[]){id_s, uid, NULL});
    if (!r || csilk_json_array_size(r) == 0) {
        CSILK_LOG_W(
            "ai_trace_get: not found user_id=%lld id=%lld", (long long)user_id, (long long)id);
        csilk_json_free(r);
        return NULL;
    }
    return r;
}

/**
 * @brief 统计用户的 AI 请求聚合指标
 *
 * 使用 SQL 聚合函数 COUNT, SUM, AVG 计算整体性能和资源消耗数据。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @return csilk_json_t* 包含统计结果的 JSON 数组
 */
csilk_json_t*
ai_trace_stats(csilk_db_pool_t* pool, int64_t user_id)
{
    char uid[32];
    uid_str(user_id, uid);

    CSILK_LOG_I("ai_trace_stats: user_id=%lld", (long long)user_id);
    csilk_json_t* r = csilk_db_query_param_json(
        pool,
        "SELECT COUNT(*) as total_traces, "
        "COALESCE(SUM(total_tokens), 0) as total_tokens, "
        "COALESCE(AVG(latency_ms), 0.0) as avg_latency_ms, "
        "COALESCE(AVG(first_token_ms), 0.0) as avg_first_token_ms, "
        "COALESCE(AVG(CAST(tokens_per_sec AS REAL)), 0.0) as avg_tokens_per_sec, "
        "COALESCE(SUM(CAST(cost_usd AS REAL)), 0.0) as total_cost_usd "
        "FROM ai_traces WHERE user_id=?",
        (const char*[]){uid, NULL});
    return r;
}

/**
 * @brief 持久化保存一条 AI 请求链路追踪记录
 *
 * 将内存结构体 `ai_trace_t` 的各个指标字段序列化为 SQL 参数并执行插入。
 * 若记录了工具调用跨度 (tool_spans)，则将其合并至 metadata JSON 字段中一并存储。
 *
 * @param pool 数据库连接池指针
 * @param t 追踪上下文结构体指针
 * @return int64_t 成功生成的主键 ID，失败返回 0
 */
int64_t
ai_trace_save(csilk_db_pool_t* pool, ai_trace_t* t)
{
    char uid[32] = {0}, sid[32] = {0}, pt[32] = {0}, ct[32] = {0}, tt[32] = {0};
    char lat[32] = {0}, ftt[32] = {0}, tps[64] = {0}, cost[64] = {0};
    char temp[32] = {0}, mtt[32] = {0}, tp[32] = {0};

    snprintf(uid, sizeof(uid), "%lld", (long long)t->user_id);
    snprintf(sid, sizeof(sid), "%lld", (long long)t->session_id);
    snprintf(pt, sizeof(pt), "%d", t->prompt_tokens);
    snprintf(ct, sizeof(ct), "%d", t->completion_tokens);
    snprintf(tt, sizeof(tt), "%d", t->total_tokens);
    snprintf(lat, sizeof(lat), "%ld", t->latency_ms);
    snprintf(ftt, sizeof(ftt), "%ld", t->first_token_ms);
    snprintf(tps, sizeof(tps), "%.2f", t->tokens_per_sec);
    snprintf(cost, sizeof(cost), "%.6f", t->cost_usd);
    snprintf(temp, sizeof(temp), "%.2f", t->temperature);
    snprintf(mtt, sizeof(mtt), "%d", t->max_tokens);
    snprintf(tp, sizeof(tp), "%.2f", t->top_p);

    /* 将 tool_spans 合并到 metadata JSON 中统一保存在单列中 */
    char*         meta_final = NULL;
    size_t        meta_len = 0;
    csilk_json_t* meta_obj = csilk_json_parse(t->metadata[0] ? t->metadata : "{}");
    if (!meta_obj) {
        meta_obj = csilk_json_object();
    }
    if (t->tool_spans[0]) {
        csilk_json_t* spans = csilk_json_parse(t->tool_spans);
        if (spans) {
            csilk_json_add_object(meta_obj, "tool_spans", spans); /* spans 归属 meta_obj 管理 */
        }
    }
    meta_final = csilk_json_serialize(meta_obj, &meta_len);
    csilk_json_free(meta_obj);
    if (!meta_final) {
        meta_final = strdup("{}");
    }

    const char* sql = "INSERT INTO ai_traces "
                      "(user_id, session_id, provider, model, input_messages, output_content, "
                      "system_prompt, prompt_tokens, completion_tokens, total_tokens, "
                      "latency_ms, first_token_ms, tokens_per_sec, cost_usd, "
                      "temperature, max_tokens, top_p, status, error_message, metadata) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
                      "RETURNING id";

    const char* err_msg =
        (strcmp(t->status, "ok") == 0) ? "" : (t->error_message[0] ? t->error_message : "");

    const char* params[] = {uid,
                            sid,
                            t->provider[0] ? t->provider : "",
                            t->model[0] ? t->model : "",
                            t->input_messages[0] ? t->input_messages : "[]",
                            t->output_content[0] ? t->output_content : "",
                            t->system_prompt[0] ? t->system_prompt : "",
                            pt,
                            ct,
                            tt,
                            lat,
                            ftt,
                            tps,
                            cost,
                            temp,
                            mtt,
                            tp,
                            t->status[0] ? t->status : "ok",
                            err_msg,
                            meta_final[0] ? meta_final : "{}",
                            NULL};

    CSILK_LOG_I("ai_trace_save: user_id=%lld model='%s' provider='%s' status='%s' tokens=%d",
                (long long)t->user_id,
                t->model,
                t->provider,
                t->status,
                t->total_tokens);
    csilk_json_t* r = csilk_db_query_param_json(pool, sql, params);
    int64_t       id = 0;
    if (r && csilk_json_array_size(r) > 0) {
        id = db_get_int(csilk_json_array_get(r, 0), "id");
    }
    CSILK_LOG_I("ai_trace_save: id=%lld", (long long)id);
    csilk_json_free(r);
    free(meta_final);
    return id;
}
