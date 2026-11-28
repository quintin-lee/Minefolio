#include "services/ai/tools/tool_helpers.h"
#include "services/ai/policy/confirmation.h"
#include "common/db.h"
#include "common/ledger_utils.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

bool
ai_tool_ctx_valid(const ai_tool_context_t* ctx)
{
    return ctx && ctx->pool && ctx->user_id > 0;
}

char*
ai_tool_json_error(const char* message, int code)
{
    csilk_json_t* err = csilk_json_object();
    csilk_json_add_string(err, "error", (message && message[0]) ? message : "internal error");
    if (code > 0) {
        csilk_json_add_int(err, "code", code);
    }
    return ai_tool_json_serialize_free(err);
}

char*
ai_tool_json_serialize_free(csilk_json_t* obj)
{
    if (!obj) {
        return strdup("{}");
    }
    size_t len = 0;
    char*  s = csilk_json_serialize(obj, &len);
    csilk_json_free(obj);
    return s ? s : strdup("{\"error\":\"serialization failed\"}");
}

void
ai_tool_parse_pagination(const csilk_json_t* args,
                         int64_t             default_size,
                         int64_t             max_size,
                         int64_t*            out_page,
                         int64_t*            out_page_size)
{
    int64_t page = 1;
    int64_t page_size = default_size > 0 ? default_size : 20;
    int64_t cap = max_size > 0 ? max_size : 100;

    if (args) {
        double p = db_get_num(args, "page");
        if (p >= 1.0) {
            page = (int64_t)p;
        }
        double ps = db_get_num(args, "page_size");
        if (ps >= 1.0) {
            page_size = (int64_t)ps;
            if (page_size > cap) {
                page_size = cap;
            }
        }
    }

    if (out_page) {
        *out_page = page;
    }
    if (out_page_size) {
        *out_page_size = page_size;
    }
}

int64_t
ai_tool_resolve_ledger_id(const ai_tool_context_t* ctx,
                          const csilk_json_t*      args,
                          bool                     fallback_default)
{
    int64_t lid = args ? db_get_int(args, "ledger_id") : 0;
    if (lid <= 0 && ctx && ctx->metadata) {
        lid = db_get_int(ctx->metadata, "ledger_id");
    }
    if (lid <= 0 && fallback_default && ctx && ctx->pool && ctx->user_id > 0) {
        lid = ledger_get_default((csilk_db_pool_t*)ctx->pool, ctx->user_id);
    }
    return lid;
}

void
ai_tool_today_str(char* buf, size_t sz)
{
    if (!buf || sz == 0) {
        return;
    }
    time_t    now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    snprintf(buf, sz, "%04d-%02d-%02d", tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday);
}

int
ai_tool_str_icontains(const char* haystack, const char* needle)
{
    if (!haystack || !needle) {
        return 0;
    }
    if (!needle[0]) {
        return 1;
    }
    size_t hlen = strlen(haystack);
    size_t nlen = strlen(needle);
    if (nlen > hlen) {
        return 0;
    }
    char h_small[256], n_small[256];
    if (hlen < sizeof(h_small) && nlen < sizeof(n_small)) {
        for (size_t i = 0; i < hlen; i++) {
            h_small[i] = (char)tolower((unsigned char)haystack[i]);
        }
        h_small[hlen] = '\0';
        for (size_t i = 0; i < nlen; i++) {
            n_small[i] = (char)tolower((unsigned char)needle[i]);
        }
        n_small[nlen] = '\0';
        return strstr(h_small, n_small) != NULL;
    }
    return strstr(haystack, needle) != NULL;
}

static void
copy_object_fields(csilk_json_t* dst, const csilk_json_t* src)
{
    if (!dst || !src || !csilk_json_is_object(src)) {
        return;
    }
    size_t n = csilk_json_object_size(src);
    for (size_t i = 0; i < n; i++) {
        const char*         k = csilk_json_object_key(src, i);
        const csilk_json_t* v = csilk_json_object_val(src, i);
        if (!k || !v) {
            continue;
        }
        if (csilk_json_is_string(v)) {
            csilk_json_add_string(dst, k, csilk_json_string_value(v));
        } else if (csilk_json_is_number(v)) {
            csilk_json_add_number(dst, k, csilk_json_number_value(v));
        } else if (csilk_json_is_bool(v)) {
            csilk_json_add_bool(dst, k, csilk_json_bool_value(v));
        } else if (csilk_json_is_null(v)) {
            csilk_json_add_null(dst, k);
        } else {
            csilk_json_add_object(dst, k, csilk_json_copy((csilk_json_t*)v));
        }
    }
}

char*
ai_tool_propose_finalize(const ai_tool_context_t* ctx,
                         const char*              action_type,
                         const char*              confirm_tool_name,
                         ai_risk_level_t          risk,
                         const csilk_json_t*      proposed_args,
                         const csilk_json_t*      display_extra)
{
    if (!ai_tool_ctx_valid(ctx) || !action_type || !confirm_tool_name || !proposed_args) {
        return ai_tool_json_error("invalid propose parameters", 0);
    }

    char* draft_token = ai_confirmation_create_bound_token(
        ctx->user_id, ctx->session_id, confirm_tool_name, proposed_args, risk, 300);
    char* draft_args_json = csilk_json_serialize((csilk_json_t*)proposed_args, NULL);

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_string(res, "action_type", action_type);
    csilk_json_add_bool(res, "propose_success", true);
    copy_object_fields(res, proposed_args);
    copy_object_fields(res, display_extra);
    csilk_json_add_string(res, "draft_token", draft_token ? draft_token : "");
    if (draft_args_json) {
        csilk_json_add_string(res, "draft_args_json", draft_args_json);
        free(draft_args_json);
    }
    if (draft_token) {
        free(draft_token);
    }

    return ai_tool_json_serialize_free(res);
}

char*
ai_tool_confirm_verify(const ai_tool_context_t* ctx,
                       const char*              confirm_tool_name,
                       const csilk_json_t*      args)
{
    if (!ai_tool_ctx_valid(ctx) || !confirm_tool_name) {
        return ai_tool_json_error("invalid context", 0);
    }
    const char*              draft_token = args ? csilk_json_get_string(args, "draft_token") : "";
    ai_confirmation_status_t st = ai_confirmation_verify_and_consume(
        ctx->user_id, ctx->session_id, confirm_tool_name, args, draft_token);
    if (st != AI_CONFIRM_OK) {
        return ai_tool_json_error(ai_confirmation_strerror(st), 0);
    }
    return NULL;
}
