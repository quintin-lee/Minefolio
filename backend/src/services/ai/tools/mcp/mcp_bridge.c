/**
 * @file mcp_bridge.c
 * @brief MCP 远端工具桥接层实现 — M2
 *
 * 三条入口：
 *  1. mcp_bridge_merge_tools：内置工具 + 当前用户已启用 MCP 缓存工具合并为
 *     一个堆分配 csilk_ai_tool_t* 数组（loop.c step4 喂 LLM）。
 *  2. mcp_bridge_dispatch：dispatcher 对 "mcp:<id>:<tool>" 前缀路由目标，
 *     完整 R-MCP 策略 + 远端调用 + 审计。
 *  3. mcp_bridge_refresh_server：controller /test + /refresh 实装，
 *     initialize + tools/list 回写缓存 + discovered_at。
 */

#include "services/ai/tools/mcp/mcp_bridge.h"

#include "services/ai/tools/mcp/mcp_client.h"
#include "services/ai/tools/mcp/mcp_config.h"
#include "services/ai/tools/mcp/mcp_stdio_pool.h"
#include "services/ai/tools/registry.h"
#include "services/ai/policy/policy.h"
#include "services/ai/policy/risk.h"
#include "services/ai/policy/permission.h"
#include "services/ai/policy/audit.h"
#include "common/db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <pthread.h>

/* 单个远端工具 description 最大长度（与 entity description[512] 对齐，截断 511） */
#define MF_MCP_TOOL_DESC_MAX 512

/* 远端 input_schema 最大长度（与 entity input_schema[16384] 对齐） */
#define MF_MCP_TOOL_SCHEMA_MAX 16384

/* ================================================================== */
/* MCP 专属限频 LRU（设计 §5 rate_limited / §9 MINEFOLIO_MCP_RATE_PER_MIN）
 * 独立于 ai_policy 的全局 60/分 LRU，按 user_id 维度 + 独立上限。
 * 仿 policy.c 环形缓冲模式：满表覆写 head 槽位淘汰最旧。
 * ================================================================== */
#define MF_MCP_RATE_LIMIT_USERS 256

typedef struct {
    int64_t user_id;
    int64_t minute_window;
    int     count;
} mcp_user_rate_t;

static mcp_user_rate_t s_mcp_rate_limits[MF_MCP_RATE_LIMIT_USERS];
static size_t          s_mcp_rate_head = 0;
static size_t          s_mcp_rate_count = 0;
static pthread_mutex_t s_mcp_rate_lock = PTHREAD_MUTEX_INITIALIZER;

/* 返回 true 表示限频通过；false 表示触发限流（调用方应拒绝）。
   limit_per_min <= 0 表示未启用 MCP 专属限频，直接放行。 */
static bool
mcp_rate_limit_check(int64_t user_id, int limit_per_min)
{
    if (user_id <= 0 || limit_per_min <= 0) {
        return true;
    }
    int64_t current_minute = (int64_t)time(NULL) / 60;

    pthread_mutex_lock(&s_mcp_rate_lock);
    size_t scan =
        (s_mcp_rate_count < MF_MCP_RATE_LIMIT_USERS) ? s_mcp_rate_count : MF_MCP_RATE_LIMIT_USERS;
    for (size_t i = 0; i < scan; i++) {
        if (s_mcp_rate_limits[i].user_id == user_id) {
            if (s_mcp_rate_limits[i].minute_window == current_minute) {
                if (s_mcp_rate_limits[i].count >= limit_per_min) {
                    pthread_mutex_unlock(&s_mcp_rate_lock);
                    return false; /* 触发限流 */
                }
                s_mcp_rate_limits[i].count++;
            } else {
                s_mcp_rate_limits[i].minute_window = current_minute;
                s_mcp_rate_limits[i].count = 1;
            }
            pthread_mutex_unlock(&s_mcp_rate_lock);
            return true;
        }
    }

    /* 未命中：表未满追加，满则覆写 head 槽位淘汰最旧 */
    if (s_mcp_rate_count < MF_MCP_RATE_LIMIT_USERS) {
        s_mcp_rate_limits[s_mcp_rate_count].user_id = user_id;
        s_mcp_rate_limits[s_mcp_rate_count].minute_window = current_minute;
        s_mcp_rate_limits[s_mcp_rate_count].count = 1;
        s_mcp_rate_count++;
    } else {
        s_mcp_rate_limits[s_mcp_rate_head].user_id = user_id;
        s_mcp_rate_limits[s_mcp_rate_head].minute_window = current_minute;
        s_mcp_rate_limits[s_mcp_rate_head].count = 1;
        s_mcp_rate_head = (s_mcp_rate_head + 1) % MF_MCP_RATE_LIMIT_USERS;
    }
    pthread_mutex_unlock(&s_mcp_rate_lock);
    return true;
}

/* ================================================================== */
/* 工具缓存 TTL（设计 §9 MINEFOLIO_MCP_CACHE_TTL）
 * fetched_at 为空或解析失败时保守保留（不跳过）。
 * ================================================================== */
static bool
mcp_tool_is_stale(const mf_mcp_server_tool_t* tool, int cache_ttl_sec)
{
    if (cache_ttl_sec <= 0) {
        return false; /* 未启用 TTL，不过期 */
    }
    if (!tool->fetched_at[0]) {
        return false; /* 无时间戳，保守保留 */
    }
    struct tm tm_buf;
    memset(&tm_buf, 0, sizeof(tm_buf));
    strptime(tool->fetched_at, "%Y-%m-%d %H:%M:%S", &tm_buf); /* end==NULL 表解析失败 */
    if (tm_buf.tm_year == 0) {
        return false;                                         /* 解析失败/空，保守保留 */
    }
    time_t fetched = mktime(&tm_buf);
    if (fetched == (time_t)-1) {
        return false;
    }
    return (time(NULL) - fetched) > (time_t)cache_ttl_sec;
}

/* ================================================================== */
/* 解析 "mcp:<serverId>:<toolName>" → (server_id, tool_name)           */
/* ================================================================== */

/*
 * 返回 0 表示解析成功；1 表示格式不符。
 * tool_buf 至少 128 字节；server_id 为解析出的 64 位整数。
 */
static int
parse_mcp_qualified_name(const char* qualified,
                         int64_t*    out_server_id,
                         char*       out_tool,
                         size_t      out_tool_sz)
{
    if (!qualified || strncmp(qualified, "mcp:", 4) != 0) {
        return 1;
    }
    const char* after_prefix = qualified + 4;

    char   id_buf[32] = {0};
    size_t i = 0;
    while (after_prefix[i] && after_prefix[i] != ':' && i < sizeof(id_buf) - 1) {
        id_buf[i] = after_prefix[i];
        i++;
    }
    if (i == 0 || after_prefix[i] != ':') {
        return 1; /* 缺少第二个冒号 */
    }
    *out_server_id = strtoll(id_buf, NULL, 10);

    const char* tool_start = after_prefix + i + 1;
    if (!tool_start[0]) {
        return 1; /* 空工具名 */
    }
    strncpy(out_tool, tool_start, out_tool_sz - 1);
    out_tool[out_tool_sz - 1] = '\0';
    return 0;
}

/* 把 risk_level 字符串映射为 ai_risk_level_t；未知/空 → AI_RISK_LOW */
static ai_risk_level_t
mcp_risk_string_to_level(const char* s)
{
    if (!s || !s[0]) {
        return AI_RISK_LOW;
    }
    if (strcasecmp(s, "high") == 0) {
        return AI_RISK_HIGH;
    }
    if (strcasecmp(s, "critical") == 0) {
        return AI_RISK_CRITICAL;
    }
    if (strcasecmp(s, "medium") == 0) {
        return AI_RISK_MEDIUM;
    }
    return AI_RISK_LOW;
}

/* 把权限需求映射为 ai_permission_level_t；mutation 工具需 WRITE。 */
static ai_permission_level_t
mcp_perm_for_tool(const mf_mcp_server_tool_t* tool)
{
    return tool->is_mutation ? AI_PERM_WRITE : AI_PERM_READ;
}

/* 将 client 层的通用错误串映射为设计 §5 定义的结构化 LLM 可见错误码。
   8 类中 transport_down/timeout/auth_failed/remote_error 由 client 错误串特征 +
   transport 类型推断；未知特征兜底 remote_error。
   必要注释：client 仅返回不透明 err 串，需在此分类供 LLM 区分"远端超时"与"认证失败"。 */
static const char*
mcp_classify_error(const char* err, int transport)
{
    if (!err || !err[0]) {
        return "mcp_remote_error";
    }
    /* HTTP 4xx 认证/授权类 */
    if (strstr(err, "401") || strstr(err, "403") || strstr(err, "unauthorized") ||
        strstr(err, "forbidden") || strstr(err, "auth failed")) {
        return "mcp_auth_failed";
    }
    /* 超时特征（curl 28 / stdio 预算耗尽） */
    if (strstr(err, "timeout") || strstr(err, "timed out") || strstr(err, "curl 28")) {
        return "mcp_timeout";
    }
    /* 传输层断连/子进程未回流（transport_down） */
    if (strstr(err, "transport") || strstr(err, "curl init") ||
        strstr(err, "produced no response") || strstr(err, "write failed") ||
        strstr(err, "read failed") || strstr(err, "fork failed") || strstr(err, "pipe()") ||
        strstr(err, "connect")) {
        return "mcp_transport_down";
    }
    /* 远端返回了响应但非有效 JSON / 非 2xx */
    if (strstr(err, "not valid JSON") || strstr(err, "invalid JSON") || strstr(err, "no result") ||
        strstr(err, "http=")) {
        return "mcp_remote_error";
    }
    /* 参数 schema 校验失败（client 侧 schema 问题） */
    if (strstr(err, "schema")) {
        return "mcp_schema_invalid";
    }
    (void)transport;
    return "mcp_remote_error";
}

/* 组装 {error:code, detail:msg} 并返回堆串（调用方 free）。 */
static char*
mcp_err_response(const char* code, const char* detail)
{
    csilk_json_t* eo = csilk_json_object();
    if (!eo) {
        return NULL;
    }
    csilk_json_add_string(eo, "error", code ? code : "mcp_remote_error");
    if (detail && detail[0]) {
        csilk_json_add_string(eo, "detail", detail);
    }
    size_t len = 0;
    char*  out = csilk_json_serialize(eo, &len);
    csilk_json_free(eo);
    if (out) {
        return out;
    }
    /* serialize OOM：退化为静态安全串（堆分配供调用方 free） */
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", code ? code : "mcp_remote_error");
    return strdup(buf);
}

/* ================================================================== */
/* 工具定义合并                                                       */
/* ================================================================== */

/* 检测 schema 是否含外部 $ref（http/https/urn 等 LLM 无法解析的引用）。
   csilk JSON API 无 remove，故策略：发现外部 $ref 时整个 schema 退化为空
   对象（外部引用对 LLM 无意义且可能有恶意 schema），保留内部 # 引用。
   design §15④ $ref 白名单。 */
static bool
mcp_schema_has_external_ref(const csilk_json_t* node)
{
    if (!csilk_json_is_object(node)) {
        return false;
    }
    csilk_json_t* ref = csilk_json_get(node, "$ref");
    if (csilk_json_is_string(ref)) {
        const char* v = csilk_json_string_value(ref);
        if (v && v[0] != '#') {
            return true; /* 外部引用 */
        }
    }
    size_t n = csilk_json_object_size(node);
    for (size_t i = 0; i < n; i++) {
        if (mcp_schema_has_external_ref(csilk_json_object_val(node, i))) {
            return true;
        }
    }
    return false;
}

const csilk_ai_tool_t*
mcp_bridge_merge_tools(csilk_db_pool_t* pool, int64_t user_id, size_t* out_count)
{
    if (!out_count) {
        return NULL;
    }
    *out_count = 0;

    /* 1) 内置工具静态数组（不计入合并堆，但需拷入 merged 数组供统一释放） */
    size_t                 builtin_count = 0;
    const csilk_ai_tool_t* builtin = ai_tool_get_csilk_definitions(&builtin_count);
    size_t                 off = builtin_count; /* 已填充条目数；oom 回滚边界 */

    /* 2) 加载当前用户已启用 MCP 服务器的工具缓存（JOIN mcp_server enabled=1），
       并按缓存 TTL 过滤过期工具（设计 §9 MINEFOLIO_MCP_CACHE_TTL）。 */
    mf_mcp_server_tool_t* cached = NULL;
    size_t                cached_count = 0;
    if (pool && user_id > 0) {
        int rc = mf_mcp_server_tool_repo_load_for_user(pool, user_id, &cached, &cached_count);
        if (rc != 0) {
            cached = NULL;
            cached_count = 0;
        }
    }

    /* 2b) TTL 过滤：构建非过期条目子集（紧凑复制进 kept），原 cached 释放 */
    int ttl = mf_mcp_config_cache_ttl_sec();
    if (cached && cached_count > 0 && ttl > 0) {
        mf_mcp_server_tool_t* kept =
            (mf_mcp_server_tool_t*)calloc(cached_count, sizeof(mf_mcp_server_tool_t));
        size_t kept_count = 0;
        if (kept) {
            for (size_t i = 0; i < cached_count; i++) {
                if (!mcp_tool_is_stale(&cached[i], ttl)) {
                    kept[kept_count++] = cached[i];
                }
            }
            free(cached); /* 释放原列表，kept 接管非过期条目 */
            cached = kept;
            cached_count = kept_count;
        }
    }

    size_t total = builtin_count + cached_count;
    if (total == 0) {
        if (cached) {
            mf_mcp_server_tool_repo_free_list(cached, cached_count);
        }
        *out_count = 0;
        return NULL;
    }

    csilk_ai_tool_t* merged = (csilk_ai_tool_t*)calloc(total, sizeof(csilk_ai_tool_t));
    if (!merged) {
        if (cached) {
            mf_mcp_server_tool_repo_free_list(cached, cached_count);
        }
        *out_count = 0;
        return NULL;
    }

    /* 3a) 深度拷贝内置条目（name/description 堆串，parameters_json 深拷贝 schema），
     *     使 merged 数组所有条目内指针统一为堆分配，free_merged 可安全统一释放 */
    for (size_t i = 0; i < builtin_count; i++) {
        char* bname =
            (char*)calloc(1, strlen(builtin[i].function.name ? builtin[i].function.name : "") + 1);
        char* bdesc = (char*)calloc(
            1, strlen(builtin[i].function.description ? builtin[i].function.description : "") + 1);
        if (!bname || !bdesc) {
            free(bname);
            free(bdesc);
            goto oom;
        }
        if (bname) {
            strcpy(bname, builtin[i].function.name ? builtin[i].function.name : "");
        }
        if (bdesc) {
            strcpy(bdesc, builtin[i].function.description ? builtin[i].function.description : "");
        }
        csilk_json_t* bparams =
            builtin[i].function.parameters_json
                ? csilk_json_copy((csilk_json_t*)builtin[i].function.parameters_json)
                : NULL;
        merged[i] = (csilk_ai_tool_t){
            .type = "function",
            .function =
                {
                           .name = bname,
                           .description = bdesc,
                           .parameters_json = bparams,
                           },
        };
        off++;
    }

    /* 3b) 追加 MCP 远端工具（name/description/parameters_json 均堆分配） */
    for (size_t i = 0; i < cached_count; i++) {
        const mf_mcp_server_tool_t* t = &cached[i];

        /* name = t->qualified_name（已是 "mcp:<id>:<tool>"） */
        char* qn = (char*)calloc(1, strlen(t->qualified_name) + 1);
        if (!qn) {
            goto oom;
        }
        strcpy(qn, t->qualified_name);

        char* desc = (char*)calloc(1, MF_MCP_TOOL_DESC_MAX);
        if (!desc) {
            free(qn);
            goto oom;
        }
        snprintf(desc,
                 MF_MCP_TOOL_DESC_MAX,
                 "%.511s",
                 t->description[0] ? t->description : "(MCP remote tool)");

        /* parameters_json：input_schema 是 JSON 文本，parse 成 csilk_json_t* 再喂 LLM。
           若含外部 $ref（LLM 无法解析的引用），整个 schema 退化为空对象。 */
        csilk_json_t* params = NULL;
        if (t->input_schema[0]) {
            params = csilk_json_parse(t->input_schema);
            if (!params) {
                params = csilk_json_object(); /* 解析失败退化为空 schema */
            } else if (mcp_schema_has_external_ref(params)) {
                csilk_json_free(params);
                params = csilk_json_object(); /* 外部 $ref 不喂给 LLM */
            }
        } else {
            params = csilk_json_object();
        }

        merged[off] = (csilk_ai_tool_t){
            .type = "function",
            .function =
                {
                           .name = qn,
                           .description = desc,
                           .parameters_json = params,
                           },
        };
        off++;
    }

    if (cached) {
        mf_mcp_server_tool_repo_free_list(cached, cached_count);
    }
    *out_count = total;
    return merged;

oom:
    /* 部分成功回滚：释放已填充条目（0..off-1）的堆指针 + 数组本身 */
    for (size_t k = 0; k < off; k++) {
        free((void*)merged[k].function.name);
        free((void*)merged[k].function.description);
        if (merged[k].function.parameters_json) {
            csilk_json_free((csilk_json_t*)merged[k].function.parameters_json);
        }
    }
    free(merged);
    if (cached) {
        mf_mcp_server_tool_repo_free_list(cached, cached_count);
    }
    *out_count = 0;
    return NULL;
}

void
mcp_bridge_free_merged(const csilk_ai_tool_t* arr, size_t count)
{
    if (!arr || count == 0) {
        return;
    }
    /* 数组内每条 name/description 为堆串、parameters_json 为堆 schema，统一深释放 */
    for (size_t i = 0; i < count; i++) {
        free((void*)arr[i].function.name);
        free((void*)arr[i].function.description);
        if (arr[i].function.parameters_json) {
            csilk_json_free((csilk_json_t*)arr[i].function.parameters_json);
        }
    }
    free((void*)arr);
}

/* ================================================================== */
/* dispatcher 路由目标：执行单个 "mcp:<id>:<tool>" 调用               */
/* ================================================================== */

char*
mcp_bridge_dispatch(const ai_tool_context_t* ctx, const char* tool_name, const csilk_json_t* args)
{
    /* 1. 认证 */
    if (!ctx || ctx->user_id <= 0) {
        return strdup("{\"error\":\"unauthorized: unauthenticated user context\"}");
    }
    if (!tool_name || !tool_name[0]) {
        return strdup("{\"error\":\"missing tool name\"}");
    }

    /* 2. 解析 qualified name */
    int64_t server_id = 0;
    char    bare_tool[128] = {0};
    if (parse_mcp_qualified_name(tool_name, &server_id, bare_tool, sizeof(bare_tool)) != 0) {
        return strdup("{\"error\":\"malformed MCP tool name (expected mcp:<serverId>:<tool>)\"}");
    }

    /* 3. 取服务器配置（校验归属 + enabled） */
    mf_mcp_server_t server;
    memset(&server, 0, sizeof(server));
    int find_rc = mf_mcp_server_repo_find_by_id(ctx->pool, ctx->user_id, server_id, &server);
    if (find_rc == 1) {
        return mcp_err_response("mcp_server_unknown", "no such MCP server");
    }
    if (find_rc < 0) {
        return mcp_err_response("mcp_server_lookup_failed", NULL);
    }
    if (!server.enabled) {
        return mcp_err_response("mcp_server_disabled", NULL);
    }

    /* 4. R-MCP 策略评估：按缓存 risk_level + mutation 需求权限 */
    mf_mcp_server_tool_t* cached = NULL;
    size_t                cached_count = 0;
    mf_mcp_server_tool_t* target = NULL;
    if (ctx->pool) {
        int rc = mf_mcp_server_tool_repo_load(
            ctx->pool, ctx->user_id, server_id, &cached, &cached_count);
        if (rc == 0 && cached) {
            for (size_t i = 0; i < cached_count; i++) {
                if (strcmp(cached[i].tool_name, bare_tool) == 0) {
                    target = &cached[i];
                    break;
                }
            }
        }
    }
    if (!target) {
        /* 缓存未命中：按 low 风险/只读处理，但仍走 strategy */
        static mf_mcp_server_tool_t anon = {0};
        anon.is_mutation = false;
        anon.risk_level[0] = '\0';
        target = &anon;
    }

    ai_permission_level_t perm = mcp_perm_for_tool(target);
    ai_risk_level_t       risk = mcp_risk_string_to_level(target->risk_level);
    if (target->is_mutation) {
        risk = risk < AI_RISK_MEDIUM ? AI_RISK_MEDIUM : risk;
    }

    /* 权限校验（mutation 需 WRITE） */
    if (perm > AI_PERM_READ && !(ctx->permissions & (1u << perm))) {
        if (cached) {
            mf_mcp_server_tool_repo_free_list(cached, cached_count);
        }
        return strdup("{\"error\":\"mcp_permission_denied: write permission required\"}");
    }

    /* MCP 专属限频（设计 §5 rate_limited / §9 MINEFOLIO_MCP_RATE_PER_MIN）。
       独立于 ai_policy 全局 60/分 LRU；超限返回结构化 mcp_rate_limited。 */
    if (!mcp_rate_limit_check(ctx->user_id, mf_mcp_config_rate_per_min())) {
        if (cached) {
            mf_mcp_server_tool_repo_free_list(cached, cached_count);
        }
        return mcp_err_response("mcp_rate_limited", "MCP per-user rate limit exceeded");
    }

    /* 统一策略引擎评估（限流 + 确认） */
    ai_policy_decision_t* decision =
        ai_policy_evaluate(ctx->user_id, ctx->session_id, tool_name, args, risk);
    if (!decision || !decision->allowed) {
        const char* reason = decision ? decision->reason : "Policy rejection";
        if (cached) {
            mf_mcp_server_tool_repo_free_list(cached, cached_count);
        }
        if (decision) {
            ai_policy_decision_free(decision);
        }
        csilk_json_t* err = csilk_json_object();
        csilk_json_add_string(err, "error", "policy_denied");
        csilk_json_add_string(err, "reason", reason ? reason : "blocked by policy");
        size_t len = 0;
        char*  out = csilk_json_serialize(err, &len);
        csilk_json_free(err);
        return out ? out : strdup("{\"error\":\"policy check failed\"}");
    }
    ai_policy_decision_free(decision);
    if (cached) {
        mf_mcp_server_tool_repo_free_list(cached, cached_count);
        cached = NULL;
    }

    /* 5. 协作式取消：用户取消对话时不发起远端调用 */
    if (ctx->cancel_token && *ctx->cancel_token) {
        return mcp_err_response("mcp_cancelled", "MCP tool call cancelled");
    }

    /* 6. 获取客户端并握手（stdio 复用池内已 initialize 会话，池禁用/满退化 short-lived） */
    char             err[256] = {0};
    mf_mcp_client_t* c;
    int              pooled = 0;
    if (server.transport == MCP_TRANSPORT_STDIO) {
        c = mf_mcp_stdio_pool_acquire(ctx->user_id, server_id, &server, err, sizeof(err));
        if (!c) {
            const char* code = mcp_classify_error(err, server.transport);
            return mcp_err_response(code, err[0] ? err : "mcp stdio pool acquire failed");
        }
        pooled = 1; /* 池条目：后续 release 而非 free */
    } else {
        c = mf_mcp_client_new(&server);
        if (!c) {
            return mcp_err_response("mcp_client_alloc_failed", "mcp_client_alloc_failed");
        }
        if (mf_mcp_client_initialize(c, err, sizeof(err)) != 0) {
            const char* code = mcp_classify_error(err, server.transport);
            mf_mcp_client_free(c);
            return mcp_err_response(code, err[0] ? err : "mcp initialize failed");
        }
    }

    /* 序列化工具参数（args 为 NULL 时用局部空对象，serialize 不接管所有权须手动 free） */
    char* args_str = NULL;
    if (args) {
        size_t args_len = 0;
        args_str = csilk_json_serialize(args, &args_len);
    }
    if (!args_str) {
        csilk_json_t* empty = csilk_json_object();
        if (empty) {
            size_t empty_len = 0;
            args_str = csilk_json_serialize(empty, &empty_len);
            csilk_json_free(empty);
        }
        if (!args_str) {
            args_str = strdup("{}");
        }
    }

    char* result = mf_mcp_client_call_tool(c, bare_tool, args_str, err, sizeof(err));
    free(args_str);
    int call_failed = (result == NULL) || (err[0] != '\0');
    /* stdio 池条目归还（保持子进程供复用）；HTTP short-lived 直接 free（双段清理） */
    if (pooled) {
        mf_mcp_stdio_pool_release(c);
    } else {
        mf_mcp_client_free(c);
    }

    /* 7. 审计归档 */
    bool              is_err = (result != NULL) && (strstr(result, "\"error\":") != NULL);
    ai_audit_record_t audit = {
        .stage = AI_AUDIT_STAGE_EXECUTION,
        .actor_id = ctx->user_id,
        .session_id = ctx->session_id,
        .risk = risk,
        .timestamp = (int64_t)time(NULL),
        .success = !is_err && !call_failed,
    };
    snprintf(audit.tool, sizeof(audit.tool), "%s", tool_name);
    snprintf(audit.result_summary,
             sizeof(audit.result_summary),
             (is_err || call_failed) ? "MCP tool call failed" : "MCP tool call succeeded");
    ai_audit_log(&audit);

    /* 调用失败或空结果：产出结构化错误码供 LLM 区分失败类型（设计 §5） */
    if (call_failed) {
        if (result) {
            free(result);
        }
        const char* code = mcp_classify_error(err, server.transport);
        const char* msg = err[0] ? err : "empty MCP tool result";
        return mcp_err_response(code, msg);
    }
    return result ? result : mcp_err_response("mcp_remote_error", "empty MCP tool result");
}

/* ================================================================== */
/* controller /test + /refresh 实装                                   */
/* ================================================================== */

int
mcp_bridge_refresh_server(
    csilk_db_pool_t* pool, int64_t user_id, int64_t server_id, size_t* out_count, char** out_err)
{
    if (out_count) {
        *out_count = 0;
    }
    if (out_err) {
        *out_err = NULL;
    }
    if (!pool || user_id <= 0 || server_id <= 0) {
        if (out_err) {
            *out_err = strdup("invalid arguments");
        }
        return -1;
    }

    /* 取服务器配置（校验归属） */
    mf_mcp_server_t server;
    memset(&server, 0, sizeof(server));
    int find_rc = mf_mcp_server_repo_find_by_id(pool, user_id, server_id, &server);
    if (find_rc == 1) {
        if (out_err) {
            *out_err = strdup("no such MCP server");
        }
        return -1;
    }
    if (find_rc < 0) {
        if (out_err) {
            *out_err = strdup("mcp_server_lookup_failed");
        }
        return -1;
    }

    /* 建立客户端 → initialize → tools/list */
    mf_mcp_client_t* c = mf_mcp_client_new(&server);
    if (!c) {
        if (out_err) {
            *out_err = strdup("mcp_client_alloc_failed");
        }
        return -1;
    }

    char err[256] = {0};
    if (mf_mcp_client_initialize(c, err, sizeof(err)) != 0) {
        const char* msg = err[0] ? err : "mcp initialize failed";
        mf_mcp_client_free(c);
        if (out_err) {
            *out_err = strdup(msg);
        }
        return -1;
    }

    mf_mcp_server_tool_t* tools = NULL;
    size_t                count = 0;
    int                   list_rc = mf_mcp_client_list_tools(c, server_id, user_id, &tools, &count);
    mf_mcp_client_free(c);

    if (list_rc != 0) {
        if (tools) {
            free(tools);
        }
        if (out_err) {
            *out_err = strdup("tools/list failed");
        }
        return -1;
    }

    /* 回写缓存 + discovered_at */
    int upsert_rc = mf_mcp_server_tool_repo_upsert(pool, user_id, server_id, tools, count);
    if (tools) {
        free(tools);
    }
    if (upsert_rc != 0) {
        if (out_err) {
            *out_err = strdup("failed to upsert tool cache");
        }
        return -1;
    }

    /* 更新服务器 discovered_at（失败不致命，工具缓存已成功） */
    time_t ts = time(NULL);
    char   ts_str[32] = {0};
    strftime(ts_str, sizeof(ts_str), "%Y-%m-%d %H:%M:%S", localtime(&ts));

    char s_id[24] = {0};
    snprintf(s_id, sizeof(s_id), "%lld", (long long)server_id);
    char uid_str[24] = {0};
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* sql = "UPDATE mcp_server SET discovered_at = ?1, updated_at = CURRENT_TIMESTAMP "
                      "WHERE id = ?2 AND user_id = ?3";
    const char* uparams[] = {ts_str, s_id, uid_str, NULL};
    if (csilk_db_exec_param(pool, sql, uparams) != 0) {
        /* 非致命 */
    }

    if (out_count) {
        *out_count = count;
    }
    return 0;
}
