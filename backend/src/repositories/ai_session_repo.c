#include "repositories/ai_session_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <string.h>

static void sid_str(int64_t sid, char out[static 32]) {
    snprintf(out, 32, "%lld", (long long)sid);
}
static void uid_str(int64_t uid, char out[static 32]) {
    snprintf(out, 32, "%lld", (long long)uid);
}

csilk_json_t* ai_session_list(csilk_db_pool_t* pool, int64_t user_id, int64_t page, int64_t page_size, int64_t* total) {
    char uid[32], lim[32], off[32];
    uid_str(user_id, uid);
    snprintf(lim, 32, "%lld", (long long)page_size);
    snprintf(off, 32, "%lld", (long long)((page - 1) * page_size));

    csilk_json_t* cnt = csilk_db_query_param_json(pool,
        "SELECT COUNT(*) as cnt FROM ai_sessions WHERE user_id=?", (const char*[]){uid, NULL});
    *total = 0;
    if (cnt && csilk_json_array_size(cnt) > 0)
        *total = db_get_int(csilk_json_array_get(cnt, 0), "cnt");
    csilk_json_free(cnt);

    return csilk_db_query_param_json(pool,
        "SELECT id, user_id, title, model, provider, "
        "datetime(created_at) as created_at, datetime(updated_at) as updated_at "
        "FROM ai_sessions WHERE user_id=? ORDER BY updated_at DESC LIMIT ? OFFSET ?",
        (const char*[]){uid, lim, off, NULL});
}

csilk_json_t* ai_session_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id) {
    char uid[32], id_s[32];
    uid_str(user_id, uid);
    sid_str(id, id_s);
    csilk_json_t* r = csilk_db_query_param_json(pool,
        "SELECT id, user_id, title, model, provider, "
        "datetime(created_at) as created_at, datetime(updated_at) as updated_at "
        "FROM ai_sessions WHERE id=? AND user_id=?", (const char*[]){id_s, uid, NULL});
    if (!r || csilk_json_array_size(r) == 0) { csilk_json_free(r); return NULL; }
    return r;
}

int64_t ai_session_insert(csilk_db_pool_t* pool, int64_t user_id, const char* title, const char* model, const char* provider) {
    char uid[32], m[128], p[64];
    uid_str(user_id, uid);
    strncpy(m, model ?: "deepseek-chat", sizeof(m) - 1); m[sizeof(m)-1] = '\0';
    strncpy(p, provider ?: "deepseek", sizeof(p) - 1); p[sizeof(p)-1] = '\0';
    csilk_json_t* r = csilk_db_query_param_json(pool,
        "INSERT INTO ai_sessions (user_id, title, model, provider) VALUES (?, ?, ?, ?) RETURNING id",
        (const char*[]){uid, title ?: "新对话", m, p, NULL});
    int64_t id = 0;
    if (r && csilk_json_array_size(r) > 0)
        id = db_get_int(csilk_json_array_get(r, 0), "id");
    csilk_json_free(r);
    return id;
}

int ai_session_update(csilk_db_pool_t* pool, int64_t user_id, int64_t id, const char* title, const char* model) {
    char uid[32], id_s[32];
    uid_str(user_id, uid);
    sid_str(id, id_s);
    if ((!title || !title[0]) && (!model || !model[0])) return 1;

    char sql[1024];
    int idx = snprintf(sql, sizeof(sql), "UPDATE ai_sessions SET updated_at=CURRENT_TIMESTAMP");
    int pc = 0;
    const char* params[8];
    if (title && title[0]) {
        idx += snprintf(sql + idx, (size_t)(sizeof(sql) - (size_t)idx), ", title=?");
        params[pc++] = title;
    }
    if (model && model[0]) {
        idx += snprintf(sql + idx, (size_t)(sizeof(sql) - (size_t)idx), ", model=?");
        params[pc++] = model;
    }
    idx += snprintf(sql + idx, (size_t)(sizeof(sql) - (size_t)idx), " WHERE id=? AND user_id=?");
    params[pc++] = id_s;
    params[pc++] = uid;
    params[pc] = NULL;

    csilk_json_t* r = csilk_db_query_param_json(pool, sql, params);
    int affected = r ? (int)csilk_json_array_size(r) : 0;
    csilk_json_free(r);
    return affected > 0;
}

int ai_session_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id) {
    char uid[32], id_s[32];
    uid_str(user_id, uid);
    sid_str(id, id_s);
    csilk_json_t* r = csilk_db_query_param_json(pool,
        "DELETE FROM ai_sessions WHERE id=? AND user_id=?", (const char*[]){id_s, uid, NULL});
    int affected = r ? (int)csilk_json_array_size(r) : 0;
    csilk_json_free(r);
    return affected > 0;
}

csilk_json_t* ai_message_list(csilk_db_pool_t* pool, int64_t session_id, int64_t page, int64_t page_size, int64_t* total) {
    char sid[32], lim[32], off[32];
    sid_str(session_id, sid);
    snprintf(lim, 32, "%lld", (long long)page_size);
    snprintf(off, 32, "%lld", (long long)((page - 1) * page_size));

    csilk_json_t* cnt = csilk_db_query_param_json(pool,
        "SELECT COUNT(*) as cnt FROM ai_messages WHERE session_id=?", (const char*[]){sid, NULL});
    *total = 0;
    if (cnt && csilk_json_array_size(cnt) > 0)
        *total = db_get_int(csilk_json_array_get(cnt, 0), "cnt");
    csilk_json_free(cnt);

    return csilk_db_query_param_json(pool,
        "SELECT id, session_id, role, content, model, datetime(created_at) as created_at "
        "FROM ai_messages WHERE session_id=? ORDER BY created_at ASC LIMIT ? OFFSET ?",
        (const char*[]){sid, lim, off, NULL});
}

csilk_json_t* ai_message_recent(csilk_db_pool_t* pool, int64_t session_id, int limit) {
    char sid[32], lim[32];
    sid_str(session_id, sid);
    snprintf(lim, 32, "%d", limit);
    return csilk_db_query_param_json(pool,
        "SELECT role, content FROM ai_messages WHERE session_id=? ORDER BY created_at ASC LIMIT ?",
        (const char*[]){sid, lim, NULL});
}

int64_t ai_message_insert(csilk_db_pool_t* pool, int64_t session_id, const char* role, const char* content, const char* model) {
    char sid[32], m[128];
    sid_str(session_id, sid);
    strncpy(m, model ?: "", sizeof(m) - 1); m[sizeof(m)-1] = '\0';
    csilk_json_t* r = csilk_db_query_param_json(pool,
        "INSERT INTO ai_messages (session_id, role, content, model) VALUES (?, ?, ?, ?) RETURNING id",
        (const char*[]){sid, role, content, m, NULL});
    int64_t id = 0;
    if (r && csilk_json_array_size(r) > 0)
        id = db_get_int(csilk_json_array_get(r, 0), "id");
    csilk_json_free(r);
    return id;
}
